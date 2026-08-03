// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "app.hpp"

#include "azimuth_dial.hpp"
#include "imgui.h"

namespace audiobat::gui
{

namespace
{
constexpr float ReconnectIntervalSeconds = 1.0f;
constexpr float StatusPollIntervalSeconds = 0.25f;
constexpr float DevicePollIntervalSeconds = 2.0f; // hotplug doesn't need to feel instant
constexpr float AzimuthSendIntervalSeconds = 0.03f; // ~33 Hz cap while dragging
} // namespace

void App::Tick(float DeltaTimeSeconds)
{
    if (!bConnected)
    {
        ReconnectTimerSeconds -= DeltaTimeSeconds;
        if (ReconnectTimerSeconds <= 0.0f)
        {
            ReconnectTimerSeconds = ReconnectIntervalSeconds;
            if (Client.Connect())
            {
                if (auto Result = Client.RequestStatus())
                {
                    LastStatus = *Result;
                    if (auto DeviceResult = Client.RequestDevices())
                    {
                        Devices = std::move(*DeviceResult);
                    }
                    DevicePollTimerSeconds = DevicePollIntervalSeconds;
                    bConnected = true;
                }
                else
                {
                    Client.Disconnect();
                }
            }
        }
    }
    else
    {
        StatusPollTimerSeconds -= DeltaTimeSeconds;
        if (StatusPollTimerSeconds <= 0.0f)
        {
            StatusPollTimerSeconds = StatusPollIntervalSeconds;
            if (auto Result = Client.RequestStatus())
            {
                LastStatus = *Result;
            }
            else
            {
                bConnected = false;
                ReconnectTimerSeconds = 0.0f; // retry immediately
            }
        }

        DevicePollTimerSeconds -= DeltaTimeSeconds;
        if (bConnected && DevicePollTimerSeconds <= 0.0f)
        {
            DevicePollTimerSeconds = DevicePollIntervalSeconds;
            if (auto Result = Client.RequestDevices())
            {
                Devices = std::move(*Result);
            }
            else
            {
                bConnected = false;
                ReconnectTimerSeconds = 0.0f;
            }
        }
    }

    DrawUI();
}

void App::DrawUI()
{
    ImGui::Begin("AudioBat Control", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (bConnected)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Connected to audiobatd");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f), "Daemon not running - retrying...");
    }

    ImGui::BeginDisabled(!bConnected);

    static const char* ModeLabels[] = {"Off", "Basic (Ambisonics)", "Advanced (HRTF Binaural)"};
    const int CurrentModeIndex = static_cast<int>(LastStatus.Mode);
    const char* CurrentModeLabel =
        (CurrentModeIndex >= 0 && CurrentModeIndex < 3) ? ModeLabels[CurrentModeIndex] : "Unknown";

    ImGui::TextUnformatted("Spatialization");
    if (ImGui::BeginCombo("##spatial_mode", CurrentModeLabel))
    {
        for (int i = 0; i < 3; ++i)
        {
            const bool bSelected = i == CurrentModeIndex;
            if (ImGui::Selectable(ModeLabels[i], bSelected))
            {
                if (auto Result = Client.SetSpatialMode(static_cast<SpatialMode>(i)))
                {
                    LastStatus = *Result;
                }
                else
                {
                    bConnected = false;
                    ReconnectTimerSeconds = 0.0f;
                }
            }
            if (bSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Output device");

    std::string PreviewLabel = LastStatus.OutputDeviceName.empty() ? "(none)" : LastStatus.OutputDeviceName;
    for (const auto& Device : Devices)
    {
        if (Device.Name == LastStatus.OutputDeviceName)
        {
            PreviewLabel = Device.Description;
            break;
        }
    }

    if (ImGui::BeginCombo("##output_device", PreviewLabel.c_str()))
    {
        for (const auto& Device : Devices)
        {
            const bool bSelected = Device.Name == LastStatus.OutputDeviceName;
            if (ImGui::Selectable(Device.Description.c_str(), bSelected))
            {
                if (auto Result = Client.SetOutputDevice(Device.Name))
                {
                    LastStatus = *Result;
                }
                else
                {
                    bConnected = false;
                    ReconnectTimerSeconds = 0.0f;
                }
            }
            if (bSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Speaker positions");

    int ChangedIndex = -1;
    const bool bDialChanged = DrawAzimuthDial("speaker_dial", LastStatus.SpeakerAzimuthDegrees, &ChangedIndex);

    if (bDialChanged)
    {
        AzimuthSendTimerSeconds -= ImGui::GetIO().DeltaTime;
        if (AzimuthSendTimerSeconds <= 0.0f)
        {
            AzimuthSendTimerSeconds = AzimuthSendIntervalSeconds;
            if (auto Result = Client.SetSpeakerAzimuth(static_cast<uint8_t>(ChangedIndex),
                                                         LastStatus.SpeakerAzimuthDegrees[ChangedIndex]))
            {
                LastStatus = *Result;
            }
            else
            {
                bConnected = false;
                ReconnectTimerSeconds = 0.0f;
            }
        }
    }
    else
    {
        AzimuthSendTimerSeconds = 0.0f;
    }

    if (ImGui::Button("Reset speaker positions"))
    {
        if (auto Result = Client.ResetSpeakerPositions())
        {
            LastStatus = *Result;
        }
        else
        {
            bConnected = false;
            ReconnectTimerSeconds = 0.0f;
        }
    }

    ImGui::EndDisabled();

    ImGui::End();
}

} // namespace audiobat::gui
