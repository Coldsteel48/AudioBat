# Per-speaker distance with genuine near-field ILD

## Context

The previous plan in this file (switchable HRTF catalog + live crossfade)
shipped earlier this session, on `master`. This is a new, separate
feature, built on branch `near_field_ILD`: the user asked whether virtual
speakers' *distance* from the head can be controlled, and — after being
offered a choice between a cheap loudness-falloff version and a
physically-grounded near-field proximity effect — explicitly chose the
latter ("I actually want the Near Field").

Real near-field proximity (the "it's right next to my ear" sensation, not
just "it got louder") comes from head diffraction boosting interaural
level difference (ILD) as a source gets close — first-order ambisonics
and today's far-field-measured HRTFs (SADIE II, MIT KEMAR, and the
synthetic spherical-head fallback all built earlier this session) have no
distance term at all, so this needs new DSP, not just a UI slider.

**This whole feature is a toggle, off by default.** When off, AudioBat's
signal path is *literally* today's shipped code, unmodified — not just
"equivalent behavior," the same code path, so there's zero regression risk
for anyone who doesn't turn it on. When on, distance affects loudness
(all modes) and Advanced mode additionally gets the real ILD proximity
effect. This came from the user's explicit ask to make it toggleable
rather than replacing existing behavior outright, and it also gives a
clean A/B way to judge whether the effect is actually worth keeping on,
the same way the HRTF catalog dropdown lets someone compare datasets.

## The key architectural constraint (why the "on" path needs new plumbing)

`BinauralStage` today: encodes all 7 non-LFE 7.1 sources into ONE shared
ambisonic field (`SpeakerLayout::Encode`, W/X/Y), decodes that field to 8
*fixed* virtual loudspeaker directions, and convolves each against a
far-field HRTF (`binaural_stage.cpp`). This is why live speaker
repositioning is cheap today (`SpeakerLayout::SnapshotDirections()` is
just cos/sin, recomputed every block) — the HRTF convolvers themselves
never change when a speaker moves, only the encode weights do.

Near-field ILD correction is fundamentally per-ear and per-source: it has
to be a *filter* applied per source, and filters don't commute with
summation. Once multiple sources are sitting in one shared W/X/Y field,
distance information can't be recovered per-source. So getting real ILD
requires rendering each of the 7 sources through its own direct
HRTF+near-field convolution and summing the resulting stereo streams,
instead of going through the shared-field/8-virtual-speaker indirection —
but only when the toggle is on. Off, `BinauralStage` keeps doing exactly
what it does today.

**Consequence when on**: live-dragging a speaker in Advanced mode now
means rebuilding that one source's HRTF+near-field convolver pair, not
just re-weighting a fixed encode. Mitigated by generalizing the
crossfade-swap machinery `HrtfDeck` already built this session (build
off-thread, publish via an atomic, ~50ms linear crossfade, deferred
garbage collection) down to a *per-source* granularity, so a rebuild
triggered by dragging never clicks. Rebuild cost itself (one
SOFA/synthetic lookup + a short filter convolver `Load()`) should be cheap
enough to do at the same throttled rate the GUI already drags azimuth at
(`AzimuthSendIntervalSeconds` in `gui/src/app.cpp`) — this gets a real
timing check during implementation (see Verification), with "commit only
on drag-release" as a documented fallback if it isn't.

## Distance model

- `SpeakerLayout` gains a per-speaker `Distance` (meters) alongside
  `AzimuthDegrees`, same get/set/reset pattern
  (`SetSpeakerDistance`/`GetSpeakerDistance`,
  `ResetSpeakerAzimuths`→also resets distance). Default 1.5m, clamped
  range [0.3m, 3.0m] — floor kept well above the 0.0875m head radius
  already established in `synthetic_hrtf.cpp` so the near-field filter
  stays numerically well-behaved. `Distance` is stored and settable
  regardless of the toggle state (so a user can position things before
  switching it on), it just has no audible effect while off.
- Global toggle: `bNearFieldEnabled`, default `false`. Gates *all*
  distance-driven audio behavior described below — the falloff and the
  ILD filter both.
- When on, universal (all modes): an inverse-distance loudness falloff
  (`Gain = ReferenceDistance / max(Distance, Floor)`, reference 1m)
  applied to each source's amplitude before encoding — a one-line scalar,
  no architecture change, so Basic/Off modes get a sensible "closer =
  louder" cue too even though they don't get ILD.
- When on, Advanced-mode-only: a per-ear near-field correction filter
  cascaded after whichever far-field HRTF (measured or synthetic) is
  already driving that source's direction, based on the rigid-sphere
  finite-range model (Duda & Martin, "Range dependence of the response of
  a spherical head model," JASA 1998) — the same head-radius/speed-of-
  sound physical model `synthetic_hrtf.cpp` already uses, extended with
  the distance term. New file `daemon/src/dsp/near_field_filter.hpp/.cpp`:
  `HrtfFilter ComputeNearFieldCorrection(float AzimuthDegrees, float DistanceMeters, float SampleRate)`,
  designed to cascade with (not replace) the existing far-field filter.
  I'll derive/tune the actual filter during implementation against known
  physical bounds rather than trust recalled coefficients blindly: at the
  1m reference distance it must reduce to ~unity, and as distance shrinks
  the ipsilateral ear's low-frequency gain should rise monotonically
  relative to the contralateral ear, bounded and stable down to the 0.3m
  floor.

## `BinauralStage`: additive, not replaced

The existing shared-field signal path in `binaural_stage.cpp` stays
exactly as it is today, verbatim — that *is* the "off" behavior. A new,
separate signal path is added alongside it for "on." Nothing about the
`DspStage` interface, `HrtfDeck`'s whole-stage crossfade for HRTF catalog
switching, the SOFA-load-failure fallback to `AmbisonicsStage`, LFE
handling, or mute/solo (handled upstream in `AudioEngine`, never touches
`BinauralStage` at all) changes. Basic and Off modes
(`AmbisonicsStage`/`PassthroughStage`) aren't touched in any way.

- New `daemon/src/dsp/binaural_voice.hpp/.cpp`: one source's Left+Right
  convolver pair (far-field HRTF cascaded with the near-field filter for
  its current azimuth/distance), with a `Rebuild(azimuth, distance, HRTF
  source)` that constructs new filters off-thread and a `Process()` that's
  realtime-safe. Internally reuses the same build-off-thread /
  atomic-publish / crossfade-blend / deferred-garbage-collect pattern
  `HrtfDeck` (`daemon/src/dsp/hrtf_deck.hpp/.cpp`) already implements —
  factored out into a small shared `CrossfadingSlot<T>` utility both
  `HrtfDeck` and `BinauralVoice` build on, rather than copy-pasting that
  concurrency logic a second time.
- `BinauralStage::Process()` gains a branch at the top: if
  `bNearFieldEnabled` is false, run exactly the code that's there today
  (unmodified). If true, feed each raw source channel into its own
  `BinauralVoice` (one per non-LFE 7.1 channel) and sum the 7 stereo
  outputs instead. `HrtfDeck` itself is unaffected — it still
  swaps/crossfades a whole `BinauralStage` for HRTF *catalog* switching
  (each freshly-built `BinauralStage` constructs its 7 voices too, built
  lazily only once the toggle is first turned on, so "off" users pay
  zero extra memory/CPU for machinery they're not using).
- Flipping the toggle itself crossfades (~50ms, same mechanism) between
  "old path's output" and "new path's output" rather than switching
  instantaneously, so turning it on/off mid-playback doesn't click either.
- While on, speaker layout changes get pushed into each `BinauralVoice`
  at block-processing time (checking current azimuth/distance against
  what it was last built for, triggering `Rebuild()` on a difference).
  `AmbisonicsStage` keeps using `SnapshotDirections()` unchanged for Basic
  mode regardless of the toggle.

## Protocol (`common/include/audiobat/protocol.hpp` + `.cpp`)

- New opcode `SetNearFieldEnabled` (`bool`) → `StatusResponse`, mirroring
  `SetTestNoise`/`EncodeSetTestNoiseRequest` exactly.
- New opcode `SetSpeakerDistance` (`SpeakerIndex` + `f32` meters) →
  `StatusResponse`, mirroring `SetSpeakerAzimuth`/
  `EncodeSetSpeakerAzimuthRequest` exactly.
- `Status` gains `bool bNearFieldEnabled` and
  `std::array<float, SpeakerCount> SpeakerDistanceMeters`; encode/decode
  extended the same way `ActiveHrtfIndex` was added earlier this session
  (append to the fixed-size portion of the payload).

## GUI

- New checkbox "Near-field distance" mirroring the existing "Play test
  noise on all speakers" checkbox in `gui/src/app.cpp` exactly (same
  `ImGui::Checkbox` + `Client.Set...` pattern), placed near the speaker
  dial.
- `gui/src/azimuth_dial.hpp/.cpp`: today each handle sits on a *fixed*
  pixel radius (`Radius` computed once from `DialSize`,
  `AzimuthToUnitVector` only encodes angle — confirmed by reading the
  current implementation). Change the handle's pixel distance from center
  to map `Distance` into `[InnerRadiusPx, OuterRadiusPx]`, and dragging
  updates both the angle (as today, via `ScreenPosToAzimuth`) and the
  radius (inverse-mapped back to meters, clamped to [0.3, 3.0]) — the
  dial stays draggable in both dimensions regardless of the toggle, so
  positioning can be done in advance. Given the widget's scope grows from
  "azimuth" to "azimuth + distance," rename to `position_dial.hpp/.cpp` /
  `DrawPositionDial` rather than stretch the old name over a wider job.
- `gui/src/app.cpp`/`app.hpp`: extend `LastStatus`-driven drag handling
  to also track/send distance changes, mirroring the existing
  `ChangedAzimuthIndex`/`AzimuthSendTimerSeconds` throttle
  (`AzimuthSendIntervalSeconds`) for the new distance value.
- `gui/src/control_client.hpp/.cpp`: add `SetSpeakerDistance(SpeakerIndex, Distance)`
  and `SetNearFieldEnabled(bool)`, mirroring existing methods exactly.

## Verification

- Regression check: with the toggle off, confirm `BinauralStage`'s output
  for a fixed test signal is unchanged from `master` (pre-this-branch) —
  concretely provable now, since the "off" path is the literal old code
  rather than a reimplementation.
- Offline probe (same style used earlier this session for the HRTF
  catalog work): compute `ComputeNearFieldCorrection` directly at the 1m
  reference distance and confirm it's ~unity gain; sweep distance down to
  the 0.3m floor and confirm the ipsilateral/contralateral gain ratio
  grows monotonically and stays bounded (no blow-up, learned from the
  gain-normalization bug caught last time on this same branch's earlier
  feature).
- Offline probe: with the toggle on, feed a continuous per-channel test
  tone through `BinauralStage` at a few azimuth/distance combinations,
  confirm output stays bounded and directionally sensible (same impulse-
  response-style check used to validate the synthetic HRTF and the
  crossfade earlier).
- Timing check: measure wall-clock cost of one `BinauralVoice::Rebuild()`
  call to confirm the GUI's existing drag-throttle rate is safe; drop to
  commit-on-release if it isn't.
- Manual run: daemon + GUI, toggle near-field on, drag a speaker inward on
  the new 2D dial with a game/test-noise source playing in Advanced mode,
  confirm the proximity effect is audible and that neither dragging, an
  HRTF-catalog switch mid-drag, nor toggling on/off produces a click.
- `git status`/`git diff` review; no commit unless explicitly asked.
