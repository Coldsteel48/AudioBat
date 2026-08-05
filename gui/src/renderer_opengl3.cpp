// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "renderer.hpp"

#include <cstdio>

#include <GL/gl.h>
#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

// The only GUI source file allowed to include GL/SDL_GL headers or the
// ImGui OpenGL/SDL2 backend headers - everything else only sees
// renderer.hpp's plain IRenderer interface.

namespace ramkolfx::gui
{

namespace
{

class OpenGL3Renderer final : public IRenderer
{
public:
    uint32 GetSDLWindowFlags() const override
    {
        return SDL_WINDOW_OPENGL;
    }

    bool Init(SDL_Window* Window) override
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

        GLContext = SDL_GL_CreateContext(Window);
        if (!GLContext)
        {
            fprintf(stderr, "[ramkolfx-gui] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
            return false;
        }
        SDL_GL_MakeCurrent(Window, GLContext);
        SDL_GL_SetSwapInterval(1); // vsync

        ImGui_ImplSDL2_InitForOpenGL(Window, GLContext);
        ImGui_ImplOpenGL3_Init("#version 330 core");

        OwningWindow = Window;
        return true;
    }

    void NewFrame() override
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
    }

    void RenderFrame(ImDrawData* DrawData) override
    {
        glViewport(0, 0, static_cast<int>(DrawData->DisplaySize.x * DrawData->FramebufferScale.x),
                   static_cast<int>(DrawData->DisplaySize.y * DrawData->FramebufferScale.y));
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(DrawData);
        SDL_GL_SwapWindow(OwningWindow);
    }

    void Shutdown() override
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        if (GLContext)
        {
            SDL_GL_DeleteContext(GLContext);
            GLContext = nullptr;
        }
    }

private:
    SDL_GLContext GLContext = nullptr;
    SDL_Window* OwningWindow = nullptr;
};

} // namespace

std::unique_ptr<IRenderer> CreateOpenGL3Renderer()
{
    return std::make_unique<OpenGL3Renderer>();
}

} // namespace ramkolfx::gui
