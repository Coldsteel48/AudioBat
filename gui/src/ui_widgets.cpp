// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "ui_widgets.hpp"

#include <algorithm>
#include <cstdio>

#include "ui_palette.hpp"

namespace ramkolfx::gui
{

namespace
{
constexpr float ToggleTrackHeight = 21.0f;
constexpr float ToggleKnobDiameter = 17.0f;
constexpr float ToggleKnobMargin = 2.0f;
constexpr float ToggleSlideSeconds = 0.15f;

constexpr float PillHeight = 34.0f;
constexpr float PillPadding = 4.0f;

float LerpF(float A, float B, float T)
{
    return A + (B - A) * T;
}

ImVec4 LerpColor(ImVec4 A, ImVec4 B, float T)
{
    return ImVec4(LerpF(A.x, B.x, T), LerpF(A.y, B.y, T), LerpF(A.z, B.z, T), LerpF(A.w, B.w, T));
}

// Raw ImDrawList draws don't automatically dim under ImGui::BeginDisabled()
// the way stock widgets do (that only pushes an Alpha style var stock
// widgets read) - multiply every custom-drawn color's alpha by the
// current style Alpha so these widgets stay visually consistent with
// whatever they're nested inside.
ImU32 DisabledAwareColor(ImVec4 Color)
{
    Color.w *= ImGui::GetStyle().Alpha;
    return ImGui::ColorConvertFloat4ToU32(Color);
}
} // namespace

bool DrawSegmentedPill(const char* Label, const char* const* Options, int OptionCount, int* CurrentIndex,
                       float Scale, float TotalWidth)
{
    using namespace palette;

    bool bChanged = false;

    ImGui::PushID(Label);
    ImGui::BeginGroup();

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const float TrackWidth = TotalWidth >= 0.0f ? TotalWidth : ImGui::GetContentRegionAvail().x;
    const float TrackHeight = PillHeight * Scale;
    const float Padding = PillPadding * Scale;

    DrawList->AddRectFilled(Origin, ImVec2(Origin.x + TrackWidth, Origin.y + TrackHeight),
                            DisabledAwareColor(NestedCardBgV4), RadiusNestedCard * Scale);

    const float SegmentWidth = (TrackWidth - 2.0f * Padding) / static_cast<float>(std::max(OptionCount, 1));
    const float SegmentHeight = TrackHeight - 2.0f * Padding;

    for (int i = 0; i < OptionCount; ++i)
    {
        ImGui::PushID(i);

        const ImVec2 SegmentMin(Origin.x + Padding + SegmentWidth * static_cast<float>(i), Origin.y + Padding);
        const ImVec2 SegmentMax(SegmentMin.x + SegmentWidth, SegmentMin.y + SegmentHeight);

        ImGui::SetCursorScreenPos(SegmentMin);
        ImGui::InvisibleButton("segment", ImVec2(SegmentWidth, SegmentHeight));
        const bool bHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked() && i != *CurrentIndex)
        {
            *CurrentIndex = i;
            bChanged = true;
        }

        const bool bActive = i == *CurrentIndex;
        if (bActive)
        {
            DrawList->AddRectFilled(SegmentMin, SegmentMax, DisabledAwareColor(AccentGradientEndV4),
                                    RadiusInput * Scale);
        }
        else if (bHovered)
        {
            DrawList->AddRectFilled(SegmentMin, SegmentMax, DisabledAwareColor(ImVec4(1, 1, 1, 0.04f)),
                                    RadiusInput * Scale);
        }

        const char* OptionLabel = Options[i];
        const ImVec2 TextSize = ImGui::CalcTextSize(OptionLabel);
        const ImVec2 TextPos(SegmentMin.x + (SegmentWidth - TextSize.x) * 0.5f,
                              SegmentMin.y + (SegmentHeight - TextSize.y) * 0.5f);
        const ImVec4 TextColorV4 = bActive ? ImVec4(0.024f, 0.071f, 0.11f, 1.0f) : TextSecondaryV4;
        DrawList->AddText(TextPos, DisabledAwareColor(TextColorV4), OptionLabel);

        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + TrackHeight));
    ImGui::EndGroup();
    ImGui::PopID();

    return bChanged;
}

bool DrawToggleSwitch(const char* Label, bool* Value, float Scale)
{
    using namespace palette;

    ImGui::PushID(Label);

    const ImVec2 TrackSize(ToggleTrackWidth * Scale, ToggleTrackHeight * Scale);
    const ImVec2 Origin = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("toggle", TrackSize);
    bool bChanged = false;
    if (ImGui::IsItemClicked())
    {
        *Value = !*Value;
        bChanged = true;
    }

    // Slide progress (0=off, 1=on) is kept in ImGui's own per-widget state
    // storage, keyed off this widget's id - self-contained, no App-level
    // animation state needed, same idea as how ImGui itself animates e.g.
    // collapsing headers internally.
    ImGuiStorage* Storage = ImGui::GetStateStorage();
    const ImGuiID ProgressId = ImGui::GetID("toggle_progress");
    float Progress = Storage->GetFloat(ProgressId, *Value ? 1.0f : 0.0f);
    const float Target = *Value ? 1.0f : 0.0f;
    const float Step = ImGui::GetIO().DeltaTime / ToggleSlideSeconds;
    Progress = Target > Progress ? std::min(Target, Progress + Step) : std::max(Target, Progress - Step);
    Storage->SetFloat(ProgressId, Progress);

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const ImVec4 TrackColorV4 = LerpColor(InputBgV4, AccentGradientEndV4, Progress);
    DrawList->AddRectFilled(Origin, ImVec2(Origin.x + TrackSize.x, Origin.y + TrackSize.y),
                            DisabledAwareColor(TrackColorV4), TrackSize.y * 0.5f);

    const float KnobRadius = ToggleKnobDiameter * 0.5f * Scale;
    const float KnobMargin = ToggleKnobMargin * Scale;
    const float KnobX = LerpF(Origin.x + KnobMargin + KnobRadius,
                              Origin.x + TrackSize.x - KnobMargin - KnobRadius, Progress);
    const float KnobY = Origin.y + TrackSize.y * 0.5f;
    DrawList->AddCircleFilled(ImVec2(KnobX, KnobY), KnobRadius,
                              DisabledAwareColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));

    ImGui::PopID();
    return bChanged;
}

bool DrawEqBandBar(const char* Label, float* GainDb, float MinDb, float MaxDb, ImVec2 Size, float Scale)
{
    using namespace palette;

    ImGui::PushID(Label);
    ImGui::BeginGroup();

    bool bChanged = false;
    ImGuiIO& IO = ImGui::GetIO();

    // Numeric readout, drag-to-change same as the bar below it.
    char GainText[16];
    std::snprintf(GainText, sizeof(GainText), *GainDb > 0.0f ? "+%.0f" : "%.0f", *GainDb);
    const ImVec2 TextSize = ImGui::CalcTextSize(GainText);
    const float ReadoutHeight = TextSize.y + 4.0f * Scale;

    const ImVec2 ReadoutOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("readout", ImVec2(Size.x, ReadoutHeight));
    const bool bReadoutActive = ImGui::IsItemActive();

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const ImVec4 GainColorV4 = *GainDb == 0.0f    ? TextSecondaryV4
                                : *GainDb > 0.0f ? AccentHoverV4
                                                  : WarningV4;
    const ImVec2 TextPos(ReadoutOrigin.x + (Size.x - TextSize.x) * 0.5f, ReadoutOrigin.y + 2.0f * Scale);
    DrawList->AddText(TextPos, DisabledAwareColor(GainColorV4), GainText);

    // Bar, immediately below the readout.
    const ImVec2 BarOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("bar", Size);
    const bool bBarActive = ImGui::IsItemActive();

    if ((bReadoutActive || bBarActive) && IO.MouseDelta.y != 0.0f)
    {
        const float DeltaVal = -IO.MouseDelta.y / Size.y * (MaxDb - MinDb);
        *GainDb = std::clamp(*GainDb + DeltaVal, MinDb, MaxDb);
        bChanged = true;
    }

    DrawList->AddRectFilled(BarOrigin, ImVec2(BarOrigin.x + Size.x, BarOrigin.y + Size.y),
                            DisabledAwareColor(NestedCardBgV4), RadiusSmall * Scale);

    const float FillFrac = std::clamp((*GainDb - MinDb) / (MaxDb - MinDb), 0.0f, 1.0f);
    const float FillHeight = Size.y * FillFrac;
    const ImVec2 FillMin(BarOrigin.x, BarOrigin.y + Size.y - FillHeight);
    const ImVec2 FillMax(BarOrigin.x + Size.x, BarOrigin.y + Size.y);
    if (FillHeight > 0.0f)
    {
        const ImU32 TopColor = DisabledAwareColor(AccentGradientEndV4);
        const ImU32 BottomColor = DisabledAwareColor(AccentGradientStartV4);
        DrawList->AddRectFilledMultiColor(FillMin, FillMax, TopColor, TopColor, BottomColor, BottomColor);
        DrawList->AddLine(ImVec2(FillMin.x, FillMin.y), ImVec2(FillMax.x, FillMin.y),
                          DisabledAwareColor(ImVec4(1, 1, 1, 1)), 2.0f * Scale);
    }

    ImGui::EndGroup();
    ImGui::PopID();

    return bChanged;
}

void RightAlignCursor(float RowStartX, float RowWidth, float ItemWidth)
{
    ImGui::SetCursorScreenPos(ImVec2(RowStartX + RowWidth - ItemWidth, ImGui::GetCursorScreenPos().y));
}

CardScope::CardScope(ImU32 InBgColor, float InRounding, ImVec2 InPadding)
    : DrawList(ImGui::GetWindowDrawList()), BgColor(InBgColor), Rounding(InRounding), Padding(InPadding)
{
    Splitter.Split(DrawList, 2);
    Splitter.SetCurrentChannel(DrawList, 1); // content draws on the foreground channel

    // Position via an explicit screen-space cursor jump, not
    // ImGui::Indent()/Dummy() - Indent() mutates window-level state
    // (the baseline X every later NewLine()/SameLine() in the *whole
    // window* measures from until Unindent()), which misbehaves when a
    // caller nests CardScope inside a tight SameLine() loop (e.g. a row
    // of chips) - same class of "don't rely on window-relative layout
    // state across a group boundary" issue DrawSpatialTab's own
    // left/right column split ran into. Explicit positions sidestep it
    // entirely, same as DrawPositionDial's own SetCursorScreenPos usage.
    Origin = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Padding.x, Origin.y + Padding.y));
    ImGui::BeginGroup();
}

CardScope::~CardScope()
{
    ImGui::EndGroup();
    const ImVec2 ContentMin = ImGui::GetItemRectMin();
    const ImVec2 ContentMax = ImGui::GetItemRectMax();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, ContentMax.y + Padding.y));

    Splitter.SetCurrentChannel(DrawList, 0); // background channel, drawn behind channel 1 on merge
    DrawList->AddRectFilled(ImVec2(ContentMin.x - Padding.x, ContentMin.y - Padding.y),
                            ImVec2(ContentMax.x + Padding.x, ContentMax.y + Padding.y), BgColor, Rounding);
    Splitter.Merge(DrawList);
}

} // namespace ramkolfx::gui
