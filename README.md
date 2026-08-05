# AudioBat

A Linux audio DSP daemon that provides ambisonics-style 7.1-to-stereo
spatialization for headphones. Unlike typical "virtual surround" plugins
that apply a fixed per-channel HRTF, AudioBat encodes the 7.1 signal into
an ambisonics sound field and decodes that field to stereo, so positioning
stays accurate as speakers move and the mix doesn't collapse into six
separately-panned point sources.

## Status

Pipeline runs end-to-end with three selectable spatialization paths. A
real 7.1 stream played into the virtual sink is heard, spatialized, on the
real output device. The `SetSpatialMode` control command switches between
three genuinely different signal paths (`SpatialMode`: `Off` / `Basic` /
`Advanced`):

- **Off**: static-gain downmix (`PassthroughStage`).
- **Basic**: `AmbisonicsStage` encodes the 7 non-LFE 7.1 channels as point
  sources (at their nominal speaker azimuths) into a shared first-order
  B-format sound field, then decodes that field to two virtual stereo
  speakers. This is a plain algebraic (non-HRTF) decode — an improvement
  over static downmix, but a linear 2-speaker ambisonic decode can't fully
  preserve front/back distinction.
- **Advanced**: `BinauralStage` encodes the same B-format field (via the
  shared `SpeakerLayout`), decodes it to a fixed 8-point virtual
  loudspeaker array, and convolves each virtual speaker's signal with an
  HRIR for that direction, summing the results into stereo. HRIR
  convolution runs through a KissFFT-backed partitioned overlap-save
  convolver. The HRIR source is either measured data (a SOFA file, loaded
  via libmysofa) or a procedurally-computed rigid-sphere model
  (`synthetic_hrtf.cpp`) with no data file at all. If a SofaFile source
  fails to load, Advanced mode falls back to the same algebraic decode
  Basic mode uses rather than going silent.

  HRTF perception is highly ear-shape-specific, so which source
  externalizes well varies per listener. `HrtfDeck` wraps two
  `BinauralStage`s so the active source can be switched live from the
  GUI's HRTF dropdown (or the `SetHrtfFile` control opcode) without
  restarting the daemon, crossfading between old and new over a fixed
  ~50ms window so switching never clicks. The bundled catalog
  (`GetHrtfCatalog`, see `data/hrtf/README.md`) is the MIT KEMAR default,
  8 individual subjects from the SADIE II database (Apache-2.0, safe to
  redistribute commercially — the CIPIC/ARI/Listen mirrors were
  deliberately avoided for exactly that reason), and the synthetic model.
  The daemon also watches a user-writable directory
  (`$XDG_CONFIG_HOME/audiobat/hrtf` by default, or `AUDIOBAT_HRTF_DIR`)
  and appends any `.sofa` files found there to the catalog, prefixed
  `(user) `, picking up additions/removals live via inotify — unlike the
  bundled entries, files placed there are never redistributed or license-
  checked by AudioBat, so it's the place for datasets with terms that
  don't allow redistribution. The `AUDIOBAT_HRTF_SOFA` environment
  variable still works as a lower-level override for the daemon's
  *initial* source, outside the catalog entirely.

All three modes sit behind the same `DspStage` interface and share one
live-repositionable `SpeakerLayout`, so speaker position changes apply
no matter which mode is active.

Virtual speaker positions are live-repositionable both at the
`SpeakerLayout` API level (`SetSpeakerAzimuth`/`GetSpeakerAzimuth`,
lock-free, callable from any thread) and over the control protocol
(`SetSpeakerAzimuth` opcode, with current azimuths included in every
`GetStatus` response) — the GUI's speaker dial drives this live, in
whichever mode is active.

## Architecture

```
   client app (game, media player)
            │  renders 7.1 via OpenAL / PipeWire
            ▼
   PipeWire virtual sink  ("AudioBat Virtual Sink")
            │  captured by the daemon
            ▼
   DSP stage  (PassthroughStage / AmbisonicsStage / BinauralStage,
               selected live by SpatialMode)
            │
            ▼
   real hardware output  (via a PipeWire playback stream)

   GUI control app  <──Unix socket, binary protocol──>  daemon
```

- **`audiobatd`** (the daemon) owns the real audio output device. It never
  talks to hardware directly on the input side — client apps render into a
  PipeWire virtual sink instead, exactly as if it were a normal output
  device.
- The daemon captures from that virtual sink, runs it through whichever
  DSP stage the current `SpatialMode` selects, and plays the result out
  through a second PipeWire stream pinned to a specific real hardware
  sink (selectable live via the control protocol/GUI, not "whatever is
  currently default").
- The **GUI control app** (`audiobat-gui`, Dear ImGui + SDL2/OpenGL3) talks
  to the daemon over the Unix domain socket to switch spatial mode and
  reposition virtual speakers live, and (later) manage EQ — all in real
  time, without restarting the daemon.

## Directory layout

- `common/` — shared code between daemon and GUI: the control protocol and
  a lock-free ring buffer. No PipeWire dependency.
- `daemon/` — `audiobatd`: the PipeWire pipeline, DSP stage, and control
  socket server.
- `gui/` — `audiobat-gui`: Dear ImGui + SDL2/OpenGL3 control app (build with
  `-DAUDIOBAT_BUILD_GUI=ON`). Talks to the daemon only over the Unix
  control socket — no PipeWire dependency.
- `docs/` — reserved for design notes as the DSP work lands.

## Engineering approach

The ambisonics math and the daemon/GUI control logic are being written from
scratch — that's the core IP. It's fine to bootstrap with permissively
(BSD-style) licensed third-party libraries (e.g. libmysofa for HRTF data,
KissFFT for FFT) once real DSP work starts, but they'll sit behind clean,
swappable interfaces (see `daemon/src/dsp/dsp_stage.hpp`) so they can be
replaced later without touching the rest of the pipeline.

## Control protocol

The daemon and any control client (GUI, CLI tools) speak a small
hand-rolled **binary** protocol over a Unix domain socket — not JSON —
to keep the control path cheap enough to poll frequently (e.g. for live
speaker repositioning) without parsing/allocation overhead. See
`common/include/audiobat/protocol.hpp` for the wire format and the full
rationale; the format is intentionally isolated to that header/.cpp pair so
it can change without touching daemon or GUI logic built on top of it.

Socket path: `$XDG_RUNTIME_DIR/audiobat/control.sock` (falls back to
`/tmp/audiobat-<uid>/control.sock`).

## Building

### Dependencies

**Daemon (`audiobatd`):**

- CMake >= 3.20, a C++20 compiler (GCC or Clang), Ninja (or Make),
  `pkg-config`
- PipeWire development headers: `libpipewire-0.3` (via pkg-config)
- zlib development headers (libmysofa's SOFA/HDF5 parsing depends on it)
- pthreads (part of the standard toolchain on Linux)
- Network access on first configure: `daemon/CMakeLists.txt` fetches
  libmysofa and KissFFT via CMake `FetchContent` (same mechanism
  `gui/CMakeLists.txt` already uses for Dear ImGui) since neither ships as
  a common distro package.

**GUI (`audiobat-gui`, only needed with `-DAUDIOBAT_BUILD_GUI=ON`):**

- SDL2 development headers: `sdl2` (via pkg-config)
- OpenGL development headers/libs
- Network access on first configure: `gui/CMakeLists.txt` fetches Dear
  ImGui via CMake `FetchContent`.

On this machine, everything above is already present (GCC 16, Clang 22,
CMake 4.4, Ninja 1.13, libpipewire-0.3 1.6.7). On a fresh machine, run
`./FetchDependencies.sh` to install your distro's packages automatically
(Debian/Ubuntu, Fedora, Arch, and openSUSE are detected via
`/etc/os-release`; pass `--no-gui` to skip the GUI-only packages). That
script's install commands, spelled out:

- Debian/Ubuntu: `sudo apt install build-essential cmake ninja-build pkg-config git libpipewire-0.3-dev zlib1g-dev libsdl2-dev libgl1-mesa-dev`
- Fedora: `sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config git pipewire-devel zlib-devel SDL2-devel mesa-libGL-devel`
- Arch: `sudo pacman -S base-devel cmake ninja pkgconf git libpipewire zlib sdl2 mesa`

### Build

```sh
cmake -S . -B build -G Ninja -DAUDIOBAT_BUILD_GUI=ON
cmake --build build
```

(Omit `-DAUDIOBAT_BUILD_GUI=ON` to build only the daemon, skipping the GUI
dependencies above.) Or just run `./build.sh`, which does the same thing
and builds both.

The daemon binary is `build/daemon/audiobatd`; the GUI binary (if built)
is `build/gui/audiobat-gui`.

### Convenience scripts

- **`./FetchDependencies.sh`** — installs the OS packages listed above for
  your distro. `./FetchDependencies.sh --no-gui` skips the GUI-only
  packages; `--dry-run` prints the install command without running it.
- **`./build.sh`** — configures and builds daemon + GUI into `build/`
  (same as the `cmake` invocation above). `./build.sh clean` wipes `build/`
  first.
- **`./run.sh`** — starts `audiobatd` in the background, waits for its
  control socket to come up, then launches `audiobat-gui` in the
  foreground; stops the daemon when the GUI exits or on Ctrl+C.
  `./run.sh daemon` runs only the daemon (foreground); `./run.sh gui` runs
  only the GUI, assuming a daemon is already running.
- **`./run_gui.sh`** — launches `audiobat-gui` on its own, assuming a
  daemon is already running and listening on the control socket. This is
  what `run.sh` calls internally for the GUI half.

All three build/run against the same `build/` directory and must be run
from the repo root (they `cd` to their own location first, so `./run.sh`
works from anywhere).

## Running

```sh
./build/daemon/audiobatd
```

This creates the "AudioBat Virtual Sink" in the PipeWire graph and starts a
playback stream pinned to a specific real hardware sink (hardcoded in
`AudioEngine::Run()` for now; swap live via `SetOutputDevice` / the GUI's
output device picker). The daemon also claims the virtual sink as your
system default output as soon as it starts (see `DeviceRegistry::
ClaimVirtualSinkAsDefault()`), so most apps route to it automatically with
no manual step. If you'd rather route a specific app instead of changing
the system default, use `pavucontrol`, `wpctl`, or `pw-play --target
audiobat_virtual_sink some_7.1_file.wav`. Stop the daemon with Ctrl+C or
`SIGTERM`; it tears down the virtual sink and unlinks the control socket
on the way out.

Setting the virtual sink as default is safe against feedback: the
daemon's own hardware output stream is pinned by `target.object` (and
`node.dont-reconnect`) to the real sink it was configured with, so it
never resolves to "whatever is default" and can't loop back into the
virtual sink it just claimed.

### Testing the control socket manually

No client app exists yet, but you can poke the protocol directly with
`socat` (`[opcode: u8][length: u16 BE][payload]`, see
`common/include/audiobat/protocol.hpp`):

```sh
# GetStatus (0x01, no payload), then SetSpatialMode(Advanced) (0x02, len=1, payload=2)
printf '\x01\x00\x00\x02\x00\x01\x02' | \
    socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/audiobat/control.sock | od -An -tx1
```

`SpatialMode` payload byte: `0` = Off, `1` = Basic (algebraic ambisonics),
`2` = Advanced (HRTF binaural). Each reply is `[0x81][len=29][SpatialMode
byte][7 x speaker azimuth: f32 BE]` (status) or `[0x82][len][message]`
(error).

## Adding your own SOFA files

Beyond the bundled catalog (MIT KEMAR + 8 SADIE II subjects + the
synthetic model, see `data/hrtf/README.md`), you can add your own HRTF
measurements — CIPIC, ARI, Listen, or your own — without rebuilding:

1. Drop the `.sofa` file into `$XDG_CONFIG_HOME/audiobat/hrtf` (falls
   back to `~/.config/audiobat/hrtf`, created automatically on first
   run), or point `AUDIOBAT_HRTF_DIR` at a directory of your choice.
2. That's it — the daemon watches the directory via inotify and adds it
   to the HRTF catalog live, prefixed `(user) `, no restart needed. It'll
   show up in the GUI's HRTF dropdown (or is selectable via the
   `SetHrtfFile` control opcode) alongside the bundled entries.

Matching is purely by `.sofa` extension (case-insensitive); files aren't
parsed until selected, so a corrupt or non-SOFA file just fails
gracefully back to silence for that entry rather than crashing the
daemon. AudioBat doesn't vet or redistribute anything placed here, so
it's the right place for datasets whose license doesn't allow
redistribution. See `data/hrtf/README.md` for the full details and for
`AUDIOBAT_HRTF_SOFA`, the lower-level env var override for a one-off
file outside the catalog entirely.

## Roadmap

1. ~~Working local pipeline: virtual sink → capture → placeholder DSP → real output~~
2. ~~One-click 3D on/off toggle: wired end-to-end and now audibly meaningful (PassthroughStage vs AmbisonicsStage)~~
3. ~~Real-time repositionable virtual speakers: control-protocol opcode + GUI dial~~
4. ~~HRTF-based binaural decode (libmysofa + KissFFT), selectable as the "Advanced" `SpatialMode` alongside algebraic ambisonics~~
5. ~~Switchable HRTF catalog (MIT KEMAR + 8 SADIE II subjects + a license-free synthetic spherical-head model) with live, crossfaded switching via `HrtfDeck` and the GUI's HRTF dropdown~~
6. Parametric EQ
7. ONNX-based automatic EQ preset selection
8. GUI polish aimed at non-audio-engineer users (v1 ships spatial mode dropdown + speaker dial only)

## License

AudioBat is dual-licensed: freely under the GNU General Public License
v3.0 (see [LICENSE](LICENSE)), or under a separate commercial license for
proprietary/closed-source use (see [LICENSE-COMMERCIAL.md](LICENSE-COMMERCIAL.md)).
Contributions are accepted only under the terms of the Contributor License
Agreement (see [CLA.md](CLA.md)), which is what makes offering both tracks
possible.
