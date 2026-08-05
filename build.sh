#!/usr/bin/env bash
# Configures and builds the RamkolFX daemon and GUI.
#
# Usage:
#   ./build.sh              # Debug/RelWithDebInfo build of daemon + GUI
#   ./build.sh clean        # wipe the build directory first
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

BUILD_DIR="build"

if [[ "${1:-}" == "clean" ]]; then
    rm -rf "$BUILD_DIR"
fi

cmake -S . -B "$BUILD_DIR" -G Ninja -DRAMKOLFX_BUILD_GUI=ON
cmake --build "$BUILD_DIR"

echo
echo "Build complete:"
echo "  daemon: $BUILD_DIR/daemon/ramkolfxd"
echo "  gui:    $BUILD_DIR/gui/ramkolfx-gui"
