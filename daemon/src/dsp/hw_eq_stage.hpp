// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>

#include "crossfading_slot.hpp"
#include "graphic_eq_filter.hpp"
#include "ramkolfx/protocol.hpp"
#include "ramkolfx/types.hpp"

namespace ramkolfx
{

// Wraps GraphicEqFilter (via CrossfadingSlot) so retuning a band never
// clicks - unlike HrtfDeck's occasional source switch, sliders get dragged
// continuously, so click-free coefficient updates matter more here. Not
// one of the three switchable DspStage modes (Off/Basic/Advanced): applied
// unconditionally, after whichever of those already ran, on the final
// downmixed stereo signal - see AudioEngine::HandleVirtualSinkAudio.
//
// Thread model: same as HrtfDeck - SetBands()/CollectGarbage() run on a
// control-client worker thread, Process() runs on the realtime audio
// thread, CrossfadingSlot keeps the two sides safely decoupled.
class HwEqStage
{
public:
    HwEqStage(const std::array<EqBand, MaxEqBands>& InitialBands, float SampleRate);

    // Same shape as DspStage::Process, always called with
    // InputChannels == OutputChannels == 2.
    void Process(const float* Input, uint32 InputChannels, float* Output, uint32 OutputChannels,
                 uint32 Frames);

    // Not realtime-safe: builds a brand-new GraphicEqFilter synchronously
    // (cheap - just coefficient math, no allocspan beyond the object
    // itself) and publishes it for Process() to crossfade into.
    void SetBands(const std::array<EqBand, MaxEqBands>& Bands);

    // Drops whatever a completed crossfade faded out of. Safe to call
    // frequently; a no-op when there's nothing to collect. Must not be
    // called from the audio thread - see CrossfadingSlot::CollectGarbage.
    void CollectGarbage();

private:
    float SampleRate;

    static constexpr uint32 ChannelsPerFrame = 2;

    CrossfadingSlot<GraphicEqFilter> Slot;
};

} // namespace ramkolfx
