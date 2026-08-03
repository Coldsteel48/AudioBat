// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "azimuth_dial.hpp"

#include <cmath>

#include "imgui.h"

namespace audiobat::gui
{

namespace
{
constexpr float DialSize = 220.0f;
constexpr float HandleRadius = 9.0f;
constexpr float Pi = 3.14159265358979323846f;
constexpr const char* SpeakerLabels[SpeakerCount] = {"FL", "FR", "FC", "RL", "RR", "SL", "SR"};

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
} // namespace

bool DrawAzimuthDial(const char* Label, std::array<float, SpeakerCount>& AzimuthsDegrees,
                      int* OutChangedIndex)
{
    *OutChangedIndex = -1;

    ImGui::PushID(Label);
    ImGui::BeginGroup();

    ImGuiIO& IO = ImGui::GetIO();
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const ImVec2 Center(Origin.x + DialSize * 0.5f, Origin.y + DialSize * 0.5f);
    const float Radius = DialSize * 0.5f - HandleRadius - 4.0f;

    DrawList->AddCircle(Center, Radius, IM_COL32(120, 120, 135, 255), 64, 1.5f);
    DrawList->AddLine(Center, ImVec2(Center.x, Center.y - Radius), IM_COL32(90, 200, 255, 180), 2.0f);
    DrawList->AddText(ImVec2(Center.x - 18, Center.y - Radius - 18), IM_COL32(180, 190, 200, 255),
                       "FRONT");

    bool bChanged = false;
    for (size_t i = 0; i < SpeakerCount; ++i)
    {
        ImGui::PushID(static_cast<int>(i));

        const ImVec2 Direction = AzimuthToUnitVector(AzimuthsDegrees[i]);
        const ImVec2 HandlePos(Center.x + Radius * Direction.x, Center.y + Radius * Direction.y);

        ImGui::SetCursorScreenPos(ImVec2(HandlePos.x - HandleRadius, HandlePos.y - HandleRadius));
        ImGui::InvisibleButton("handle", ImVec2(HandleRadius * 2.0f, HandleRadius * 2.0f));
        const bool bActive = ImGui::IsItemActive();
        const bool bHovered = ImGui::IsItemHovered();

        if (bActive)
        {
            AzimuthsDegrees[i] = ScreenPosToAzimuth(Center, IO.MousePos);
            bChanged = true;
            *OutChangedIndex = static_cast<int>(i);
        }

        const ImU32 HandleColor = bActive     ? IM_COL32(255, 200, 80, 255)
                                   : bHovered ? IM_COL32(255, 255, 255, 255)
                                              : IM_COL32(90, 200, 255, 255);
        DrawList->AddCircleFilled(HandlePos, HandleRadius, HandleColor);
        DrawList->AddText(ImVec2(HandlePos.x + HandleRadius + 2.0f, HandlePos.y - 7.0f),
                           IM_COL32(200, 205, 210, 255), SpeakerLabels[i]);

        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + DialSize));
    ImGui::EndGroup();
    ImGui::PopID();

    return bChanged;
}

} // namespace audiobat::gui
