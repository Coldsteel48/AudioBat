# AudioDock
# Copyright (C) 2026 Roman Levin (Coldsteel48)
#
# This file is part of AudioDock, dual-licensed under the GNU General
# Public License v3.0 (see LICENSE) or a separate commercial license
# (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
# terms of the Contributor License Agreement (see CLA.md).

add_library(audiodock_common STATIC
    src/protocol.cpp
)

target_include_directories(audiodock_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(audiodock_common PUBLIC cxx_std_20)
