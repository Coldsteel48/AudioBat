# RamkolFX — Per-Class Overview

A map of every meaningful class/struct in the codebase, grouped by module, with
responsibilities, key interfaces, and collaborators. Generated from the source
under `common/`, `daemon/`, and `gui/`.

## Ownership chain (top level)

```
main (daemon) -> AudioEngine -> {
  SingleInstanceLock, VirtualSink, HardwareOutput, ControlServer,
  DeviceRegistry, SettingsStore, SpeakerLayout,
  PassthroughStage (Off),
  AmbisonicsStage (Basic),
  HrtfDeck (Advanced) -> CrossfadingSlot<BinauralStage> -> BinauralStage -> {
    AmbisonicsStage (fallback), HrtfLoader, PartitionedConvolver[],
    BinauralVoice[] -> CrossfadingSlot<BinauralVoiceFilter> -> BinauralVoiceFilter -> PartitionedConvolver
  }
}

main (gui) -> IRenderer + App -> ControlClient (talks to AudioEngine's ControlServer
  over the Unix socket using protocol.hpp), rendering via DrawPositionDial
```

---

## Module: `common` (`common/include/ramkolfx/`)

Shared code linked into both `ramkolfxd` (daemon) and `ramkolfx-gui`.

### Protocol types & free functions
- **File:** `common/include/ramkolfx/protocol.hpp` (+ `protocol.cpp`)
- **Responsibility:** Defines the daemon's binary control-socket wire protocol — a fixed 3-byte header (opcode + big-endian u16 length), request `Command`s, response `Status`, and free encode/decode functions for each message type. No class/object state; purely protocol structs (`Opcode`, `SpatialMode`, `MessageHeader`, `Command`, `Status`, `AudioDeviceInfo`) plus `Encode*`/`Decode*` free functions.
- **Key interface:** `TryReadHeader`, `DecodeCommand`, `DecodeStatusResponse`, `DecodeErrorResponse`, `DecodeDeviceListResponse`, `DecodeHrtfCatalogResponse`, `EncodeStatusResponse`, `EncodeErrorResponse`, `EncodeDeviceListResponse`, `EncodeHrtfCatalogResponse`, `Encode*Request` family, `DefaultControlSocketPath`.
- **Collaborators:** Used by `ControlServer`/`AudioEngine` (daemon side, decodes requests + encodes responses) and `ControlClient` (GUI side, encodes requests + decodes responses).

### `RingBuffer<T>`
- **File:** `common/include/ramkolfx/ring_buffer.hpp`
- **Responsibility:** Single-producer/single-consumer lock-free ring buffer of samples, bridging audio blocks between PipeWire capture and playback callbacks whose block sizes may not match.
- **Key interface:** `Push`, `Pop`, `AvailableToRead`.
- **Collaborators:** Owned by `AudioEngine` (as `StereoMixBuffer`) to decouple `VirtualSink`'s capture callback from `HardwareOutput`'s playback-request callback.

---

## Module: `daemon` (core, `daemon/src/`)

### `AudioEngine`
- **File:** `daemon/src/audio_engine.hpp` (+ `.cpp`)
- **Responsibility:** Top-level owner of the entire daemon pipeline — PipeWire main loop, virtual sink (capture), all three DSP stages, hardware output (playback), control socket, device registry, settings persistence, and the runtime HRTF catalog. Dispatches captured 7.1 audio to whichever `DspStage` the current `SpatialMode` selects, bridging capture/playback via `RingBuffer`.
- **Key public methods:** `Run`, `Stop`.
- **Key private machinery:** `HandleVirtualSinkAudio`, `HandleHardwareOutputRequest`, `HandleControlCommand`, `PersistCurrentSettings`, `ApplySpeakerMute`, `ApplyNearFieldLoudnessFalloff`, `FillTestNoise`, `ActiveStage`, `ResolveInitialOutputDevice`, `ResolveHrtfSofaPath`, `BuildHrtfCatalog`, `ResolveUserHrtfDirectory`, `ScanUserHrtfDirectory`, `RebuildHrtfCatalog`, `OnHrtfDirectoryChanged`.
- **Collaborators:** Owns `SingleInstanceLock`, `VirtualSink`, `HardwareOutput`, `ControlServer`, `DeviceRegistry`, `SettingsStore`, `SpeakerLayout` (`SharedLayout`), `PassthroughStage` (Off), `AmbisonicsStage` (Basic), `HrtfDeck` (Advanced), and a `RingBuffer<float>`. Instantiated directly by `daemon/src/main.cpp`.

### `ControlServer`
- **File:** `daemon/src/control/control_server.hpp` (+ `.cpp`)
- **Responsibility:** Unix domain socket server implementing the control protocol; accepts multiple concurrent clients, each on its own detached thread, dispatching decoded commands to an injected handler.
- **Key interface:** `SetCommandHandler`, `Start`, `Stop` (private `AcceptLoop`, `HandleClient`).
- **Collaborators:** Owned by `AudioEngine`, which supplies its `CommandHandler` (bound to `AudioEngine::HandleControlCommand`); consumed by the GUI's `ControlClient` over the socket path from `protocol.hpp`.

### `DeviceRegistry`
- **File:** `daemon/src/device_registry.hpp` (+ `.cpp`)
- **Responsibility:** Maintains a live map of real hardware playback sinks by listening to PipeWire registry global add/remove events on its own context/core connection; also claims the RamkolFX virtual sink as the system default output once the session's "default" metadata object appears.
- **Key interface:** `Start`, `WaitForInitialSync`, `GetDevices`, `HasDevice`, `PickAnyDevice`; PipeWire trampoline targets `HandleGlobalAdded`/`HandleGlobalRemoved`.
- **Collaborators:** Owned by `AudioEngine`; feeds `AudioDeviceInfo` results into `AudioEngine`'s device commands and `HardwareOutput`'s target selection.

### `SettingsStore` (+ `PersistedSettings`)
- **File:** `daemon/src/settings_store.hpp` (+ `.cpp`)
- **Responsibility:** Loads/saves `PersistedSettings` (spatial mode, per-speaker layout, near-field toggle, output device, active HRTF display name) as a small text file under `$XDG_CONFIG_HOME/ramkolfx`, crash-safe via temp-file + rename.
- **Key interface:** `Load`, `Save`.
- **Collaborators:** Owned by `AudioEngine`, called on startup (`Run`) and after mutating control commands (`PersistCurrentSettings`).

### `SingleInstanceLock`
- **File:** `daemon/src/single_instance_lock.hpp` (+ `.cpp`)
- **Responsibility:** Guards against two `ramkolfxd` processes running simultaneously via `flock()` on a lock file (kernel auto-releases on exit/crash).
- **Key interface:** `TryAcquire`, `HolderPid`.
- **Collaborators:** Owned by `AudioEngine`; acquired first, before any PipeWire setup.

### `VirtualSink`
- **File:** `daemon/src/virtual_sink.hpp` (+ `.cpp`)
- **Responsibility:** Wraps a PipeWire stream presenting itself as an 8-channel (7.1) "RamkolFX Virtual Sink" that client apps route output to; invokes a callback with captured interleaved audio on the realtime thread.
- **Key interface:** `SetAudioCallback`, `Start`, `HandleProcess` (PW trampoline).
- **Collaborators:** Owned by `AudioEngine`, which supplies the callback bound to `HandleVirtualSinkAudio`. `DeviceRegistry` excludes its node name from the selectable hardware list.

### `HardwareOutput`
- **File:** `daemon/src/hardware_output.hpp` (+ `.cpp`)
- **Responsibility:** Wraps a PipeWire playback stream that sends the final processed stereo mix to a specific pinned hardware sink (by node name, not "default"), avoiding feedback loops with the virtual sink; supports live retargeting.
- **Key interface:** `SetFillCallback`, `Start`, `SetTargetNode`, `GetTargetNodeName`, `HandleProcess` (PW trampoline).
- **Collaborators:** Owned by `AudioEngine`, which supplies the callback bound to `HandleHardwareOutputRequest`; retargeted via `AudioEngine`'s output-device handling, informed by `DeviceRegistry`.

---

## Module: `daemon/dsp` (`daemon/src/dsp/`)

### `DspStage` (abstract interface)
- **File:** `daemon/src/dsp/dsp_stage.hpp`
- **Responsibility:** Abstract interface for a spatialization DSP stage between captured 7.1 audio and stereo hardware output; also defines the shared `MaxProcessFrames` bound.
- **Key interface:** `Process` (pure virtual).
- **Collaborators:** Implemented by `PassthroughStage`, `AmbisonicsStage`, `BinauralStage`, `HrtfDeck`; `AudioEngine` holds one instance of each concrete stage and dispatches via this interface.

### `PassthroughStage`
- **File:** `daemon/src/dsp/passthrough_stage.hpp` (+ `.cpp`)
- **Responsibility:** "Off" spatial mode — static-gain downmix from 7.1 to stereo, no spatialization.
- **Key interface:** `Process`.
- **Collaborators:** Owned by `AudioEngine` as `OffStage`; also reused inside `BinauralStage` as `FallbackStage` when HRTF loading fails.

### `SpeakerLayout`
- **File:** `daemon/src/dsp/speaker_layout.hpp` (+ `.cpp`)
- **Responsibility:** Holds live-repositionable azimuth/distance/mute state for the 7 non-LFE 7.1 virtual speakers, and encodes per-frame point sources into a shared first-order B-format field (W/X/Y) used by both algebraic and HRTF decode paths.
- **Key interface:** `SetSpeakerAzimuth`/`GetSpeakerAzimuth`, `SetSpeakerDistance`/`GetSpeakerDistance`, `SetSpeakerMuted`/`IsSpeakerMuted`, `ResetSpeakerPositions`, `SnapshotDirections`, static `Encode`.
- **Collaborators:** Owned once by `AudioEngine` (`SharedLayout`), passed by reference into `AmbisonicsStage`, `BinauralStage`, and (indirectly) `HrtfDeck`.

### `AmbisonicsStage`
- **File:** `daemon/src/dsp/ambisonics_stage.hpp` (+ `.cpp`)
- **Responsibility:** "Basic" spatial mode — encodes 7.1 input into a shared B-format field via `SpeakerLayout`, then algebraically decodes to two stereo virtual speakers (non-HRTF ambisonic decode).
- **Key interface:** `Process`.
- **Collaborators:** Takes a `const SpeakerLayout&`; owned by `AudioEngine` as `BasicStage`; also embedded inside `BinauralStage` as `FallbackStage`.

### `HrtfFilter` (struct) / `BuildDelayedHrtfFilter`
- **File:** `daemon/src/dsp/hrtf_filter.hpp`
- **Responsibility:** Shared data type — one direction's stereo HRIR pair plus per-ear delay, produced interchangeably by `HrtfLoader` (measured SOFA) and `synthetic_hrtf.hpp` (procedural); `BuildDelayedHrtfFilter` bakes a fractional delay into taps as an integer-sample prepend.
- **Collaborators:** Consumed/produced by `HrtfLoader`, `synthetic_hrtf.hpp`, `near_field_filter.hpp`, `BinauralVoiceFilter`, `BinauralStage`.

### `HrtfLoader`
- **File:** `daemon/src/dsp/hrtf_loader.hpp` (+ `.cpp`)
- **Responsibility:** Thin wrapper around libmysofa's "easy" API — opens/parses/resamples a SOFA HRTF file once at construction time and performs interpolated nearest-neighbor HRIR lookups by azimuth/elevation. Not realtime-safe (only called at stage construction/rebuild time).
- **Key interface:** `Open`, `IsOpen`, `FilterLength`, `GetFilter`.
- **Collaborators:** Owned by `BinauralStage` (`Hrtf`); its pointer is passed into `BinauralVoice`/`BinauralVoiceFilter` for the near-field per-voice path.

### `ComputeHrtfNormalizationGain` (free function)
- **File:** `daemon/src/dsp/hrtf_gain_normalization.hpp` (+ `.cpp`)
- **Responsibility:** Computes a per-source normalization scale factor (peak-tap based, across 8 canonical azimuths) so switching HRTF sources doesn't jump output level; always 1.0 for the synthetic source.
- **Collaborators:** Called once by `BinauralStage`'s constructor to compute `NearFieldNormalizationGain`, consumed by `BinauralVoice`.

### `ComputeSphericalHeadFilter` (free function)
- **File:** `daemon/src/dsp/synthetic_hrtf.hpp` (+ `.cpp`)
- **Responsibility:** Procedurally computes a license-free stand-in HRTF filter from a rigid-sphere head model (Woodworth-Schlosberg ITD + single-pole head-shadow lowpass), matching `HrtfLoader::GetFilter`'s signature so it's interchangeable.
- **Collaborators:** Used by `BinauralStage`/`BinauralVoice` when `HrtfSourceKind::SyntheticSphericalHead` is selected; built on `rigid_sphere_math.hpp` helpers.

### Rigid-sphere math helpers (free functions/constants)
- **File:** `daemon/src/dsp/rigid_sphere_math.hpp`
- **Responsibility:** Shared physical constants and small DSP-math building blocks (`HeadRadiusMeters`, `SpeedOfSoundMetersPerSecond`, `NormalizeAzimuthDegrees`, `LateralAngleDegrees`, `BuildUnityGainLowpassIR`) used by both `synthetic_hrtf.cpp` and `near_field_filter.cpp`.

### Near-field correction functions (free functions)
- **File:** `daemon/src/dsp/near_field_filter.hpp` (+ `.cpp`)
- **Responsibility:** `ComputeNearFieldCorrection`/`ApplyNearFieldCorrection` model near-field proximity ILD boost (low-frequency shelf per ear, derived from a rigid-sphere DC/potential-flow limit) as a source approaches the head, distinct from broadband loudness falloff (handled in `AudioEngine`). `ApplyNearFieldCorrection` cascades this shelf onto an existing far-field HRTF filter via convolution.
- **Collaborators:** Used by `BinauralVoice`/`BinauralVoiceFilter` construction for the near-field-enabled per-source direct-convolution path in `BinauralStage`.

### `PartitionedConvolver`
- **File:** `daemon/src/dsp/partitioned_convolver.hpp` (+ `.cpp`)
- **Responsibility:** Single-partition overlap-save FFT convolver (KissFFT-backed) that convolves a mono signal against a loaded HRIR in fixed small internal blocks (128 samples) regardless of caller block size, for low added latency.
- **Key interface:** `Load`, `ProcessAccumulate`.
- **Collaborators:** Used in pairs (Left/Right) inside `BinauralStage` (`LeftConvolvers`/`RightConvolvers`) and inside `BinauralVoiceFilter`.

### `BinauralVoiceFilter`
- **File:** `daemon/src/dsp/binaural_voice.hpp` (+ `binaural_voice.cpp`)
- **Responsibility:** One direction+distance's loaded Left/Right `PartitionedConvolver` pair — the "live" object a `CrossfadingSlot<BinauralVoiceFilter>` swaps between on rebuild; only meant to be constructed by `BinauralVoice`.
- **Key interface:** `Process`.
- **Collaborators:** Owned/managed by `BinauralVoice`'s `CrossfadingSlot`.

### `BinauralVoice`
- **File:** `daemon/src/dsp/binaural_voice.hpp` (+ `binaural_voice.cpp`)
- **Responsibility:** Renders one 7.1 source channel straight to binaural stereo — far-field HRTF (measured or synthetic) cascaded with near-field proximity correction for its current distance, normalized in level — using a `CrossfadingSlot<BinauralVoiceFilter>` internally for click-free rebuilds when azimuth/distance changes.
- **Key interface:** `Rebuild`, `Process`, `CollectGarbage`.
- **Collaborators:** One instance per non-LFE speaker (`std::array<std::unique_ptr<BinauralVoice>, SpeakerCount>`) owned by `BinauralStage` (`Voices`); wraps a `CrossfadingSlot<BinauralVoiceFilter>`.

### `CrossfadingSlot<T>` (template utility)
- **File:** `daemon/src/dsp/crossfading_slot.hpp`
- **Responsibility:** Generic click-free "swap a live object for a new one without blocking the realtime thread" primitive — a control thread `Publish()`es a new `T`, the audio thread `Process()`es a crossfade between old/new over a fixed window, and `CollectGarbage()`/`ForwardToLive()` round out the lifecycle. Originally written for `HrtfDeck`, reused by `BinauralVoice`.
- **Key interface:** `Publish`, `CollectGarbage`, `Process`, `ForwardToLive`.
- **Collaborators:** Used inside `HrtfDeck` (`CrossfadingSlot<BinauralStage>`) and `BinauralVoice` (`CrossfadingSlot<BinauralVoiceFilter>`).

### `HrtfSourceKind` (enum, in `binaural_stage.hpp`)
Widely-shared discriminator (`SofaFile` / `SyntheticSphericalHead`) across `BinauralStage`, `HrtfDeck`, `BinauralVoice`, `hrtf_gain_normalization`, `HrtfCatalogEntry`.

### `BinauralStage`
- **File:** `daemon/src/dsp/binaural_stage.hpp` (+ `.cpp`)
- **Responsibility:** "Advanced" spatial mode — HRTF-based binaural decode. Maintains two parallel signal paths: the "original" path (shared B-format field decoded to 8 fixed virtual loudspeakers, each convolved via `PartitionedConvolver` pairs and summed) and an additive "near-field" path (each of the 7 sources rendered directly through its own `BinauralVoice`). `Process()` crossfades between the two paths based on the near-field toggle. Falls back to `AmbisonicsStage` behavior if a SOFA source fails to load.
- **Key interface:** `Process`, `SetNearFieldEnabled`, `RebuildVoiceForSpeaker`, `CollectVoiceGarbage`.
- **Collaborators:** Takes `const SpeakerLayout&`; owns `AmbisonicsStage FallbackStage`, `HrtfLoader Hrtf`, arrays of `PartitionedConvolver`, and `std::array<std::unique_ptr<BinauralVoice>, SpeakerCount> Voices`. Managed (constructed/swapped) by `HrtfDeck` via `CrossfadingSlot<BinauralStage>`.

### `HrtfDeck`
- **File:** `daemon/src/dsp/hrtf_deck.hpp` (+ `.cpp`)
- **Responsibility:** Wraps two `BinauralStage`s (via `CrossfadingSlot<BinauralStage>`) so switching the active HRTF source (SOFA file vs. synthetic) never restarts the daemon or clicks — a pending stage is built off the realtime thread, then crossfaded into on publish. Forwards near-field toggle and per-speaker rebuild calls to whichever `BinauralStage` is live.
- **Key interface:** `Process`, `SwitchTo`, `SetNearFieldEnabled`, `RebuildVoiceForSpeaker`, `CollectGarbage`.
- **Collaborators:** Implements `DspStage`; owned by `AudioEngine` as `AdvancedStage`; internally owns `CrossfadingSlot<BinauralStage>`.

### `HrtfCatalogEntry` (struct, generated header `hrtf_catalog.hpp` from `hrtf_catalog.hpp.in`)
- **Responsibility:** CMake-generated bundled list of HRTF sources (`DisplayName`, `HrtfSourceKind Kind`, `Path`) that Advanced mode can switch between; `AudioEngine` filters/extends it at runtime (`BuildHrtfCatalog`/`ScanUserHrtfDirectory`/`RebuildHrtfCatalog`) into `RuntimeHrtfCatalog`.
- **Collaborators:** Consumed by `AudioEngine` to drive `GetHrtfCatalog`/`SetHrtfFile` control commands and `HrtfDeck::SwitchTo`.

---

## Module: `gui` (`gui/src/`)

### `App`
- **File:** `gui/src/app.hpp` (+ `.cpp`)
- **Responsibility:** Owns the control connection (`ControlClient`) and draws the entire ImGui UI each frame — status polling, device/HRTF-catalog polling, throttled position-drag sending, mirror-mode UX, reconnect handling.
- **Key interface:** `Tick` (private `DrawUI`).
- **Collaborators:** Owns `ControlClient Client`; holds `Status LastStatus`, `std::vector<AudioDeviceInfo> Devices`, `std::vector<std::string> HrtfCatalog` from `protocol.hpp`; calls `DrawPositionDial` inside `DrawUI`. Instantiated by `gui/src/main.cpp`.

### `ControlClient`
- **File:** `gui/src/control_client.hpp` (+ `.cpp`)
- **Responsibility:** Thin synchronous wrapper around the daemon's Unix control socket, called directly on the render thread (sub-ms local RTT). Handles connect/disconnect and one request/response round trip per public method, closing the connection on any failure.
- **Key interface:** `Connect`, `Disconnect`, `IsConnected`, `RequestStatus`, `SetSpatialMode`, `SetSpeakerAzimuth`, `SetSpeakerDistance`, `SetNearFieldEnabled`, `ResetSpeakerPositions`, `SetOutputDevice`, `SetSpeakerMute`, `SetTestNoiseEnabled`, `RequestDevices`, `RequestHrtfCatalog`, `SetHrtfFile`.
- **Collaborators:** Owned by `App`; uses `protocol.hpp`'s encode/decode functions and talks to the daemon's `ControlServer` over `DefaultControlSocketPath()`.

### `IRenderer` (abstract interface) + OpenGL3 implementation
- **File:** `gui/src/renderer.hpp` (+ `renderer_opengl3.cpp`)
- **Responsibility:** Isolates all graphics-API-specific calls (context/device creation, frame submission, present) behind one interface so the rest of the GUI only issues plain ImGui draw calls; `CreateOpenGL3Renderer()` factory returns the concrete backend.
- **Key interface:** `GetSDLWindowFlags`, `Init`, `NewFrame`, `RenderFrame`, `Shutdown`.
- **Collaborators:** Instantiated in `gui/src/main.cpp` via `CreateOpenGL3Renderer()`, driven each frame around `App::Tick`.

### `DrawPositionDial` (free function)
- **File:** `gui/src/position_dial.hpp` (+ `.cpp`)
- **Responsibility:** Draws a custom top-down dial widget with one draggable handle per virtual 7.1 speaker (angle = azimuth, distance-from-center = distance), doubling each speaker's label as a mute/solo control (click = mute toggle, Ctrl+click = solo); supports a mirror-drag mode for left/right speaker pairs.
- **Collaborators:** Called from `App::DrawUI`, operating directly on `Status`'s azimuth/distance/mute arrays (`protocol.hpp` types) and reporting back which indices changed for `App` to push via `ControlClient`.

---

## Top-level `main.cpp` wiring

- **`daemon/src/main.cpp`:** trivial — constructs `ramkolfx::AudioEngine` and calls `Run()`, returning its exit code. All daemon composition happens inside `AudioEngine`.
- **`gui/src/main.cpp`:** initializes SDL + ImGui, resolves a DPI scale (`ResolveDpiScale`, with `RAMKOLFX_GUI_SCALE` env override), creates an `IRenderer` via `CreateOpenGL3Renderer()`, creates the SDL window sized/scaled accordingly, constructs `ramkolfx::gui::App`, then runs the SDL event/render loop calling `Renderer->NewFrame()` → `ImGui::NewFrame()` → `App.Tick(DeltaTime)` → `ImGui::Render()` → `Renderer->RenderFrame(...)` each frame.
