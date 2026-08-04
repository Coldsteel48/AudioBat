#!/usr/bin/env bash
# Configures and builds the AudioBat daemon and GUI.
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

cmake -S . -B "$BUILD_DIR" -G Ninja -DAUDIOBAT_BUILD_GUI=ON
cmake --build "$BUILD_DIR"

echo
echo "Build complete:"
echo "  daemon: $BUILD_DIR/daemon/audiobatd"
echo "  gui:    $BUILD_DIR/gui/audiobat-gui"
