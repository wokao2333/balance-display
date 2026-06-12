# cc switch BLE display bridge

This project uses Bluetooth Low Energy to push cc switch status from this Mac to an ESP32-S3 display.

Flow:

```text
Mac LaunchAgent
  -> reads ~/.cc-switch/cc-switch.db every 2 minutes
  -> fetches the provider usage/balance data locally on the Mac
  -> caches the last good balance in ~/.cc-switch/ble-display-status-cache.json
  -> writes a small JSON payload to the ESP32-S3 BLE characteristic
  -> ESP32 updates the screen

ESP32 refresh button
  -> sends a BLE notify event to the Mac bridge
  -> Mac fetches the current provider usage immediately
  -> Mac writes the refreshed JSON payload back to the ESP32
```

No server is required.

## BLE shape

- ESP32-S3 role: BLE peripheral / GATT server
- Mac role: BLE central / client
- Device name: `CCSwitch`
- Service UUID: `7b3d0001-7c2f-4f81-9f2f-2d5a91c3cc01`
- Status characteristic UUID: `7b3d0002-7c2f-4f81-9f2f-2d5a91c3cc01`
- Refresh request characteristic UUID: `7b3d0003-7c2f-4f81-9f2f-2d5a91c3cc01`

The status characteristic is written by the Mac. The refresh request
characteristic is notified by the ESP32 when the LVGL button is clicked.

Status payload example:

```json
{
  "providerId": "nightyu-1779944998890",
  "providerName": "NightYu",
  "status": "using",
  "updatedAt": 1780844488063,
  "refreshedAt": 1780844488063,
  "refreshedAtText": "2026-06-09 09:12:34",
  "ageSeconds": 0,
  "balanceText": "20.73",
  "currency": "USD",
  "resetAt": "2026-06-07T16:00:00.000Z"
}
```

If the provider usage request times out, the Mac script keeps the current
provider and reuses the last cached `balanceText`, `currency`, and `resetAt`
when available, so the display does not blank the quota line during a transient
network/API failure.

The `ageSeconds` field is the age of the usage data at send time. A fresh usage
response sends `0`; cached fallback responses send the elapsed time since the
cached balance was captured. The ESP32 keeps incrementing that age locally and
only redraws the small age text region.

The bottom timestamp on the ESP32 screen is the Mac refresh time from
`refreshedAtText`; it does not display the provider quota reset time.

## ESP32 firmware

The Arduino sketch is in `esp32/cc_switch_display_ble/cc_switch_display_ble.ino`.
It uses `GFX Library for Arduino` version `1.5.9`, `U8g2` version `2.36.19`,
and ESP32 Arduino core `2.0.16`. `U8g2` is used only to let Arduino_GFX draw
the small Chinese labels in the status card.

Install/check libraries:

```bash
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" lib install U8g2
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" lib list | rg '^U8g2|GFX Library'
```

Compile check:

```bash
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" compile --fqbn esp32:esp32:esp32s3 "/Users/wokao2333/Desktop/p图/cc-switch-ble-display/esp32/cc_switch_display_ble"
```

Push a fixed visual-check sample that mirrors the reference screenshot:

```bash
BLE_PUSH_TIMEOUT=25 mac/push_demo_status.sh
```

Run the long-lived Mac bridge that listens for the ESP32 refresh button and
also keeps the display periodically refreshed:

```bash
mac/watch_refresh_requests.sh
```

Or run the full visual-check helper, which generates the expected SVG preview
and pushes the same demo payload to the ESP32:

```bash
scripts/run_visual_check.sh
```

Generate a desktop SVG preview from the current Arduino_GFX layout constants:

```bash
scripts/render_ui_preview.py
python3 scripts/render_ui_preview.py --payload "$(python3 mac/cc_switch_provider_status.py)" --out scripts/ui_preview_real.svg
```

The default preview is written to `scripts/ui_preview.svg` and should match the
demo BLE payload: `9 分钟前`, `25.31`, `USD`, and `使用中`. Use it as a coordinate
reference when comparing a photo of the physical screen.

Before uploading, close Arduino Serial Monitor or stop any `arduino-cli monitor`
process using `/dev/cu.usbserial-110`, otherwise the upload port will be busy.
