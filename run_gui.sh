#!/usr/bin/env bash
# Runs the RamkolFX GUI control app.
#
# Assumes a daemon is already running and listening on the control socket
# (see run.sh, which starts the daemon before calling this script).
#
# Usage:
#   ./run_gui.sh
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

GUI_BIN="build/gui/ramkolfx-gui"

[[ -x "$GUI_BIN" ]] || { echo "error: $GUI_BIN not found, run ./build.sh first" >&2; exit 1; }

exec "$GUI_BIN"
