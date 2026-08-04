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
#include <memory>
#include <string>
#include <vector>

#include "binaural_stage.hpp"
#include "dsp_stage.hpp"
#include "speaker_layout.hpp"

namespace audiobat
{

// Wraps two BinauralStages so switching the active HRTF source (SOFA file
// or synthetic model) never restarts the daemon and never clicks: a
// pending switch is built off the realtime thread, then Process()
// crossfades into it over a fixed window once published.
//
// Thread model: SwitchTo()/CollectGarbage() run on one of the daemon's
// control-client worker threads (ControlServer::HandleClient spawns a
// plain detached thread per connection - never the PipeWire main loop or
// the audio callback). Process() runs on the realtime audio thread. The
// two sides only ever communicate through the Pending/Trash atomics
// below; Process() itself never allocates, blocks, or frees memory.
//
// Overlapping switches don't need to be rejected: if a second SwitchTo()
// publishes to Pending before Process() has consumed the first one, the
// second simply wins (latest request published last is what gets read) -
// the first stage's shared_ptr is dropped on the calling control thread
// when Pending is overwritten, never on the audio thread.
class HrtfDeck final : public DspStage
{
public:
    HrtfDeck(const SpeakerLayout& InLayout, HrtfSourceKind Kind, const std::string& SofaPath, float SampleRate);

    void Process(const float* Input, uint32_t InputChannels,
                 float* Output, uint32_t OutputChannels,
                 uint32_t Frames) override;

    // Not realtime-safe: builds a brand-new BinauralStage synchronously
    // (SOFA parse + convolver setup - same "call once, not from
    // Process()" cost BinauralStage's own constructor already carries),
    // then publishes it for Process() to crossfade into on its next call.
    void SwitchTo(HrtfSourceKind Kind, const std::string& SofaPath);

    // Drops any stage(s) a completed crossfade faded out of. Safe to call
    // frequently; a no-op when there's nothing to collect. Must not be
    // called from the audio thread - the drop itself may deallocate.
    // AudioEngine piggybacks this on every HandleControlCommand call
    // (i.e. every GUI status poll), so no dedicated timer is needed.
    void CollectGarbage();

private:
    const SpeakerLayout& Layout;
    float SampleRate;

    // RT-thread-owned: only Process() ever reads or writes these.
    std::shared_ptr<BinauralStage> Live;
    std::shared_ptr<BinauralStage> FadingOut;
    uint32_t FadeFramesRemaining = 0;
    uint32_t TrashSlot = 0;
    const uint32_t CrossfadeFrames; // ~50ms of frames at construction SampleRate

    std::atomic<std::shared_ptr<BinauralStage>> Pending{nullptr};

    // Two slots rather than one: if a second crossfade completes before
    // CollectGarbage() has drained the first, the second still lands in
    // an empty slot instead of forcing an overwrite that would deallocate
    // the first slot's contents right there on the audio thread.
    std::array<std::atomic<std::shared_ptr<BinauralStage>>, 2> Trash{};

    // Crossfade scratch, sized once against MaxProcessFrames so Process()
    // never allocates - same pattern as BinauralStage's own scratch
    // buffers.
    std::vector<float> ScratchOld;
    std::vector<float> ScratchNew;
};

} // namespace audiobat
