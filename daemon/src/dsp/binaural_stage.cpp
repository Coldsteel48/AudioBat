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

#include "synthetic_hrtf.hpp"

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

// Reference peak HRIR tap magnitude every SofaFile source gets normalized
// to (see the normalization pass in the constructor) - chosen to closely
// match the bundled default.sofa's own natural peak (~0.68), so its
// output level is essentially unchanged from before this normalization
// existed.
constexpr float TargetPeakTap = 0.7f;

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

BinauralStage::BinauralStage(const SpeakerLayout& InLayout, HrtfSourceKind Kind, const std::string& SofaPath,
                             float SampleRate)
    : Layout(InLayout), FallbackStage(InLayout)
{
    for (uint32_t k = 0; k < VirtualSpeakerCount; ++k)
    {
        const float AngleRadians = static_cast<float>(k) * (360.0f / VirtualSpeakerCount) * DegToRad;
        DecodeCos[k] = std::cos(AngleRadians);
        DecodeSin[k] = std::sin(AngleRadians);
    }

    if (Kind == HrtfSourceKind::SyntheticSphericalHead)
    {
        // Pure computation, can't fail to "load" the way a SOFA file can.
        bHrtfLoaded = true;
        for (uint32_t k = 0; k < VirtualSpeakerCount; ++k)
        {
            const float AzimuthDegrees = static_cast<float>(k) * (360.0f / VirtualSpeakerCount);
            const HrtfFilter Filter = ComputeSphericalHeadFilter(AzimuthDegrees, 0.0f, SampleRate);

            const std::vector<float> DelayedLeft = BuildDelayedFilter(Filter.Left, Filter.DelayLeftSamples);
            const std::vector<float> DelayedRight = BuildDelayedFilter(Filter.Right, Filter.DelayRightSamples);
            LeftConvolvers[k].Load(DelayedLeft.data(), static_cast<uint32_t>(DelayedLeft.size()));
            RightConvolvers[k].Load(DelayedRight.data(), static_cast<uint32_t>(DelayedRight.size()));
        }
        fprintf(stderr, "[audiobatd] using synthetic spherical-head HRTF model @ %.0f Hz\n", SampleRate);
    }
    else
    {
        bHrtfLoaded = Hrtf.Open(SofaPath, SampleRate);
        if (bHrtfLoaded)
        {
            std::array<HrtfFilter, VirtualSpeakerCount> Filters;
            float PeakTap = 0.0f;
            for (uint32_t k = 0; k < VirtualSpeakerCount; ++k)
            {
                const float AzimuthDegrees = static_cast<float>(k) * (360.0f / VirtualSpeakerCount);
                Filters[k] = Hrtf.GetFilter(AzimuthDegrees, 0.0f);
                for (float Tap : Filters[k].Left)
                {
                    PeakTap = std::max(PeakTap, std::fabs(Tap));
                }
                for (float Tap : Filters[k].Right)
                {
                    PeakTap = std::max(PeakTap, std::fabs(Tap));
                }
            }

            // Different SOFA sources calibrate absolute HRIR amplitude very
            // differently (mic gain, measurement distance normalization,
            // etc.) - e.g. SADIE II's raw taps measured ~8x louder than
            // MIT KEMAR's SOFA conversion for the same direction. Without
            // this, switching HRTF sources from the GUI could suddenly
            // blast audio many times louder or quieter. Scale every
            // source to a fixed reference peak instead of trusting each
            // dataset's own calibration.
            const float NormalizationGain = PeakTap > 1e-6f ? TargetPeakTap / PeakTap : 1.0f;

            for (uint32_t k = 0; k < VirtualSpeakerCount; ++k)
            {
                for (float& Tap : Filters[k].Left)
                {
                    Tap *= NormalizationGain;
                }
                for (float& Tap : Filters[k].Right)
                {
                    Tap *= NormalizationGain;
                }

                const std::vector<float> DelayedLeft =
                    BuildDelayedFilter(Filters[k].Left, Filters[k].DelayLeftSamples);
                const std::vector<float> DelayedRight =
                    BuildDelayedFilter(Filters[k].Right, Filters[k].DelayRightSamples);
                LeftConvolvers[k].Load(DelayedLeft.data(), static_cast<uint32_t>(DelayedLeft.size()));
                RightConvolvers[k].Load(DelayedRight.data(), static_cast<uint32_t>(DelayedRight.size()));
            }
            fprintf(stderr, "[audiobatd] loaded HRTF SOFA file '%s' (%d taps @ %.0f Hz, normalized x%.3f)\n",
                    SofaPath.c_str(), Hrtf.FilterLength(), SampleRate, NormalizationGain);
        }
        else
        {
            fprintf(stderr, "[audiobatd] Advanced spatial mode will fall back to algebraic decode until a "
                             "valid HRTF SOFA file is available\n");
        }
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
