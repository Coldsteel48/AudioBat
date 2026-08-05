// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include <cstdio>
#include <cstdlib>

#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"

#include "app.hpp"
#include "renderer.hpp"

namespace
{

// Base window/widget size the UI was designed at (100% / 96 DPI); every
// other size the UI computes is this times DpiScale.
constexpr int BaseWindowWidth = 420;
constexpr int BaseWindowHeight = 480;
constexpr float BaseFontSizePixels = 13.0f; // ImGui's own default bake size

// Picks a UI scale factor so the window and widgets read at a sane
// physical size on high-DPI displays instead of staying pinned to a tiny
// fixed pixel size. SDL's per-display DPI query is authoritative where
// it works, but Linux/X11 in particular is notorious for not reporting
// the desktop's actual scale factor through it - RAMKOLFX_GUI_SCALE lets
// a user override the guess on setups where auto-detection gets it wrong.
float ResolveDpiScale()
{
    if (const char* Override = std::getenv("RAMKOLFX_GUI_SCALE"))
    {
        const float Value = static_cast<float>(std::atof(Override));
        if (Value >= 0.5f && Value <= 4.0f)
        {
            return Value;
        }
    }

    float DiagonalDpi = 96.0f;
    if (SDL_GetDisplayDPI(0, &DiagonalDpi, nullptr, nullptr) != 0 || DiagonalDpi <= 0.0f)
    {
        DiagonalDpi = 96.0f;
    }
    float Scale = DiagonalDpi / 96.0f;
    if (Scale < 1.0f)
    {
        Scale = 1.0f; // never shrink below the design size
    }
    if (Scale > 4.0f)
    {
        Scale = 4.0f; // guard against a bogus DPI reading blowing up the window
    }
    return Scale;
}

} // namespace

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        fprintf(stderr, "[ramkolfx-gui] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const float DpiScale = ResolveDpiScale();

    auto Renderer = ramkolfx::gui::CreateOpenGL3Renderer();

    SDL_Window* Window = SDL_CreateWindow(
        "RamkolFX Control", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        static_cast<int>(BaseWindowWidth * DpiScale), static_cast<int>(BaseWindowHeight * DpiScale),
        Renderer->GetSDLWindowFlags() | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!Window)
    {
        fprintf(stderr, "[ramkolfx-gui] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(DpiScale);

    ImFontConfig FontConfig;
    FontConfig.SizePixels = BaseFontSizePixels * DpiScale;
    ImGui::GetIO().Fonts->AddFontDefault(&FontConfig);

    if (!Renderer->Init(Window))
    {
        ImGui::DestroyContext();
        SDL_DestroyWindow(Window);
        SDL_Quit();
        return 1;
    }

    ramkolfx::gui::App App(DpiScale);

    bool bQuit = false;
    Uint64 LastFrameTicks = SDL_GetPerformanceCounter();
    while (!bQuit)
    {
        SDL_Event Event;
        while (SDL_PollEvent(&Event))
        {
            ImGui_ImplSDL2_ProcessEvent(&Event);
            if (Event.type == SDL_QUIT)
            {
                bQuit = true;
            }
            if (Event.type == SDL_WINDOWEVENT && Event.window.event == SDL_WINDOWEVENT_CLOSE &&
                Event.window.windowID == SDL_GetWindowID(Window))
            {
                bQuit = true;
            }
        }

        const Uint64 NowTicks = SDL_GetPerformanceCounter();
        const float DeltaTimeSeconds =
            static_cast<float>(NowTicks - LastFrameTicks) / static_cast<float>(SDL_GetPerformanceFrequency());
        LastFrameTicks = NowTicks;

        Renderer->NewFrame();
        ImGui::NewFrame();

        App.Tick(DeltaTimeSeconds);

        ImGui::Render();
        Renderer->RenderFrame(ImGui::GetDrawData());
    }

    Renderer->Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyWindow(Window);
    SDL_Quit();
    return 0;
}
