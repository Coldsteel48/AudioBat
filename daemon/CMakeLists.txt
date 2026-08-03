# AudioDock
# Copyright (C) 2026 Roman Levin (Coldsteel48)
#
# This file is part of AudioDock, dual-licensed under the GNU General
# Public License v3.0 (see LICENSE) or a separate commercial license
# (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
# terms of the Contributor License Agreement (see CLA.md).

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
find_package(Threads REQUIRED)

add_executable(audiodockd
    src/main.cpp
    src/audio_engine.cpp
    src/virtual_sink.cpp
    src/hardware_output.cpp
    src/dsp/passthrough_stage.cpp
    src/dsp/ambisonics_stage.cpp
    src/control/control_server.cpp
)

target_link_libraries(audiodockd PRIVATE
    audiodock_common
    PkgConfig::PIPEWIRE
    Threads::Threads
)
