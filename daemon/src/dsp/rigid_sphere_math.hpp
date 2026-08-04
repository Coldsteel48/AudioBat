// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace audiobat
{

// Physical constants shared by every rigid-sphere head model in this
// pipeline: the license-free synthetic HRTF (synthetic_hrtf.cpp) and the
// near-field proximity correction (near_field_filter.cpp) both model the
// head as a rigid sphere of this radius, in this medium.
inline constexpr float HeadRadiusMeters = 0.0875f;
inline constexpr float SpeedOfSoundMetersPerSecond = 343.0f;

// Wraps an azimuth to (-180, 180] so callers can read off which side a
// source is on (sign) regardless of how the caller phrased the azimuth
// (this pipeline's virtual speakers run 0..315).
inline float NormalizeAzimuthDegrees(float AzimuthDegrees)
{
    float Azimuth = std::fmod(AzimuthDegrees, 360.0f);
    if (Azimuth > 180.0f)
    {
        Azimuth -= 360.0f;
    }
    else if (Azimuth <= -180.0f)
    {
        Azimuth += 360.0f;
    }
    return Azimuth;
}

// Lateral angle from the interaural axis, in degrees: 0 at dead
// front/behind (equidistant to both ears), 90 at either side (maximum
// path-length difference between ears). Folds AudioBat's azimuth
// convention (0 = front, positive = left, wrapping at +-180) onto the
// classic Woodworth-formula convention (0 = front, 90 = side) that both
// rigid-sphere models here are built on. Takes an already-normalized
// azimuth (see NormalizeAzimuthDegrees) since callers typically need both.
inline float LateralAngleDegrees(float NormalizedAzimuthDegrees)
{
    return 90.0f - std::fabs(90.0f - std::fabs(NormalizedAzimuthDegrees));
}

// Impulse response of a single-pole low-pass with unity DC gain,
// h[n] = (1-p)*p^n where p = exp(-2*pi*CutoffHz/SampleRate). Shared
// building block: synthetic_hrtf.cpp uses this directly as a head-shadow
// shelf; near_field_filter.cpp scales and offsets it into a boost shelf
// (see BuildBoostShelf there). Not itself scaled by any gain - callers
// multiply/combine as needed.
inline std::vector<float> BuildUnityGainLowpassIR(float CutoffHz, float SampleRate, uint32_t TapCount)
{
    const float Pole = std::exp(-2.0f * std::numbers::pi_v<float> * CutoffHz / SampleRate);
    std::vector<float> Taps(TapCount);
    float PolePower = 1.0f;
    for (uint32_t n = 0; n < TapCount; ++n)
    {
        Taps[n] = (1.0f - Pole) * PolePower;
        PolePower *= Pole;
    }
    return Taps;
}

} // namespace audiobat
