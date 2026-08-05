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
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "audiobat/protocol.hpp"
#include "audiobat/ring_buffer.hpp"
#include "audiobat/types.hpp"
#include "dsp/speaker_layout.hpp"
#include "hrtf_catalog.hpp"
#include "settings_store.hpp"

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
class SingleInstanceLock;

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

    void HandleVirtualSinkAudio(const float* Interleaved, uint32 Frames);
    uint32 HandleHardwareOutputRequest(float* Interleaved, uint32 Frames);
    std::vector<uint8> HandleControlCommand(const Command& InCommand);

    // Saves CurrentStatus (as returned to the control client) plus the
    // HRTF display name it maps to via RuntimeHrtfCatalog. Called from
    // HandleControlCommand() after any command that changes persisted
    // state - see PersistedSettings for what's included.
    void PersistCurrentSettings(const Status& CurrentStatus);

    // Zeroes out the non-LFE channels of any currently-muted speaker
    // in-place. Snapshots mute flags once per block, same pattern as
    // SpeakerLayout::SnapshotDirections - mute state only changes from
    // control commands, never at audio rate.
    void ApplySpeakerMute(float* Interleaved, uint32 Frames);

    // Scales each non-LFE channel by an inverse-distance loudness falloff
    // (1.0 at ReferenceSpeakerDistanceMeters, louder when closer, quieter
    // when farther) when near-field mode is on; a no-op otherwise. Applies
    // in every spatial mode (unlike the near-field ILD filter, which is
    // Advanced-only inside BinauralStage) since it's just a gain, no
    // per-ear meaning required - see docs/near-field-distance-plan.md.
    void ApplyNearFieldLoudnessFalloff(float* Interleaved, uint32 Frames);

    // Fills Interleaved with decorrelated white noise on every non-LFE
    // channel (LFE stays silent), for speaker-calibration purposes:
    // combined with ApplySpeakerMute, lets the user isolate one physical
    // speaker at a time regardless of what's actually playing.
    void FillTestNoise(float* Interleaved, uint32 Frames);

    // Returns whichever concrete stage Mode currently selects.
    DspStage& ActiveStage();

    // Picks the output device Run() should start HardwareOutput on: a
    // persisted choice from LoadedSettings if it's non-empty and still
    // present in Devices (populated via a prior WaitForInitialSync() call),
    // otherwise an arbitrary currently-available real sink (first run, or
    // the saved device having disappeared - e.g. unplugged/renamed since
    // last run), otherwise the last-resort hardcoded TestOutputNodeName if
    // Devices has discovered no real sinks at all yet. Doesn't persist the
    // result itself - Run() does that if it differs from what was loaded.
    std::string ResolveInitialOutputDevice(const std::optional<PersistedSettings>& LoadedSettings) const;

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

    // Resolves the directory the daemon watches for user-supplied SOFA
    // files: AUDIOBAT_HRTF_DIR if set, else $XDG_CONFIG_HOME/audiobat/hrtf
    // (falling back to ~/.config/audiobat/hrtf, mirroring SettingsStore's
    // own XDG resolution), created if it doesn't exist yet. Unlike the
    // bundled catalog, files placed here are never vetted for license or
    // validity - see data/hrtf/README.md.
    static std::string ResolveUserHrtfDirectory();

    // Scans DirectoryPath for *.sofa files (case-insensitive extension),
    // sorted alphabetically by path. Only checks that each file exists and
    // has the right extension, same as BuildHrtfCatalog's own bundled-file
    // check - actual SOFA parsing happens lazily in HrtfLoader::Open when
    // an entry is selected, and fails gracefully on a corrupt file.
    static std::vector<HrtfCatalogEntry> ScanUserHrtfDirectory(const std::string& DirectoryPath);

    // Rebuilds RuntimeHrtfCatalog from BuildHrtfCatalog() plus a fresh
    // ScanUserHrtfDirectory(UserHrtfDirectory), under HrtfCatalogMutex.
    // Called once at startup and again from OnHrtfDirectoryChanged
    // whenever the user directory's contents change. If the currently
    // active entry shifted index (or disappeared) since the last build,
    // re-locates it by display name so ActiveHrtfIndex doesn't silently
    // end up pointing at a different entry than what's actually loaded -
    // same reasoning as PersistedSettings::ActiveHrtfDisplayName.
    void RebuildHrtfCatalog();

    // inotify readiness callback for UserHrtfDirectory, hooked into the
    // PipeWire main loop via pw_loop_add_io in Run() (the same facility
    // Run() already uses for SIGINT/SIGTERM) rather than polling on a
    // timer, so new/removed files are picked up immediately. Runs on the
    // PipeWire main loop thread - see HrtfCatalogMutex's doc comment for
    // why that matters.
    static void OnHrtfDirectoryChanged(void* Data, int Fd, uint32 Mask);

    bool bPipeWireInitialized = false;
    pw_main_loop* MainLoop = nullptr;
    pw_loop* Loop = nullptr;

    // Acquired first, before anything PipeWire-related, so a second
    // instance refuses to start rather than creating a duplicate virtual
    // sink. See single_instance_lock.hpp.
    std::unique_ptr<SingleInstanceLock> InstanceLock;

    std::unique_ptr<VirtualSink> Sink;
    std::unique_ptr<HardwareOutput> Output;
    std::unique_ptr<ControlServer> Server;
    std::unique_ptr<DeviceRegistry> Devices;

    // Loaded once at startup (Run()) to seed initial state below, and
    // saved to after any control command that changes persisted state -
    // see PersistCurrentSettings().
    SettingsStore Settings;

    SpeakerLayout SharedLayout;
    std::atomic<SpatialMode> Mode{SpatialMode::Off};
    std::atomic<bool> bTestNoiseEnabled{false};
    std::atomic<bool> bNearFieldEnabled{false};

    // State for FillTestNoise()'s xorshift32 PRNG; advanced once per
    // generated sample. Only ever touched from the realtime audio thread.
    uint32 NoiseState = 0x9E3779B9u;

    std::unique_ptr<PassthroughStage> OffStage;
    std::unique_ptr<AmbisonicsStage> BasicStage;
    std::unique_ptr<HrtfDeck> AdvancedStage;

    // Bundled catalog (BuildHrtfCatalog()) plus whatever's currently in
    // UserHrtfDirectory; index into this is what the GetHrtfCatalog/
    // SetHrtfFile/Status wire values mean. No longer fixed for the process
    // lifetime - RebuildHrtfCatalog() replaces it whenever the user
    // directory changes, from the PipeWire main loop thread
    // (OnHrtfDirectoryChanged), while HandleControlCommand reads/writes it
    // from per-connection control threads (ControlServer::HandleClient) -
    // HrtfCatalogMutex guards every access from either side. Neither side
    // is the realtime audio callback, so a plain mutex (rather than
    // HrtfDeck's lock-free Slot/Publish pattern) is fine here.
    std::mutex HrtfCatalogMutex;
    std::vector<HrtfCatalogEntry> RuntimeHrtfCatalog;
    std::atomic<uint8> ActiveHrtfIndex{0};

    // Resolved once in Run() by ResolveUserHrtfDirectory(); re-scanned by
    // RebuildHrtfCatalog() on every OnHrtfDirectoryChanged callback.
    std::string UserHrtfDirectory;

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
