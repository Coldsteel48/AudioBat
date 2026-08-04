// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "hrtf_deck.hpp"

#include <algorithm>

namespace audiobat
{

HrtfDeck::HrtfDeck(const SpeakerLayout& InLayout, HrtfSourceKind Kind, const std::string& SofaPath,
                   float InSampleRate)
    : Layout(InLayout), SampleRate(InSampleRate),
      CrossfadeFrames(static_cast<uint32_t>(0.05f * InSampleRate)) // ~50ms
{
    Live = std::make_shared<BinauralStage>(Layout, Kind, SofaPath, SampleRate);
    ScratchOld.assign(static_cast<size_t>(MaxProcessFrames) * 2, 0.0f); // stereo
    ScratchNew.assign(static_cast<size_t>(MaxProcessFrames) * 2, 0.0f);
}

void HrtfDeck::SwitchTo(HrtfSourceKind Kind, const std::string& SofaPath)
{
    // Building a BinauralStage does real work (SOFA parse, convolver FFT
    // setup) - deliberately done here, off the realtime thread, before
    // anything realtime-visible changes. std::atomic<shared_ptr>::store is
    // safe against concurrent callers on its own (last publish wins); no
    // extra locking needed since nothing else about this call touches
    // shared state.
    auto NewStage = std::make_shared<BinauralStage>(Layout, Kind, SofaPath, SampleRate);
    Pending.store(std::move(NewStage), std::memory_order_release);
}

void HrtfDeck::CollectGarbage()
{
    for (auto& Slot : Trash)
    {
        Slot.store(nullptr, std::memory_order_acq_rel);
    }
}

void HrtfDeck::Process(const float* Input, uint32_t InputChannels,
                       float* Output, uint32_t OutputChannels,
                       uint32_t Frames)
{
    if (Frames > MaxProcessFrames)
    {
        Frames = MaxProcessFrames; // defensive; AudioEngine already clamps to this bound
    }

    if (FadeFramesRemaining == 0)
    {
        if (auto NewStage = Pending.exchange(nullptr, std::memory_order_acq_rel))
        {
            FadingOut = std::move(Live);
            Live = std::move(NewStage);
            FadeFramesRemaining = CrossfadeFrames;
        }
    }

    if (FadeFramesRemaining == 0)
    {
        // Common case: no crossfade in flight, single Process() call, no
        // scratch mixing overhead.
        Live->Process(Input, InputChannels, Output, OutputChannels, Frames);
        return;
    }

    Live->Process(Input, InputChannels, ScratchNew.data(), OutputChannels, Frames);
    FadingOut->Process(Input, InputChannels, ScratchOld.data(), OutputChannels, Frames);

    for (uint32_t FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        const float NewGain =
            1.0f - static_cast<float>(FadeFramesRemaining) / static_cast<float>(CrossfadeFrames);
        const float OldGain = 1.0f - NewGain;

        float* OutFrame = Output + FrameIndex * OutputChannels;
        const float* OldFrame = ScratchOld.data() + FrameIndex * OutputChannels;
        const float* NewFrame = ScratchNew.data() + FrameIndex * OutputChannels;
        for (uint32_t Channel = 0; Channel < OutputChannels; ++Channel)
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
        // Hand the faded-out stage off for the control thread to actually
        // drop (see CollectGarbage) - shared_ptr's refcount reaching zero
        // here would deallocate right on the audio thread otherwise.
        Trash[TrashSlot].store(std::move(FadingOut), std::memory_order_release);
        TrashSlot = (TrashSlot + 1) % static_cast<uint32_t>(Trash.size());
    }
}

} // namespace audiobat
