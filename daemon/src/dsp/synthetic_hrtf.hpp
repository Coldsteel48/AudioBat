// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include "hrtf_filter.hpp"

namespace audiobat
{

// Procedurally-computed stand-in for a measured HRTF: a rigid-sphere head
// model (Woodworth-Schlosberg interaural time difference, plus a simple
// single-pole low-pass approximating high-frequency head shadowing on the
// far ear). No measured data at all, so unlike every SOFA-backed source
// this carries zero data-licensing exposure - see data/hrtf/README.md.
//
// This is a deliberately simplified approximation, not a full closed-form
// spherical-head solution (e.g. Duda & Martin's Legendre-series model):
// real HRTFs also carry pinna reflections and torso/shoulder cues this
// doesn't attempt. It exists as a license-free fallback, not as a
// replacement for measured data - expect noticeably weaker front/back and
// externalization cues than any of the bundled SADIE II/KEMAR sources.
//
// Matches AudioBat's azimuth convention (0 = front, positive = left) and
// HrtfLoader::GetFilter's signature so BinauralStage can use either
// interchangeably. ElevationDegrees is accepted for interface symmetry
// but ignored: every caller in this pipeline only ever sources from
// horizontal-plane 7.1 speakers.
HrtfFilter ComputeSphericalHeadFilter(float AzimuthDegrees, float ElevationDegrees, float SampleRate);

} // namespace audiobat
