// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "app.hpp"

#include "imgui.h"
#include "position_dial.hpp"

namespace ramkolfx::gui
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
                    if (auto EqResult = Client.RequestHwEqState())
                    {
                        HwEqBands = *EqResult;
                    }
                    if (auto BassResult = Client.RequestBassEnhancerState())
                    {
                        BassEnhancer = *BassResult;
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
    ImGui::Begin("RamkolFX Control", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (bConnected)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Connected to ramkolfxd");
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
                if (auto Result = Client.SetHrtfFile(static_cast<uint8>(i)))
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
    if (ImGui::Checkbox("Mirror left/right", &bMirrorModeEnabled))
    {
        SaveGuiPreferences({.bMirrorModeEnabled = bMirrorModeEnabled});
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("When on, dragging FL/FR, RL/RR, or SL/SR also drags its counterpart to the "
                           "mirrored angle at the same distance.\nOff: every speaker moves independently. FC "
                           "has no counterpart either way.");
    }

    // Sends one mute/unmute command and folds the response into LastStatus;
    // on failure, flags disconnected the same way every other request
    // above does and reports the failure to the caller so a multi-command
    // sequence (Solo, Unmute all) stops rather than spamming a dead
    // connection.
    auto SetSpeakerMuted = [&](uint8 SpeakerIndex, bool bMuted) -> bool
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

    // Sends one speaker's given (already-captured) azimuth and distance and
    // folds the response into LastStatus, the same fail-and-disconnect
    // pattern as SetSpeakerMuted above. Takes the values as parameters
    // rather than reading them from LastStatus itself: the azimuth
    // round-trip's LastStatus assignment overwrites every field, including
    // other speakers', with whatever the daemon had *before* this call, so
    // a caller sending more than one speaker's position (e.g. a mirrored
    // pair) must capture both speakers' target values up front rather than
    // reading them fresh between calls.
    auto SendSpeakerPositionValues = [&](uint8 SpeakerIndex, float AzimuthDegrees, float DistanceMeters)
    {
        if (auto Result = Client.SetSpeakerAzimuth(SpeakerIndex, AzimuthDegrees))
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
            if (auto Result = Client.SetSpeakerDistance(SpeakerIndex, DistanceMeters))
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

    // Sends PrimaryIndex's current (locally-dragged) position, and
    // MirrorIndex's too if it's >= 0 (mirror mode dragged a counterpart
    // alongside it). Both speakers' target values are captured before
    // either round-trip starts, for the reason described above.
    auto SendSpeakerPosition = [&](int PrimaryIndex, int MirrorIndex)
    {
        const float PrimaryAzimuth = LastStatus.SpeakerAzimuthDegrees[static_cast<size_t>(PrimaryIndex)];
        const float PrimaryDistance = LastStatus.SpeakerDistanceMeters[static_cast<size_t>(PrimaryIndex)];
        float MirrorAzimuth = 0.0f;
        float MirrorDistance = 0.0f;
        if (MirrorIndex >= 0)
        {
            MirrorAzimuth = LastStatus.SpeakerAzimuthDegrees[static_cast<size_t>(MirrorIndex)];
            MirrorDistance = LastStatus.SpeakerDistanceMeters[static_cast<size_t>(MirrorIndex)];
        }
        SendSpeakerPositionValues(static_cast<uint8>(PrimaryIndex), PrimaryAzimuth, PrimaryDistance);
        if (MirrorIndex >= 0 && bConnected)
        {
            SendSpeakerPositionValues(static_cast<uint8>(MirrorIndex), MirrorAzimuth, MirrorDistance);
        }
    };

    int ChangedIndex = -1;
    int MirroredIndex = -1;
    int MuteToggledIndex = -1;
    int SoloIndex = -1;
    const bool bDialChanged =
        DrawPositionDial("speaker_dial", LastStatus.SpeakerAzimuthDegrees, LastStatus.SpeakerDistanceMeters,
                          LastStatus.SpeakerMuted, bMirrorModeEnabled, &ChangedIndex, &MirroredIndex,
                          &MuteToggledIndex, &SoloIndex, DpiScale);

    if (bDialChanged)
    {
        PendingPositionIndex = ChangedIndex;
        PendingMirroredIndex = MirroredIndex;
        PositionSendTimerSeconds -= ImGui::GetIO().DeltaTime;
        if (PositionSendTimerSeconds <= 0.0f)
        {
            PositionSendTimerSeconds = PositionSendIntervalSeconds;
            SendSpeakerPosition(ChangedIndex, MirroredIndex);
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
            SendSpeakerPosition(PendingPositionIndex, PendingMirroredIndex);
            bPositionSendPending = false;
        }
        PendingPositionIndex = -1;
        PendingMirroredIndex = -1;
    }

    if (MuteToggledIndex >= 0)
    {
        SetSpeakerMuted(static_cast<uint8>(MuteToggledIndex),
                         !LastStatus.SpeakerMuted[static_cast<size_t>(MuteToggledIndex)]);
    }

    if (SoloIndex >= 0)
    {
        // Mute every other speaker and unmute this one so it can be heard
        // in isolation; stop early if the connection drops.
        for (uint8 j = 0; j < SpeakerCount && bConnected; ++j)
        {
            if (!SetSpeakerMuted(j, j != static_cast<uint8>(SoloIndex)))
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
        for (uint8 i = 0; i < SpeakerCount && bConnected; ++i)
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

    ImGui::Spacing();
    ImGui::TextUnformatted("Equalizer (10-band)");
    if (ImGui::Checkbox("Advanced band editing", &bAdvancedEqMode))
    {
        SaveGuiPreferences({.bMirrorModeEnabled = bMirrorModeEnabled, .bAdvancedEqMode = bAdvancedEqMode});
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Off: gain-only sliders at fixed ISO center frequencies.\nOn: also edit each "
                           "band's filter type, frequency, and Q - the wire format supports this either "
                           "way, this only changes what the panel shows.");
    }

    // Sends BandIndex's current (already locally-updated) EqBand and folds
    // the response into LastStatus, same fail-and-disconnect pattern as
    // every other control above.
    auto SendHwEqBand = [&](uint8 BandIndex)
    {
        if (auto Result = Client.SetHwEqBand(BandIndex, HwEqBands[BandIndex]))
        {
            LastStatus = *Result;
        }
        else
        {
            bConnected = false;
            ReconnectTimerSeconds = 0.0f;
        }
    };

    static const char* EqFilterTypeLabels[] = {"Peaking", "Low Shelf", "High Shelf",
                                                "Low Pass", "High Pass", "Notch"};

    for (size_t i = 0; i < MaxEqBands; ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginGroup();

        float Gain = HwEqBands[i].GainDb;
        if (ImGui::VSliderFloat("##gain", ImVec2(28.0f * DpiScale, 120.0f * DpiScale), &Gain, -12.0f, 12.0f,
                                 "%.0f"))
        {
            HwEqBands[i].GainDb = Gain;
            SendHwEqBand(static_cast<uint8>(i));
        }

        const float NominalHz = DefaultEqCenterFrequenciesHz[i];
        const std::string FreqLabel = NominalHz >= 1000.0f
                                           ? std::to_string(static_cast<int>(NominalHz / 1000.0f)) + "k"
                                           : std::to_string(static_cast<int>(NominalHz));
        ImGui::TextUnformatted(FreqLabel.c_str());

        if (bAdvancedEqMode)
        {
            ImGui::SetNextItemWidth(72.0f * DpiScale);
            int FilterTypeIndex = static_cast<int>(HwEqBands[i].FilterType);
            if (ImGui::Combo("##type", &FilterTypeIndex, EqFilterTypeLabels, IM_ARRAYSIZE(EqFilterTypeLabels)))
            {
                HwEqBands[i].FilterType = static_cast<EqFilterType>(FilterTypeIndex);
                SendHwEqBand(static_cast<uint8>(i));
            }

            ImGui::SetNextItemWidth(72.0f * DpiScale);
            float Frequency = HwEqBands[i].FrequencyHz;
            if (ImGui::DragFloat("##freq", &Frequency, 1.0f, 20.0f, 20000.0f, "%.0f Hz"))
            {
                HwEqBands[i].FrequencyHz = Frequency;
                SendHwEqBand(static_cast<uint8>(i));
            }

            ImGui::SetNextItemWidth(72.0f * DpiScale);
            float Q = HwEqBands[i].Q;
            if (ImGui::DragFloat("##q", &Q, 0.01f, 0.1f, 10.0f, "Q %.2f"))
            {
                HwEqBands[i].Q = Q;
                SendHwEqBand(static_cast<uint8>(i));
            }
        }

        ImGui::EndGroup();
        ImGui::PopID();
        if (i + 1 < MaxEqBands)
        {
            ImGui::SameLine();
        }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Bass Enhancer");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Psychoacoustic bass enhancement for headphones: synthesizes harmonics of "
                           "the sub-bass so small drivers that can't reproduce it directly still make "
                           "it audible.");
    }

    // Sends the current (already locally-updated) BassEnhancer settings
    // and folds the response into LastStatus, same pattern as
    // SendHwEqBand above.
    auto SendBassEnhancer = [&]()
    {
        if (auto Result = Client.SetBassEnhancer(BassEnhancer))
        {
            LastStatus = *Result;
        }
        else
        {
            bConnected = false;
            ReconnectTimerSeconds = 0.0f;
        }
    };

    if (ImGui::Checkbox("Enabled##bass", &BassEnhancer.bEnabled))
    {
        SendBassEnhancer();
    }

    ImGui::SetNextItemWidth(180.0f * DpiScale);
    float Amount = BassEnhancer.Mix;
    if (ImGui::SliderFloat("Amount##bass", &Amount, 0.0f, 1.0f))
    {
        BassEnhancer.Mix = Amount;
        SendBassEnhancer();
    }

    if (ImGui::Checkbox("Advanced##bass", &bAdvancedBassMode))
    {
        SaveGuiPreferences({.bMirrorModeEnabled = bMirrorModeEnabled,
                             .bAdvancedEqMode = bAdvancedEqMode,
                             .bAdvancedBassMode = bAdvancedBassMode});
    }

    if (bAdvancedBassMode)
    {
        ImGui::SetNextItemWidth(180.0f * DpiScale);
        float Cutoff = BassEnhancer.CutoffHz;
        if (ImGui::DragFloat("Cutoff Hz##bass", &Cutoff, 1.0f, 40.0f, 250.0f, "%.0f Hz"))
        {
            BassEnhancer.CutoffHz = Cutoff;
            SendBassEnhancer();
        }

        ImGui::SetNextItemWidth(180.0f * DpiScale);
        float Drive = BassEnhancer.Drive;
        if (ImGui::DragFloat("Drive##bass", &Drive, 0.01f, 0.0f, 1.0f, "%.2f"))
        {
            BassEnhancer.Drive = Drive;
            SendBassEnhancer();
        }
    }

    ImGui::EndDisabled();

    ImGui::End();
}

} // namespace ramkolfx::gui
