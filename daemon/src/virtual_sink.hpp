// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <cstdint>
#include <functional>

struct pw_stream;
struct pw_loop;

namespace audiobat
{

// Wraps a PipeWire stream that presents itself to the rest of the system as
// an audio Sink ("AudioBat Virtual Sink"). Client applications (games,
// media players rendering 7.1 through OpenAL/PipeWire) send their output
// here instead of to real hardware; we consume it via the audio callback.
//
// PipeWire types are kept out of this header (only forward-declared) so
// nothing outside virtual_sink.cpp needs pipewire.h.
class VirtualSink
{
public:
    static constexpr uint32_t Channels = 8; // 7.1: FL, FR, FC, LFE, RL, RR, SL, SR
    static constexpr uint32_t SampleRate = 48000;

    // PipeWire node.name this stream registers under; also Audio/Sink, so
    // DeviceRegistry filters it out of the selectable hardware output list
    // by this same name (selecting ourselves as the hardware target would
    // loop the signal back into the capture side).
    static constexpr const char* NodeName = "audiobat_virtual_sink";

    // Called on the PipeWire realtime thread whenever a block of
    // interleaved float audio (Channels per frame) arrives.
    using AudioCallback = std::function<void(const float* Interleaved, uint32_t Frames)>;

    explicit VirtualSink(pw_loop* InLoop);
    ~VirtualSink();

    VirtualSink(const VirtualSink&) = delete;
    VirtualSink& operator=(const VirtualSink&) = delete;

    void SetAudioCallback(AudioCallback InCallback);

    // Creates the stream and connects it into the PipeWire graph as a sink.
    // Returns false on failure.
    bool Start();

    // PipeWire callback trampoline target; not for external use.
    void HandleProcess();

private:
    pw_loop* Loop;
    pw_stream* Stream = nullptr;
    AudioCallback Callback;
};

} // namespace audiobat
