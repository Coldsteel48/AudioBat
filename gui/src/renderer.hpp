// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <cstdint>
#include <memory>

struct ImDrawData;
struct SDL_Window;

namespace audiobat::gui
{

// Isolates every graphics-API-specific call (context/device creation,
// frame submission, present) behind one small interface, so the rest of
// the GUI (App, ControlClient, the azimuth dial widget) only ever issues
// plain ImGui:: draw calls and never includes a GL/Vulkan header. Adding
// a Vulkan backend later means a new IRenderer implementation and a
// one-line factory swap in main.cpp - nothing else changes.
class IRenderer
{
public:
    virtual ~IRenderer() = default;

    // SDL_WindowFlags bit(s) the window must be created with for this
    // backend (e.g. SDL_WINDOW_OPENGL).
    virtual uint32_t GetSDLWindowFlags() const = 0;

    // Creates the graphics context/device and initializes the matching
    // ImGui platform + renderer backends. Returns false on failure.
    virtual bool Init(SDL_Window* Window) = 0;

    // Starts a new backend frame (e.g. ImGui_ImplOpenGL3_NewFrame()).
    // Must be called before ImGui::NewFrame().
    virtual void NewFrame() = 0;

    // Clears the frame buffer, submits ImGui's draw data, and presents.
    virtual void RenderFrame(ImDrawData* DrawData) = 0;

    // Tears down the renderer backend and graphics context/device.
    virtual void Shutdown() = 0;
};

std::unique_ptr<IRenderer> CreateOpenGL3Renderer();

} // namespace audiobat::gui
