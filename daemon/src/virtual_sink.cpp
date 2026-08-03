// AudioDock
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioDock, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "virtual_sink.hpp"

#include <cstdio>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

namespace audiodock
{

namespace
{

void OnProcess(void* UserData)
{
    static_cast<VirtualSink*>(UserData)->HandleProcess();
}

void OnStateChanged(void* UserData, enum pw_stream_state OldState, enum pw_stream_state NewState,
                     const char* ErrorMessage)
{
    (void)UserData;
    (void)OldState;
    fprintf(stderr, "[audiodockd] virtual sink state: %s%s%s\n", pw_stream_state_as_string(NewState),
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

VirtualSink::VirtualSink(pw_loop* InLoop) : Loop(InLoop)
{
}

VirtualSink::~VirtualSink()
{
    if (Stream)
    {
        pw_stream_destroy(Stream);
    }
}

void VirtualSink::SetAudioCallback(AudioCallback InCallback)
{
    Callback = std::move(InCallback);
}

bool VirtualSink::Start()
{
    pw_properties* Props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
                                              "Capture", PW_KEY_MEDIA_CLASS, "Audio/Sink",
                                              PW_KEY_NODE_NAME, "audiodock_virtual_sink",
                                              PW_KEY_NODE_DESCRIPTION, "AudioDock Virtual Sink",
                                              PW_KEY_NODE_VIRTUAL, "true", PW_KEY_AUDIO_CHANNELS,
                                              "8", nullptr);

    Stream = pw_stream_new_simple(Loop, "AudioDock Virtual Sink", Props, &StreamEvents, this);
    if (!Stream)
    {
        fprintf(stderr, "[audiodockd] failed to create virtual sink stream\n");
        return false;
    }

    uint8_t Buffer[1024];
    spa_pod_builder Builder = SPA_POD_BUILDER_INIT(Buffer, sizeof(Buffer));

    spa_audio_info_raw Info{};
    Info.format = SPA_AUDIO_FORMAT_F32;
    Info.rate = SampleRate;
    Info.channels = Channels;
    Info.position[0] = SPA_AUDIO_CHANNEL_FL;
    Info.position[1] = SPA_AUDIO_CHANNEL_FR;
    Info.position[2] = SPA_AUDIO_CHANNEL_FC;
    Info.position[3] = SPA_AUDIO_CHANNEL_LFE;
    Info.position[4] = SPA_AUDIO_CHANNEL_RL;
    Info.position[5] = SPA_AUDIO_CHANNEL_RR;
    Info.position[6] = SPA_AUDIO_CHANNEL_SL;
    Info.position[7] = SPA_AUDIO_CHANNEL_SR;

    const spa_pod* Params[1];
    Params[0] = spa_format_audio_raw_build(&Builder, SPA_PARAM_EnumFormat, &Info);

    // We ARE the sink (a fixed node in the graph), so we deliberately don't
    // autoconnect anywhere - other clients connect to us.
    const auto Flags = static_cast<enum pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS |
                                                          PW_STREAM_FLAG_RT_PROCESS);

    int Result = pw_stream_connect(Stream, PW_DIRECTION_INPUT, PW_ID_ANY, Flags, Params, 1);
    if (Result < 0)
    {
        fprintf(stderr, "[audiodockd] failed to connect virtual sink stream: %s\n",
                spa_strerror(Result));
        return false;
    }

    return true;
}

void VirtualSink::HandleProcess()
{
    pw_buffer* PwBuffer = pw_stream_dequeue_buffer(Stream);
    if (!PwBuffer)
    {
        return;
    }

    spa_buffer* Buf = PwBuffer->buffer;
    const uint8_t* Data = static_cast<const uint8_t*>(Buf->datas[0].data);
    if (Data && Buf->datas[0].chunk)
    {
        const uint32_t Stride = sizeof(float) * Channels;
        const uint32_t Frames = Buf->datas[0].chunk->size / Stride;
        if (Callback && Frames > 0)
        {
            Callback(reinterpret_cast<const float*>(Data), Frames);
        }
    }

    pw_stream_queue_buffer(Stream, PwBuffer);
}

} // namespace audiodock
