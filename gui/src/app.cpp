// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "app.hpp"

#include "imgui.h"
#include "position_dial.hpp"

namespace audiobat::gui
{

namespace
{
constexpr float ReconnectIntervalSeconds = 1.0f;
constexpr float StatusPollIntervalSeconds = 0.25f;
constexpr float DevicePollIntervalSeconds = 2.0f; // hotplug doesn't need to feel instant
constexpr float PositionSendIntervalSeconds = 0.03f; // ~33 Hz cap while dragging
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
                    if (auto HrtfResult = Client.RequestHrtfCatalog())
                    {
                        HrtfCatalog = std::move(*HrtfResult);
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
            if (bConnected)
            {
                if (auto HrtfResult = Client.RequestHrtfCatalog())
                {
                    HrtfCatalog = std::move(*HrtfResult);
                }
                else
                {
                    bConnected = false;
                    ReconnectTimerSeconds = 0.0f;
                }
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
    ImGui::TextUnformatted("HRTF (Advanced spatial mode)");

    const int CurrentHrtfIndex = static_cast<int>(LastStatus.ActiveHrtfIndex);
    const char* CurrentHrtfLabel =
        (CurrentHrtfIndex >= 0 && CurrentHrtfIndex < static_cast<int>(HrtfCatalog.size()))
            ? HrtfCatalog[static_cast<size_t>(CurrentHrtfIndex)].c_str()
            : "(none)";

    if (ImGui::BeginCombo("##hrtf_source", CurrentHrtfLabel))
    {
        for (int i = 0; i < static_cast<int>(HrtfCatalog.size()); ++i)
        {
            const bool bSelected = i == CurrentHrtfIndex;
            if (ImGui::Selectable(HrtfCatalog[static_cast<size_t>(i)].c_str(), bSelected))
            {
                if (auto Result = Client.SetHrtfFile(static_cast<uint8_t>(i)))
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
    bool bNearFieldEnabled = LastStatus.bNearFieldEnabled;
    if (ImGui::Checkbox("Near-field distance", &bNearFieldEnabled))
    {
        if (auto Result = Client.SetNearFieldEnabled(bNearFieldEnabled))
        {
            LastStatus = *Result;
        }
        else
        {
            bConnected = false;
            ReconnectTimerSeconds = 0.0f;
        }
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Off: speaker distance (drag radially on the dial below) has no effect - today's "
                           "behavior.\nOn: closer speakers get louder in every mode, and Advanced (HRTF) mode "
                           "additionally gets a real interaural-level-difference proximity effect.");
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(
        "Speaker positions (drag: angle + distance; click a label to mute, Ctrl+click to solo)");

    // Sends one mute/unmute command and folds the response into LastStatus;
    // on failure, flags disconnected the same way every other request
    // above does and reports the failure to the caller so a multi-command
    // sequence (Solo, Unmute all) stops rather than spamming a dead
    // connection.
    auto SetSpeakerMuted = [&](uint8_t SpeakerIndex, bool bMuted) -> bool
    {
        if (auto Result = Client.SetSpeakerMute(SpeakerIndex, bMuted))
        {
            LastStatus = *Result;
            return true;
        }
        bConnected = false;
        ReconnectTimerSeconds = 0.0f;
        return false;
    };

    // Sends the given speaker's current (locally-dragged) azimuth and
    // distance and folds the response into LastStatus, the same
    // fail-and-disconnect pattern as SetSpeakerMuted above.
    auto SendSpeakerPosition = [&](uint8_t SpeakerIndex)
    {
        // Captured before the azimuth round-trip: that response's
        // LastStatus assignment below overwrites SpeakerDistanceMeters
        // with whatever the daemon had *before* this drag's distance is
        // sent, so reading it fresh afterward would send the stale
        // pre-drag distance right back to the daemon.
        const float DistanceToSend = LastStatus.SpeakerDistanceMeters[SpeakerIndex];
        if (auto Result = Client.SetSpeakerAzimuth(SpeakerIndex, LastStatus.SpeakerAzimuthDegrees[SpeakerIndex]))
        {
            LastStatus = *Result;
        }
        else
        {
            bConnected = false;
            ReconnectTimerSeconds = 0.0f;
        }
        if (bConnected)
        {
            if (auto Result = Client.SetSpeakerDistance(SpeakerIndex, DistanceToSend))
            {
                LastStatus = *Result;
            }
            else
            {
                bConnected = false;
                ReconnectTimerSeconds = 0.0f;
            }
        }
    };

    int ChangedIndex = -1;
    int MuteToggledIndex = -1;
    int SoloIndex = -1;
    const bool bDialChanged =
        DrawPositionDial("speaker_dial", LastStatus.SpeakerAzimuthDegrees, LastStatus.SpeakerDistanceMeters,
                          LastStatus.SpeakerMuted, &ChangedIndex, &MuteToggledIndex, &SoloIndex, DpiScale);

    if (bDialChanged)
    {
        PendingPositionIndex = ChangedIndex;
        PositionSendTimerSeconds -= ImGui::GetIO().DeltaTime;
        if (PositionSendTimerSeconds <= 0.0f)
        {
            PositionSendTimerSeconds = PositionSendIntervalSeconds;
            SendSpeakerPosition(static_cast<uint8_t>(ChangedIndex));
            bPositionSendPending = false;
        }
        else
        {
            bPositionSendPending = true;
        }
    }
    else
    {
        PositionSendTimerSeconds = 0.0f;
        // The drag just ended (or this is an unrelated frame). If the last
        // dragged position was throttled out and never sent, flush it now -
        // otherwise the handle sits at its final dragged spot only until the
        // next status poll, which still has the last-sent (stale) value and
        // snaps the handle back to it.
        if (bPositionSendPending && PendingPositionIndex >= 0)
        {
            SendSpeakerPosition(static_cast<uint8_t>(PendingPositionIndex));
            bPositionSendPending = false;
        }
        PendingPositionIndex = -1;
    }

    if (MuteToggledIndex >= 0)
    {
        SetSpeakerMuted(static_cast<uint8_t>(MuteToggledIndex),
                         !LastStatus.SpeakerMuted[static_cast<size_t>(MuteToggledIndex)]);
    }

    if (SoloIndex >= 0)
    {
        // Mute every other speaker and unmute this one so it can be heard
        // in isolation; stop early if the connection drops.
        for (uint8_t j = 0; j < SpeakerCount && bConnected; ++j)
        {
            if (!SetSpeakerMuted(j, j != static_cast<uint8_t>(SoloIndex)))
            {
                break;
            }
        }
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

    ImGui::SameLine();
    if (ImGui::Button("Unmute all"))
    {
        for (uint8_t i = 0; i < SpeakerCount && bConnected; ++i)
        {
            if (!SetSpeakerMuted(i, false))
            {
                break;
            }
        }
    }

    ImGui::Spacing();
    bool bTestNoiseEnabled = LastStatus.bTestNoiseEnabled;
    if (ImGui::Checkbox("Play test noise on all speakers", &bTestNoiseEnabled))
    {
        if (auto Result = Client.SetTestNoiseEnabled(bTestNoiseEnabled))
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
