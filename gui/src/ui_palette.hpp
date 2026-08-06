// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include "imgui.h"

// Shared color/radius palette for the redesigned control panel. Two forms
// of each color are provided since the two consumers need different
// types: ui_widgets.cpp/position_dial.cpp draw with raw ImDrawList calls
// (which want packed ImU32), while main.cpp's ImGuiStyle::Colors[]
// override and any stock ImGui widget (which want ImVec4). Both forms are
// derived from the same 0-255 components below so they can't drift apart.
namespace ramkolfx::gui::palette
{

namespace detail
{
constexpr ImVec4 Rgba(int R, int G, int B, float A = 1.0f)
{
    return ImVec4(static_cast<float>(R) / 255.0f, static_cast<float>(G) / 255.0f,
                   static_cast<float>(B) / 255.0f, A);
}

constexpr ImU32 ToU32(ImVec4 C)
{
    return IM_COL32(static_cast<int>(C.x * 255.0f + 0.5f), static_cast<int>(C.y * 255.0f + 0.5f),
                     static_cast<int>(C.z * 255.0f + 0.5f), static_cast<int>(C.w * 255.0f + 0.5f));
}
} // namespace detail

// Backgrounds
inline const ImVec4 PageBgV4 = detail::Rgba(0x0a, 0x0c, 0x10);
inline const ImVec4 PanelBgV4 = detail::Rgba(0x12, 0x15, 0x1b);
inline const ImVec4 NestedCardBgV4 = detail::Rgba(0x0d, 0x0f, 0x14);
inline const ImVec4 InputBgV4 = detail::Rgba(0x17, 0x1b, 0x22);
inline const ImVec4 ChipBgV4 = detail::Rgba(0x1a, 0x1e, 0x26);

// Borders
inline const ImVec4 BorderV4 = detail::Rgba(255, 255, 255, 0.08f);

// Text
inline const ImVec4 TextPrimaryV4 = detail::Rgba(0xee, 0xf2, 0xf7);
inline const ImVec4 TextSecondaryV4 = detail::Rgba(0x9a, 0xa3, 0xb2);
inline const ImVec4 TextMutedV4 = detail::Rgba(0x5b, 0x64, 0x72);

// Accent
inline const ImVec4 AccentGradientStartV4 = detail::Rgba(0x35, 0xc7, 0xff);
inline const ImVec4 AccentGradientEndV4 = detail::Rgba(0x3f, 0x7c, 0xff);
inline const ImVec4 AccentSolidV4 = detail::Rgba(0x38, 0xbd, 0xf8);
inline const ImVec4 AccentHoverV4 = detail::Rgba(0x5f, 0xc7, 0xff);

// Status
inline const ImVec4 SuccessV4 = detail::Rgba(0x4a, 0xde, 0x80);
inline const ImVec4 WarningV4 = detail::Rgba(0xf5, 0xa5, 0x24);

// U32 forms, for ImDrawList calls in ui_widgets.cpp/position_dial.cpp.
inline const ImU32 PageBg = detail::ToU32(PageBgV4);
inline const ImU32 PanelBg = detail::ToU32(PanelBgV4);
inline const ImU32 NestedCardBg = detail::ToU32(NestedCardBgV4);
inline const ImU32 InputBg = detail::ToU32(InputBgV4);
inline const ImU32 ChipBg = detail::ToU32(ChipBgV4);
inline const ImU32 Border = detail::ToU32(BorderV4);
inline const ImU32 TextPrimary = detail::ToU32(TextPrimaryV4);
inline const ImU32 TextSecondary = detail::ToU32(TextSecondaryV4);
inline const ImU32 TextMuted = detail::ToU32(TextMutedV4);
inline const ImU32 AccentGradientStart = detail::ToU32(AccentGradientStartV4);
inline const ImU32 AccentGradientEnd = detail::ToU32(AccentGradientEndV4);
inline const ImU32 AccentSolid = detail::ToU32(AccentSolidV4);
inline const ImU32 AccentHover = detail::ToU32(AccentHoverV4);
inline const ImU32 Success = detail::ToU32(SuccessV4);
inline const ImU32 Warning = detail::ToU32(WarningV4);

// Radii, in design-space (unscaled) pixels - multiply by DpiScale/Scale at
// the call site the same way every other custom-drawn constant in this
// codebase does (see position_dial.cpp's Scale parameter convention).
constexpr float RadiusTopCard = 16.0f;
constexpr float RadiusNestedCard = 12.0f;
constexpr float RadiusInput = 10.0f;
constexpr float RadiusSmall = 8.0f;

} // namespace ramkolfx::gui::palette
