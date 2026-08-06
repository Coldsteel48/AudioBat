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
#include "ui_palette.hpp"
#include "ui_widgets.hpp"

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

    // Drives the speaker dial's test-noise pulse animation (see
    // DrawPositionDial's PulseTimeSeconds parameter) - reset whenever
    // test noise isn't playing so the animation always restarts cleanly
    // from the same phase the next time it's turned on.
    if (LastStatus.bTestNoiseEnabled)
    {
        TestNoisePulseTimeSeconds += DeltaTimeSeconds;
    }
    else
    {
        TestNoisePulseTimeSeconds = 0.0f;
    }

    DrawUI();
}

void App::PersistGuiPreferences() const
{
    SaveGuiPreferences({.bMirrorModeEnabled = bMirrorModeEnabled,
                         .bAdvancedEqMode = bAdvancedEqMode,
                         .bAdvancedBassMode = bAdvancedBassMode,
                         .ActiveTabIndex = ActiveTabIndex});
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

    ImGui::Spacing();

    // Tab nav is deliberately kept outside BeginDisabled(!bConnected) below
    // so it stays browsable while reconnecting, rather than locking the
    // whole panel to whichever tab happened to be active when the
    // connection dropped.
    static const char* const TabLabels[] = {"Spatial Audio & Speakers", "Equalizer & Bass"};
    if (DrawSegmentedPill("tab_nav", TabLabels, IM_ARRAYSIZE(TabLabels), &ActiveTabIndex, DpiScale))
    {
        PersistGuiPreferences();
    }

    ImGui::Spacing();

    ImGui::BeginDisabled(!bConnected);
    if (ActiveTabIndex == 0)
    {
        DrawSpatialTab();
    }
    else
    {
        DrawEqTab();
    }
    ImGui::EndDisabled();

    ImGui::End();
}

void App::DrawSpatialTab()
{
    // Sends one mute/unmute command and folds the response into LastStatus;
    // on failure, flags disconnected the same way every other request
    // below does and reports the failure to the caller so a multi-command
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

    // Two side-by-side columns, the left one at a fixed design width. The
    // width must be explicit (not "available space") because
    // DrawSegmentedPill's spatial_mode pill below fills whatever width
    // it's given - inside a plain BeginGroup() (no child window
    // constraining it), "available space" reflects the whole window, not
    // an informal column, which would stretch the pill (and therefore
    // this group's own measured bounding box) to the full window width
    // and push the right column off its right edge.
    constexpr float LeftColumnWidth = 260.0f;
    const float ScaledLeftColumnWidth = LeftColumnWidth * DpiScale;
    const ImVec2 RowStart = ImGui::GetCursorScreenPos();

    ImGui::BeginGroup();
    {
        ImGui::TextUnformatted("Spatial Audio");
        ImGui::Spacing();

        static const char* const ModeLabels[] = {"Off", "Basic", "Advanced"};
        int ModeIndex = static_cast<int>(LastStatus.Mode);
        if (DrawSegmentedPill("spatial_mode", ModeLabels, IM_ARRAYSIZE(ModeLabels), &ModeIndex, DpiScale,
                              ScaledLeftColumnWidth))
        {
            if (auto Result = Client.SetSpatialMode(static_cast<SpatialMode>(ModeIndex)))
            {
                LastStatus = *Result;
            }
            else
            {
                bConnected = false;
                ReconnectTimerSeconds = 0.0f;
            }
        }

        ImGui::TextColored(palette::TextMutedV4, "What does this mean?");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Off - plain stereo, no processing.\nBasic - lightweight virtual surround, "
                               "low CPU.\nAdvanced - HRTF binaural render, most accurate.");
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

        ImGui::SetNextItemWidth(ScaledLeftColumnWidth);
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

        if (LastStatus.Mode == SpatialMode::Advanced)
        {
            ImGui::Spacing();
            ImGui::TextUnformatted("HRTF profile");

            const int CurrentHrtfIndex = static_cast<int>(LastStatus.ActiveHrtfIndex);
            const char* CurrentHrtfLabel =
                (CurrentHrtfIndex >= 0 && CurrentHrtfIndex < static_cast<int>(HrtfCatalog.size()))
                    ? HrtfCatalog[static_cast<size_t>(CurrentHrtfIndex)].c_str()
                    : "(none)";

            ImGui::SetNextItemWidth(ScaledLeftColumnWidth);
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
            ImGui::TextUnformatted("Near-field distance");
            ImGui::SameLine();
            bool bNearFieldEnabled = LastStatus.bNearFieldEnabled;
            if (DrawToggleSwitch("near_field", &bNearFieldEnabled, DpiScale))
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
                ImGui::SetTooltip(
                    "Off: speaker distance (drag radially on the dial) has no effect - today's "
                    "behavior.\nOn: closer speakers get louder in every mode, and Advanced (HRTF) mode "
                    "additionally gets a real interaural-level-difference proximity effect.");
            }
        }
    }
    ImGui::EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(RowStart.x + ScaledLeftColumnWidth + 22.0f * DpiScale, RowStart.y));

    int MuteToggledIndex = -1;
    int SoloIndex = -1;

    ImGui::BeginGroup();
    {
        ImGui::TextUnformatted("Speaker Layout");
        ImGui::SameLine(0.0f, 10.0f * DpiScale);
        ImGui::TextUnformatted("Mirror");
        ImGui::SameLine();
        if (DrawToggleSwitch("mirror", &bMirrorModeEnabled, DpiScale))
        {
            PersistGuiPreferences();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("When on, dragging FL/FR, RL/RR, or SL/SR also drags its counterpart to the "
                               "mirrored angle at the same distance.\nOff: every speaker moves "
                               "independently. FC has no counterpart either way.");
        }

        ImGui::TextColored(palette::TextMutedV4, "Drag to set angle/distance.");
        ImGui::TextColored(palette::TextMutedV4, "Click to mute, Ctrl+click to solo.");

        int ChangedIndex = -1;
        int MirroredIndex = -1;
        const bool bDialChanged = DrawPositionDial(
            "speaker_dial", LastStatus.SpeakerAzimuthDegrees, LastStatus.SpeakerDistanceMeters,
            LastStatus.SpeakerMuted, bMirrorModeEnabled, &ChangedIndex, &MirroredIndex, &MuteToggledIndex,
            &SoloIndex, DpiScale, LastStatus.bTestNoiseEnabled, TestNoisePulseTimeSeconds);

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
            // The drag just ended (or this is an unrelated frame). If the
            // last dragged position was throttled out and never sent,
            // flush it now - otherwise the handle sits at its final
            // dragged spot only until the next status poll, which still
            // has the last-sent (stale) value and snaps the handle back
            // to it.
            if (bPositionSendPending && PendingPositionIndex >= 0)
            {
                SendSpeakerPosition(PendingPositionIndex, PendingMirroredIndex);
                bPositionSendPending = false;
            }
            PendingPositionIndex = -1;
            PendingMirroredIndex = -1;
        }

        // Explicit per-speaker mute/solo chip row - a more discoverable
        // alternative to the dial's own click/Ctrl+click gesture. Feeds
        // the exact same MuteToggledIndex/SoloIndex the dial's out-params
        // feed, so the dispatch below doesn't need to know which of the
        // two triggered it.
        ImGui::Spacing();
        for (uint8 i = 0; i < SpeakerCount; ++i)
        {
            ImGui::PushID(i);
            if (i > 0)
            {
                ImGui::SameLine();
                if (i % 4 == 0)
                {
                    ImGui::NewLine();
                }
            }

            ImGui::BeginGroup();
            ImGui::TextUnformatted(GetSpeakerLabel(i));
            ImGui::SameLine();

            const bool bChipMuted = LastStatus.SpeakerMuted[i];
            if (bChipMuted)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, palette::WarningV4);
            }
            if (ImGui::SmallButton("M"))
            {
                MuteToggledIndex = static_cast<int>(i);
            }
            if (bChipMuted)
            {
                ImGui::PopStyleColor();
            }

            ImGui::SameLine();
            // Unlike mute, solo has no lasting per-speaker flag in Status
            // to reflect here - it's a one-shot "mute everyone else"
            // action (see the *OutSoloIndex dispatch below), so the S
            // button has no persistent "currently soloed" look to show.
            if (ImGui::SmallButton("S"))
            {
                SoloIndex = static_cast<int>(i);
            }
            ImGui::EndGroup();

            ImGui::PopID();
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset"))
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
        ImGui::TextUnformatted("Test noise");
        ImGui::SameLine();
        bool bTestNoiseEnabled = LastStatus.bTestNoiseEnabled;
        if (DrawToggleSwitch("test_noise", &bTestNoiseEnabled, DpiScale))
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
    }
    ImGui::EndGroup();

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
}

void App::DrawEqTab()
{
    ImGui::TextUnformatted("Equalizer & Bass");
    ImGui::SameLine(0.0f, 16.0f * DpiScale);
    ImGui::TextUnformatted("Advanced editing");
    ImGui::SameLine();
    if (DrawToggleSwitch("advanced_eq", &bAdvancedEqMode, DpiScale))
    {
        PersistGuiPreferences();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Off: gain-only bars at fixed ISO center frequencies.\nOn: also edit each "
                           "band's filter type, frequency, and Q - the wire format supports this either "
                           "way, this only changes what the panel shows.");
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Bass Enhancer");
    ImGui::SameLine();

    // Sends the current (already locally-updated) BassEnhancer settings
    // and folds the response into LastStatus, same fail-and-disconnect
    // pattern as every other control here.
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

    if (DrawToggleSwitch("bass_enabled", &BassEnhancer.bEnabled, DpiScale))
    {
        SendBassEnhancer();
    }
    ImGui::TextColored(palette::TextMutedV4, "Boosts low end for headphones");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Psychoacoustic bass enhancement for headphones: synthesizes harmonics of "
                           "the sub-bass so small drivers that can't reproduce it directly still make "
                           "it audible.");
    }

    if (BassEnhancer.bEnabled)
    {
        ImGui::SetNextItemWidth(180.0f * DpiScale);
        float AmountPercent = BassEnhancer.Mix * 100.0f;
        if (ImGui::SliderFloat("Amount##bass", &AmountPercent, 0.0f, 100.0f, "%.0f%%"))
        {
            BassEnhancer.Mix = AmountPercent / 100.0f;
            SendBassEnhancer();
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Advanced tuning");
        ImGui::SameLine();
        if (DrawToggleSwitch("bass_advanced", &bAdvancedBassMode, DpiScale))
        {
            PersistGuiPreferences();
        }

        if (bAdvancedBassMode)
        {
            ImGui::SetNextItemWidth(160.0f * DpiScale);
            float Cutoff = BassEnhancer.CutoffHz;
            if (ImGui::DragFloat("Cutoff Hz##bass", &Cutoff, 1.0f, 40.0f, 250.0f, "%.0f Hz"))
            {
                BassEnhancer.CutoffHz = Cutoff;
                SendBassEnhancer();
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f * DpiScale);
            float Drive = BassEnhancer.Drive;
            if (ImGui::DragFloat("Drive##bass", &Drive, 0.01f, 0.0f, 1.0f, "%.2f"))
            {
                BassEnhancer.Drive = Drive;
                SendBassEnhancer();
            }
        }
    }

    // Static, non-interactive placeholders for the not-yet-implemented
    // per-device/per-app named preset system - see
    // AudioEngine::HandleControlCommand, where SetHwEqPreset/
    // SaveHwEqPreset/SetContentEqPreset/SaveContentEqPreset/
    // GetEqPresetCatalog/GetContentStreams are decoded but have no
    // handler yet. Rendered disabled/inert rather than wired to fake
    // data, same treatment as the Genre auto-detect row below.
    ImGui::Spacing();
    {
        static const char* const NoPresets[] = {"(none)"};
        const float CardWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        const float CardHeight = 118.0f * DpiScale;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, palette::NestedCardBgV4);
        ImGui::BeginChild("device_preset_card", ImVec2(CardWidth, CardHeight), true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::BeginDisabled(true);
        ImGui::TextUnformatted("Device preset");
        ImGui::SameLine();
        static bool bDeviceGlobalPlaceholder = false;
        ImGui::Checkbox("Global default##device_preset", &bDeviceGlobalPlaceholder);
        ImGui::TextColored(palette::TextMutedV4, "Target: %s",
                           LastStatus.OutputDeviceName.empty() ? "(none)" : LastStatus.OutputDeviceName.c_str());
        static int DevicePresetPlaceholder = 0;
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##device_preset_select", &DevicePresetPlaceholder, NoPresets, IM_ARRAYSIZE(NoPresets));
        ImGui::Button("Save as...##device_preset");
        ImGui::SameLine();
        ImGui::Button("Delete##device_preset");
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, palette::NestedCardBgV4);
        ImGui::BeginChild("app_preset_card", ImVec2(CardWidth, CardHeight), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::BeginDisabled(true);
        ImGui::TextUnformatted("App preset");
        ImGui::SameLine();
        static bool bAppGlobalPlaceholder = false;
        ImGui::Checkbox("Global default##app_preset", &bAppGlobalPlaceholder);
        static int AppPlaceholder = 0;
        static const char* const NoApps[] = {"(none)"};
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##app_select", &AppPlaceholder, NoApps, IM_ARRAYSIZE(NoApps));
        static int AppPresetPlaceholder = 0;
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##app_preset_select", &AppPresetPlaceholder, NoPresets, IM_ARRAYSIZE(NoPresets));
        ImGui::Button("Save as...##app_preset");
        ImGui::SameLine();
        ImGui::Button("Delete##app_preset");
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, palette::NestedCardBgV4);
        ImGui::BeginChild("genre_row", ImVec2(0.0f, 30.0f * DpiScale), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::BeginDisabled(true);
        ImGui::TextUnformatted("Genre auto-detect (music / jazz / rock / movie / game)");
        ImGui::SameLine();
        ImGui::TextColored(palette::TextMutedV4, "Coming soon");
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Equalizer (10-band)");

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
        if (DrawEqBandBar("##gain", &Gain, -12.0f, 12.0f, ImVec2(28.0f * DpiScale, 120.0f * DpiScale), DpiScale))
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
}

} // namespace ramkolfx::gui
