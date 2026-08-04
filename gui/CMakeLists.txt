# AudioBat
# Copyright (C) 2026 Roman Levin (Coldsteel48)
#
# This file is part of AudioBat, dual-licensed under the GNU General
# Public License v3.0 (see LICENSE) or a separate commercial license
# (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
# terms of the Contributor License Agreement (see CLA.md).

# Dear ImGui + SDL2/OpenGL3 control app. Talks to audiobatd purely over
# the Unix control socket (see common/include/audiobat/protocol.hpp) -
# deliberately no PipeWire dependency here.
#
# Dear ImGui (MIT-licensed) is vendored via FetchContent rather than a
# system package: it avoids Qt/GTK's LGPL relinking obligations mattering
# for AudioBat's commercial license track, and sidesteps cross-distro
# Qt5/Qt6 or GTK version skew since ImGui is meant to be compiled straight
# into the app rather than linked as a versioned shared library.

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_package(OpenGL REQUIRED)

include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.6
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC PkgConfig::SDL2 OpenGL::GL)

add_executable(audiobat-gui
    src/main.cpp
    src/app.cpp
    src/control_client.cpp
    src/position_dial.cpp
    src/renderer_opengl3.cpp
)

target_link_libraries(audiobat-gui PRIVATE
    audiobat_common
    imgui
    PkgConfig::SDL2
    OpenGL::GL
)
