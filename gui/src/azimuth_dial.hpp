// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>

#include "audiobat/protocol.hpp"

namespace audiobat::gui
{

// Draws a top-down dial with one draggable handle per virtual 7.1 speaker
// (order: FL,FR,FC,RL,RR,SL,SR, matching AmbisonicsStage::SpeakerChannel
// and Status::SpeakerAzimuthDegrees). AzimuthDegrees convention: 0 = front,
// positive = left, negative = right - same as AmbisonicsStage.
//
// Returns true if the user dragged a handle this frame, in which case
// *OutChangedAzimuthIndex is the speaker index whose AzimuthsDegrees entry
// was updated in place; the caller decides when/how often to push that
// value to the daemon (e.g. throttled while dragging, or on release).
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
bool DrawAzimuthDial(const char* Label, std::array<float, SpeakerCount>& AzimuthsDegrees,
                      const std::array<bool, SpeakerCount>& Muted, int* OutChangedAzimuthIndex,
                      int* OutMuteToggledIndex, int* OutSoloIndex, float Scale = 1.0f);

} // namespace audiobat::gui
