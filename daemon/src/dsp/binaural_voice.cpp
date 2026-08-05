// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "binaural_voice.hpp"

#include <algorithm>

#include "dsp_stage.hpp"
#include "near_field_filter.hpp"
#include "synthetic_hrtf.hpp"

namespace ramkolfx
{

namespace
{

HrtfFilter LookUpFarFieldFilter(HrtfSourceKind Kind, const HrtfLoader* Source, float AzimuthDegrees,
                                 float SampleRate)
{
    if (Kind == HrtfSourceKind::SyntheticSphericalHead)
    {
        return ComputeSphericalHeadFilter(AzimuthDegrees, 0.0f, SampleRate);
    }
    return Source->GetFilter(AzimuthDegrees, 0.0f);
}

std::shared_ptr<BinauralVoiceFilter> BuildVoiceFilter(HrtfSourceKind Kind, const HrtfLoader* Source,
                                                       float NormalizationGain, float AzimuthDegrees,
                                                       float DistanceMeters, float SampleRate)
{
    HrtfFilter FarField = LookUpFarFieldFilter(Kind, Source, AzimuthDegrees, SampleRate);
    for (float& Tap : FarField.Left)
    {
        Tap *= NormalizationGain;
    }
    for (float& Tap : FarField.Right)
    {
        Tap *= NormalizationGain;
    }

    const HrtfFilter Combined = ApplyNearFieldCorrection(FarField, AzimuthDegrees, DistanceMeters, SampleRate);
    return std::make_shared<BinauralVoiceFilter>(Combined);
}

} // namespace

BinauralVoiceFilter::BinauralVoiceFilter(const HrtfFilter& Filter)
{
    const std::vector<float> DelayedLeft = BuildDelayedHrtfFilter(Filter.Left, Filter.DelayLeftSamples);
    const std::vector<float> DelayedRight = BuildDelayedHrtfFilter(Filter.Right, Filter.DelayRightSamples);
    LeftConvolver.Load(DelayedLeft.data(), static_cast<uint32>(DelayedLeft.size()));
    RightConvolver.Load(DelayedRight.data(), static_cast<uint32>(DelayedRight.size()));

    ScratchLeft.assign(MaxProcessFrames, 0.0f);
    ScratchRight.assign(MaxProcessFrames, 0.0f);
}

void BinauralVoiceFilter::Process(const float* MonoInput, float* StereoOutput, uint32 Frames)
{
    std::fill_n(ScratchLeft.begin(), Frames, 0.0f);
    std::fill_n(ScratchRight.begin(), Frames, 0.0f);
    LeftConvolver.ProcessAccumulate(MonoInput, ScratchLeft.data(), Frames);
    RightConvolver.ProcessAccumulate(MonoInput, ScratchRight.data(), Frames);
    for (uint32 FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        StereoOutput[FrameIndex * 2 + 0] = ScratchLeft[FrameIndex];
        StereoOutput[FrameIndex * 2 + 1] = ScratchRight[FrameIndex];
    }
}

BinauralVoice::BinauralVoice(HrtfSourceKind Kind, const HrtfLoader* Source, float NormalizationGain,
                             float AzimuthDegrees, float DistanceMeters, float InSampleRate)
    : SampleRate(InSampleRate),
      Slot(BuildVoiceFilter(Kind, Source, NormalizationGain, AzimuthDegrees, DistanceMeters, SampleRate),
           static_cast<uint32>(0.05f * InSampleRate), // ~50ms crossfade
           ChannelsPerFrame)
{
}

void BinauralVoice::Rebuild(HrtfSourceKind Kind, const HrtfLoader* Source, float NormalizationGain,
                            float AzimuthDegrees, float DistanceMeters)
{
    Slot.Publish(BuildVoiceFilter(Kind, Source, NormalizationGain, AzimuthDegrees, DistanceMeters, SampleRate));
}

void BinauralVoice::CollectGarbage()
{
    Slot.CollectGarbage();
}

void BinauralVoice::Process(const float* MonoInput, float* StereoOutput, uint32 Frames)
{
    Slot.Process(
        [MonoInput](BinauralVoiceFilter& Filter, float* Out, uint32 InFrames)
        {
            Filter.Process(MonoInput, Out, InFrames);
        },
        StereoOutput, Frames);
}

} // namespace ramkolfx
