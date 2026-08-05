// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "graphic_eq_filter.hpp"

namespace ramkolfx
{

std::array<EqBand, MaxEqBands> DefaultHwEqBands()
{
    std::array<EqBand, MaxEqBands> Bands;
    for (size_t i = 0; i < MaxEqBands; ++i)
    {
        Bands[i].FilterType = EqFilterType::Peaking;
        Bands[i].FrequencyHz = DefaultEqCenterFrequenciesHz[i];
        Bands[i].GainDb = 0.0f;
        Bands[i].Q = DefaultGraphicEqQ;
    }
    return Bands;
}

GraphicEqFilter::GraphicEqFilter(const std::array<EqBand, MaxEqBands>& Bands, float SampleRate)
{
    for (size_t i = 0; i < MaxEqBands; ++i)
    {
        Left[i].SetCoefficients(Bands[i].FilterType, Bands[i].FrequencyHz, Bands[i].GainDb, Bands[i].Q,
                                 SampleRate);
        Right[i].SetCoefficients(Bands[i].FilterType, Bands[i].FrequencyHz, Bands[i].GainDb, Bands[i].Q,
                                  SampleRate);
    }
}

void GraphicEqFilter::Process(const float* Input, uint32 InputChannels, float* Output,
                               uint32 OutputChannels, uint32 Frames)
{
    (void)InputChannels;
    (void)OutputChannels;
    for (uint32 FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        float LeftSample = Input[FrameIndex * 2 + 0];
        float RightSample = Input[FrameIndex * 2 + 1];
        for (Biquad& Section : Left)
        {
            LeftSample = Section.ProcessSample(LeftSample);
        }
        for (Biquad& Section : Right)
        {
            RightSample = Section.ProcessSample(RightSample);
        }
        Output[FrameIndex * 2 + 0] = LeftSample;
        Output[FrameIndex * 2 + 1] = RightSample;
    }
}

} // namespace ramkolfx
