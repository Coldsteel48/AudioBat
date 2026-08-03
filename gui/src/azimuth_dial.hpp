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
// *OutChangedIndex is the speaker index whose AzimuthsDegrees entry was
// updated in place; the caller decides when/how often to push that value
// to the daemon (e.g. throttled while dragging, or on release).
bool DrawAzimuthDial(const char* Label, std::array<float, SpeakerCount>& AzimuthsDegrees,
                      int* OutChangedIndex);

} // namespace audiobat::gui
