// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "hrtf_gain_normalization.hpp"

#include <algorithm>
#include <cmath>

#include "hrtf_loader.hpp"

namespace ramkolfx
{

namespace
{
// Matches binaural_stage.cpp's own (separately-maintained) TargetPeakTap,
// chosen there to closely match the bundled default.sofa's natural peak.
// Kept as its own constant here rather than shared - see this file's
// header comment.
constexpr float TargetPeakTap = 0.7f;
} // namespace

float ComputeHrtfNormalizationGain(HrtfSourceKind Kind, const std::string& SofaPath, float SampleRate)
{
    if (Kind == HrtfSourceKind::SyntheticSphericalHead)
    {
        return 1.0f;
    }

    HrtfLoader Loader;
    if (!Loader.Open(SofaPath, SampleRate))
    {
        return 1.0f; // caller falls back to algebraic decode anyway when a SOFA file won't load
    }

    float PeakTap = 0.0f;
    for (uint32 k = 0; k < 8; ++k)
    {
        const float AzimuthDegrees = static_cast<float>(k) * 45.0f;
        const HrtfFilter Filter = Loader.GetFilter(AzimuthDegrees, 0.0f);
        for (float Tap : Filter.Left)
        {
            PeakTap = std::max(PeakTap, std::fabs(Tap));
        }
        for (float Tap : Filter.Right)
        {
            PeakTap = std::max(PeakTap, std::fabs(Tap));
        }
    }
    return PeakTap > 1e-6f ? TargetPeakTap / PeakTap : 1.0f;
}

} // namespace ramkolfx
