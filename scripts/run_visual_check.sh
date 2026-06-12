#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PREVIEW="$ROOT_DIR/scripts/ui_preview.svg"

cd "$ROOT_DIR"

echo "Generating expected preview..."
scripts/render_ui_preview.py --out "$PREVIEW" >/dev/null

echo "Pushing demo payload to ESP32..."
BLE_PUSH_TIMEOUT="${BLE_PUSH_TIMEOUT:-25}" mac/push_demo_status.sh

cat <<EOF

Visual check is ready.

Expected preview:
  $PREVIEW

The ESP32 screen should show:
  9 分钟前 / 剩余：25.31 USD / 使用中

Take a photo of the physical screen and compare it with the SVG preview.
EOF
