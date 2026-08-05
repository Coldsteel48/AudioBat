// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "audio_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <filesystem>

#include <sys/inotify.h>
#include <unistd.h>

#include <pipewire/pipewire.h>

#include "control/control_server.hpp"
#include "device_registry.hpp"
#include "dsp/ambisonics_stage.hpp"
#include "dsp/binaural_stage.hpp"
#include "dsp/dsp_stage.hpp"
#include "dsp/hrtf_deck.hpp"
#include "dsp/hw_eq_stage.hpp"
#include "dsp/passthrough_stage.hpp"
#include "hardware_output.hpp"
#include "hrtf_default_path.hpp"
#include "single_instance_lock.hpp"
#include "virtual_sink.hpp"

namespace ramkolfx
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
constexpr uint32 MaxScratchFrames = MaxProcessFrames;

// Last-resort fallback, only reached if DeviceRegistry hasn't discovered
// any real hardware sink at all by the time Run() picks an initial output
// device (see ResolveInitialOutputDevice) - normally that never happens,
// since a persisted choice or an auto-picked live sink both win first.
constexpr const char* TestOutputNodeName = "alsa_output.usb-SteelSeries_Arctis_7P_-00.analog-stereo";

// Raw 7.1 input channel order, matching the layout VirtualSink captures
// (same convention duplicated locally in ambisonics_stage.cpp and
// passthrough_stage.cpp).
enum SevenOneChannel : uint32
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
constexpr uint32 SpeakerToFrameIndex[SpeakerCount] = {FL, FR, FC, RL, RR, SL, SR};

// Test noise amplitude: loud enough to clearly identify a speaker, quiet
// enough not to be unpleasant or risk clipping once decoded/summed.
constexpr float TestNoiseGain = 0.25f;

} // namespace

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    Teardown();
}

std::string AudioEngine::ResolveInitialOutputDevice(const std::optional<PersistedSettings>& LoadedSettings) const
{
    if (LoadedSettings && !LoadedSettings->OutputDeviceName.empty())
    {
        if (Devices->HasDevice(LoadedSettings->OutputDeviceName))
        {
            return LoadedSettings->OutputDeviceName;
        }
        fprintf(stderr,
                "[ramkolfxd] saved output device '%s' not found, falling back to auto-selection\n",
                LoadedSettings->OutputDeviceName.c_str());
    }

    if (const std::optional<std::string> Picked = Devices->PickAnyDevice())
    {
        return *Picked;
    }

    fprintf(stderr, "[ramkolfxd] no output devices found, falling back to '%s'\n", TestOutputNodeName);
    return TestOutputNodeName;
}

std::string AudioEngine::ResolveHrtfSofaPath()
{
    if (const char* Override = std::getenv("RAMKOLFX_HRTF_SOFA"))
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
            fprintf(stderr, "[ramkolfxd] HRTF catalog entry '%s' skipped, file not found: %s\n",
                    Entry.DisplayName.c_str(), Entry.Path.c_str());
        }
    }
    return Result;
}

std::string AudioEngine::ResolveUserHrtfDirectory()
{
    std::string Directory;
    if (const char* Override = std::getenv("RAMKOLFX_HRTF_DIR"); Override && *Override)
    {
        Directory = Override;
    }
    else
    {
        std::string ConfigDir;
        if (const char* XdgConfigHome = std::getenv("XDG_CONFIG_HOME"); XdgConfigHome && *XdgConfigHome)
        {
            ConfigDir = XdgConfigHome;
        }
        else if (const char* Home = std::getenv("HOME"); Home && *Home)
        {
            ConfigDir = std::string(Home) + "/.config";
        }
        else
        {
            ConfigDir = "/tmp"; // last resort; matches SettingsStore's own /tmp fallback
        }
        Directory = ConfigDir + "/ramkolfx/hrtf";
    }

    std::error_code Ignored;
    std::filesystem::create_directories(Directory, Ignored);
    return Directory;
}

std::vector<HrtfCatalogEntry> AudioEngine::ScanUserHrtfDirectory(const std::string& DirectoryPath)
{
    std::vector<HrtfCatalogEntry> Entries;
    std::error_code Ignored;
    for (const std::filesystem::directory_entry& DirEntry :
         std::filesystem::directory_iterator(DirectoryPath, Ignored))
    {
        if (!DirEntry.is_regular_file(Ignored))
        {
            continue;
        }
        std::string Extension = DirEntry.path().extension().string();
        std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                        [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
        if (Extension != ".sofa")
        {
            continue;
        }
        HrtfCatalogEntry Entry;
        Entry.DisplayName = "(user) " + DirEntry.path().stem().string();
        Entry.Kind = HrtfSourceKind::SofaFile;
        Entry.Path = DirEntry.path().string();
        Entries.push_back(std::move(Entry));
    }
    std::sort(Entries.begin(), Entries.end(),
              [](const HrtfCatalogEntry& A, const HrtfCatalogEntry& B) { return A.Path < B.Path; });
    return Entries;
}

void AudioEngine::RebuildHrtfCatalog()
{
    std::vector<HrtfCatalogEntry> NewCatalog = BuildHrtfCatalog();
    std::vector<HrtfCatalogEntry> UserEntries = ScanUserHrtfDirectory(UserHrtfDirectory);
    NewCatalog.insert(NewCatalog.end(), std::make_move_iterator(UserEntries.begin()),
                       std::make_move_iterator(UserEntries.end()));

    // HrtfIndex/ActiveHrtfIndex are a single byte on the wire (see
    // protocol.hpp) - drop excess user entries rather than silently
    // wrapping or corrupting an index elsewhere.
    constexpr size_t MaxCatalogEntries = 255;
    if (NewCatalog.size() > MaxCatalogEntries)
    {
        fprintf(stderr,
                "[ramkolfxd] HRTF catalog has %zu entries after scanning %s, truncating to %zu "
                "(the wire protocol's HrtfIndex is a single byte)\n",
                NewCatalog.size(), UserHrtfDirectory.c_str(), MaxCatalogEntries);
        NewCatalog.resize(MaxCatalogEntries);
    }

    std::lock_guard<std::mutex> Lock(HrtfCatalogMutex);
    const uint8 OldIndex = ActiveHrtfIndex.load(std::memory_order_relaxed);
    if (OldIndex < RuntimeHrtfCatalog.size())
    {
        const std::string OldDisplayName = RuntimeHrtfCatalog[OldIndex].DisplayName;
        for (size_t i = 0; i < NewCatalog.size(); ++i)
        {
            if (NewCatalog[i].DisplayName == OldDisplayName)
            {
                ActiveHrtfIndex.store(static_cast<uint8>(i), std::memory_order_relaxed);
                break;
            }
        }
    }
    RuntimeHrtfCatalog = std::move(NewCatalog);
}

void AudioEngine::OnHrtfDirectoryChanged(void* Data, int Fd, uint32 Mask)
{
    (void)Mask;

    // Drain every pending event before rebuilding - inotify can coalesce
    // many filesystem changes (e.g. `cp *.sofa dir/`) into one readiness
    // notification, and reading only one event per call would leave the
    // rest queued and immediately re-fire. The event contents aren't
    // needed: any change at all just triggers a full rescan.
    char EventBuffer[4096];
    while (read(Fd, EventBuffer, sizeof(EventBuffer)) > 0)
    {
    }
    static_cast<AudioEngine*>(Data)->RebuildHrtfCatalog();
}

int AudioEngine::Run()
{
    const std::string SocketPath = DefaultControlSocketPath();
    const std::string LockPath = SocketPath.substr(0, SocketPath.find_last_of('/')) + "/daemon.lock";
    InstanceLock = std::make_unique<SingleInstanceLock>(LockPath);
    if (!InstanceLock->TryAcquire())
    {
        fprintf(stderr, "[ramkolfxd] another instance is already running (pid %s), exiting\n",
                InstanceLock->HolderPid().empty() ? "unknown" : InstanceLock->HolderPid().c_str());
        return 1;
    }

    pw_init(nullptr, nullptr);
    bPipeWireInitialized = true;

    MainLoop = pw_main_loop_new(nullptr);
    if (!MainLoop)
    {
        fprintf(stderr, "[ramkolfxd] failed to create PipeWire main loop\n");
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
        fprintf(stderr, "[ramkolfxd] received signal %d, shutting down\n", SignalNumber);
        static_cast<AudioEngine*>(Data)->Stop();
    };
    pw_loop_add_signal(Loop, SIGINT, OnSignal, this);
    pw_loop_add_signal(Loop, SIGTERM, OnSignal, this);

    DspScratch.resize(MaxScratchFrames * HardwareOutput::Channels);
    InputScratch.resize(MaxScratchFrames * VirtualSink::Channels);
    OffStage = std::make_unique<PassthroughStage>();
    BasicStage = std::make_unique<AmbisonicsStage>(SharedLayout);

    // Restores whatever the user last chose via the control protocol (mode,
    // speaker layout, near-field toggle) - see settings_store.hpp. Applied
    // to SharedLayout before AdvancedStage is constructed below, since
    // BinauralStage reads speaker azimuth/distance directly from it at
    // construction time rather than picking them up later.
    const std::optional<PersistedSettings> LoadedSettings = Settings.Load();
    if (LoadedSettings)
    {
        Mode.store(LoadedSettings->Mode, std::memory_order_relaxed);
        bNearFieldEnabled.store(LoadedSettings->bNearFieldEnabled, std::memory_order_relaxed);
        for (uint8 i = 0; i < SpeakerCount; ++i)
        {
            const auto Channel = static_cast<SpeakerLayout::SpeakerChannel>(i);
            SharedLayout.SetSpeakerAzimuth(Channel, LoadedSettings->SpeakerAzimuthDegrees[i]);
            SharedLayout.SetSpeakerDistance(Channel, LoadedSettings->SpeakerDistanceMeters[i]);
            SharedLayout.SetSpeakerMuted(Channel, LoadedSettings->SpeakerMuted[i]);
        }
        fprintf(stderr, "[ramkolfxd] restored settings from previous session\n");
    }

    {
        std::lock_guard<std::mutex> Lock(HwEqMutex);
        HwEqBandState = LoadedSettings ? LoadedSettings->HwEqBands : DefaultHwEqBands();
    }
    HwEq = std::make_unique<HwEqStage>(HwEqBandState, static_cast<float>(HardwareOutput::SampleRate));

    // Directory the daemon watches for user-supplied SOFA files - unlike
    // the bundled catalog, this isn't rebuilt on a timer: the inotify
    // watch set up below rebuilds it the moment a file is added or
    // removed, so a running daemon never needs restarting to pick up new
    // files (see ScanUserHrtfDirectory's doc comment).
    UserHrtfDirectory = ResolveUserHrtfDirectory();
    RebuildHrtfCatalog();

    const int HrtfDirWatchFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (HrtfDirWatchFd < 0)
    {
        fprintf(stderr, "[ramkolfxd] inotify_init1 failed: %s (user HRTF directory won't auto-refresh)\n",
                strerror(errno));
    }
    else if (inotify_add_watch(HrtfDirWatchFd, UserHrtfDirectory.c_str(),
                                IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE) < 0)
    {
        fprintf(stderr, "[ramkolfxd] failed to watch HRTF directory %s: %s (won't auto-refresh)\n",
                UserHrtfDirectory.c_str(), strerror(errno));
        close(HrtfDirWatchFd);
    }
    else
    {
        // close=true: PipeWire owns closing this fd when the loop (and
        // therefore this source) is torn down in Teardown(), so no
        // matching cleanup is needed there - same as the pw_loop_add_signal
        // sources registered above needing no explicit removal.
        pw_loop_add_io(Loop, HrtfDirWatchFd, SPA_IO_IN, true, &AudioEngine::OnHrtfDirectoryChanged, this);
    }

    // ResolveHrtfSofaPath() (RAMKOLFX_HRTF_SOFA or the bundled default)
    // picks the initial HRTF source unless a persisted choice overrides it
    // below. RAMKOLFX_HRTF_SOFA is a lower-level dev/testing override (see
    // its doc comment) so it always wins over a persisted choice when set.
    std::string InitialSofaPath = ResolveHrtfSofaPath();
    HrtfSourceKind InitialHrtfKind = HrtfSourceKind::SofaFile;

    bool bAppliedPersistedHrtfChoice = false;
    if (!std::getenv("RAMKOLFX_HRTF_SOFA") && LoadedSettings && !LoadedSettings->ActiveHrtfDisplayName.empty())
    {
        for (size_t i = 0; i < RuntimeHrtfCatalog.size(); ++i)
        {
            if (LoadedSettings->ActiveHrtfDisplayName == RuntimeHrtfCatalog[i].DisplayName)
            {
                InitialSofaPath = RuntimeHrtfCatalog[i].Path;
                InitialHrtfKind = RuntimeHrtfCatalog[i].Kind;
                ActiveHrtfIndex.store(static_cast<uint8>(i), std::memory_order_relaxed);
                bAppliedPersistedHrtfChoice = true;
                break;
            }
        }
    }

    // If it happens to match a catalog entry, report that entry as active
    // so the GUI's dropdown starts in sync; otherwise ActiveHrtfIndex just
    // stays at its 0 default, since an env-var override sits outside the
    // catalog entirely (see ResolveHrtfSofaPath's comment).
    if (!bAppliedPersistedHrtfChoice)
    {
        for (size_t i = 0; i < RuntimeHrtfCatalog.size(); ++i)
        {
            if (RuntimeHrtfCatalog[i].Kind == HrtfSourceKind::SofaFile &&
                InitialSofaPath == RuntimeHrtfCatalog[i].Path)
            {
                ActiveHrtfIndex.store(static_cast<uint8>(i), std::memory_order_relaxed);
                break;
            }
        }
    }

    AdvancedStage = std::make_unique<HrtfDeck>(SharedLayout, InitialHrtfKind, InitialSofaPath,
                                                static_cast<float>(HardwareOutput::SampleRate),
                                                bNearFieldEnabled.load(std::memory_order_relaxed));

    Sink = std::make_unique<VirtualSink>(Loop);
    Sink->SetAudioCallback(
        [this](const float* Interleaved, uint32 Frames)
        {
            HandleVirtualSinkAudio(Interleaved, Frames);
        });
    if (!Sink->Start())
    {
        Teardown();
        return 1;
    }

    // Started before Output below (and given a moment to enumerate what's
    // already in the PipeWire graph via WaitForInitialSync) so
    // ResolveInitialOutputDevice can validate a persisted device choice, or
    // auto-pick a live one, instead of blindly trusting a name that might
    // no longer exist.
    Devices = std::make_unique<DeviceRegistry>(Loop);
    if (!Devices->Start())
    {
        Teardown();
        return 1;
    }
    Devices->WaitForInitialSync();

    const std::string InitialOutputDevice = ResolveInitialOutputDevice(LoadedSettings);
    if (!LoadedSettings || LoadedSettings->OutputDeviceName != InitialOutputDevice)
    {
        // First run (nothing persisted yet) or the persisted device is
        // gone - remember whatever we resolved to instead so the next
        // start (and the GUI's picker) reflect it rather than re-running
        // this fallback every time.
        PersistedSettings ToSave = LoadedSettings.value_or(PersistedSettings{});
        ToSave.OutputDeviceName = InitialOutputDevice;
        Settings.Save(ToSave);
    }

    Output = std::make_unique<HardwareOutput>(Loop, InitialOutputDevice);
    Output->SetFillCallback(
        [this](float* Interleaved, uint32 Frames)
        {
            return HandleHardwareOutputRequest(Interleaved, Frames);
        });
    if (!Output->Start())
    {
        Teardown();
        return 1;
    }

    Server = std::make_unique<ControlServer>(SocketPath);
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

    fprintf(stderr, "[ramkolfxd] running (pass-through mode). Ctrl+C to stop.\n");
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
    HwEq.reset();

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

void AudioEngine::HandleVirtualSinkAudio(const float* Interleaved, uint32 Frames)
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
    ApplyNearFieldLoudnessFalloff(InputScratch.data(), Frames);

    ActiveStage().Process(InputScratch.data(), VirtualSink::Channels, DspScratch.data(),
                           HardwareOutput::Channels, Frames);
    HwEq->Process(DspScratch.data(), HardwareOutput::Channels, DspScratch.data(), HardwareOutput::Channels,
                  Frames);
    StereoMixBuffer.Push(DspScratch.data(), Frames * HardwareOutput::Channels);
}

void AudioEngine::ApplySpeakerMute(float* Interleaved, uint32 Frames)
{
    bool MuteFlags[SpeakerCount];
    bool bAnyMuted = false;
    for (uint32 Speaker = 0; Speaker < SpeakerCount; ++Speaker)
    {
        MuteFlags[Speaker] = SharedLayout.IsSpeakerMuted(static_cast<SpeakerLayout::SpeakerChannel>(Speaker));
        bAnyMuted |= MuteFlags[Speaker];
    }
    if (!bAnyMuted)
    {
        return;
    }

    for (uint32 FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        float* Frame = Interleaved + FrameIndex * VirtualSink::Channels;
        for (uint32 Speaker = 0; Speaker < SpeakerCount; ++Speaker)
        {
            if (MuteFlags[Speaker])
            {
                Frame[SpeakerToFrameIndex[Speaker]] = 0.0f;
            }
        }
    }
}

void AudioEngine::ApplyNearFieldLoudnessFalloff(float* Interleaved, uint32 Frames)
{
    if (!bNearFieldEnabled.load(std::memory_order_relaxed))
    {
        return;
    }

    float Gains[SpeakerCount];
    for (uint32 Speaker = 0; Speaker < SpeakerCount; ++Speaker)
    {
        const float Distance = SharedLayout.GetSpeakerDistance(static_cast<SpeakerLayout::SpeakerChannel>(Speaker));
        Gains[Speaker] = ReferenceSpeakerDistanceMeters / std::max(Distance, MinSpeakerDistanceMeters);
    }

    for (uint32 FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        float* Frame = Interleaved + FrameIndex * VirtualSink::Channels;
        for (uint32 Speaker = 0; Speaker < SpeakerCount; ++Speaker)
        {
            Frame[SpeakerToFrameIndex[Speaker]] *= Gains[Speaker];
        }
    }
}

void AudioEngine::FillTestNoise(float* Interleaved, uint32 Frames)
{
    for (uint32 FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        float* Frame = Interleaved + FrameIndex * VirtualSink::Channels;
        for (uint32 Channel = 0; Channel < VirtualSink::Channels; ++Channel)
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

uint32 AudioEngine::HandleHardwareOutputRequest(float* Interleaved, uint32 Frames)
{
    const size_t Popped = StereoMixBuffer.Pop(Interleaved, Frames * HardwareOutput::Channels);
    return static_cast<uint32>(Popped / HardwareOutput::Channels);
}

std::vector<uint8> AudioEngine::HandleControlCommand(const Command& InCommand)
{
    // Reclaims whatever HrtfDeck's last completed crossfade faded out of.
    // Not realtime-safe (may deallocate) - fine here, this runs on a
    // per-client control thread, never the audio callback. Piggybacked on
    // every command rather than needing a dedicated timer, since the GUI
    // already polls GetStatus at a steady ~4Hz.
    AdvancedStage->CollectGarbage();
    HwEq->CollectGarbage();

    if (InCommand.CommandOpcode == Opcode::GetDevices)
    {
        return EncodeDeviceListResponse(Devices->GetDevices());
    }

    if (InCommand.CommandOpcode == Opcode::GetHwEqState)
    {
        std::lock_guard<std::mutex> Lock(HwEqMutex);
        return EncodeHwEqStateResponse(HwEqBandState);
    }

    if (InCommand.CommandOpcode == Opcode::GetHrtfCatalog)
    {
        std::vector<std::string> DisplayNames;
        {
            std::lock_guard<std::mutex> Lock(HrtfCatalogMutex);
            DisplayNames.reserve(RuntimeHrtfCatalog.size());
            for (const HrtfCatalogEntry& Entry : RuntimeHrtfCatalog)
            {
                DisplayNames.emplace_back(Entry.DisplayName);
            }
        }
        return EncodeHrtfCatalogResponse(DisplayNames);
    }

    // Set whenever this command changes state PersistedSettings tracks, so
    // it gets saved to disk below. Left false for read-only commands
    // (GetStatus and the two early-returned Get* above) and for
    // SetTestNoise, which is deliberately not persisted - see
    // PersistedSettings' doc comment.
    bool bPersistedStateChanged = false;

    if (InCommand.CommandOpcode == Opcode::SetSpatialMode)
    {
        Mode.store(InCommand.ModeValue, std::memory_order_relaxed);
        bPersistedStateChanged = true;
        fprintf(stderr, "[ramkolfxd] spatial mode set to %d\n", static_cast<int>(InCommand.ModeValue));
    }
    else if (InCommand.CommandOpcode == Opcode::SetSpeakerAzimuth)
    {
        const auto Channel = static_cast<SpeakerLayout::SpeakerChannel>(InCommand.SpeakerIndex);
        SharedLayout.SetSpeakerAzimuth(Channel, InCommand.AzimuthDegrees);
        bPersistedStateChanged = true;
        // Keeps the near-field path's per-voice filter in sync with live
        // repositioning regardless of whether that mode is currently on,
        // so it's never stale by the time someone switches it on - see
        // BinauralStage::RebuildVoiceForSpeaker's doc comment.
        AdvancedStage->RebuildVoiceForSpeaker(Channel, InCommand.AzimuthDegrees,
                                               SharedLayout.GetSpeakerDistance(Channel));
    }
    else if (InCommand.CommandOpcode == Opcode::SetSpeakerDistance)
    {
        const auto Channel = static_cast<SpeakerLayout::SpeakerChannel>(InCommand.SpeakerIndex);
        SharedLayout.SetSpeakerDistance(Channel, InCommand.DistanceMeters);
        bPersistedStateChanged = true;
        AdvancedStage->RebuildVoiceForSpeaker(Channel, SharedLayout.GetSpeakerAzimuth(Channel),
                                               InCommand.DistanceMeters);
    }
    else if (InCommand.CommandOpcode == Opcode::SetNearFieldEnabled)
    {
        bNearFieldEnabled.store(InCommand.bNearFieldEnabled, std::memory_order_relaxed);
        bPersistedStateChanged = true;
        AdvancedStage->SetNearFieldEnabled(InCommand.bNearFieldEnabled);
    }
    else if (InCommand.CommandOpcode == Opcode::ResetSpeakerPositions)
    {
        SharedLayout.ResetSpeakerPositions();
        bPersistedStateChanged = true;
        for (uint8 i = 0; i < SpeakerCount; ++i)
        {
            const auto Channel = static_cast<SpeakerLayout::SpeakerChannel>(i);
            AdvancedStage->RebuildVoiceForSpeaker(Channel, SharedLayout.GetSpeakerAzimuth(Channel),
                                                   SharedLayout.GetSpeakerDistance(Channel));
        }
    }
    else if (InCommand.CommandOpcode == Opcode::SetOutputDevice)
    {
        bPersistedStateChanged = true;
        if (!Output->SetTargetNode(InCommand.OutputDeviceName))
        {
            fprintf(stderr, "[ramkolfxd] failed to switch output device to %s\n",
                    InCommand.OutputDeviceName.c_str());
        }
    }
    else if (InCommand.CommandOpcode == Opcode::SetSpeakerMute)
    {
        SharedLayout.SetSpeakerMuted(static_cast<SpeakerLayout::SpeakerChannel>(InCommand.SpeakerIndex),
                                      InCommand.bMuted);
        bPersistedStateChanged = true;
    }
    else if (InCommand.CommandOpcode == Opcode::SetTestNoise)
    {
        bTestNoiseEnabled.store(InCommand.bTestNoiseEnabled, std::memory_order_relaxed);
    }
    else if (InCommand.CommandOpcode == Opcode::SetHrtfFile)
    {
        // DecodeCommand only checked the payload shape, not that the index
        // is actually in range for this daemon's catalog - bounds-check
        // here before indexing. Copy the selected entry out while holding
        // the lock, then release it before calling SwitchTo() below: SOFA
        // parsing there can take a noticeable moment (see HrtfDeck's own
        // doc comment), and there's no reason to block a concurrent
        // OnHrtfDirectoryChanged rebuild for that long.
        bool bIndexValid = false;
        HrtfCatalogEntry SelectedEntry;
        size_t CatalogSize = 0;
        {
            std::lock_guard<std::mutex> Lock(HrtfCatalogMutex);
            CatalogSize = RuntimeHrtfCatalog.size();
            bIndexValid = InCommand.HrtfIndex < CatalogSize;
            if (bIndexValid)
            {
                SelectedEntry = RuntimeHrtfCatalog[InCommand.HrtfIndex];
            }
        }
        if (bIndexValid)
        {
            AdvancedStage->SwitchTo(SelectedEntry.Kind, SelectedEntry.Path);
            ActiveHrtfIndex.store(InCommand.HrtfIndex, std::memory_order_relaxed);
            bPersistedStateChanged = true;
            fprintf(stderr, "[ramkolfxd] HRTF source switched to '%s'\n", SelectedEntry.DisplayName.c_str());
        }
        else
        {
            fprintf(stderr, "[ramkolfxd] SetHrtfFile: index %u out of range (catalog has %zu entries)\n",
                    InCommand.HrtfIndex, CatalogSize);
        }
    }
    else if (InCommand.CommandOpcode == Opcode::SetHwEqBand)
    {
        {
            std::lock_guard<std::mutex> Lock(HwEqMutex);
            HwEqBandState[InCommand.BandIndex] = InCommand.Band;
        }
        HwEq->SetBands(HwEqBandState);
        bPersistedStateChanged = true;
    }
    // SetHwEqPreset/SaveHwEqPreset and every Content* EQ opcode decode
    // successfully (see protocol.cpp) but have no handler yet - they're
    // Phase 2 (named presets keyed per-output-device and per-app). Falling
    // through here just returns the current Status, same as any other
    // unrecognized-but-decodable command.

    Status OutStatus;
    OutStatus.Mode = Mode.load(std::memory_order_relaxed);
    OutStatus.OutputDeviceName = Output->GetTargetNodeName();
    OutStatus.bTestNoiseEnabled = bTestNoiseEnabled.load(std::memory_order_relaxed);
    OutStatus.ActiveHrtfIndex = ActiveHrtfIndex.load(std::memory_order_relaxed);
    OutStatus.bNearFieldEnabled = bNearFieldEnabled.load(std::memory_order_relaxed);
    for (uint8 i = 0; i < SpeakerCount; ++i)
    {
        OutStatus.SpeakerAzimuthDegrees[i] =
            SharedLayout.GetSpeakerAzimuth(static_cast<SpeakerLayout::SpeakerChannel>(i));
        OutStatus.SpeakerMuted[i] =
            SharedLayout.IsSpeakerMuted(static_cast<SpeakerLayout::SpeakerChannel>(i));
        OutStatus.SpeakerDistanceMeters[i] =
            SharedLayout.GetSpeakerDistance(static_cast<SpeakerLayout::SpeakerChannel>(i));
    }

    if (bPersistedStateChanged)
    {
        PersistCurrentSettings(OutStatus);
    }

    return EncodeStatusResponse(OutStatus);
}

void AudioEngine::PersistCurrentSettings(const Status& CurrentStatus)
{
    PersistedSettings ToSave;
    ToSave.Mode = CurrentStatus.Mode;
    ToSave.SpeakerAzimuthDegrees = CurrentStatus.SpeakerAzimuthDegrees;
    ToSave.SpeakerDistanceMeters = CurrentStatus.SpeakerDistanceMeters;
    ToSave.SpeakerMuted = CurrentStatus.SpeakerMuted;
    ToSave.bNearFieldEnabled = CurrentStatus.bNearFieldEnabled;
    ToSave.OutputDeviceName = CurrentStatus.OutputDeviceName;
    {
        std::lock_guard<std::mutex> Lock(HwEqMutex);
        ToSave.HwEqBands = HwEqBandState;
    }
    {
        std::lock_guard<std::mutex> Lock(HrtfCatalogMutex);
        if (CurrentStatus.ActiveHrtfIndex < RuntimeHrtfCatalog.size())
        {
            ToSave.ActiveHrtfDisplayName = RuntimeHrtfCatalog[CurrentStatus.ActiveHrtfIndex].DisplayName;
        }
    }
    Settings.Save(ToSave);
}

} // namespace ramkolfx
