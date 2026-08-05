// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "passthrough_stage.hpp"

#include <cstring>

namespace audiobat
{

namespace
{

// 7.1 channel order used throughout the pipeline (matches the SPA channel
// positions set on the virtual sink stream).
enum SevenOneChannel : uint32
{
    FL = 0,
    FR = 1,
    FC = 2,
    LFE = 3,
    RL = 4,
    RR = 5,
    SL = 6,
    SR = 7,
    SevenOneChannelCount = 8,
};

constexpr float CenterGain = 0.707f;
constexpr float SurroundGain = 0.5f;
constexpr float LfeGain = 0.5f;

} // namespace

void PassthroughStage::Process(const float* Input, uint32 InputChannels,
                                float* Output, uint32 OutputChannels,
                                uint32 Frames)
{
    if (InputChannels != SevenOneChannelCount || OutputChannels != 2)
    {
        // Not the shape this placeholder knows how to handle; output
        // silence rather than reading/writing out of bounds.
        std::memset(Output, 0, sizeof(float) * OutputChannels * Frames);
        return;
    }

    for (uint32 FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        const float* InFrame = Input + FrameIndex * InputChannels;
        float* OutFrame = Output + FrameIndex * OutputChannels;

        OutFrame[0] = InFrame[FL] + CenterGain * InFrame[FC] + SurroundGain * InFrame[RL] +
                      SurroundGain * InFrame[SL] + LfeGain * InFrame[LFE];
        OutFrame[1] = InFrame[FR] + CenterGain * InFrame[FC] + SurroundGain * InFrame[RR] +
                      SurroundGain * InFrame[SR] + LfeGain * InFrame[LFE];
    }
}

} // namespace audiobat
