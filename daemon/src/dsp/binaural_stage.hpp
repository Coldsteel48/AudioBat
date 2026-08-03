// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>
#include <string>
#include <vector>

#include "ambisonics_stage.hpp"
#include "dsp_stage.hpp"
#include "hrtf_loader.hpp"
#include "partitioned_convolver.hpp"
#include "speaker_layout.hpp"

namespace audiobat
{

// "Advanced" spatial mode: HRTF-based binaural decode. Encodes 7.1 input
// to the same first-order B-format field AmbisonicsStage uses (via the
// shared SpeakerLayout), decodes that field to a fixed 8-point virtual
// loudspeaker array, then convolves each virtual speaker's signal with
// that direction's measured HRIR (from a SOFA file, via HrtfLoader) and
// sums the results into stereo output. This is what actually preserves
// front/back distinction that a 2-speaker algebraic decode can't.
//
// If the configured SOFA file fails to load, falls back to delegating to
// the same algebraic decode AmbisonicsStage does, so Advanced mode never
// produces silence just because HRTF data is unavailable.
class BinauralStage final : public DspStage
{
public:
    // SofaPath: path to a SOFA-format HRTF file. SampleRate: the pipeline's
    // fixed sample rate (HRIRs are resampled to this rate on load).
    BinauralStage(const SpeakerLayout& InLayout, const std::string& SofaPath, float SampleRate);

    void Process(const float* Input, uint32_t InputChannels,
                 float* Output, uint32_t OutputChannels,
                 uint32_t Frames) override;

    static constexpr uint32_t VirtualSpeakerCount = 8;

private:
    const SpeakerLayout& Layout;
    bool bHrtfLoaded = false;

    // Reused as the "HRTF unavailable" fallback path, same pattern
    // AmbisonicsStage previously used for its own "3D off" fallback.
    AmbisonicsStage FallbackStage;

    HrtfLoader Hrtf;
    std::array<PartitionedConvolver, VirtualSpeakerCount> LeftConvolvers;
    std::array<PartitionedConvolver, VirtualSpeakerCount> RightConvolvers;

    // Fixed virtual speaker decode directions (0 deg = front, positive =
    // left), independent of SpeakerLayout's live-repositionable encode
    // azimuths - these are where BinauralStage renders the sound field
    // to before convolving, not where the 7.1 sources sit.
    std::array<float, VirtualSpeakerCount> DecodeCos{};
    std::array<float, VirtualSpeakerCount> DecodeSin{};

    // Per-block scratch, sized once against MaxProcessFrames so Process()
    // never allocates. VirtualSpeakerSignal[k] holds virtual speaker k's
    // mono decode for the current block; MixLeft/MixRight accumulate each
    // speaker's convolved contribution before final interleaving.
    std::array<std::vector<float>, VirtualSpeakerCount> VirtualSpeakerSignal;
    std::vector<float> MixLeft;
    std::vector<float> MixRight;
};

} // namespace audiobat
