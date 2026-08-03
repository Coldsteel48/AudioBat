// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include <cstdio>

#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"

#include "app.hpp"
#include "renderer.hpp"

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        fprintf(stderr, "[audiobat-gui] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    auto Renderer = audiobat::gui::CreateOpenGL3Renderer();

    SDL_Window* Window =
        SDL_CreateWindow("AudioBat Control", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 420, 480,
                          Renderer->GetSDLWindowFlags() | SDL_WINDOW_RESIZABLE);
    if (!Window)
    {
        fprintf(stderr, "[audiobat-gui] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    if (!Renderer->Init(Window))
    {
        ImGui::DestroyContext();
        SDL_DestroyWindow(Window);
        SDL_Quit();
        return 1;
    }

    audiobat::gui::App App;

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
