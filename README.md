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
  loudspeaker array, and convolves each virtual speaker's signal with a
  measured HRIR for that direction (loaded from a SOFA file via
  libmysofa), summing the results into stereo. HRIR convolution runs
  through a KissFFT-backed partitioned overlap-save convolver. A small
  public-domain default HRTF dataset (MIT KEMAR, see
  `data/hrtf/README.md`) is bundled; point the `AUDIOBAT_HRTF_SOFA`
  environment variable at a different SOFA file to use another one. If no
  valid SOFA file is available, Advanced mode falls back to the same
  algebraic decode Basic mode uses rather than going silent.

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
  through a second PipeWire stream that targets the real default sink.
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

- CMake >= 3.20, a C++20 compiler (GCC or Clang), Ninja (or Make)
- PipeWire development headers: `libpipewire-0.3` (via pkg-config)
- zlib development headers (libmysofa's SOFA/HDF5 parsing depends on it)
- pthreads (part of the standard toolchain on Linux)
- Network access on first configure: `daemon/CMakeLists.txt` fetches
  libmysofa and KissFFT via CMake `FetchContent` (same mechanism
  `gui/CMakeLists.txt` already uses for Dear ImGui) since neither ships as
  a common distro package.

On this machine, everything above is already present (GCC 16, Clang 22,
CMake 4.4, Ninja 1.13, libpipewire-0.3 1.6.7). On a fresh machine, install
your distro's PipeWire development package, e.g. `pipewire-devel` /
`libpipewire-0.3-dev` / `libpipewire-0.3-devel` depending on distro, plus
`zlib1g-dev` / `zlib-devel` depending on distro.

### Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

The daemon binary is `build/daemon/audiobatd`.

## Running

```sh
./build/daemon/audiobatd
```

This creates the "AudioBat Virtual Sink" in the PipeWire graph and starts
a playback stream to your default hardware sink. Route any app's output to
the virtual sink (e.g. via `pavucontrol`, `wpctl`, or `pw-play --target
audiobat_virtual_sink some_7.1_file.wav`) to hear it downmixed through
AudioBat. Stop the daemon with Ctrl+C or `SIGTERM`; it tears down the
virtual sink and unlinks the control socket on the way out.

**Caveat:** don't set "AudioBat Virtual Sink" as your system default
sink — the daemon's own output stream autoconnects to whatever the default
sink is, and making the virtual sink the default would create a feedback
loop.

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

## Roadmap

1. ~~Working local pipeline: virtual sink → capture → placeholder DSP → real output~~
2. ~~One-click 3D on/off toggle: wired end-to-end and now audibly meaningful (PassthroughStage vs AmbisonicsStage)~~
3. ~~Real-time repositionable virtual speakers: control-protocol opcode + GUI dial~~
4. ~~HRTF-based binaural decode (libmysofa + KissFFT), selectable as the "Advanced" `SpatialMode` alongside algebraic ambisonics~~
5. Parametric EQ
6. ONNX-based automatic EQ preset selection
7. GUI polish aimed at non-audio-engineer users (v1 ships spatial mode dropdown + speaker dial only)

## License

AudioBat is dual-licensed: freely under the GNU General Public License
v3.0 (see [LICENSE](LICENSE)), or under a separate commercial license for
proprietary/closed-source use (see [LICENSE-COMMERCIAL.md](LICENSE-COMMERCIAL.md)).
Contributions are accepted only under the terms of the Contributor License
Agreement (see [CLA.md](CLA.md)), which is what makes offering both tracks
possible.
