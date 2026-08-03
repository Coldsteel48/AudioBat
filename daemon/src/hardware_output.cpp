// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "hardware_output.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

namespace audiobat
{

namespace
{

void OnProcess(void* UserData)
{
    static_cast<HardwareOutput*>(UserData)->HandleProcess();
}

void OnStateChanged(void* UserData, enum pw_stream_state OldState, enum pw_stream_state NewState,
                     const char* ErrorMessage)
{
    (void)UserData;
    (void)OldState;
    fprintf(stderr, "[audiobatd] hardware output state: %s%s%s\n", pw_stream_state_as_string(NewState),
            ErrorMessage ? " error: " : "", ErrorMessage ? ErrorMessage : "");
}

const struct pw_stream_events StreamEvents = []
{
    struct pw_stream_events Events{};
    Events.version = PW_VERSION_STREAM_EVENTS;
    Events.process = OnProcess;
    Events.state_changed = OnStateChanged;
    return Events;
}();

} // namespace

HardwareOutput::HardwareOutput(pw_loop* InLoop, std::string InTargetNodeName)
    : Loop(InLoop), TargetNodeName(std::move(InTargetNodeName))
{
}

HardwareOutput::~HardwareOutput()
{
    if (Stream)
    {
        pw_stream_destroy(Stream);
    }
}

void HardwareOutput::SetFillCallback(FillCallback InCallback)
{
    Callback = std::move(InCallback);
}

bool HardwareOutput::Start()
{
    pw_properties* Props =
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                           PW_KEY_MEDIA_ROLE, "Music", PW_KEY_NODE_NAME, "audiobat_output",
                           PW_KEY_NODE_DESCRIPTION, "AudioBat Spatialized Output", nullptr);

    // Pin to a specific hardware node instead of "default sink": this is
    // what makes it safe to later set the AudioBat virtual sink as the
    // system default without the output stream looping back into it.
    //
    // target.object alone is only an initial-link hint - WirePlumber's
    // default-device policy relinks every stream lacking dont-reconnect to
    // the new default sink whenever it changes, target.object included.
    // Confirmed live: setting the virtual sink as default silently moved
    // this stream onto it, closing the loop for real. dont-reconnect is
    // what makes the pin actually stick across default-sink changes.
    pw_properties_set(Props, PW_KEY_TARGET_OBJECT, TargetNodeName.c_str());
    pw_properties_set(Props, PW_KEY_NODE_DONT_RECONNECT, "true");

    Stream = pw_stream_new_simple(Loop, "AudioBat Output", Props, &StreamEvents, this);
    if (!Stream)
    {
        fprintf(stderr, "[audiobatd] failed to create hardware output stream\n");
        return false;
    }

    return ConnectStream();
}

bool HardwareOutput::SetTargetNode(std::string NewTargetNodeName)
{
    if (NewTargetNodeName == TargetNodeName)
    {
        return true;
    }
    if (!Stream)
    {
        // Not started yet; Start() will pick up TargetNodeName below.
        TargetNodeName = std::move(NewTargetNodeName);
        return true;
    }

    // Assigning TargetNodeName here (rather than inside
    // ReconnectOnLoopThread) is safe even though it happens off the loop
    // thread: the loop thread only reads it via ConnectStream(), which is
    // invoked strictly after this assignment completes (pw_loop_invoke
    // below blocks until it runs).
    TargetNodeName = std::move(NewTargetNodeName);

    struct InvokeContext
    {
        HardwareOutput* Self;
        bool bSuccess = false;
    } Context{this};

    auto InvokeFunc = [](struct spa_loop*, bool, uint32_t, const void*, size_t, void* UserData) -> int
    {
        auto* Ctx = static_cast<InvokeContext*>(UserData);
        Ctx->bSuccess = Ctx->Self->ReconnectOnLoopThread();
        return 0;
    };

    // Blocking invoke: pw_stream_disconnect/update_properties/connect are
    // only safe to call from the PipeWire loop thread, and the caller
    // (ControlServer's client thread) needs the result before it can
    // reply, so there's no reason to make this async.
    pw_loop_invoke(Loop, InvokeFunc, 0, nullptr, 0, true, &Context);
    return Context.bSuccess;
}

bool HardwareOutput::ReconnectOnLoopThread()
{
    pw_stream_disconnect(Stream);

    // Same reasoning as Start(): target.object is only an initial-link
    // hint, dont-reconnect is what keeps WirePlumber's default-device
    // policy from relinking this stream onto the new default sink instead.
    pw_properties* Props =
        pw_properties_new(PW_KEY_TARGET_OBJECT, TargetNodeName.c_str(), PW_KEY_NODE_DONT_RECONNECT,
                           "true", nullptr);
    pw_stream_update_properties(Stream, &Props->dict);
    pw_properties_free(Props);

    return ConnectStream();
}

bool HardwareOutput::ConnectStream()
{
    uint8_t Buffer[1024];
    spa_pod_builder Builder = SPA_POD_BUILDER_INIT(Buffer, sizeof(Buffer));

    spa_audio_info_raw Info{};
    Info.format = SPA_AUDIO_FORMAT_F32;
    Info.rate = SampleRate;
    Info.channels = Channels;
    Info.position[0] = SPA_AUDIO_CHANNEL_FL;
    Info.position[1] = SPA_AUDIO_CHANNEL_FR;

    const spa_pod* Params[1];
    Params[0] = spa_format_audio_raw_build(&Builder, SPA_PARAM_EnumFormat, &Info);

    // AUTOCONNECT combined with the PW_KEY_TARGET_OBJECT property above
    // links to that specific named node rather than "whatever is default".
    const auto Flags = static_cast<enum pw_stream_flags>(
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);

    int Result = pw_stream_connect(Stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, Flags, Params, 1);
    if (Result < 0)
    {
        fprintf(stderr, "[audiobatd] failed to connect hardware output stream: %s\n",
                spa_strerror(Result));
        return false;
    }

    return true;
}

void HardwareOutput::HandleProcess()
{
    pw_buffer* PwBuffer = pw_stream_dequeue_buffer(Stream);
    if (!PwBuffer)
    {
        return;
    }

    spa_buffer* Buf = PwBuffer->buffer;
    float* Dst = static_cast<float*>(Buf->datas[0].data);
    if (!Dst)
    {
        pw_stream_queue_buffer(Stream, PwBuffer);
        return;
    }

    const uint32_t Stride = sizeof(float) * Channels;
    uint32_t Frames = Buf->datas[0].maxsize / Stride;
    if (PwBuffer->requested > 0)
    {
        Frames = std::min<uint32_t>(Frames, static_cast<uint32_t>(PwBuffer->requested));
    }

    uint32_t Written = Callback ? Callback(Dst, Frames) : 0;
    if (Written < Frames)
    {
        std::memset(Dst + Written * Channels, 0, (Frames - Written) * Stride);
    }

    Buf->datas[0].chunk->offset = 0;
    Buf->datas[0].chunk->stride = static_cast<int32_t>(Stride);
    Buf->datas[0].chunk->size = Frames * Stride;

    pw_stream_queue_buffer(Stream, PwBuffer);
}

} // namespace audiobat
