#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/.build"
PUSHER="$BUILD_DIR/ble_push_status"

mkdir -p "$BUILD_DIR"

if [ ! -x "$PUSHER" ] || [ "$SCRIPT_DIR/ble_push_status.swift" -nt "$PUSHER" ]; then
  swiftc "$SCRIPT_DIR/ble_push_status.swift" -o "$PUSHER"
fi

payload="${CC_SWITCH_STATUS_PAYLOAD:-}"
if [ -z "$payload" ]; then
  payload="$(python3 "$SCRIPT_DIR/cc_switch_provider_status.py")"
fi
"$PUSHER" "$payload" "${BLE_PUSH_TIMEOUT:-25}"
