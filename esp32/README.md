# ESP32-S3 BLE 显示固件

ESP32-S3 应暴露一个 BLE GATT 服务器：

- 设备名称：`CCSwitch`
- 服务 UUID：`7b3d0001-7c2f-4f81-9f2f-2d5a91c3cc01`
- 状态特征 UUID：`7b3d0002-7c2f-4f81-9f2f-2d5a91c3cc01`
- 刷新请求特征 UUID：`7b3d0003-7c2f-4f81-9f2f-2d5a91c3cc01`
- 状态特征属性：write，可选支持 write without response
- 刷新请求特征属性：read 和 notify

当特征收到 JSON 后，会解析提供商名称、余额、货币、状态、重置时间以及可选的
`ageText`，然后更新 ST7796 屏幕。紧凑界面参考示例截图，屏幕上只明显展示同步时间、
刷新图标、状态标签、剩余额度和货币。

LVGL 刷新按钮会通过刷新请求特征发送 `{"action":"refresh_usage"}`。Mac 端桥接程序会监听
这个 notify 事件，获取当前用量数据，并写回刷新后的状态载荷。

底部时间字段显示来自 `refreshedAtText` 的 Mac 刷新时间，格式为 `YYYY-MM-DD HH:MM:SS`。
这里有意不显示提供商额度的重置时间。

当前 Arduino 草图使用：

- 开发板：`ESP32S3 Dev Module`
- 显示屏：ST7796，320x480，8080 16 位并口
- 图形库：`GFX Library for Arduino@1.5.9`
- 中文标签字体依赖：`U8g2@2.36.19`
- 屏幕方向：横屏

首次显示的画面为 `等待数据`。Mac 通过 BLE 推送状态后，静态 Arduino_GFX 卡片会保持在屏幕上，
仅动态文本区域刷新：同步时间、状态标签、剩余额度和货币。

如需进行视觉 QA，请在项目根目录运行 `BLE_PUSH_TIMEOUT=25 mac/push_demo_status.sh`。
它会发送 `9 分钟前`、`25.31`、`USD` 和 `使用中`。
