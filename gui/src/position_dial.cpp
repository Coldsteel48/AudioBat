// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "position_dial.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

namespace ramkolfx::gui
{

namespace
{
constexpr float DialSize = 220.0f;
constexpr float HandleRadius = 9.0f;
constexpr float Pi = 3.14159265358979323846f;
constexpr const char* SpeakerLabels[SpeakerCount] = {"FL", "FR", "FC", "RL", "RR", "SL", "SR"};

// Left/right counterpart of each speaker, by index into AzimuthsDegrees /
// DistancesMeters (matching SpeakerLabels' order FL,FR,FC,RL,RR,SL,SR); -1
// for FC, which has no counterpart.
constexpr int MirrorPartnerIndex[SpeakerCount] = {1, 0, -1, 4, 3, 6, 5};

// The closest distance (MinSpeakerDistanceMeters) maps to this fraction of
// the dial's outer radius rather than to the exact center - keeps handles
// clickable/draggable even at minimum distance and keeps azimuth (which is
// meaningless exactly at the center) always well-defined.
constexpr float InnerRadiusFraction = 0.25f;

// Screen-space direction for a given azimuth: 0=front(up), positive=left,
// negative=right, viewed top-down.
ImVec2 AzimuthToUnitVector(float AzimuthDegrees)
{
    const float ThetaRad = AzimuthDegrees * (Pi / 180.0f);
    return ImVec2(-sinf(ThetaRad), -cosf(ThetaRad));
}

float ScreenPosToAzimuth(ImVec2 Center, ImVec2 Pos)
{
    const float Dx = Pos.x - Center.x;
    const float Dy = Pos.y - Center.y;
    if (Dx == 0.0f && Dy == 0.0f)
    {
        return 0.0f;
    }
    return atan2f(-Dx, -Dy) * (180.0f / Pi);
}

float Lerp(float A, float B, float T)
{
    return A + (B - A) * T;
}

float InverseLerp(float A, float B, float V)
{
    return (V - A) / (B - A);
}

// Maps a distance in meters to a pixel radius within [InnerRadiusPx,
// OuterRadiusPx].
float DistanceToPixelRadius(float DistanceMeters, float InnerRadiusPx, float OuterRadiusPx)
{
    const float Clamped = std::clamp(DistanceMeters, MinSpeakerDistanceMeters, MaxSpeakerDistanceMeters);
    const float T = InverseLerp(MinSpeakerDistanceMeters, MaxSpeakerDistanceMeters, Clamped);
    return Lerp(InnerRadiusPx, OuterRadiusPx, T);
}

float PixelRadiusToDistance(float RadiusPx, float InnerRadiusPx, float OuterRadiusPx)
{
    const float Clamped = std::clamp(RadiusPx, InnerRadiusPx, OuterRadiusPx);
    const float T = InverseLerp(InnerRadiusPx, OuterRadiusPx, Clamped);
    return Lerp(MinSpeakerDistanceMeters, MaxSpeakerDistanceMeters, T);
}
} // namespace

bool DrawPositionDial(const char* Label, std::array<float, SpeakerCount>& AzimuthsDegrees,
                      std::array<float, SpeakerCount>& DistancesMeters,
                      const std::array<bool, SpeakerCount>& Muted, bool bMirrorEnabled,
                      int* OutChangedIndex, int* OutMirroredIndex, int* OutMuteToggledIndex,
                      int* OutSoloIndex, float Scale)
{
    *OutChangedIndex = -1;
    *OutMirroredIndex = -1;
    *OutMuteToggledIndex = -1;
    *OutSoloIndex = -1;

    const float ScaledDialSize = DialSize * Scale;
    const float ScaledHandleRadius = HandleRadius * Scale;

    ImGui::PushID(Label);
    ImGui::BeginGroup();

    ImGuiIO& IO = ImGui::GetIO();
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const ImVec2 Center(Origin.x + ScaledDialSize * 0.5f, Origin.y + ScaledDialSize * 0.5f);
    const float OuterRadiusPx = ScaledDialSize * 0.5f - ScaledHandleRadius - 4.0f * Scale;
    const float InnerRadiusPx = OuterRadiusPx * InnerRadiusFraction;

    DrawList->AddCircle(Center, OuterRadiusPx, IM_COL32(120, 120, 135, 255), 64, 1.5f * Scale);
    DrawList->AddCircle(Center, InnerRadiusPx, IM_COL32(80, 80, 92, 200), 64, 1.0f * Scale);
    DrawList->AddLine(Center, ImVec2(Center.x, Center.y - OuterRadiusPx), IM_COL32(90, 200, 255, 180),
                       2.0f * Scale);
    DrawList->AddText(ImVec2(Center.x - 18.0f * Scale, Center.y - OuterRadiusPx - 18.0f * Scale),
                       IM_COL32(180, 190, 200, 255), "FRONT");

    bool bChanged = false;
    for (size_t i = 0; i < SpeakerCount; ++i)
    {
        ImGui::PushID(static_cast<int>(i));

        const ImVec2 Direction = AzimuthToUnitVector(AzimuthsDegrees[i]);
        const float HandlePixelRadius = DistanceToPixelRadius(DistancesMeters[i], InnerRadiusPx, OuterRadiusPx);
        const ImVec2 HandlePos(Center.x + HandlePixelRadius * Direction.x,
                                Center.y + HandlePixelRadius * Direction.y);

        ImGui::SetCursorScreenPos(ImVec2(HandlePos.x - ScaledHandleRadius, HandlePos.y - ScaledHandleRadius));
        ImGui::InvisibleButton("handle", ImVec2(ScaledHandleRadius * 2.0f, ScaledHandleRadius * 2.0f));
        const bool bActive = ImGui::IsItemActive();
        const bool bHovered = ImGui::IsItemHovered();

        if (bActive)
        {
            AzimuthsDegrees[i] = ScreenPosToAzimuth(Center, IO.MousePos);
            const float Dx = IO.MousePos.x - Center.x;
            const float Dy = IO.MousePos.y - Center.y;
            const float DragPixelRadius = std::sqrt(Dx * Dx + Dy * Dy);
            DistancesMeters[i] = PixelRadiusToDistance(DragPixelRadius, InnerRadiusPx, OuterRadiusPx);
            bChanged = true;
            *OutChangedIndex = static_cast<int>(i);

            const int MirrorIndex = MirrorPartnerIndex[i];
            if (bMirrorEnabled && MirrorIndex >= 0)
            {
                AzimuthsDegrees[static_cast<size_t>(MirrorIndex)] = -AzimuthsDegrees[i];
                DistancesMeters[static_cast<size_t>(MirrorIndex)] = DistancesMeters[i];
                *OutMirroredIndex = MirrorIndex;
            }
        }

        const bool bIsMuted = Muted[i];
        const ImU32 HandleColor = bIsMuted   ? IM_COL32(140, 90, 90, 255)
                                   : bActive  ? IM_COL32(255, 200, 80, 255)
                                   : bHovered ? IM_COL32(255, 255, 255, 255)
                                              : IM_COL32(90, 200, 255, 255);
        DrawList->AddCircleFilled(HandlePos, ScaledHandleRadius, HandleColor);

        // The label doubles as the speaker's mute control - right next to
        // its handle, rather than in a separate list elsewhere in the UI.
        const ImVec2 LabelPos(HandlePos.x + ScaledHandleRadius + 2.0f * Scale, HandlePos.y - 7.0f * Scale);
        const ImVec2 LabelTextSize = ImGui::CalcTextSize(SpeakerLabels[i]);

        ImGui::SetCursorScreenPos(LabelPos);
        ImGui::InvisibleButton("mute_toggle", LabelTextSize);
        const bool bLabelHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
        {
            if (ImGui::GetIO().KeyCtrl)
            {
                *OutSoloIndex = static_cast<int>(i);
            }
            else
            {
                *OutMuteToggledIndex = static_cast<int>(i);
            }
        }
        if (bLabelHovered)
        {
            ImGui::SetTooltip("Click: mute/unmute %s\nCtrl+click: solo %s (mute all others)\nDrag: azimuth "
                               "(angle) + distance (radius)",
                               SpeakerLabels[i], SpeakerLabels[i]);
        }

        const ImU32 LabelColor = bIsMuted        ? IM_COL32(235, 90, 90, 255)
                                  : bLabelHovered ? IM_COL32(255, 255, 255, 255)
                                                  : IM_COL32(200, 205, 210, 255);
        DrawList->AddText(LabelPos, LabelColor, SpeakerLabels[i]);
        if (bIsMuted)
        {
            const float StrikeY = LabelPos.y + LabelTextSize.y * 0.5f;
            DrawList->AddLine(ImVec2(LabelPos.x, StrikeY), ImVec2(LabelPos.x + LabelTextSize.x, StrikeY),
                               IM_COL32(235, 90, 90, 255), 1.5f * Scale);
        }

        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + ScaledDialSize));
    ImGui::EndGroup();
    ImGui::PopID();

    return bChanged;
}

} // namespace ramkolfx::gui
