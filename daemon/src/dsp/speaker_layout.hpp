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

#include "audiobat/protocol.hpp"

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

    // Sets a virtual speaker's distance from the head, in meters; safe to
    // call from any thread. Clamped to [MinSpeakerDistanceMeters,
    // MaxSpeakerDistanceMeters]. Always settable regardless of whether
    // near-field mode is on - see SetNearFieldEnabled in AudioEngine -
    // it just has no audible effect while off.
    void SetSpeakerDistance(SpeakerChannel Speaker, float DistanceMeters);
    float GetSpeakerDistance(SpeakerChannel Speaker) const;

    // Mutes/unmutes a virtual speaker live; safe to call from any thread.
    // AudioEngine zeroes a muted speaker's source signal before it's
    // encoded into the shared field, so muting is independent of which
    // spatial mode is active.
    void SetSpeakerMuted(SpeakerChannel Speaker, bool bMuted);
    bool IsSpeakerMuted(SpeakerChannel Speaker) const;

    // Restores all 7 speakers to the default ITU-ish azimuths and default
    // distance; safe to call from any thread, same as SetSpeakerAzimuth.
    void ResetSpeakerPositions();

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
    std::array<std::atomic<float>, SpeakerCount> SpeakerDistanceMeters;
    std::array<std::atomic<bool>, SpeakerCount> SpeakerMuted{};
};

} // namespace audiobat
