// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <functional>
#include <string>

#include "audiobat/types.hpp"

struct pw_stream;
struct pw_loop;

namespace audiobat
{

// Wraps a PipeWire playback stream that sends the final processed stereo
// mix out to a specific real hardware sink, pinned by PipeWire node name
// rather than "whatever is currently the default sink". This is what makes
// it safe to set the AudioBat virtual sink as the system default: without
// a pinned target, the output stream would resolve "default" to the
// virtual sink itself and feed back into the capture side.
class HardwareOutput
{
public:
    static constexpr uint32 Channels = 2;
    static constexpr uint32 SampleRate = 48000;

    // Called on the PipeWire realtime thread when PipeWire needs more
    // audio. Implementations should fill up to `Frames` interleaved stereo
    // frames into `Interleaved` and return how many frames were written;
    // anything left unwritten is zero-filled by the caller.
    using FillCallback = std::function<uint32(float* Interleaved, uint32 Frames)>;

    // InTargetNodeName: PipeWire node.name of the real hardware sink to
    // pin to (e.g. "alsa_output.usb-SteelSeries_Arctis_7P_-00.analog-stereo",
    // see `pw-cli info <sink-id>` or `wpctl status`). Hardcoded by the
    // caller for now; a future control-protocol command will let the user
    // pick this live from any compatible stereo sink.
    HardwareOutput(pw_loop* InLoop, std::string InTargetNodeName);
    ~HardwareOutput();

    HardwareOutput(const HardwareOutput&) = delete;
    HardwareOutput& operator=(const HardwareOutput&) = delete;

    void SetFillCallback(FillCallback InCallback);

    // Creates the stream and connects it into the PipeWire graph, targeting
    // TargetNodeName specifically. Returns false on failure.
    bool Start();

    // Retargets an already-started stream to a different real hardware
    // sink live: disconnects, updates target.object, and reconnects with
    // the same format parameters Start() used - without recreating the
    // pw_stream object itself, so the realtime process callback wiring
    // stays intact. Returns false if the reconnect failed (the stream is
    // left disconnected in that case, same as a failed Start()).
    //
    // Safe to call from any thread: the actual pw_stream_* calls are only
    // safe on the PipeWire loop thread, so this hops over via
    // pw_loop_invoke (blocking) rather than touching Stream directly -
    // callers are typically a ControlServer client thread, not the loop
    // thread that owns Stream.
    bool SetTargetNode(std::string NewTargetNodeName);

    const std::string& GetTargetNodeName() const
    {
        return TargetNodeName;
    }

    // PipeWire callback trampoline target; not for external use.
    void HandleProcess();

private:
    // Builds format params and calls pw_stream_connect(); shared by Start()
    // and SetTargetNode() since both end with the same negotiation step.
    bool ConnectStream();

    // Runs on the PipeWire loop thread (invoked via pw_loop_invoke from
    // SetTargetNode()): disconnects, retargets, and reconnects Stream.
    bool ReconnectOnLoopThread();

    pw_loop* Loop;
    pw_stream* Stream = nullptr;
    FillCallback Callback;
    std::string TargetNodeName;
};

} // namespace audiobat
