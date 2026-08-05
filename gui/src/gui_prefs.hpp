// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

namespace ramkolfx::gui
{

// Preferences that belong to this GUI instance alone and are never sent
// to the daemon (see App::bMirrorModeEnabled) - kept in their own file
// rather than folded into the daemon's SettingsStore so a headless
// client or a second GUI instance isn't affected by one GUI's local UX
// choices.
struct GuiPreferences
{
    bool bMirrorModeEnabled = false;

    // Whether the HW EQ panel shows per-band frequency/Q/filter-type
    // controls (advanced) or just the 10 ISO-labeled gain sliders
    // (simple). Purely a display choice - the daemon's wire format is
    // already fully parametric either way, see protocol.hpp's EqBand.
    bool bAdvancedEqMode = false;

    // Whether the bass enhancer panel shows Cutoff/Drive controls
    // (advanced) or just the Enabled toggle and Amount slider (simple).
    // Purely a display choice, same reasoning as bAdvancedEqMode - see
    // protocol.hpp's BassEnhancerSettings.
    bool bAdvancedBassMode = false;
};

// Reads $XDG_CONFIG_HOME/ramkolfx/gui_prefs.conf (falling back to
// ~/.config/ramkolfx/gui_prefs.conf), or GuiPreferences{} defaults if the
// file doesn't exist or can't be read.
GuiPreferences LoadGuiPreferences();

// Writes InPreferences to the same path Load() reads from. Best-effort:
// logs to stderr and returns on failure rather than throwing.
void SaveGuiPreferences(const GuiPreferences& InPreferences);

} // namespace ramkolfx::gui
