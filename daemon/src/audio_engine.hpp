// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "audiobat/protocol.hpp"
#include "audiobat/ring_buffer.hpp"
#include "dsp/speaker_layout.hpp"
#include "hrtf_catalog.hpp"

struct pw_main_loop;
struct pw_loop;

namespace audiobat
{

class VirtualSink;
class HardwareOutput;
class DspStage;
class PassthroughStage;
class AmbisonicsStage;
class HrtfDeck;
class ControlServer;
class DeviceRegistry;

// Owns the whole daemon pipeline: PipeWire main loop, the virtual sink
// (capture), the DSP stages, the hardware output (playback), and the
// control socket. Bridges the capture and playback streams' process
// callbacks through a ring buffer since their block sizes aren't
// guaranteed to match.
//
// One instance of every spatial mode's stage (Off/Basic/Advanced) is kept
// alive at once; HandleVirtualSinkAudio() just dispatches to whichever
// Mode currently selects. All three encode from the same SharedLayout, so
// repositioning a virtual speaker affects whichever mode is active and
// GetStatus always reports one consistent layout regardless of mode.
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
    std::vector<uint8_t> HandleControlCommand(const Command& InCommand);

    // Zeroes out the non-LFE channels of any currently-muted speaker
    // in-place. Snapshots mute flags once per block, same pattern as
    // SpeakerLayout::SnapshotDirections - mute state only changes from
    // control commands, never at audio rate.
    void ApplySpeakerMute(float* Interleaved, uint32_t Frames);

    // Fills Interleaved with decorrelated white noise on every non-LFE
    // channel (LFE stays silent), for speaker-calibration purposes:
    // combined with ApplySpeakerMute, lets the user isolate one physical
    // speaker at a time regardless of what's actually playing.
    void FillTestNoise(float* Interleaved, uint32_t Frames);

    // Returns whichever concrete stage Mode currently selects.
    DspStage& ActiveStage();

    // Resolves the SOFA file BinauralStage should load initially: the
    // AUDIOBAT_HRTF_SOFA environment variable if set, else the bundled
    // default (see data/hrtf/README.md). The GetHrtfCatalog/SetHrtfFile
    // control opcodes are the normal way to switch afterward; this is a
    // lower-level override for a SOFA file outside the catalog entirely.
    static std::string ResolveHrtfSofaPath();

    // Filters the CMake-generated HrtfCatalog (hrtf_catalog.hpp) down to
    // entries that are actually usable right now: SyntheticSphericalHead
    // always is, SofaFile entries need their file present on disk (a
    // bundled SADIE subject could be missing if data/hrtf/sadie/ wasn't
    // populated). Runs once at startup.
    static std::vector<HrtfCatalogEntry> BuildHrtfCatalog();

    bool bPipeWireInitialized = false;
    pw_main_loop* MainLoop = nullptr;
    pw_loop* Loop = nullptr;

    std::unique_ptr<VirtualSink> Sink;
    std::unique_ptr<HardwareOutput> Output;
    std::unique_ptr<ControlServer> Server;
    std::unique_ptr<DeviceRegistry> Devices;

    SpeakerLayout SharedLayout;
    std::atomic<SpatialMode> Mode{SpatialMode::Off};
    std::atomic<bool> bTestNoiseEnabled{false};

    // State for FillTestNoise()'s xorshift32 PRNG; advanced once per
    // generated sample. Only ever touched from the realtime audio thread.
    uint32_t NoiseState = 0x9E3779B9u;

    std::unique_ptr<PassthroughStage> OffStage;
    std::unique_ptr<AmbisonicsStage> BasicStage;
    std::unique_ptr<HrtfDeck> AdvancedStage;

    // Result of BuildHrtfCatalog(), fixed for the process lifetime; index
    // into this is what the GetHrtfCatalog/SetHrtfFile/Status wire values
    // mean. Entries point at static-duration strings from the generated
    // HrtfCatalog, so copying HrtfCatalogEntry by value here is cheap and
    // safe.
    std::vector<HrtfCatalogEntry> RuntimeHrtfCatalog;
    std::atomic<uint8_t> ActiveHrtfIndex{0};

    // Holds processed (post-DSP) interleaved stereo samples awaiting
    // playback. Sized generously relative to a typical PipeWire quantum.
    RingBuffer<float> StereoMixBuffer{48000 * 2}; // ~1s of stereo audio at 48kHz

    // Scratch buffer for the DSP stage's stereo output, reused per virtual
    // sink callback to avoid allocating on the realtime thread.
    std::vector<float> DspScratch;

    // Holds the 7.1 signal actually fed to the active DSP stage: either a
    // copy of the captured input or synthesized test noise, with muted
    // speakers zeroed - see FillTestNoise()/ApplySpeakerMute(). Always
    // copied into rather than mutating the captured buffer in place, since
    // that buffer belongs to VirtualSink.
    std::vector<float> InputScratch;
};

} // namespace audiobat
