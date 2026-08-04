// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "audio_engine.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <filesystem>

#include <pipewire/pipewire.h>

#include "control/control_server.hpp"
#include "device_registry.hpp"
#include "dsp/ambisonics_stage.hpp"
#include "dsp/binaural_stage.hpp"
#include "dsp/dsp_stage.hpp"
#include "dsp/hrtf_deck.hpp"
#include "dsp/passthrough_stage.hpp"
#include "hardware_output.hpp"
#include "hrtf_default_path.hpp"
#include "virtual_sink.hpp"

namespace audiobat
{

namespace
{
// Scratch/mix buffer sizing: generous relative to a typical PipeWire
// quantum (usually 1024 frames, occasionally up to a few thousand under
// high-latency settings). Blocks larger than this are clamped rather than
// risking an allocation on the realtime thread. Kept equal to
// DspStage::MaxProcessFrames so every stage's own scratch buffers (e.g.
// BinauralStage's virtual speaker signals) are sized consistently with
// what AudioEngine will ever actually pass to Process().
constexpr uint32_t MaxScratchFrames = MaxProcessFrames;

// Hardcoded for now (Arctis 7P+ Analog Stereo, from `wpctl status` /
// `pw-cli info <sink-id>`) - test-only target while the pipeline is being
// validated. A future control-protocol command will let the user pick any
// compatible stereo sink live instead of this being fixed at startup.
constexpr const char* TestOutputNodeName = "alsa_output.usb-SteelSeries_Arctis_7P_-00.analog-stereo";

// Raw 7.1 input channel order, matching the layout VirtualSink captures
// (same convention duplicated locally in ambisonics_stage.cpp and
// passthrough_stage.cpp).
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
};

// Maps SpeakerLayout::SpeakerChannel index (0..6, non-LFE order FL,FR,FC,
// RL,RR,SL,SR) to its frame offset within the 8-channel interleaved 7.1
// layout above.
constexpr uint32_t SpeakerToFrameIndex[SpeakerCount] = {FL, FR, FC, RL, RR, SL, SR};

// Test noise amplitude: loud enough to clearly identify a speaker, quiet
// enough not to be unpleasant or risk clipping once decoded/summed.
constexpr float TestNoiseGain = 0.25f;

} // namespace

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    Teardown();
}

std::string AudioEngine::ResolveHrtfSofaPath()
{
    if (const char* Override = std::getenv("AUDIOBAT_HRTF_SOFA"))
    {
        return Override;
    }
    return DefaultHrtfSofaPath;
}

std::vector<HrtfCatalogEntry> AudioEngine::BuildHrtfCatalog()
{
    std::vector<HrtfCatalogEntry> Result;
    Result.reserve(HrtfCatalogCount);
    for (size_t i = 0; i < HrtfCatalogCount; ++i)
    {
        const HrtfCatalogEntry& Entry = HrtfCatalog[i];
        if (Entry.Kind == HrtfSourceKind::SyntheticSphericalHead || std::filesystem::exists(Entry.Path))
        {
            Result.push_back(Entry);
        }
        else
        {
            fprintf(stderr, "[audiobatd] HRTF catalog entry '%s' skipped, file not found: %s\n",
                    Entry.DisplayName, Entry.Path);
        }
    }
    return Result;
}

int AudioEngine::Run()
{
    pw_init(nullptr, nullptr);
    bPipeWireInitialized = true;

    MainLoop = pw_main_loop_new(nullptr);
    if (!MainLoop)
    {
        fprintf(stderr, "[audiobatd] failed to create PipeWire main loop\n");
        Teardown();
        return 1;
    }
    Loop = pw_main_loop_get_loop(MainLoop);

    // Signal handlers must be registered before any other thread is
    // spawned (e.g. ControlServer's accept thread below): POSIX signal
    // masks are per-thread, so a thread created beforehand inherits SIGINT/
    // SIGTERM unblocked and the kernel can kill the whole process via
    // default disposition, bypassing this handler entirely.
    auto OnSignal = [](void* Data, int SignalNumber)
    {
        fprintf(stderr, "[audiobatd] received signal %d, shutting down\n", SignalNumber);
        static_cast<AudioEngine*>(Data)->Stop();
    };
    pw_loop_add_signal(Loop, SIGINT, OnSignal, this);
    pw_loop_add_signal(Loop, SIGTERM, OnSignal, this);

    DspScratch.resize(MaxScratchFrames * HardwareOutput::Channels);
    InputScratch.resize(MaxScratchFrames * VirtualSink::Channels);
    OffStage = std::make_unique<PassthroughStage>();
    BasicStage = std::make_unique<AmbisonicsStage>(SharedLayout);

    RuntimeHrtfCatalog = BuildHrtfCatalog();

    // ResolveHrtfSofaPath() (AUDIOBAT_HRTF_SOFA or the bundled default)
    // picks the initial HRTF source, same as before this catalog existed.
    // If it happens to match a catalog entry, report that entry as active
    // so the GUI's dropdown starts in sync; otherwise ActiveHrtfIndex just
    // stays at its 0 default, since an env-var override sits outside the
    // catalog entirely (see ResolveHrtfSofaPath's comment).
    const std::string InitialSofaPath = ResolveHrtfSofaPath();
    for (size_t i = 0; i < RuntimeHrtfCatalog.size(); ++i)
    {
        if (RuntimeHrtfCatalog[i].Kind == HrtfSourceKind::SofaFile &&
            InitialSofaPath == RuntimeHrtfCatalog[i].Path)
        {
            ActiveHrtfIndex.store(static_cast<uint8_t>(i), std::memory_order_relaxed);
            break;
        }
    }

    AdvancedStage = std::make_unique<HrtfDeck>(SharedLayout, HrtfSourceKind::SofaFile, InitialSofaPath,
                                                static_cast<float>(HardwareOutput::SampleRate));

    Sink = std::make_unique<VirtualSink>(Loop);
    Sink->SetAudioCallback(
        [this](const float* Interleaved, uint32_t Frames)
        {
            HandleVirtualSinkAudio(Interleaved, Frames);
        });
    if (!Sink->Start())
    {
        Teardown();
        return 1;
    }

    Output = std::make_unique<HardwareOutput>(Loop, TestOutputNodeName);
    Output->SetFillCallback(
        [this](float* Interleaved, uint32_t Frames)
        {
            return HandleHardwareOutputRequest(Interleaved, Frames);
        });
    if (!Output->Start())
    {
        Teardown();
        return 1;
    }

    Devices = std::make_unique<DeviceRegistry>(Loop);
    if (!Devices->Start())
    {
        Teardown();
        return 1;
    }

    Server = std::make_unique<ControlServer>(DefaultControlSocketPath());
    Server->SetCommandHandler(
        [this](const Command& InCommand)
        {
            return HandleControlCommand(InCommand);
        });
    if (!Server->Start())
    {
        Teardown();
        return 1;
    }

    fprintf(stderr, "[audiobatd] running (pass-through mode). Ctrl+C to stop.\n");
    pw_main_loop_run(MainLoop);

    Teardown();
    return 0;
}

void AudioEngine::Stop()
{
    if (MainLoop)
    {
        pw_main_loop_quit(MainLoop);
    }
}

void AudioEngine::Teardown()
{
    if (Server)
    {
        Server->Stop();
        Server.reset();
    }
    Devices.reset();
    Output.reset();
    Sink.reset();
    OffStage.reset();
    BasicStage.reset();
    AdvancedStage.reset();

    if (MainLoop)
    {
        pw_main_loop_destroy(MainLoop);
        MainLoop = nullptr;
        Loop = nullptr;
    }

    if (bPipeWireInitialized)
    {
        pw_deinit();
        bPipeWireInitialized = false;
    }
}

DspStage& AudioEngine::ActiveStage()
{
    switch (Mode.load(std::memory_order_relaxed))
    {
    case SpatialMode::Basic:
        return *BasicStage;
    case SpatialMode::Advanced:
        return *AdvancedStage;
    case SpatialMode::Off:
    default:
        return *OffStage;
    }
}

void AudioEngine::HandleVirtualSinkAudio(const float* Interleaved, uint32_t Frames)
{
    if (Frames > MaxScratchFrames)
    {
        Frames = MaxScratchFrames; // drop overflow rather than overrun the scratch buffer
    }

    if (bTestNoiseEnabled.load(std::memory_order_relaxed))
    {
        FillTestNoise(InputScratch.data(), Frames);
    }
    else
    {
        std::memcpy(InputScratch.data(), Interleaved, sizeof(float) * Frames * VirtualSink::Channels);
    }
    ApplySpeakerMute(InputScratch.data(), Frames);

    ActiveStage().Process(InputScratch.data(), VirtualSink::Channels, DspScratch.data(),
                           HardwareOutput::Channels, Frames);
    StereoMixBuffer.Push(DspScratch.data(), Frames * HardwareOutput::Channels);
}

void AudioEngine::ApplySpeakerMute(float* Interleaved, uint32_t Frames)
{
    bool MuteFlags[SpeakerCount];
    bool bAnyMuted = false;
    for (uint32_t Speaker = 0; Speaker < SpeakerCount; ++Speaker)
    {
        MuteFlags[Speaker] = SharedLayout.IsSpeakerMuted(static_cast<SpeakerLayout::SpeakerChannel>(Speaker));
        bAnyMuted |= MuteFlags[Speaker];
    }
    if (!bAnyMuted)
    {
        return;
    }

    for (uint32_t FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        float* Frame = Interleaved + FrameIndex * VirtualSink::Channels;
        for (uint32_t Speaker = 0; Speaker < SpeakerCount; ++Speaker)
        {
            if (MuteFlags[Speaker])
            {
                Frame[SpeakerToFrameIndex[Speaker]] = 0.0f;
            }
        }
    }
}

void AudioEngine::FillTestNoise(float* Interleaved, uint32_t Frames)
{
    for (uint32_t FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        float* Frame = Interleaved + FrameIndex * VirtualSink::Channels;
        for (uint32_t Channel = 0; Channel < VirtualSink::Channels; ++Channel)
        {
            if (Channel == LFE)
            {
                Frame[Channel] = 0.0f;
                continue;
            }
            // xorshift32: cheap, allocation-free, good enough decorrelation
            // between channels for identifying a speaker by ear.
            NoiseState ^= NoiseState << 13;
            NoiseState ^= NoiseState >> 17;
            NoiseState ^= NoiseState << 5;
            const float Uniform01 = static_cast<float>(NoiseState >> 8) * (1.0f / 16777216.0f); // 24 bits -> [0,1)
            Frame[Channel] = (Uniform01 * 2.0f - 1.0f) * TestNoiseGain;
        }
    }
}

uint32_t AudioEngine::HandleHardwareOutputRequest(float* Interleaved, uint32_t Frames)
{
    const size_t Popped = StereoMixBuffer.Pop(Interleaved, Frames * HardwareOutput::Channels);
    return static_cast<uint32_t>(Popped / HardwareOutput::Channels);
}

std::vector<uint8_t> AudioEngine::HandleControlCommand(const Command& InCommand)
{
    // Reclaims whatever HrtfDeck's last completed crossfade faded out of.
    // Not realtime-safe (may deallocate) - fine here, this runs on a
    // per-client control thread, never the audio callback. Piggybacked on
    // every command rather than needing a dedicated timer, since the GUI
    // already polls GetStatus at a steady ~4Hz.
    AdvancedStage->CollectGarbage();

    if (InCommand.CommandOpcode == Opcode::GetDevices)
    {
        return EncodeDeviceListResponse(Devices->GetDevices());
    }

    if (InCommand.CommandOpcode == Opcode::GetHrtfCatalog)
    {
        std::vector<std::string> DisplayNames;
        DisplayNames.reserve(RuntimeHrtfCatalog.size());
        for (const HrtfCatalogEntry& Entry : RuntimeHrtfCatalog)
        {
            DisplayNames.emplace_back(Entry.DisplayName);
        }
        return EncodeHrtfCatalogResponse(DisplayNames);
    }

    if (InCommand.CommandOpcode == Opcode::SetSpatialMode)
    {
        Mode.store(InCommand.ModeValue, std::memory_order_relaxed);
        fprintf(stderr, "[audiobatd] spatial mode set to %d\n", static_cast<int>(InCommand.ModeValue));
    }
    else if (InCommand.CommandOpcode == Opcode::SetSpeakerAzimuth)
    {
        SharedLayout.SetSpeakerAzimuth(
            static_cast<SpeakerLayout::SpeakerChannel>(InCommand.SpeakerIndex), InCommand.AzimuthDegrees);
    }
    else if (InCommand.CommandOpcode == Opcode::ResetSpeakerPositions)
    {
        SharedLayout.ResetSpeakerAzimuths();
    }
    else if (InCommand.CommandOpcode == Opcode::SetOutputDevice)
    {
        if (!Output->SetTargetNode(InCommand.OutputDeviceName))
        {
            fprintf(stderr, "[audiobatd] failed to switch output device to %s\n",
                    InCommand.OutputDeviceName.c_str());
        }
    }
    else if (InCommand.CommandOpcode == Opcode::SetSpeakerMute)
    {
        SharedLayout.SetSpeakerMuted(static_cast<SpeakerLayout::SpeakerChannel>(InCommand.SpeakerIndex),
                                      InCommand.bMuted);
    }
    else if (InCommand.CommandOpcode == Opcode::SetTestNoise)
    {
        bTestNoiseEnabled.store(InCommand.bTestNoiseEnabled, std::memory_order_relaxed);
    }
    else if (InCommand.CommandOpcode == Opcode::SetHrtfFile)
    {
        // DecodeCommand only checked the payload shape, not that the index
        // is actually in range for this daemon's catalog - bounds-check
        // here before indexing.
        if (InCommand.HrtfIndex < RuntimeHrtfCatalog.size())
        {
            const HrtfCatalogEntry& Entry = RuntimeHrtfCatalog[InCommand.HrtfIndex];
            AdvancedStage->SwitchTo(Entry.Kind, Entry.Path);
            ActiveHrtfIndex.store(InCommand.HrtfIndex, std::memory_order_relaxed);
            fprintf(stderr, "[audiobatd] HRTF source switched to '%s'\n", Entry.DisplayName);
        }
        else
        {
            fprintf(stderr, "[audiobatd] SetHrtfFile: index %u out of range (catalog has %zu entries)\n",
                    InCommand.HrtfIndex, RuntimeHrtfCatalog.size());
        }
    }

    Status OutStatus;
    OutStatus.Mode = Mode.load(std::memory_order_relaxed);
    OutStatus.OutputDeviceName = Output->GetTargetNodeName();
    OutStatus.bTestNoiseEnabled = bTestNoiseEnabled.load(std::memory_order_relaxed);
    OutStatus.ActiveHrtfIndex = ActiveHrtfIndex.load(std::memory_order_relaxed);
    for (uint8_t i = 0; i < SpeakerCount; ++i)
    {
        OutStatus.SpeakerAzimuthDegrees[i] =
            SharedLayout.GetSpeakerAzimuth(static_cast<SpeakerLayout::SpeakerChannel>(i));
        OutStatus.SpeakerMuted[i] =
            SharedLayout.IsSpeakerMuted(static_cast<SpeakerLayout::SpeakerChannel>(i));
    }
    return EncodeStatusResponse(OutStatus);
}

} // namespace audiobat
