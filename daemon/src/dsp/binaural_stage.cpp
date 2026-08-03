// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "binaural_stage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>

namespace audiobat
{

namespace
{

constexpr float OneOverSqrtTwo = 0.70710678f;
constexpr float DegToRad = std::numbers::pi_v<float> / 180.0f;
constexpr float LfeGain = 0.5f; // matches AmbisonicsStage/PassthroughStage

// Basic ("sampling") first-order decode gain for N evenly-spaced virtual
// loudspeakers: for a regular N-point array, the pseudo-inverse of the
// encode matrix works out to S_k = (2/N) * (W/sqrt(2) + X*cos(theta_k) +
// Y*sin(theta_k)) - unlike the 2-speaker algebraic decode, N=8 already
// has enough spatial resolution that no extra hand-tuned width boost is
// needed here.
constexpr float VirtualSpeakerGain = 2.0f / static_cast<float>(BinauralStage::VirtualSpeakerCount);

// Raw 7.1 input channel order, matching the layout VirtualSink captures
// (same as AmbisonicsStage/PassthroughStage).
enum SevenOneChannel : uint32_t
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

// Prepends IntDelay zero samples to Taps, approximating the inter-aural
// time difference libmysofa reports separately from the FIR itself with
// an integer-sample delay. A future refinement could use a fractional
// (all-pass) delay for sub-sample accuracy.
std::vector<float> BuildDelayedFilter(const std::vector<float>& Taps, float DelaySamples)
{
    const uint32_t IntDelay = static_cast<uint32_t>(std::lround(std::max(0.0f, DelaySamples)));
    std::vector<float> Delayed(Taps.size() + IntDelay, 0.0f);
    std::copy(Taps.begin(), Taps.end(), Delayed.begin() + IntDelay);
    return Delayed;
}

} // namespace

BinauralStage::BinauralStage(const SpeakerLayout& InLayout, const std::string& SofaPath, float SampleRate)
    : Layout(InLayout), FallbackStage(InLayout)
{
    for (uint32_t k = 0; k < VirtualSpeakerCount; ++k)
    {
        const float AngleRadians = static_cast<float>(k) * (360.0f / VirtualSpeakerCount) * DegToRad;
        DecodeCos[k] = std::cos(AngleRadians);
        DecodeSin[k] = std::sin(AngleRadians);
    }

    bHrtfLoaded = Hrtf.Open(SofaPath, SampleRate);
    if (bHrtfLoaded)
    {
        for (uint32_t k = 0; k < VirtualSpeakerCount; ++k)
        {
            const float AzimuthDegrees = static_cast<float>(k) * (360.0f / VirtualSpeakerCount);
            const HrtfLoader::Filter Filter = Hrtf.GetFilter(AzimuthDegrees, 0.0f);

            const std::vector<float> DelayedLeft = BuildDelayedFilter(Filter.Left, Filter.DelayLeftSamples);
            const std::vector<float> DelayedRight = BuildDelayedFilter(Filter.Right, Filter.DelayRightSamples);
            LeftConvolvers[k].Load(DelayedLeft.data(), static_cast<uint32_t>(DelayedLeft.size()));
            RightConvolvers[k].Load(DelayedRight.data(), static_cast<uint32_t>(DelayedRight.size()));
        }
        fprintf(stderr, "[audiobatd] loaded HRTF SOFA file '%s' (%d taps @ %.0f Hz)\n", SofaPath.c_str(),
                Hrtf.FilterLength(), SampleRate);
    }
    else
    {
        fprintf(stderr, "[audiobatd] Advanced spatial mode will fall back to algebraic decode until a "
                         "valid HRTF SOFA file is available\n");
    }

    for (auto& Signal : VirtualSpeakerSignal)
    {
        Signal.assign(MaxProcessFrames, 0.0f);
    }
    MixLeft.assign(MaxProcessFrames, 0.0f);
    MixRight.assign(MaxProcessFrames, 0.0f);
}

void BinauralStage::Process(const float* Input, uint32_t InputChannels,
                             float* Output, uint32_t OutputChannels,
                             uint32_t Frames)
{
    if (InputChannels != SevenOneChannelCount || OutputChannels != 2)
    {
        std::memset(Output, 0, sizeof(float) * OutputChannels * Frames);
        return;
    }

    if (!bHrtfLoaded)
    {
        FallbackStage.Process(Input, InputChannels, Output, OutputChannels, Frames);
        return;
    }

    if (Frames > MaxProcessFrames)
    {
        Frames = MaxProcessFrames; // defensive; AudioEngine already clamps to this bound
    }

    // Snapshot live speaker positions once per block, not per sample -
    // they only change from control commands, never at audio rate.
    const SpeakerLayout::Directions Dirs = Layout.SnapshotDirections();

    for (uint32_t FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        const float* InFrame = Input + FrameIndex * InputChannels;

        const float Sources[SpeakerLayout::SpeakerCount] = {
            InFrame[FL], InFrame[FR], InFrame[FC], InFrame[RL], InFrame[RR], InFrame[SL], InFrame[SR],
        };

        float FieldW, FieldX, FieldY;
        SpeakerLayout::Encode(Sources, Dirs, FieldW, FieldX, FieldY);

        for (uint32_t k = 0; k < VirtualSpeakerCount; ++k)
        {
            VirtualSpeakerSignal[k][FrameIndex] =
                VirtualSpeakerGain * (FieldW * OneOverSqrtTwo + FieldX * DecodeCos[k] + FieldY * DecodeSin[k]);
        }
    }

    std::fill_n(MixLeft.begin(), Frames, 0.0f);
    std::fill_n(MixRight.begin(), Frames, 0.0f);
    for (uint32_t k = 0; k < VirtualSpeakerCount; ++k)
    {
        LeftConvolvers[k].ProcessAccumulate(VirtualSpeakerSignal[k].data(), MixLeft.data(), Frames);
        RightConvolvers[k].ProcessAccumulate(VirtualSpeakerSignal[k].data(), MixRight.data(), Frames);
    }

    for (uint32_t FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        const float* InFrame = Input + FrameIndex * InputChannels;
        float* OutFrame = Output + FrameIndex * OutputChannels;
        OutFrame[0] = MixLeft[FrameIndex] + LfeGain * InFrame[LFE];
        OutFrame[1] = MixRight[FrameIndex] + LfeGain * InFrame[LFE];
    }
}

} // namespace audiobat
