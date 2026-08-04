#!/usr/bin/env bash
# Runs the AudioBat daemon and GUI together.
#
# Starts audiobatd in the background, waits for its control socket to come
# up, launches audiobat-gui in the foreground, then stops the daemon when
# the GUI exits (or on Ctrl+C).
#
# Usage:
#   ./run.sh              # run daemon + gui
#   ./run.sh daemon        # run only the daemon (foreground)
#   ./run.sh gui            # run only the gui (assumes a daemon is already running)
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

BUILD_DIR="build"
DAEMON_BIN="$BUILD_DIR/daemon/audiobatd"
GUI_BIN="$BUILD_DIR/gui/audiobat-gui"

SOCK_PATH="${XDG_RUNTIME_DIR:-/tmp/audiobat-$(id -u)}/audiobat/control.sock"

run_daemon_only() {
    exec "$DAEMON_BIN"
}

run_gui_only() {
    exec ./run_gui.sh
}

run_both() {
    [[ -x "$DAEMON_BIN" ]] || { echo "error: $DAEMON_BIN not found, run ./build.sh first" >&2; exit 1; }
    [[ -x "$GUI_BIN" ]] || { echo "error: $GUI_BIN not found, run ./build.sh first" >&2; exit 1; }

    "$DAEMON_BIN" &
    daemon_pid=$!

    cleanup() {
        if kill -0 "$daemon_pid" 2>/dev/null; then
            kill "$daemon_pid" 2>/dev/null || true
            wait "$daemon_pid" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    for _ in $(seq 1 50); do
        [[ -S "$SOCK_PATH" ]] && break
        kill -0 "$daemon_pid" 2>/dev/null || { echo "error: daemon exited before starting the control socket" >&2; exit 1; }
        sleep 0.1
    done

    ./run_gui.sh
}

case "${1:-both}" in
    daemon) run_daemon_only ;;
    gui) run_gui_only ;;
    both) run_both ;;
    *) echo "usage: $0 [daemon|gui|both]" >&2; exit 1 ;;
esac
