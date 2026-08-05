// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "hw_eq_stage.hpp"

namespace ramkolfx
{

HwEqStage::HwEqStage(const std::array<EqBand, MaxEqBands>& InitialBands, float InSampleRate)
    : SampleRate(InSampleRate),
      Slot(std::make_shared<GraphicEqFilter>(InitialBands, SampleRate),
           static_cast<uint32>(0.05f * InSampleRate), // ~50ms crossfade
           ChannelsPerFrame)
{
}

void HwEqStage::SetBands(const std::array<EqBand, MaxEqBands>& Bands)
{
    Slot.Publish(std::make_shared<GraphicEqFilter>(Bands, SampleRate));
}

void HwEqStage::CollectGarbage()
{
    Slot.CollectGarbage();
}

void HwEqStage::Process(const float* Input, uint32 InputChannels, float* Output, uint32 OutputChannels,
                         uint32 Frames)
{
    Slot.Process(
        [Input, InputChannels, OutputChannels](GraphicEqFilter& Filter, float* Out, uint32 InFrames)
        {
            Filter.Process(Input, InputChannels, Out, OutputChannels, InFrames);
        },
        Output, Frames);
}

} // namespace ramkolfx
