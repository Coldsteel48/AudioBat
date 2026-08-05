// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "audiobat/types.hpp"

namespace audiobat
{

// One direction's stereo HRIR pair, in samples at the pipeline's sample
// rate, plus each ear's propagation delay (some sources, e.g. measured
// SOFA data, report delay separately from the FIR taps; others bake it
// into the taps themselves and leave these at 0 - see BuildDelayedFilter
// below). Shared between every HRTF source BinauralStage can use
// (HrtfLoader's measured SOFA data, ComputeSphericalHeadFilter's synthetic
// model) so they're interchangeable from BinauralStage's point of view.
struct HrtfFilter
{
    std::vector<float> Left;
    std::vector<float> Right;
    float DelayLeftSamples = 0.0f;
    float DelayRightSamples = 0.0f;
};

// Prepends IntDelay zero samples to Taps, approximating a reported
// fractional-sample delay (e.g. from libmysofa) with an integer-sample
// delay. A future refinement could use a fractional (all-pass) delay for
// sub-sample accuracy. Named distinctly from binaural_stage.cpp's own
// private, identically-shaped BuildDelayedFilter (not this one, and not
// renamed to match) - that original (pre-near-field) signal path is meant
// to stay byte-for-byte what shipped before this file existed, see
// docs/near-field-distance-plan.md ("additive, not replaced"); giving
// this one a distinct name avoids an unqualified-lookup ambiguity between
// the two in translation units that end up seeing both.
inline std::vector<float> BuildDelayedHrtfFilter(const std::vector<float>& Taps, float DelaySamples)
{
    const uint32 IntDelay = static_cast<uint32>(std::lround(std::max(0.0f, DelaySamples)));
    std::vector<float> Delayed(Taps.size() + IntDelay, 0.0f);
    std::copy(Taps.begin(), Taps.end(), Delayed.begin() + IntDelay);
    return Delayed;
}

} // namespace audiobat
