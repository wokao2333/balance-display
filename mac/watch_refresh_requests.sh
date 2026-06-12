#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/.build"
WATCHER="$BUILD_DIR/ble_watch_refresh"

mkdir -p "$BUILD_DIR"

if [ ! -x "$WATCHER" ] || [ "$SCRIPT_DIR/ble_watch_refresh.swift" -nt "$WATCHER" ]; then
  swiftc "$SCRIPT_DIR/ble_watch_refresh.swift" -o "$WATCHER"
fi

"$WATCHER" "$SCRIPT_DIR/cc_switch_provider_status.py"
