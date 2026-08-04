// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <string>

#include "binaural_stage.hpp" // HrtfSourceKind

namespace audiobat
{

// Different SOFA datasets calibrate absolute HRIR amplitude very
// differently (mic gain, measurement distance normalization, etc.) - e.g.
// SADIE II's raw taps measured several times louder than MIT KEMAR's SOFA
// conversion for the same direction (see data/hrtf/README.md). Without
// correcting for this, switching HRTF sources could suddenly change
// output level by a large factor.
//
// Returns a scale factor: samples 8 canonical azimuths (0/45/.../315,
// elevation 0) from the given source, finds the peak absolute tap across
// all of them, and returns TargetPeakTap/PeakTap. Always 1.0 for
// HrtfSourceKind::SyntheticSphericalHead - it's already designed at a
// consistent, fixed level, nothing to normalize against.
//
// This is a deliberate, separate implementation from BinauralStage's own
// (older) per-source normalization used by its original 8-virtual-speaker
// signal path - see docs/near-field-distance-plan.md ("additive, not
// replaced"): that path keeps its own copy, untouched, rather than being
// refactored to call this. This one exists for the newer per-voice direct-
// convolution path (binaural_voice.cpp) instead.
float ComputeHrtfNormalizationGain(HrtfSourceKind Kind, const std::string& SofaPath, float SampleRate);

} // namespace audiobat
