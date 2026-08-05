// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include "biquad.hpp"
#include "ramkolfx/protocol.hpp"
#include "ramkolfx/types.hpp"

namespace ramkolfx
{

// Psychoacoustic "virtual bass" enhancer for headphones: isolates the
// mono sub-bass band below Settings.CutoffHz, drives it through a tanh
// waveshaper to synthesize upper harmonics of the fundamental (many small
// headphone drivers can't reproduce sub-bass cleanly, but can reproduce
// those harmonics - the ear reconstructs the perception of bass from
// them), strips the re-synthesized fundamental back out via a second
// high-pass so only the newly-generated harmonics remain, and blends them
// into both channels equally. Mono throughout: bass isn't spatially
// perceptible at these frequencies, and summing before filtering
// sidesteps phase-cancellation between L/R.
class BassEnhancerFilter
{
public:
    BassEnhancerFilter(const BassEnhancerSettings& Settings, float SampleRate);

    // Matches GraphicEqFilter::Process's shape so BassEnhancerStage can
    // wrap this exactly like HwEqStage wraps GraphicEqFilter, always
    // called with InputChannels == OutputChannels == 2. Input == Output is
    // safe - see GraphicEqFilter::Process's own comment on Biquad state.
    void Process(const float* Input, uint32 InputChannels, float* Output, uint32 OutputChannels,
                 uint32 Frames);

private:
    bool bEnabled;
    float Drive; // internal waveshaper gain, mapped from Settings.Drive's 0..1 wire range - see .cpp
    float TanhDrive; // tanh(Drive), precomputed to normalize the waveshaper's output gain
    float Mix;

    Biquad LowPass;  // isolates the mono sub-bass band feeding the waveshaper
    Biquad HighPass; // strips the re-synthesized fundamental back out, keeping only its harmonics
};

} // namespace ramkolfx
