#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/.build"
PUSHER="$BUILD_DIR/ble_push_status"

mkdir -p "$BUILD_DIR"

if [ ! -x "$PUSHER" ] || [ "$SCRIPT_DIR/ble_push_status.swift" -nt "$PUSHER" ]; then
  swiftc "$SCRIPT_DIR/ble_push_status.swift" -o "$PUSHER"
fi

refreshed_at_ms="$(python3 - <<'PY'
import time
print(int(time.time() * 1000))
PY
)"
refreshed_at_text="$(date '+%Y-%m-%d %H:%M:%S')"
payload="{\"providerName\":\"Demo Provider\",\"status\":\"using\",\"ageText\":\"9 分钟前\",\"balanceText\":\"25.31\",\"currency\":\"USD\",\"resetAt\":\"2026-06-08T16:00:00.000Z\",\"refreshedAt\":${refreshed_at_ms},\"refreshedAtText\":\"${refreshed_at_text}\"}"
"$PUSHER" "$payload" "${BLE_PUSH_TIMEOUT:-25}"
