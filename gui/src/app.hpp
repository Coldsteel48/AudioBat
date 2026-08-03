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
    void Tick(float DeltaTimeSeconds);

private:
    void DrawUI();

    ControlClient Client;
    Status LastStatus;
    std::vector<AudioDeviceInfo> Devices;
    bool bConnected = false;

    float ReconnectTimerSeconds = 0.0f;
    float StatusPollTimerSeconds = 0.0f;
    float DevicePollTimerSeconds = 0.0f;
    float AzimuthSendTimerSeconds = 0.0f;
};

} // namespace audiobat::gui
