// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include "imgui.h"

// Small set of custom-drawn widgets used by the redesigned control panel,
// following the same convention as position_dial.hpp/.cpp: stateless free
// functions operating on caller-owned data, a trailing Scale parameter
// (since these are raw ImDrawList draws that don't pick up DPI scaling
// from ImGui's own style/font the way stock widgets do), and
// InvisibleButton + IsItemActive/IsItemHovered for hit-testing.
namespace ramkolfx::gui
{

// A horizontal segmented control (pill group). Used both for the
// top-level tab nav and the spatial-mode Off/Basic/Advanced selector.
// Returns true and updates *CurrentIndex if the user clicked a different
// option this frame.
//
// TotalWidth < 0 (the default) fills the available content width, correct
// for a pill that's the only/widest thing on its row (e.g. the tab nav).
// Pass an explicit TotalWidth when the pill sits inside a column that's
// narrower than the window - GetContentRegionAvail() reflects the whole
// window, not any informal "column" a caller lays out via BeginGroup()/
// SameLine() (there's no child window constraining it), so relying on it
// there would silently stretch the pill (and therefore its enclosing
// group's measured bounding box) to the full window width.
bool DrawSegmentedPill(const char* Label, const char* const* Options, int OptionCount, int* CurrentIndex,
                       float Scale = 1.0f, float TotalWidth = -1.0f);

// An iOS-style toggle switch (38x21 track, 17px knob). Returns true and
// flips *Value if clicked this frame. The knob's slide animation is
// self-contained (kept in ImGui's per-widget state storage, keyed off
// Label), so the caller never needs to track animation progress itself.
bool DrawToggleSwitch(const char* Label, bool* Value, float Scale = 1.0f);

// A vertical gain bar for one EQ band: a gradient-filled bar (height
// proportional to (*GainDb - MinDb) / (MaxDb - MinDb)) with a draggable
// numeric gain readout above it. Dragging either the readout or the bar
// itself changes *GainDb by the same delta-to-value math, spread over
// Size.y pixels of vertical travel - same idiom as ImGui::VSliderFloat,
// which this replaces. Returns true if *GainDb changed this frame. Does
// not draw the frequency label below the bar - callers keep drawing that
// themselves, same as today.
bool DrawEqBandBar(const char* Label, float* GainDb, float MinDb, float MaxDb, ImVec2 Size, float Scale = 1.0f);

} // namespace ramkolfx::gui
