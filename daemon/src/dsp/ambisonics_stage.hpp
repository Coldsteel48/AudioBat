// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include "dsp_stage.hpp"
#include "speaker_layout.hpp"

namespace audiobat
{

// First-order horizontal ambisonics spatialization ("Basic" spatial mode):
// encodes each of the 7 non-LFE 7.1 channels as a mono point source at its
// speaker azimuth into a shared B-format sound field (W/X/Y) via the
// injected SpeakerLayout, then decodes that field to two virtual stereo
// speakers. Positioning comes from the field itself rather than a fixed
// per-channel pan/gain table, so moving a virtual speaker is just updating
// the azimuth SpeakerLayout reads next block - no structural change needed
// for live repositioning.
//
// This is a plain algebraic (non-HRTF) decode: it improves on a static
// downmix but a linear 2-speaker ambisonic decode can't fully preserve
// front/back distinction (that needs at least 3 non-collinear speakers,
// or a real binaural HRTF decode). See BinauralStage for the HRTF-based
// "Advanced" alternative, which shares this same SpeakerLayout encode step.
class AmbisonicsStage final : public DspStage
{
public:
    explicit AmbisonicsStage(const SpeakerLayout& InLayout) : Layout(InLayout)
    {
    }

    void Process(const float* Input, uint32_t InputChannels,
                 float* Output, uint32_t OutputChannels,
                 uint32_t Frames) override;

private:
    const SpeakerLayout& Layout;
};

} // namespace audiobat
