// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "bass_enhancer_filter.hpp"

#include <algorithm>
#include <cmath>

namespace ramkolfx
{

namespace
{
// Butterworth Q for the isolation low-pass/high-pass pair - maximally
// flat, no resonant peak to color the split.
constexpr float BassEnhancerFilterQ = 0.7071067811865476f;

// Real program material's sub-bass band rarely gets anywhere near full
// scale (typical peaks land around 0.2-0.5, RMS well under that), so
// tanh(Settings.Drive * x) with Settings.Drive in its wire range of
// [0,1] stays almost perfectly linear - the waveshaper never actually
// saturates and next to no harmonics get generated. Mapping the 0..1
// knob onto this much larger internal gain range through an exponential
// curve (gentle near 0, aggressive near 1 - the usual feel for a "drive"
// control) pushes typical bass content into the nonlinear region where
// tanh actually produces audible harmonics.
constexpr float MinInternalGain = 1.0f;
constexpr float MaxInternalGain = 20.0f;
} // namespace

BassEnhancerFilter::BassEnhancerFilter(const BassEnhancerSettings& Settings, float SampleRate)
    : bEnabled(Settings.bEnabled),
      Drive(MinInternalGain *
            std::pow(MaxInternalGain / MinInternalGain, std::clamp(Settings.Drive, 0.0f, 1.0f))),
      Mix(Settings.Mix)
{
    TanhDrive = std::tanh(Drive);
    LowPass.SetCoefficients(EqFilterType::LowPass, Settings.CutoffHz, 0.0f, BassEnhancerFilterQ, SampleRate);
    HighPass.SetCoefficients(EqFilterType::HighPass, Settings.CutoffHz, 0.0f, BassEnhancerFilterQ,
                              SampleRate);
}

void BassEnhancerFilter::Process(const float* Input, uint32 InputChannels, float* Output,
                                  uint32 OutputChannels, uint32 Frames)
{
    (void)InputChannels;
    (void)OutputChannels;

    if (!bEnabled)
    {
        std::copy(Input, Input + static_cast<size_t>(Frames) * 2, Output);
        return;
    }

    for (uint32 FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        const float LeftSample = Input[FrameIndex * 2 + 0];
        const float RightSample = Input[FrameIndex * 2 + 1];

        const float MonoLow = LowPass.ProcessSample(0.5f * (LeftSample + RightSample));
        const float Driven = std::tanh(Drive * MonoLow) / TanhDrive;
        const float Harmonics = HighPass.ProcessSample(Driven);
        const float Wet = Harmonics * Mix;

        Output[FrameIndex * 2 + 0] = LeftSample + Wet;
        Output[FrameIndex * 2 + 1] = RightSample + Wet;
    }
}

} // namespace ramkolfx
