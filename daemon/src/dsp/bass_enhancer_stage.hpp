// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include "bass_enhancer_filter.hpp"
#include "crossfading_slot.hpp"
#include "ramkolfx/protocol.hpp"
#include "ramkolfx/types.hpp"

namespace ramkolfx
{

// Wraps BassEnhancerFilter (via CrossfadingSlot) so retuning it never
// clicks - same reasoning as HwEqStage. Not one of the three switchable
// DspStage modes: applied unconditionally, after HwEq, on the final
// downmixed stereo signal - see AudioEngine::HandleVirtualSinkAudio.
//
// Thread model: same as HwEqStage - SetSettings()/CollectGarbage() run on
// a control-client worker thread, Process() runs on the realtime audio
// thread, CrossfadingSlot keeps the two sides safely decoupled.
class BassEnhancerStage
{
public:
    BassEnhancerStage(const BassEnhancerSettings& InitialSettings, float SampleRate);

    // Same shape as DspStage::Process, always called with
    // InputChannels == OutputChannels == 2.
    void Process(const float* Input, uint32 InputChannels, float* Output, uint32 OutputChannels,
                 uint32 Frames);

    // Not realtime-safe: builds a brand-new BassEnhancerFilter
    // synchronously and publishes it for Process() to crossfade into.
    void SetSettings(const BassEnhancerSettings& Settings);

    // Drops whatever a completed crossfade faded out of. Safe to call
    // frequently; a no-op when there's nothing to collect. Must not be
    // called from the audio thread - see CrossfadingSlot::CollectGarbage.
    void CollectGarbage();

private:
    float SampleRate;

    static constexpr uint32 ChannelsPerFrame = 2;

    CrossfadingSlot<BassEnhancerFilter> Slot;
};

} // namespace ramkolfx
