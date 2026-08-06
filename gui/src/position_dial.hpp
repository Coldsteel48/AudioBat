// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>

#include "ramkolfx/protocol.hpp"

namespace ramkolfx::gui
{

// Draws a top-down dial with one draggable handle per virtual 7.1 speaker
// (order: FL,FR,FC,RL,RR,SL,SR, matching AmbisonicsStage::SpeakerChannel
// and Status::SpeakerAzimuthDegrees/SpeakerDistanceMeters). A handle's
// angle is its azimuth (0 = front, positive = left, negative = right,
// same as AmbisonicsStage) and its distance from center is its distance
// (meters, mapped between MinSpeakerDistanceMeters at the innermost ring
// and MaxSpeakerDistanceMeters at the outermost - see protocol.hpp).
// Distance is always draggable regardless of whether near-field mode is
// on; it just has no audible effect while off (see
// docs/near-field-distance-plan.md).
//
// Returns true if the user dragged a handle this frame, in which case
// *OutChangedIndex is the speaker index whose AzimuthsDegrees and
// DistancesMeters entries were both updated in place; the caller decides
// when/how often to push those values to the daemon (e.g. throttled while
// dragging, or on release).
//
// Each speaker's label doubles as its mute control, right next to its
// handle: a plain click sets *OutMuteToggledIndex to that speaker's index
// (caller should send the opposite of its current Muted state); Ctrl+click
// sets *OutSoloIndex instead (caller should mute every other speaker and
// unmute this one). Muted is read-only here - it only reflects
// daemon-confirmed state for drawing (e.g. a strikethrough label), the
// caller owns actually sending mute/solo changes and updating it.
// *OutMuteToggledIndex and *OutSoloIndex are set to -1 when nothing of that
// kind happened this frame.
//
// Scale multiplies every pixel size the dial draws itself at (radius,
// handle size, line thickness, label offsets) - the widget is entirely
// custom ImDrawList calls, so it doesn't pick up DPI scaling from ImGui's
// style/font the way regular widgets do.
//
// When bMirrorEnabled is true, dragging a speaker that has a left/right
// counterpart (FL<->FR, RL<->RR, SL<->SR) also moves that counterpart to
// the mirrored azimuth (negated) at the same distance; FC has no
// counterpart and is unaffected. *OutMirroredIndex is set to that
// counterpart's index when this happens this frame, -1 otherwise - the
// caller should send its updated azimuth/distance to the daemon the same
// way it does for *OutChangedIndex.
//
// When bTestNoiseEnabled is true, every currently-unmuted speaker's cone
// pulses (opacity/scale animation) to indicate it's part of the test-noise
// signal - purely cosmetic, driven by PulseTimeSeconds (a caller-owned
// accumulator, so the animation phase survives across frames without this
// widget needing any state of its own). Note that solo already works
// today by muting every other speaker (see the caller's *OutSoloIndex
// handling), so Muted[i] alone is always the correct "is this speaker
// currently part of the mix" signal - no separate solo state needed here.
bool DrawPositionDial(const char* Label, std::array<float, SpeakerCount>& AzimuthsDegrees,
                      std::array<float, SpeakerCount>& DistancesMeters,
                      const std::array<bool, SpeakerCount>& Muted, bool bMirrorEnabled,
                      int* OutChangedIndex, int* OutMirroredIndex, int* OutMuteToggledIndex,
                      int* OutSoloIndex, float Scale = 1.0f, bool bTestNoiseEnabled = false,
                      float PulseTimeSeconds = 0.0f);

// Display label for speaker Index (0..SpeakerCount-1), in the same fixed
// order DrawPositionDial uses (FL,FR,FC,RL,RR,SL,SR). Exported so callers
// building their own per-speaker UI alongside the dial (e.g. an explicit
// mute/solo button row) don't need to duplicate this array and risk it
// drifting out of sync with the dial's own index order.
const char* GetSpeakerLabel(size_t Index);

} // namespace ramkolfx::gui
