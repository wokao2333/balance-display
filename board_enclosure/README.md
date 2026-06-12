# ESP32-S3 N16R8 V1.0 外壳

根据你提供的机械尺寸图和贴片位号图生成。

## 尺寸来源

- PCB 外形：60.26 x 103.00 mm
- 安装孔孔距：52.26 x 95.00 mm
- 安装孔：4 个，直径 3.10 mm
- 屏幕窗口：51.00 x 75.70 mm
- 前壳外形：65.06 x 107.80 mm
- 后盖外形：64.66 x 107.40 mm
- 前壳高度：9.60 mm
- 后盖厚度：1.80 mm
- 装配间隙：有装配关系的位置按单边 0.20 mm 留量

## 输出文件

- `output/esp32s3_n16r8_front_shell.stl`
- `output/esp32s3_n16r8_rear_cover.stl`
- `output/esp32s3_n16r8_case_preview.blend`
- `output/esp32s3_n16r8_case_preview.png`

## 打印建议

- 前壳：屏幕面朝下贴在热床上打印。
- 后盖：平放打印。
- 支撑：通常不需要；如果切片软件提示侧边接口窗口桥接风险，可以开启轻量支撑。
- 螺丝：默认按 M2.5 螺丝设计，后盖通孔，拧入前壳打印柱。PCB 孔径是 3.10 mm，所以也可以改成 M3，但会比较紧；如果要用 M3，请在 `create_esp32s3_case.py` 里调整 `SCREW_CLEAR_D` 和 `SCREW_PILOT_D`。

接口开孔根据贴片位号图做了偏宽裕设计。USB、COM、TF 卡和 UART 主要从侧边插入，后盖只在侧边对应位置让位，不再开大面积背面窗口。因为图纸没有完整标出每个接口的高度和插入深度，建议先低填充试打一版，实际装板后如果某个接口偏紧，再修改脚本里的 `side_slot(...)` 或 `side_notch_for_cover(...)` 参数，然后重新运行 Blender 生成 STL。

当前脚本里的关键公差参数：

- `FIT_CLEARANCE = 0.20`：通用单边装配间隙。
- `BOARD_CLEARANCE = FIT_CLEARANCE * 2.0`：PCB 腔体总间隙，即左右/上下各 0.20 mm。
- `COVER_EDGE_CLEARANCE = FIT_CLEARANCE`：后盖相对前壳外缘每边缩小 0.20 mm。
- `PORT_CLEARANCE = FIT_CLEARANCE * 2.0`：接口窗口额外总间隙。
