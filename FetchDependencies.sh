#!/usr/bin/env bash
# RamkolFX
# Copyright (C) 2026 Roman Levin (Coldsteel48)
#
# This file is part of RamkolFX, dual-licensed under the GNU General
# Public License v3.0 (see LICENSE) or a separate commercial license
# (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
# terms of the Contributor License Agreement (see CLA.md).
#
# Installs the OS packages needed to build and run RamkolFX (see the
# "Dependencies" section of README.md). Detects the distro via
# /etc/os-release and dispatches to its native package manager.
#
# Usage:
#   ./FetchDependencies.sh              # daemon + GUI deps (default, matches build.sh)
#   ./FetchDependencies.sh --no-gui     # daemon-only deps
#   ./FetchDependencies.sh --dry-run    # print the install command without running it
set -euo pipefail

WITH_GUI=1
DRY_RUN=0

for arg in "$@"; do
    case "$arg" in
        --no-gui) WITH_GUI=0 ;;
        --dry-run) DRY_RUN=1 ;;
        -h|--help)
            echo "usage: $0 [--no-gui] [--dry-run]"
            exit 0
            ;;
        *)
            echo "error: unknown argument: $arg" >&2
            echo "usage: $0 [--no-gui] [--dry-run]" >&2
            exit 1
            ;;
    esac
done

if [[ ! -r /etc/os-release ]]; then
    echo "error: /etc/os-release not found, can't detect distro" >&2
    exit 1
fi

# shellcheck disable=SC1091
. /etc/os-release
ID_LIKE="${ID_LIKE:-}"

# Common build tooling, plus git: not otherwise listed in README.md since
# it's assumed present, but daemon/CMakeLists.txt and gui/CMakeLists.txt
# both fetch third-party sources via CMake FetchContent's GIT_REPOSITORY,
# so a fresh machine needs it too.
run() {
    echo "+ $*"
    if [[ "$DRY_RUN" -eq 0 ]]; then
        "$@"
    fi
}

case "$ID $ID_LIKE" in
    *debian*|*ubuntu*)
        PACKAGES=(build-essential cmake ninja-build pkg-config git
                   libpipewire-0.3-dev zlib1g-dev)
        [[ "$WITH_GUI" -eq 1 ]] && PACKAGES+=(libsdl2-dev libgl1-mesa-dev)
        run sudo apt update
        run sudo apt install -y "${PACKAGES[@]}"
        ;;
    *fedora*|*rhel*)
        PACKAGES=(gcc-c++ cmake ninja-build pkgconf-pkg-config git
                   pipewire-devel zlib-devel)
        [[ "$WITH_GUI" -eq 1 ]] && PACKAGES+=(SDL2-devel mesa-libGL-devel)
        run sudo dnf install -y "${PACKAGES[@]}"
        ;;
    *arch*)
        PACKAGES=(base-devel cmake ninja pkgconf git libpipewire zlib)
        [[ "$WITH_GUI" -eq 1 ]] && PACKAGES+=(sdl2 mesa)
        run sudo pacman -S --needed "${PACKAGES[@]}"
        ;;
    *opensuse*|*suse*)
        PACKAGES=(gcc-c++ cmake ninja pkgconf-pkg-config git
                   pipewire-devel zlib-devel)
        [[ "$WITH_GUI" -eq 1 ]] && PACKAGES+=(libSDL2-devel Mesa-libGL-devel)
        run sudo zypper install -y "${PACKAGES[@]}"
        ;;
    *)
        echo "error: unrecognized distro (ID=$ID ID_LIKE=$ID_LIKE)" >&2
        echo "See the \"Dependencies\" section of README.md and install the" >&2
        echo "equivalent packages for your distro manually." >&2
        exit 1
        ;;
esac

echo
echo "Dependencies installed. Build with ./build.sh (or omit -DRAMKOLFX_BUILD_GUI=ON for a daemon-only build)."
