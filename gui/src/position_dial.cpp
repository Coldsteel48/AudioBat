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
#include "ui_palette.hpp"

namespace ramkolfx::gui
{

namespace
{
constexpr float DialSize = 220.0f;
constexpr float HandleRadius = 9.0f;
constexpr float Pi = 3.14159265358979323846f;
constexpr const char* SpeakerLabels[SpeakerCount] = {"FL", "FR", "FC", "RL", "RR", "SL", "SR"};

// Local-space "speaker cone" trapezoid points (narrow end = the front of
// the cone, facing away from the dial's center) and its two ridge lines,
// in the same unscaled unit space as HandleRadius - see DrawSpeakerCone.
constexpr float ConeTipHalfWidth = 4.0f;
constexpr float ConeTipOffset = 11.0f;
constexpr float ConeBaseHalfWidth = 9.0f;
constexpr float ConeBaseOffset = 7.0f;
constexpr float ConeRidge1Offset = 0.0f;
constexpr float ConeRidge1HalfWidth = 6.0f;
constexpr float ConeRidge2Offset = 4.0f;
constexpr float ConeRidge2HalfWidth = 7.5f;

// The test-noise pulse animation loops once per this many seconds (see
// DrawPositionDial's PulseTimeSeconds parameter), oscillating between
// scale/alpha extremes matching the reference design's ~1s ease-in-out
// pulse.
constexpr float PulsePeriodSeconds = 1.0f;
constexpr float PulseScaleMax = 1.25f;
constexpr float PulseAlphaMin = 0.55f;

// Draws one speaker's cone shape, rotated to face Direction (the same
// unit vector used to place its handle - see AzimuthToUnitVector), with
// two ridge lines for a driver-cone look. Perp is Direction rotated 90
// degrees, giving the cone its width axis. EffectiveScale folds together
// Scale and the current pulse scale (1.0 when not pulsing).
void DrawSpeakerCone(ImDrawList* DrawList, ImVec2 Center, ImVec2 Direction, ImVec2 Perp, float EffectiveScale,
                     ImU32 FillColor, ImU32 RidgeColor)
{
    auto LocalToWorld = [&](float Lx, float Ly)
    {
        return ImVec2(Center.x + (Direction.x * -Ly + Perp.x * Lx) * EffectiveScale,
                       Center.y + (Direction.y * -Ly + Perp.y * Lx) * EffectiveScale);
    };

    const ImVec2 Poly[4] = {
        LocalToWorld(-ConeTipHalfWidth, -ConeTipOffset),
        LocalToWorld(ConeTipHalfWidth, -ConeTipOffset),
        LocalToWorld(ConeBaseHalfWidth, ConeBaseOffset),
        LocalToWorld(-ConeBaseHalfWidth, ConeBaseOffset),
    };
    DrawList->AddConvexPolyFilled(Poly, 4, FillColor);

    const ImVec2 Ridge1A = LocalToWorld(-ConeRidge1HalfWidth, ConeRidge1Offset);
    const ImVec2 Ridge1B = LocalToWorld(ConeRidge1HalfWidth, ConeRidge1Offset);
    DrawList->AddLine(Ridge1A, Ridge1B, RidgeColor, 1.0f * EffectiveScale);

    const ImVec2 Ridge2A = LocalToWorld(-ConeRidge2HalfWidth, ConeRidge2Offset);
    const ImVec2 Ridge2B = LocalToWorld(ConeRidge2HalfWidth, ConeRidge2Offset);
    DrawList->AddLine(Ridge2A, Ridge2B, RidgeColor, 1.0f * EffectiveScale);
}

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
                      int* OutSoloIndex, float Scale, bool bTestNoiseEnabled, float PulseTimeSeconds)
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

    // Concentric rings (faint accent-blue), matching the reference radar's
    // "range ring" look, from innermost to outermost.
    DrawList->AddCircle(Center, OuterRadiusPx * 0.37f, IM_COL32(56, 193, 255, 36), 64, 1.0f * Scale);
    DrawList->AddCircle(Center, OuterRadiusPx * 0.63f, IM_COL32(56, 193, 255, 36), 64, 1.0f * Scale);
    DrawList->AddCircle(Center, OuterRadiusPx, IM_COL32(56, 193, 255, 56), 64, 1.5f * Scale);

    // Faint radial spokes every 45 degrees, from the inner ring out to the
    // outer ring.
    for (int i = 0; i < 8; ++i)
    {
        const float SpokeAngle = static_cast<float>(i) * (Pi / 4.0f);
        const ImVec2 SpokeDir(sinf(SpokeAngle), -cosf(SpokeAngle));
        DrawList->AddLine(
            ImVec2(Center.x + SpokeDir.x * InnerRadiusPx, Center.y + SpokeDir.y * InnerRadiusPx),
            ImVec2(Center.x + SpokeDir.x * OuterRadiusPx, Center.y + SpokeDir.y * OuterRadiusPx),
            IM_COL32(255, 255, 255, 13), 1.0f * Scale);
    }

    // Center crosshair reticle.
    DrawList->AddCircle(Center, 14.0f * Scale, IM_COL32(56, 193, 255, 90), 32, 1.0f * Scale);
    DrawList->AddLine(ImVec2(Center.x - 7.0f * Scale, Center.y), ImVec2(Center.x + 7.0f * Scale, Center.y),
                       IM_COL32(56, 193, 255, 90), 1.0f * Scale);
    DrawList->AddLine(ImVec2(Center.x, Center.y - 7.0f * Scale), ImVec2(Center.x, Center.y + 7.0f * Scale),
                       IM_COL32(56, 193, 255, 90), 1.0f * Scale);

    DrawList->AddText(ImVec2(Center.x - 18.0f * Scale, Center.y - OuterRadiusPx - 18.0f * Scale),
                       palette::TextMuted, "FRONT");

    // Pulse phase (0..1, loops every PulsePeriodSeconds) for the
    // test-noise animation - shared by every unmuted speaker this frame.
    const float PulsePhase =
        sinf(PulseTimeSeconds * (2.0f * Pi / PulsePeriodSeconds)) * 0.5f + 0.5f;
    const float PulseScale = 1.0f + (PulseScaleMax - 1.0f) * PulsePhase;
    const float PulseAlphaMul = 1.0f - (1.0f - PulseAlphaMin) * PulsePhase;

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
        const bool bPulsing = bTestNoiseEnabled && !bIsMuted;
        const float SpeakerScale = Scale * (bPulsing ? PulseScale : 1.0f);
        const float AlphaMul = bPulsing ? PulseAlphaMul : 1.0f;

        const ImU32 ConeFillColor = bIsMuted   ? IM_COL32(58, 66, 80, 255)
                                     : bActive  ? IM_COL32(255, 200, 80, 255)
                                     : bHovered ? IM_COL32(255, 255, 255, 255)
                                                : IM_COL32(56, 189, 248, static_cast<int>(255 * AlphaMul));
        const ImU32 RidgeColor = IM_COL32(6, 18, 28, static_cast<int>(102 * AlphaMul));

        if (!bIsMuted)
        {
            // Soft glow behind the cone for every active speaker, matching
            // the reference design's drop-shadow accent glow; brighter
            // while pulsing.
            const int GlowAlpha = static_cast<int>((bPulsing ? 90 : 55) * AlphaMul);
            DrawList->AddCircleFilled(HandlePos, ScaledHandleRadius * 1.9f * (bPulsing ? PulseScale : 1.0f),
                                      IM_COL32(56, 189, 248, GlowAlpha));
        }

        const ImVec2 Perp(-Direction.y, Direction.x);
        DrawSpeakerCone(DrawList, HandlePos, Direction, Perp, SpeakerScale, ConeFillColor, RidgeColor);

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

        const ImU32 LabelColor = bIsMuted        ? palette::Warning
                                  : bLabelHovered ? IM_COL32(255, 255, 255, 255)
                                                  : palette::TextSecondary;
        DrawList->AddText(LabelPos, LabelColor, SpeakerLabels[i]);
        if (bIsMuted)
        {
            const float StrikeY = LabelPos.y + LabelTextSize.y * 0.5f;
            DrawList->AddLine(ImVec2(LabelPos.x, StrikeY), ImVec2(LabelPos.x + LabelTextSize.x, StrikeY),
                               palette::Warning, 1.5f * Scale);
        }

        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + ScaledDialSize));
    ImGui::EndGroup();
    ImGui::PopID();

    return bChanged;
}

const char* GetSpeakerLabel(size_t Index)
{
    return SpeakerLabels[Index];
}

} // namespace ramkolfx::gui
