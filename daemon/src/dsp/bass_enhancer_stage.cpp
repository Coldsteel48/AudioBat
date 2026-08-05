// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "bass_enhancer_stage.hpp"

namespace ramkolfx
{

BassEnhancerStage::BassEnhancerStage(const BassEnhancerSettings& InitialSettings, float InSampleRate)
    : SampleRate(InSampleRate),
      Slot(std::make_shared<BassEnhancerFilter>(InitialSettings, SampleRate),
           static_cast<uint32>(0.05f * InSampleRate), // ~50ms crossfade
           ChannelsPerFrame)
{
}

void BassEnhancerStage::SetSettings(const BassEnhancerSettings& Settings)
{
    Slot.Publish(std::make_shared<BassEnhancerFilter>(Settings, SampleRate));
}

void BassEnhancerStage::CollectGarbage()
{
    Slot.CollectGarbage();
}

void BassEnhancerStage::Process(const float* Input, uint32 InputChannels, float* Output,
                                 uint32 OutputChannels, uint32 Frames)
{
    Slot.Process(
        [Input, InputChannels, OutputChannels](BassEnhancerFilter& Filter, float* Out, uint32 InFrames)
        {
            Filter.Process(Input, InputChannels, Out, OutputChannels, InFrames);
        },
        Output, Frames);
}

} // namespace ramkolfx
