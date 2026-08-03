// AudioDock
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioDock, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "audio_engine.hpp"

#include <csignal>
#include <cstdio>

#include <pipewire/pipewire.h>

#include "control/control_server.hpp"
#include "dsp/ambisonics_stage.hpp"
#include "dsp/dsp_stage.hpp"
#include "hardware_output.hpp"
#include "virtual_sink.hpp"

namespace audiodock
{

namespace
{
// Scratch/mix buffer sizing: generous relative to a typical PipeWire
// quantum (usually 1024 frames, occasionally up to a few thousand under
// high-latency settings). Blocks larger than this are clamped rather than
// risking an allocation on the realtime thread.
constexpr uint32_t MaxScratchFrames = 8192;

// Hardcoded for now (Arctis 7P+ Analog Stereo, from `wpctl status` /
// `pw-cli info <sink-id>`) - test-only target while the pipeline is being
// validated. A future control-protocol command will let the user pick any
// compatible stereo sink live instead of this being fixed at startup.
constexpr const char* TestOutputNodeName = "alsa_output.usb-SteelSeries_Arctis_7P_-00.analog-stereo";
} // namespace

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    Teardown();
}

int AudioEngine::Run()
{
    pw_init(nullptr, nullptr);
    bPipeWireInitialized = true;

    MainLoop = pw_main_loop_new(nullptr);
    if (!MainLoop)
    {
        fprintf(stderr, "[audiodockd] failed to create PipeWire main loop\n");
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
        fprintf(stderr, "[audiodockd] received signal %d, shutting down\n", SignalNumber);
        static_cast<AudioEngine*>(Data)->Stop();
    };
    pw_loop_add_signal(Loop, SIGINT, OnSignal, this);
    pw_loop_add_signal(Loop, SIGTERM, OnSignal, this);

    DspScratch.resize(MaxScratchFrames * HardwareOutput::Channels);
    Stage = std::make_unique<AmbisonicsStage>();

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

    fprintf(stderr, "[audiodockd] running (pass-through mode). Ctrl+C to stop.\n");
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
    Output.reset();
    Sink.reset();
    Stage.reset();

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

void AudioEngine::HandleVirtualSinkAudio(const float* Interleaved, uint32_t Frames)
{
    if (Frames > MaxScratchFrames)
    {
        Frames = MaxScratchFrames; // drop overflow rather than overrun the scratch buffer
    }

    Stage->Process(Interleaved, VirtualSink::Channels, DspScratch.data(),
                    HardwareOutput::Channels, Frames);
    StereoMixBuffer.Push(DspScratch.data(), Frames * HardwareOutput::Channels);
}

uint32_t AudioEngine::HandleHardwareOutputRequest(float* Interleaved, uint32_t Frames)
{
    const size_t Popped = StereoMixBuffer.Pop(Interleaved, Frames * HardwareOutput::Channels);
    return static_cast<uint32_t>(Popped / HardwareOutput::Channels);
}

Status AudioEngine::HandleControlCommand(const Command& InCommand)
{
    if (InCommand.CommandOpcode == Opcode::SetThreeDEnabled)
    {
        Stage->SetThreeDEnabled(InCommand.bEnabledValue);
        fprintf(stderr, "[audiodockd] 3D processing %s\n", InCommand.bEnabledValue ? "enabled" : "disabled");
    }

    Status OutStatus;
    OutStatus.bThreeDEnabled = Stage->IsThreeDEnabled();
    return OutStatus;
}

} // namespace audiodock
