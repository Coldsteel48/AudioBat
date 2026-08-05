# RamkolFX
# Copyright (C) 2026 Roman Levin (Coldsteel48)
#
# This file is part of RamkolFX, dual-licensed under the GNU General
# Public License v3.0 (see LICENSE) or a separate commercial license
# (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
# terms of the Contributor License Agreement (see CLA.md).

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
find_package(Threads REQUIRED)

# libmysofa (HRTF/SOFA file loading) and KissFFT (partitioned convolution)
# back BinauralStage's HRTF binaural decode ("Advanced" spatial mode). Both
# are BSD-3-Clause (permissive), matching the constraint in README.md's
# "Engineering approach" section; fetched the same way gui/CMakeLists.txt
# already fetches Dear ImGui, since neither ships as a common distro
# package.
include(FetchContent)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    mysofa
    GIT_REPOSITORY https://github.com/hoene/libmysofa.git
    GIT_TAG v1.3.5
)
FetchContent_MakeAvailable(mysofa)

set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG 131.2.0
)
FetchContent_MakeAvailable(kissfft)

# Absolute path to the bundled default HRTF SOFA file (see
# data/hrtf/README.md), baked in as the fallback when the
# RAMKOLFX_HRTF_SOFA environment variable isn't set.
set(RAMKOLFX_DEFAULT_HRTF_SOFA_PATH "${CMAKE_SOURCE_DIR}/data/hrtf/default.sofa")
configure_file(
    src/hrtf_default_path.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/hrtf_default_path.hpp
    @ONLY
)

# Full switchable HRTF catalog (see data/hrtf/README.md): the bundled MIT
# KEMAR default, the bundled SADIE II subjects (Apache-2.0, safe to
# redistribute - unlike the CIPIC/ARI/Listen mirrors this project
# deliberately avoids bundling), and the license-free synthetic
# spherical-head model. AudioEngine filters this list down to entries
# that are actually present on disk at startup.
set(RAMKOLFX_HRTF_CATALOG_ENTRIES
    "    { \"MIT KEMAR (normal pinna)\", HrtfSourceKind::SofaFile, \"${CMAKE_SOURCE_DIR}/data/hrtf/default.sofa\" },\n")
set(RAMKOLFX_SADIE_SUBJECTS H3 H5 H8 H10 H13 H15 H18 H20)
foreach(SUBJECT ${RAMKOLFX_SADIE_SUBJECTS})
    string(APPEND RAMKOLFX_HRTF_CATALOG_ENTRIES
        "    { \"SADIE II - ${SUBJECT}\", HrtfSourceKind::SofaFile, \"${CMAKE_SOURCE_DIR}/data/hrtf/sadie/sadie_${SUBJECT}.sofa\" },\n")
endforeach()
string(APPEND RAMKOLFX_HRTF_CATALOG_ENTRIES
    "    { \"Synthetic spherical head\", HrtfSourceKind::SyntheticSphericalHead, \"\" },\n")

configure_file(
    src/hrtf_catalog.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/hrtf_catalog.hpp
    @ONLY
)

add_executable(ramkolfxd
    src/main.cpp
    src/audio_engine.cpp
    src/virtual_sink.cpp
    src/hardware_output.cpp
    src/device_registry.cpp
    src/single_instance_lock.cpp
    src/settings_store.cpp
    src/dsp/passthrough_stage.cpp
    src/dsp/speaker_layout.cpp
    src/dsp/ambisonics_stage.cpp
    src/dsp/hrtf_loader.cpp
    src/dsp/synthetic_hrtf.cpp
    src/dsp/near_field_filter.cpp
    src/dsp/hrtf_gain_normalization.cpp
    src/dsp/partitioned_convolver.cpp
    src/dsp/binaural_stage.cpp
    src/dsp/binaural_voice.cpp
    src/dsp/hrtf_deck.cpp
    src/dsp/biquad.cpp
    src/dsp/graphic_eq_filter.cpp
    src/dsp/hw_eq_stage.cpp
    src/dsp/bass_enhancer_filter.cpp
    src/dsp/bass_enhancer_stage.cpp
    src/control/control_server.cpp
)

target_include_directories(ramkolfxd PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src # so the generated headers above can #include "dsp/..." like everything else does
    ${mysofa_BINARY_DIR}/src # generated mysofa_export.h, not part of mysofa-static's public interface
)

target_link_libraries(ramkolfxd PRIVATE
    ramkolfx_common
    PkgConfig::PIPEWIRE
    Threads::Threads
    mysofa-static
    kissfft::kissfft
)
