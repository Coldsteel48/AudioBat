// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>
#include <vector>

#include "ramkolfx/protocol.hpp"
#include "control_client.hpp"
#include "gui_prefs.hpp"

namespace ramkolfx::gui
{

// Owns the control connection and draws the whole UI. Tick() is called
// once per frame between ImGui::NewFrame() and ImGui::Render().
class App
{
public:
    explicit App(float InDpiScale)
        : DpiScale(InDpiScale), bMirrorModeEnabled(LoadGuiPreferences().bMirrorModeEnabled),
          bAdvancedEqMode(LoadGuiPreferences().bAdvancedEqMode),
          bAdvancedBassMode(LoadGuiPreferences().bAdvancedBassMode),
          ActiveTabIndex(LoadGuiPreferences().ActiveTabIndex)
    {
    }

    void Tick(float DeltaTimeSeconds);

private:
    void DrawUI();
    void DrawSpatialTab();
    void DrawEqTab();

    // Writes every local-only GUI preference field to gui_prefs.hpp in one
    // call - used everywhere a preference-backed toggle changes, so no
    // call site can accidentally reset a sibling field to its default the
    // way constructing a partial GuiPreferences{} inline used to risk.
    void PersistGuiPreferences() const;

    float DpiScale = 1.0f;

    ControlClient Client;
    Status LastStatus;
    std::vector<AudioDeviceInfo> Devices;
    std::vector<std::string> HrtfCatalog;
    bool bConnected = false;

    float ReconnectTimerSeconds = 0.0f;
    float StatusPollTimerSeconds = 0.0f;
    float DevicePollTimerSeconds = 0.0f;
    float PositionSendTimerSeconds = 0.0f; // throttles azimuth+distance sends together while dragging
    // True when a locally-dragged azimuth/distance change hasn't been sent
    // to the daemon yet (throttled out by PositionSendTimerSeconds). Flushed
    // as soon as the drag ends so the next status poll can't overwrite the
    // handle with a stale server value - see the drag-release send in
    // DrawUI().
    bool bPositionSendPending = false;
    int PendingPositionIndex = -1;
    int PendingMirroredIndex = -1;

    // Local-only UX toggle (never sent to the daemon): when on, dragging a
    // left/right speaker also drags its mirrored counterpart - see
    // DrawPositionDial's bMirrorEnabled. Persisted separately via
    // gui_prefs.hpp since it never touches the daemon's SettingsStore.
    bool bMirrorModeEnabled = false;

    // Local cache of the current 10-band HW EQ curve - fetched once on
    // connect/reconnect via RequestHwEqState() and kept in sync
    // optimistically as the user drags sliders (Status deliberately
    // doesn't carry EQ state - see protocol.hpp's HwEqStateResponse).
    // Default-constructed EqBands (all at EqBand's own 1kHz/0dB default)
    // until the first successful RequestHwEqState() overwrites them - the
    // panel is disabled (see ImGui::BeginDisabled(!bConnected) in DrawUI)
    // until then anyway, so the placeholder is never actually shown.
    std::array<EqBand, MaxEqBands> HwEqBands{};

    // Whether the EQ panel shows per-band frequency/Q/filter-type controls
    // - local-only UX toggle, persisted via gui_prefs.hpp same as
    // bMirrorModeEnabled.
    bool bAdvancedEqMode = false;

    // Local cache of the current bass enhancer settings - fetched once on
    // connect/reconnect via RequestBassEnhancerState() and kept in sync
    // optimistically as the user adjusts controls, same pattern as
    // HwEqBands.
    BassEnhancerSettings BassEnhancer{};

    // Whether the bass enhancer panel shows Cutoff/Drive controls -
    // local-only UX toggle, persisted via gui_prefs.hpp same as
    // bAdvancedEqMode.
    bool bAdvancedBassMode = false;

    // Which top-level tab (0 = Spatial Audio & Speakers, 1 = Equalizer &
    // Bass) is currently showing - only one tab's DrawXxxTab() runs per
    // frame, so the AlwaysAutoResize window only ever measures whichever
    // tab is active, same as before this tab split existed. Persisted via
    // gui_prefs.hpp same as the other local-only UX toggles above.
    int ActiveTabIndex = 0;

    // Accumulated while LastStatus.bTestNoiseEnabled is true (reset to 0
    // otherwise, so the animation phase always restarts cleanly); drives
    // the speaker dial's test-noise pulse animation. Purely cosmetic - see
    // DrawPositionDial's PulseTimeSeconds parameter.
    float TestNoisePulseTimeSeconds = 0.0f;
};

} // namespace ramkolfx::gui
