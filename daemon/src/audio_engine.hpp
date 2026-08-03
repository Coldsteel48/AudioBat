// AudioDock
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioDock, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <memory>
#include <vector>

#include "audiodock/protocol.hpp"
#include "audiodock/ring_buffer.hpp"

struct pw_main_loop;
struct pw_loop;

namespace audiodock
{

class VirtualSink;
class HardwareOutput;
class DspStage;
class ControlServer;

// Owns the whole daemon pipeline: PipeWire main loop, the virtual sink
// (capture), the DSP stage, the hardware output (playback), and the
// control socket. Bridges the capture and playback streams' process
// callbacks through a ring buffer since their block sizes aren't
// guaranteed to match.
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Sets everything up and runs the PipeWire main loop until Stop() is
    // called (e.g. from a signal handler) or a fatal error occurs. Returns
    // a process exit code.
    int Run();

    void Stop();

private:
    // Idempotent teardown, safe to call after a partial/failed setup and
    // again from the destructor. Order matters: streams and the control
    // server must be torn down before the main loop, which must be
    // destroyed before pw_deinit().
    void Teardown();

    void HandleVirtualSinkAudio(const float* Interleaved, uint32_t Frames);
    uint32_t HandleHardwareOutputRequest(float* Interleaved, uint32_t Frames);
    Status HandleControlCommand(const Command& InCommand);

    bool bPipeWireInitialized = false;
    pw_main_loop* MainLoop = nullptr;
    pw_loop* Loop = nullptr;

    std::unique_ptr<VirtualSink> Sink;
    std::unique_ptr<HardwareOutput> Output;
    std::unique_ptr<DspStage> Stage;
    std::unique_ptr<ControlServer> Server;

    // Holds processed (post-DSP) interleaved stereo samples awaiting
    // playback. Sized generously relative to a typical PipeWire quantum.
    RingBuffer<float> StereoMixBuffer{48000 * 2}; // ~1s of stereo audio at 48kHz

    // Scratch buffer for the DSP stage's stereo output, reused per virtual
    // sink callback to avoid allocating on the realtime thread.
    std::vector<float> DspScratch;
};

} // namespace audiodock
