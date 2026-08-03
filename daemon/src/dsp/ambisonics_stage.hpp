// AudioDock
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioDock, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>
#include <atomic>

#include "dsp_stage.hpp"
#include "passthrough_stage.hpp"

namespace audiodock
{

// First-order horizontal ambisonics spatialization: encodes each of the 7
// non-LFE 7.1 channels as a mono point source at its speaker azimuth into
// a shared B-format sound field (W/X/Y), then decodes that field to two
// virtual stereo speakers. Positioning comes from the field itself rather
// than a fixed per-channel pan/gain table, so moving a virtual speaker is
// just updating the azimuth this stage reads next block - no structural
// change needed for live repositioning.
//
// This is a plain algebraic (non-HRTF) decode: it improves on a static
// downmix but a linear 2-speaker ambisonic decode can't fully preserve
// front/back distinction (that needs at least 3 non-collinear speakers,
// or a real binaural HRTF decode). HRTF-based binaural decode is the
// planned upgrade path, swapped in behind this same DspStage interface.
class AmbisonicsStage final : public DspStage
{
public:
    // The 7 non-LFE 7.1 source channels; LFE bypasses spatialization
    // entirely and is mixed into both output channels at fixed gain, same
    // as PassthroughStage.
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

    AmbisonicsStage();

    void Process(const float* Input, uint32_t InputChannels,
                 float* Output, uint32_t OutputChannels,
                 uint32_t Frames) override;

    void SetThreeDEnabled(bool bEnabled) override
    {
        bThreeDEnabled.store(bEnabled, std::memory_order_relaxed);
    }

    bool IsThreeDEnabled() const override
    {
        return bThreeDEnabled.load(std::memory_order_relaxed);
    }

    // Repositions a virtual speaker live; safe to call from any thread.
    // AzimuthDegrees: 0 = front, positive = left, negative = right.
    void SetSpeakerAzimuth(SpeakerChannel Speaker, float AzimuthDegrees);
    float GetSpeakerAzimuth(SpeakerChannel Speaker) const;

private:
    std::atomic<bool> bThreeDEnabled{false};
    std::array<std::atomic<float>, SpeakerCount> SpeakerAzimuthDegrees;

    // Reused as the "3D disabled" fallback path so the two states are
    // genuinely different signal paths instead of the toggle being a
    // no-op, without duplicating the static downmix formula here too.
    PassthroughStage FallbackStage;
};

} // namespace audiodock
