// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "synthetic_hrtf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "rigid_sphere_math.hpp"

namespace audiobat
{

namespace
{

constexpr float DegToRad = std::numbers::pi_v<float> / 180.0f;

// Short relative to measured HRIRs (which run into the hundreds of taps):
// this model has no fine spectral detail to represent, just a single-pole
// shelf, so a long tail would only waste convolver cycles.
constexpr uint32_t TapCount = 128;

// Far-ear low-pass shelf cutoff at broadside (fully lateral, alpha=90) vs.
// dead ahead/behind (alpha=0, effectively no shadowing to model).
constexpr float FarEarCutoffHzAtFront = 20000.0f;
constexpr float FarEarCutoffHzAtSide = 1500.0f;

// Broadband gain trim on top of the shelf, same front/side interpolation.
constexpr float NearEarGainAtFront = 1.0f;
constexpr float NearEarGainAtSide = 0.9f;
constexpr float FarEarGainAtFront = 1.0f;
constexpr float FarEarGainAtSide = 0.55f;

float Lerp(float A, float B, float T)
{
    return A + (B - A) * T;
}

// Flat-gain-scaled version of the shared unity-DC-gain low-pass primitive.
std::vector<float> BuildShelfFilter(float CutoffHz, float Gain, float SampleRate)
{
    std::vector<float> Taps = BuildUnityGainLowpassIR(CutoffHz, SampleRate, TapCount);
    for (float& Tap : Taps)
    {
        Tap *= Gain;
    }
    return Taps;
}

} // namespace

HrtfFilter ComputeSphericalHeadFilter(float AzimuthDegrees, float /*ElevationDegrees*/, float SampleRate)
{
    const float Azimuth = NormalizeAzimuthDegrees(AzimuthDegrees);
    const float Alpha = LateralAngleDegrees(Azimuth);
    const float AlphaRad = Alpha * DegToRad;
    const float AlphaFraction = Alpha / 90.0f; // 0 at front/behind, 1 at full lateral

    const float ItdSeconds =
        (HeadRadiusMeters / SpeedOfSoundMetersPerSecond) * (AlphaRad + std::sin(AlphaRad));
    const float ItdSamples = ItdSeconds * SampleRate;

    const float NearGain = Lerp(NearEarGainAtFront, NearEarGainAtSide, AlphaFraction);
    const float FarGain = Lerp(FarEarGainAtFront, FarEarGainAtSide, AlphaFraction);
    const float FarCutoffHz = Lerp(FarEarCutoffHzAtFront, FarEarCutoffHzAtSide, AlphaFraction);

    // Near ear stays close to transparent (a brief, near-unity shelf);
    // matches BuildShelfFilter's shape for code-path consistency rather
    // than special-casing a bare impulse.
    const std::vector<float> NearFilter = BuildShelfFilter(FarEarCutoffHzAtFront, NearGain, SampleRate);
    const std::vector<float> FarFilter = BuildShelfFilter(FarCutoffHz, FarGain, SampleRate);

    HrtfFilter Out;
    if (Azimuth > 0.0f)
    {
        // Source on the left: left ear is near (leads), right ear is far
        // (lags, shadowed).
        Out.Left = NearFilter;
        Out.Right = FarFilter;
        Out.DelayRightSamples = ItdSamples;
    }
    else if (Azimuth < 0.0f)
    {
        Out.Right = NearFilter;
        Out.Left = FarFilter;
        Out.DelayLeftSamples = ItdSamples;
    }
    else
    {
        // Dead ahead or behind: symmetric, no ITD.
        Out.Left = NearFilter;
        Out.Right = NearFilter;
    }
    return Out;
}

} // namespace audiobat
