# cc switch BLE 显示桥接项目

这个项目通过 Bluetooth Low Energy，把本机 Mac 上的 cc switch 状态推送到 ESP32-S3 显示屏。

工作流程：

```text
Mac LaunchAgent
  -> 每 2 分钟读取 ~/.cc-switch/cc-switch.db
  -> 在 Mac 本地获取 provider 的用量/余额数据
  -> 将最近一次可用余额缓存到 ~/.cc-switch/ble-display-status-cache.json
  -> 向 ESP32-S3 的 BLE 特征写入一段小型 JSON 载荷
  -> ESP32 更新屏幕内容

ESP32 刷新按钮
  -> 向 Mac 桥接程序发送 BLE notify 事件
  -> Mac 立即获取当前 provider 用量
  -> Mac 将刷新后的 JSON 载荷写回 ESP32
```

整个链路不需要服务器。

## BLE 结构

- ESP32-S3 角色：BLE peripheral / GATT server
- Mac 角色：BLE central / client
- 设备名称：`CCSwitch`
- 服务 UUID：`7b3d0001-7c2f-4f81-9f2f-2d5a91c3cc01`
- 状态特征 UUID：`7b3d0002-7c2f-4f81-9f2f-2d5a91c3cc01`
- 刷新请求特征 UUID：`7b3d0003-7c2f-4f81-9f2f-2d5a91c3cc01`

状态特征由 Mac 写入。点击 ESP32 上的 LVGL 按钮时，ESP32 会通过刷新请求特征发送 notify。

状态载荷示例：

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

如果 provider 用量请求超时，Mac 脚本会保留当前 provider，并在可用时复用上次缓存的
`balanceText`、`currency` 和 `resetAt`。这样在临时网络/API 故障时，屏幕上的额度行不会变空。

`ageSeconds` 字段表示发送时用量数据的年龄。新鲜的用量响应会发送 `0`；缓存兜底响应会发送从缓存余额生成到当前的经过秒数。ESP32 会在本地继续递增这个时间，并只重绘小号时间文本区域。

ESP32 屏幕底部的时间来自 `refreshedAtText`，表示 Mac 刷新时间；它不显示 provider 额度重置时间。

## ESP32 固件

Arduino 草图位于 `esp32/cc_switch_display_ble/cc_switch_display_ble.ino`。
它使用 `GFX Library for Arduino` `1.5.9`、`U8g2` `2.36.19`，以及 ESP32 Arduino core `2.0.16`。`U8g2` 只用于让 Arduino_GFX 绘制状态卡片里的小号中文标签。

安装/检查库：

```bash
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" lib install U8g2
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" lib list | rg '^U8g2|GFX Library'
```

编译检查：

```bash
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" compile --fqbn esp32:esp32:esp32s3 "/Users/wokao2333/Desktop/p图/cc-switch-ble-display/esp32/cc_switch_display_ble"
```

推送一个固定的视觉检查示例，内容与参考截图一致：

```bash
BLE_PUSH_TIMEOUT=25 mac/push_demo_status.sh
```

运行常驻 Mac 桥接程序：监听 ESP32 刷新按钮，并定期刷新显示内容。

```bash
mac/watch_refresh_requests.sh
```

也可以运行完整视觉检查脚本：它会生成预期 SVG 预览，并向 ESP32 推送同一份演示载荷。

```bash
scripts/run_visual_check.sh
```

根据当前 Arduino_GFX 布局常量生成桌面 SVG 预览：

```bash
scripts/render_ui_preview.py
python3 scripts/render_ui_preview.py --payload "$(python3 mac/cc_switch_provider_status.py)" --out scripts/ui_preview_real.svg
```

默认预览会写入 `scripts/ui_preview.svg`，并且应该匹配演示 BLE 载荷：`9 分钟前`、`25.31`、`USD` 和 `使用中`。对比实体屏幕照片时，可以把它作为坐标参考。

上传前，请关闭 Arduino Serial Monitor，或停止任何正在使用 `/dev/cu.usbserial-110` 的 `arduino-cli monitor` 进程，否则上传端口会被占用。
