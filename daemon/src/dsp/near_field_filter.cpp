// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "near_field_filter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "audiobat/protocol.hpp"
#include "audiobat/types.hpp"
#include "rigid_sphere_math.hpp"

namespace audiobat
{

namespace
{

// Short: this shelf only shapes frequencies below CornerHz (see below,
// ~624Hz), and gets convolved onto the far-field filter afterward, which
// carries all the fine spectral detail.
constexpr uint32 TapCount = 64;

// Where the near-field boost concentrates: below roughly c/(2*pi*a), the
// wavelength is large relative to the head, which is where rigid-sphere
// diffraction departs most from the far-field measurement.
float CornerHz()
{
    return SpeedOfSoundMetersPerSecond / (2.0f * std::numbers::pi_v<float> * HeadRadiusMeters);
}

// Classical DC (incompressible-flow) pressure gain, on-axis, for a
// monopole source at normalized distance rho = r/a from a rigid sphere of
// radius a, relative to the same source at infinite range. Diverges as
// rho->1 (source at the sphere surface) - callers must keep rho well
// above 1, which the clamp to MinSpeakerDistanceMeters (>> HeadRadiusMeters)
// guarantees.
float OnAxisDcGain(float DistanceMeters)
{
    const float Rho = DistanceMeters / HeadRadiusMeters;
    return Rho / (Rho - 1.0f);
}

// Builds a low shelf: unity above CornerHz, Gain at DC, via the shared
// unity-DC-gain low-pass primitive offset into a boost/cut shape:
// delta[n] + (Gain-1)*lowpass[n] -> DC value Gain, high-frequency value 1.
std::vector<float> BuildBoostShelf(float Gain, float SampleRate)
{
    std::vector<float> Taps = BuildUnityGainLowpassIR(CornerHz(), SampleRate, TapCount);
    for (float& Tap : Taps)
    {
        Tap *= (Gain - 1.0f);
    }
    Taps[0] += 1.0f;
    return Taps;
}

std::vector<float> Convolve(const std::vector<float>& A, const std::vector<float>& B)
{
    if (A.empty() || B.empty())
    {
        return {};
    }
    std::vector<float> Out(A.size() + B.size() - 1, 0.0f);
    for (size_t i = 0; i < A.size(); ++i)
    {
        for (size_t j = 0; j < B.size(); ++j)
        {
            Out[i + j] += A[i] * B[j];
        }
    }
    return Out;
}

} // namespace

HrtfFilter ComputeNearFieldCorrection(float AzimuthDegrees, float DistanceMeters, float SampleRate)
{
    const float Distance = std::clamp(DistanceMeters, MinSpeakerDistanceMeters, MaxSpeakerDistanceMeters);

    const float NearGainRaw = OnAxisDcGain(Distance);
    const float FarGainRaw = std::sqrt(NearGainRaw); // deliberately weaker than the near ear - see header

    const float ReferenceGain = OnAxisDcGain(ReferenceSpeakerDistanceMeters);
    const float NearGainNormalized = NearGainRaw / ReferenceGain;
    const float FarGainNormalized = FarGainRaw / std::sqrt(ReferenceGain);

    const float Azimuth = NormalizeAzimuthDegrees(AzimuthDegrees);
    const float AlphaFraction = LateralAngleDegrees(Azimuth) / 90.0f; // 0 at front/behind, 1 at full lateral

    // At AlphaFraction=0 (dead front/behind) neither ear is preferentially
    // close, so there's no ILD to widen - gain stays at 1 regardless of
    // distance (the broadband "closer=louder" cue is handled separately,
    // upstream - see AudioEngine).
    const float NearEarGain = 1.0f + (NearGainNormalized - 1.0f) * AlphaFraction;
    const float FarEarGain = 1.0f + (FarGainNormalized - 1.0f) * AlphaFraction;

    const std::vector<float> NearShelf = BuildBoostShelf(NearEarGain, SampleRate);
    const std::vector<float> FarShelf = BuildBoostShelf(FarEarGain, SampleRate);

    HrtfFilter Out;
    if (Azimuth > 0.0f)
    {
        // Source on the left: left ear is near, right ear is far - same
        // convention synthetic_hrtf.cpp uses.
        Out.Left = NearShelf;
        Out.Right = FarShelf;
    }
    else if (Azimuth < 0.0f)
    {
        Out.Right = NearShelf;
        Out.Left = FarShelf;
    }
    else
    {
        Out.Left = NearShelf;
        Out.Right = NearShelf;
    }
    return Out;
}

HrtfFilter ApplyNearFieldCorrection(const HrtfFilter& FarFieldFilter, float AzimuthDegrees,
                                     float DistanceMeters, float SampleRate)
{
    const HrtfFilter Correction = ComputeNearFieldCorrection(AzimuthDegrees, DistanceMeters, SampleRate);

    HrtfFilter Out;
    Out.Left = Convolve(FarFieldFilter.Left, Correction.Left);
    Out.Right = Convolve(FarFieldFilter.Right, Correction.Right);
    Out.DelayLeftSamples = FarFieldFilter.DelayLeftSamples;
    Out.DelayRightSamples = FarFieldFilter.DelayRightSamples;
    return Out;
}

} // namespace audiobat
