// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "hrtf_deck.hpp"

namespace audiobat
{

HrtfDeck::HrtfDeck(const SpeakerLayout& InLayout, HrtfSourceKind Kind, const std::string& SofaPath,
                   float InSampleRate, bool bInitialNearFieldEnabled)
    : Layout(InLayout), SampleRate(InSampleRate), bNearFieldEnabled(bInitialNearFieldEnabled),
      Slot(std::make_shared<BinauralStage>(Layout, Kind, SofaPath, SampleRate, bInitialNearFieldEnabled),
           static_cast<uint32_t>(0.05f * InSampleRate), // ~50ms crossfade
           ChannelsPerFrame)
{
}

void HrtfDeck::SwitchTo(HrtfSourceKind Kind, const std::string& SofaPath)
{
    // Building a BinauralStage does real work (SOFA parse, convolver FFT
    // setup) - deliberately done here, off the realtime thread, before
    // anything realtime-visible changes.
    Slot.Publish(std::make_shared<BinauralStage>(Layout, Kind, SofaPath, SampleRate,
                                                  bNearFieldEnabled.load(std::memory_order_relaxed)));
}

void HrtfDeck::SetNearFieldEnabled(bool bEnabled)
{
    bNearFieldEnabled.store(bEnabled, std::memory_order_relaxed);
    Slot.ForwardToLive([bEnabled](BinauralStage& Stage) { Stage.SetNearFieldEnabled(bEnabled); });
}

void HrtfDeck::RebuildVoiceForSpeaker(SpeakerLayout::SpeakerChannel Speaker, float AzimuthDegrees,
                                      float DistanceMeters)
{
    Slot.ForwardToLive(
        [Speaker, AzimuthDegrees, DistanceMeters](BinauralStage& Stage)
        {
            Stage.RebuildVoiceForSpeaker(Speaker, AzimuthDegrees, DistanceMeters);
        });
}

void HrtfDeck::CollectGarbage()
{
    Slot.CollectGarbage();
    Slot.ForwardToLive([](BinauralStage& Stage) { Stage.CollectVoiceGarbage(); });
}

void HrtfDeck::Process(const float* Input, uint32_t InputChannels,
                       float* Output, uint32_t OutputChannels,
                       uint32_t Frames)
{
    Slot.Process(
        [Input, InputChannels, OutputChannels](BinauralStage& Stage, float* Out, uint32_t InFrames)
        {
            Stage.Process(Input, InputChannels, Out, OutputChannels, InFrames);
        },
        Output, Frames);
}

} // namespace audiobat
