// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>

#include "biquad.hpp"
#include "ramkolfx/protocol.hpp"
#include "ramkolfx/types.hpp"

namespace ramkolfx
{

// A flat (all bands 0dB, so audibly a no-op - see GraphicEqFilter's own
// comment) 10-band curve at the ISO centers above. Used to seed a fresh
// GraphicEqFilter/PersistedSettings::HwEqBands before any SetHwEqBand
// command has ever been received.
std::array<EqBand, MaxEqBands> DefaultHwEqBands();

// Cascaded 10-band stereo graphic EQ: two independent Biquad chains (left,
// right), each MaxEqBands sections deep. Every band contributes a peaking/
// shelf/pass/notch section per BandIndex's EqBand; at GainDb=0 a peaking
// section is exactly unity (b0=a0, b1=a1, b2=a2 after RBJ's own
// normalization), so the all-default curve is a true identity pass, not
// just a "close to 0dB" approximation - no separate bypass flag needed.
class GraphicEqFilter
{
public:
    GraphicEqFilter(const std::array<EqBand, MaxEqBands>& Bands, float SampleRate);

    // Matches DspStage::Process's shape so HwEqStage can wrap this exactly
    // like HrtfDeck wraps BinauralStage, but always called with
    // InputChannels == OutputChannels == 2 (this stage never changes
    // channel count). Input == Output is safe (see Biquad::ProcessSample -
    // each output sample only depends on already-consumed input/state).
    void Process(const float* Input, uint32 InputChannels, float* Output, uint32 OutputChannels,
                 uint32 Frames);

private:
    std::array<Biquad, MaxEqBands> Left;
    std::array<Biquad, MaxEqBands> Right;
};

} // namespace ramkolfx
