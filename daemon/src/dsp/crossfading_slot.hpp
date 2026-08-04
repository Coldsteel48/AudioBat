// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "dsp_stage.hpp"

namespace audiobat
{

// Generic "swap this live object for a new one without clicking or
// blocking the realtime thread" utility. First written for HrtfDeck (HRTF
// catalog switching), pulled out here so BinauralVoice's per-speaker
// azimuth/distance rebuilds could reuse the exact same, already-verified
// concurrency logic rather than re-deriving it.
//
// Thread model: Publish()/CollectGarbage() are not realtime-safe (they
// build/free T instances) and are meant to be called from a control
// thread. Process() is realtime-safe: no allocation, no blocking, no
// deallocation - it only ever touches Pending/Trash via atomics.
//
// Overlapping Publish() calls don't need to be rejected: if a second one
// lands before Process() has consumed the first, the second simply wins
// (last store before Process() reads it); the first T is dropped on
// whichever control thread called Publish() the second time, never on the
// audio thread.
template <typename T>
class CrossfadingSlot
{
public:
    CrossfadingSlot(std::shared_ptr<T> Initial, uint32_t InCrossfadeFrames, uint32_t InChannelsPerFrame)
        : Live(std::move(Initial)), CrossfadeFrames(InCrossfadeFrames), ChannelsPerFrame(InChannelsPerFrame)
    {
        ScratchOld.assign(static_cast<size_t>(MaxProcessFrames) * ChannelsPerFrame, 0.0f);
        ScratchNew.assign(static_cast<size_t>(MaxProcessFrames) * ChannelsPerFrame, 0.0f);
    }

    // Not realtime-safe. Publishes a newly-built T for Process() to
    // crossfade into on its next call.
    void Publish(std::shared_ptr<T> NewValue)
    {
        Pending.store(std::move(NewValue), std::memory_order_release);
    }

    // Not realtime-safe (may deallocate). Drops whatever a completed
    // crossfade faded out of. Safe to call frequently; a no-op when
    // there's nothing to collect.
    void CollectGarbage()
    {
        for (auto& Slot : Trash)
        {
            Slot.store(nullptr, std::memory_order_acq_rel);
        }
    }

    // Realtime-safe. RunOne(T&, float* Output, uint32_t Frames) must
    // render exactly one T instance's output (ChannelsPerFrame channels)
    // into Output; called once or twice per Process() call depending on
    // whether a crossfade is in flight. Interleaved output written to
    // Output (ChannelsPerFrame * Frames floats).
    template <typename RunOneFn>
    void Process(RunOneFn&& RunOne, float* Output, uint32_t Frames)
    {
        if (Frames > MaxProcessFrames)
        {
            Frames = MaxProcessFrames; // defensive; callers already clamp to this bound
        }

        if (FadeFramesRemaining == 0)
        {
            if (auto NewValue = Pending.exchange(nullptr, std::memory_order_acq_rel))
            {
                // Relaxed: Live is only ever written from this thread (the
                // audio thread) - no cross-thread synchronization is
                // needed for this thread to see its own prior writes.
                FadingOut = Live.load(std::memory_order_relaxed);
                // Release: pairs with ForwardToLive's acquire-load, so a
                // control thread calling ForwardToLive concurrently always
                // sees a fully-constructed T, never a partial one.
                Live.store(std::move(NewValue), std::memory_order_release);
                FadeFramesRemaining = CrossfadeFrames;
            }
        }

        const std::shared_ptr<T> LiveCopy = Live.load(std::memory_order_relaxed);

        if (FadeFramesRemaining == 0)
        {
            // Common case: no crossfade in flight, single render, no
            // scratch mixing overhead.
            RunOne(*LiveCopy, Output, Frames);
            return;
        }

        RunOne(*LiveCopy, ScratchNew.data(), Frames);
        RunOne(*FadingOut, ScratchOld.data(), Frames);

        for (uint32_t FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
        {
            const float NewGain =
                1.0f - static_cast<float>(FadeFramesRemaining) / static_cast<float>(CrossfadeFrames);
            const float OldGain = 1.0f - NewGain;

            float* OutFrame = Output + FrameIndex * ChannelsPerFrame;
            const float* OldFrame = ScratchOld.data() + FrameIndex * ChannelsPerFrame;
            const float* NewFrame = ScratchNew.data() + FrameIndex * ChannelsPerFrame;
            for (uint32_t Channel = 0; Channel < ChannelsPerFrame; ++Channel)
            {
                OutFrame[Channel] = OldFrame[Channel] * OldGain + NewFrame[Channel] * NewGain;
            }

            if (FadeFramesRemaining > 0)
            {
                --FadeFramesRemaining;
            }
        }

        if (FadeFramesRemaining == 0)
        {
            // Hand the faded-out instance off for the control thread to
            // actually drop (see CollectGarbage) - shared_ptr's refcount
            // reaching zero here would deallocate right on the audio
            // thread otherwise. Two slots rather than one: if a second
            // crossfade completes before CollectGarbage() has drained the
            // first, the second still lands in an empty slot instead of
            // forcing an overwrite that would deallocate the first slot's
            // contents right here.
            Trash[TrashSlot].store(std::move(FadingOut), std::memory_order_release);
            TrashSlot = (TrashSlot + 1) % static_cast<uint32_t>(Trash.size());
        }
    }

    // Forwards Fn(T&) to the current live instance, callable from any
    // thread, concurrently with Process() running on the audio thread -
    // Live is atomic specifically so this is safe. Fn must only perform
    // further atomic-based operations that are themselves safe to run
    // concurrently with Process() (e.g. another nested
    // CrossfadingSlot::Publish() inside T) - never plain field mutation.
    // Used for pass-through control operations, e.g. HrtfDeck forwarding
    // "rebuild one voice" into whichever BinauralStage is currently live
    // without exposing Live directly. The shared_ptr copy taken here keeps
    // that instance alive for Fn's duration even if Process() swaps Live
    // to something new concurrently.
    template <typename Fn>
    void ForwardToLive(Fn&& RunFn)
    {
        const std::shared_ptr<T> LiveCopy = Live.load(std::memory_order_acquire);
        RunFn(*LiveCopy);
    }

private:
    // Atomic (not a plain shared_ptr) specifically so ForwardToLive can
    // safely read it from a control thread while Process() reassigns it
    // concurrently on the audio thread - its load()/store() are cheap
    // enough at "once per block, plus occasional ForwardToLive calls"
    // frequency. FadingOut, unlike Live, is never touched cross-thread
    // (ForwardToLive only ever reaches the current live instance), so it
    // stays a plain, cheaper shared_ptr.
    std::atomic<std::shared_ptr<T>> Live;
    std::shared_ptr<T> FadingOut; // RT-thread-owned
    uint32_t FadeFramesRemaining = 0;
    uint32_t TrashSlot = 0;
    const uint32_t CrossfadeFrames;
    const uint32_t ChannelsPerFrame;

    std::atomic<std::shared_ptr<T>> Pending{nullptr};
    std::array<std::atomic<std::shared_ptr<T>>, 2> Trash{};

    std::vector<float> ScratchOld;
    std::vector<float> ScratchNew;
};

} // namespace audiobat
