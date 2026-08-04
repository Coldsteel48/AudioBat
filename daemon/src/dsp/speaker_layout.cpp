// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "speaker_layout.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace audiobat
{

namespace
{

// 1/sqrt(2), the standard first-order ambisonics W-channel weight. Written
// as a literal rather than std::sqrt(2.0f): std::sqrt isn't portably
// constexpr in C++20, and the value is fixed at compile time.
constexpr float OneOverSqrtTwo = 0.70710678f;

constexpr float DegToRad = std::numbers::pi_v<float> / 180.0f;

// Default ITU-ish 7.1 speaker layout, indexed by SpeakerChannel. 0 = front,
// positive = left, negative = right. Shared by the constructor and
// ResetSpeakerPositions() so the two can't drift apart.
constexpr std::array<float, SpeakerLayout::SpeakerCount> DefaultSpeakerAzimuthDegrees = {
    30.0f, -30.0f, 0.0f, 135.0f, -135.0f, 90.0f, -90.0f,
};

} // namespace

SpeakerLayout::SpeakerLayout()
{
    ResetSpeakerPositions();
}

void SpeakerLayout::SetSpeakerAzimuth(SpeakerChannel Speaker, float AzimuthDegrees)
{
    SpeakerAzimuthDegrees[Speaker].store(AzimuthDegrees, std::memory_order_relaxed);
}

float SpeakerLayout::GetSpeakerAzimuth(SpeakerChannel Speaker) const
{
    return SpeakerAzimuthDegrees[Speaker].load(std::memory_order_relaxed);
}

void SpeakerLayout::SetSpeakerDistance(SpeakerChannel Speaker, float DistanceMeters)
{
    const float Clamped = std::clamp(DistanceMeters, MinSpeakerDistanceMeters, MaxSpeakerDistanceMeters);
    SpeakerDistanceMeters[Speaker].store(Clamped, std::memory_order_relaxed);
}

float SpeakerLayout::GetSpeakerDistance(SpeakerChannel Speaker) const
{
    return SpeakerDistanceMeters[Speaker].load(std::memory_order_relaxed);
}

void SpeakerLayout::SetSpeakerMuted(SpeakerChannel Speaker, bool bMuted)
{
    SpeakerMuted[Speaker].store(bMuted, std::memory_order_relaxed);
}

bool SpeakerLayout::IsSpeakerMuted(SpeakerChannel Speaker) const
{
    return SpeakerMuted[Speaker].load(std::memory_order_relaxed);
}

void SpeakerLayout::ResetSpeakerPositions()
{
    for (uint32_t Speaker = 0; Speaker < SpeakerCount; ++Speaker)
    {
        SpeakerAzimuthDegrees[Speaker].store(DefaultSpeakerAzimuthDegrees[Speaker], std::memory_order_relaxed);
        SpeakerDistanceMeters[Speaker].store(DefaultSpeakerDistanceMeters, std::memory_order_relaxed);
    }
}

SpeakerLayout::Directions SpeakerLayout::SnapshotDirections() const
{
    Directions Dirs;
    for (uint32_t Speaker = 0; Speaker < SpeakerCount; ++Speaker)
    {
        const float AzimuthRadians = SpeakerAzimuthDegrees[Speaker].load(std::memory_order_relaxed) * DegToRad;
        Dirs.Cos[Speaker] = std::cos(AzimuthRadians);
        Dirs.Sin[Speaker] = std::sin(AzimuthRadians);
    }
    return Dirs;
}

void SpeakerLayout::Encode(const float Sources[SpeakerCount], const Directions& Dirs,
                            float& OutFieldW, float& OutFieldX, float& OutFieldY)
{
    OutFieldW = 0.0f;
    OutFieldX = 0.0f;
    OutFieldY = 0.0f;
    for (uint32_t Speaker = 0; Speaker < SpeakerCount; ++Speaker)
    {
        OutFieldW += Sources[Speaker] * OneOverSqrtTwo;
        OutFieldX += Sources[Speaker] * Dirs.Cos[Speaker];
        OutFieldY += Sources[Speaker] * Dirs.Sin[Speaker];
    }
}

} // namespace audiobat
