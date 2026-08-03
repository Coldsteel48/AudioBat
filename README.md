# AudioDock

A Linux audio DSP daemon that provides ambisonics-style 7.1-to-stereo
spatialization for headphones. Unlike typical "virtual surround" plugins
that apply a fixed per-channel HRTF, AudioDock encodes the 7.1 signal into
an ambisonics sound field and decodes that field to stereo, so positioning
stays accurate as speakers move and the mix doesn't collapse into six
separately-panned point sources.

## Status

Pipeline runs end-to-end with a first real spatialization pass. A real
7.1 stream played into the virtual sink is heard, spatialized, on the real
output device. The `SetThreeDEnabled` control command now switches between
two genuinely different signal paths:

- **3D off**: static-gain downmix (`PassthroughStage`).
- **3D on**: `AmbisonicsStage` encodes the 7 non-LFE 7.1 channels as point
  sources (at their nominal speaker azimuths) into a shared first-order
  B-format sound field, then decodes that field to two virtual stereo
  speakers. This is a plain algebraic (non-HRTF) decode — an improvement
  over static downmix, but a linear 2-speaker ambisonic decode can't fully
  preserve front/back distinction. HRTF-based binaural decode (via
  libmysofa + KissFFT) is the planned upgrade, swapped in behind the same
  `DspStage` interface without touching the rest of the pipeline.

Virtual speaker positions are already live-repositionable at the
`AmbisonicsStage` API level (`SetSpeakerAzimuth`/`GetSpeakerAzimuth`,
lock-free, callable from any thread) — not yet exposed over the control
protocol, which is the natural next step.

## Architecture

```
   client app (game, media player)
            │  renders 7.1 via OpenAL / PipeWire
            ▼
   PipeWire virtual sink  ("AudioDock Virtual Sink")
            │  captured by the daemon
            ▼
   DSP stage  (AmbisonicsStage: B-format encode/decode, or PassthroughStage downmix when 3D is off)
            │
            ▼
   real hardware output  (via a PipeWire playback stream)

   GUI control app  <──Unix socket, binary protocol──>  daemon
```

- **`audiodockd`** (the daemon) owns the real audio output device. It never
  talks to hardware directly on the input side — client apps render into a
  PipeWire virtual sink instead, exactly as if it were a normal output
  device.
- The daemon captures from that virtual sink, runs it through a DSP stage,
  and plays the result out through a second PipeWire stream that targets
  the real default sink.
- A **GUI control app** (not yet built) will talk to the daemon over a Unix
  domain socket to toggle 3D processing, reposition virtual speakers live,
  and (later) manage EQ — all in real time, without restarting the daemon.

## Directory layout

- `common/` — shared code between daemon and GUI: the control protocol and
  a lock-free ring buffer. No PipeWire dependency.
- `daemon/` — `audiodockd`: the PipeWire pipeline, DSP stage, and control
  socket server.
- `gui/` — control app (not started; toolkit not yet chosen).
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
`common/include/audiodock/protocol.hpp` for the wire format and the full
rationale; the format is intentionally isolated to that header/.cpp pair so
it can change without touching daemon or GUI logic built on top of it.

Socket path: `$XDG_RUNTIME_DIR/audiodock/control.sock` (falls back to
`/tmp/audiodock-<uid>/control.sock`).

## Building

### Dependencies

- CMake >= 3.20, a C++20 compiler (GCC or Clang), Ninja (or Make)
- PipeWire development headers: `libpipewire-0.3` (via pkg-config)
- pthreads (part of the standard toolchain on Linux)

On this machine, everything above is already present (GCC 16, Clang 22,
CMake 4.4, Ninja 1.13, libpipewire-0.3 1.6.7). On a fresh machine, install
your distro's PipeWire development package, e.g. `pipewire-devel` /
`libpipewire-0.3-dev` / `libpipewire-0.3-devel` depending on distro.

### Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

The daemon binary is `build/daemon/audiodockd`.

## Running

```sh
./build/daemon/audiodockd
```

This creates the "AudioDock Virtual Sink" in the PipeWire graph and starts
a playback stream to your default hardware sink. Route any app's output to
the virtual sink (e.g. via `pavucontrol`, `wpctl`, or `pw-play --target
audiodock_virtual_sink some_7.1_file.wav`) to hear it downmixed through
AudioDock. Stop the daemon with Ctrl+C or `SIGTERM`; it tears down the
virtual sink and unlinks the control socket on the way out.

**Caveat:** don't set "AudioDock Virtual Sink" as your system default
sink — the daemon's own output stream autoconnects to whatever the default
sink is, and making the virtual sink the default would create a feedback
loop.

### Testing the control socket manually

No client app exists yet, but you can poke the protocol directly with
`socat` (`[opcode: u8][length: u16 BE][payload]`, see
`common/include/audiodock/protocol.hpp`):

```sh
# GetStatus (0x01, no payload), then SetThreeDEnabled(true) (0x02, len=1, payload=1)
printf '\x01\x00\x00\x02\x00\x01\x01' | \
    socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/audiodock/control.sock | od -An -tx1
```

Each reply is `[0x81][len=1][0 or 1]` (status) or `[0x82][len][message]`
(error).

## Roadmap

1. ~~Working local pipeline: virtual sink → capture → placeholder DSP → real output~~
2. ~~One-click 3D on/off toggle: wired end-to-end and now audibly meaningful (PassthroughStage vs AmbisonicsStage)~~ — still needs a GUI, currently only reachable via raw protocol bytes
3. Real-time repositionable virtual speakers: `AmbisonicsStage` API exists; needs a control-protocol opcode + GUI hookup
4. Parametric EQ
5. ONNX-based automatic EQ preset selection
6. GUI aimed at non-audio-engineer users

## License

AudioDock is dual-licensed: freely under the GNU General Public License
v3.0 (see [LICENSE](LICENSE)), or under a separate commercial license for
proprietary/closed-source use (see [LICENSE-COMMERCIAL.md](LICENSE-COMMERCIAL.md)).
Contributions are accepted only under the terms of the Contributor License
Agreement (see [CLA.md](CLA.md)), which is what makes offering both tracks
possible.
