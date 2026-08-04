// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <vector>

namespace audiobat
{

// One direction's stereo HRIR pair, in samples at the pipeline's sample
// rate, plus each ear's propagation delay (some sources, e.g. measured
// SOFA data, report delay separately from the FIR taps; others bake it
// into the taps themselves and leave these at 0 - see BuildDelayedFilter
// in binaural_stage.cpp). Shared between every HRTF source BinauralStage
// can use (HrtfLoader's measured SOFA data, ComputeSphericalHeadFilter's
// synthetic model) so they're interchangeable from BinauralStage's point
// of view.
struct HrtfFilter
{
    std::vector<float> Left;
    std::vector<float> Right;
    float DelayLeftSamples = 0.0f;
    float DelayRightSamples = 0.0f;
};

} // namespace audiobat
