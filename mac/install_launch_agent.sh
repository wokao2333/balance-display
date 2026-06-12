#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$HOME/Library/Application Support/cc-switch-ble-display"
PLIST_PATH="$HOME/Library/LaunchAgents/com.cc-switch-ble-display.plist"

install -d "$APP_DIR/mac"
install -m 755 "$SOURCE_DIR/cc_switch_provider_status.py" "$APP_DIR/mac/cc_switch_provider_status.py"
install -m 644 "$SOURCE_DIR/ble_push_status.swift" "$APP_DIR/mac/ble_push_status.swift"
install -m 644 "$SOURCE_DIR/ble_watch_refresh.swift" "$APP_DIR/mac/ble_watch_refresh.swift"
install -m 755 "$SOURCE_DIR/push_status_once.sh" "$APP_DIR/mac/push_status_once.sh"
install -m 755 "$SOURCE_DIR/watch_and_push.sh" "$APP_DIR/mac/watch_and_push.sh"
install -m 755 "$SOURCE_DIR/watch_refresh_requests.sh" "$APP_DIR/mac/watch_refresh_requests.sh"

cat > "$PLIST_PATH" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.cc-switch-ble-display</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/bin/env</string>
    <string>bash</string>
    <string>$APP_DIR/mac/watch_refresh_requests.sh</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>CC_SWITCH_BLE_INTERVAL</key>
    <string>120</string>
    <key>BLE_PUSH_TIMEOUT</key>
    <string>25</string>
  </dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>StandardOutPath</key>
  <string>$HOME/Library/Logs/cc-switch-ble-display.log</string>
  <key>StandardErrorPath</key>
  <string>$HOME/Library/Logs/cc-switch-ble-display.err.log</string>
</dict>
</plist>
EOF

launchctl unload "$PLIST_PATH" 2>/dev/null || true
launchctl load "$PLIST_PATH"
launchctl start com.cc-switch-ble-display
launchctl list | grep com.cc-switch-ble-display || true
