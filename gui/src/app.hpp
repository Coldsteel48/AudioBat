// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <vector>

#include "audiobat/protocol.hpp"
#include "control_client.hpp"

namespace audiobat::gui
{

// Owns the control connection and draws the whole UI. Tick() is called
// once per frame between ImGui::NewFrame() and ImGui::Render().
class App
{
public:
    explicit App(float InDpiScale) : DpiScale(InDpiScale)
    {
    }

    void Tick(float DeltaTimeSeconds);

private:
    void DrawUI();

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
    // DrawPositionDial's bMirrorEnabled.
    bool bMirrorModeEnabled = false;
};

} // namespace audiobat::gui
