// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace audiobat
{

// Live-repositionable 7.1 virtual speaker azimuths, shared by every
// spatialization stage that needs to encode 7.1 input into a first-order
// B-format sound field (AmbisonicsStage's algebraic decode and
// BinauralStage's HRTF decode both start from the same encode). Owned once
// by AudioEngine so repositioning a speaker affects whichever mode is
// currently active and GetStatus always reports one consistent layout.
class SpeakerLayout
{
public:
    // The 7 non-LFE 7.1 source channels; LFE bypasses spatialization
    // entirely in every stage and is mixed in directly at a fixed gain.
    enum SpeakerChannel
    {
        SpeakerFL,
        SpeakerFR,
        SpeakerFC,
        SpeakerRL,
        SpeakerRR,
        SpeakerSL,
        SpeakerSR,
        SpeakerCount,
    };

    SpeakerLayout();

    // Repositions a virtual speaker live; safe to call from any thread.
    // AzimuthDegrees: 0 = front, positive = left, negative = right.
    void SetSpeakerAzimuth(SpeakerChannel Speaker, float AzimuthDegrees);
    float GetSpeakerAzimuth(SpeakerChannel Speaker) const;

    // Restores all 7 speakers to the default ITU-ish layout; safe to call
    // from any thread, same as SetSpeakerAzimuth.
    void ResetSpeakerAzimuths();

    // Per-speaker direction cosines/sines, snapshotted once per audio block
    // (not per sample - azimuths only change from control commands, never
    // at audio rate) and then reused by Encode() for every frame in that
    // block.
    struct Directions
    {
        std::array<float, SpeakerCount> Cos;
        std::array<float, SpeakerCount> Sin;
    };
    Directions SnapshotDirections() const;

    // Encodes one frame's worth of the 7 non-LFE point sources into a
    // shared first-order B-format field: W = omnidirectional, X =
    // front/back, Y = left/right. Sources must be ordered to match
    // SpeakerChannel.
    static void Encode(const float Sources[SpeakerCount], const Directions& Dirs,
                        float& OutFieldW, float& OutFieldX, float& OutFieldY);

private:
    std::array<std::atomic<float>, SpeakerCount> SpeakerAzimuthDegrees;
};

} // namespace audiobat
