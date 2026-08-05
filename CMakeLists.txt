# RamkolFX
# Copyright (C) 2026 Roman Levin (Coldsteel48)
#
# This file is part of RamkolFX, dual-licensed under the GNU General
# Public License v3.0 (see LICENSE) or a separate commercial license
# (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
# terms of the Contributor License Agreement (see CLA.md).

cmake_minimum_required(VERSION 3.20)
project(RamkolFX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra)

add_subdirectory(common)
add_subdirectory(daemon)

option(RAMKOLFX_BUILD_GUI "Build the RamkolFX GUI control app" OFF)
if(RAMKOLFX_BUILD_GUI)
    add_subdirectory(gui)
endif()
