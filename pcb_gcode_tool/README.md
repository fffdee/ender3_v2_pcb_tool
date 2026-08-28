# Banux PCB 锡膏路径生成器

使用 PyQt6 编写的 Windows 上位机，将 PCB Pick and Place XLSX 坐标表或 Gerber Paste 层转换为 Banux 固件可执行的锡膏 G-code。

## 启动

双击 `start_tool.bat`，或者执行：

```powershell
& "C:\Users\admin\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" .\pcb_gcode_tool\main.py
```

程序启动时自动读取仓库根目录中的默认坐标表，也可以通过“导入 XLSX”读取其他文件。Gerber 焊盘铺膏请点击“导入 Gerber”，优先选择 Top Paste `.GTP` 或 Bottom Paste `.GBP` 文件。依赖记录在 `requirements.txt`。

## 使用流程

1. 在“路径来源”选择 `坐标表点胶` 或 `Gerber 焊盘铺膏`。
2. 坐标表模式选择 `Mid`、`Ref` 或 `Pad` 坐标，通常点胶使用元件中心 `Mid X/Y`。
3. Gerber 模式导入 Paste 层后，右侧会列出每个焊盘的中心、形状和宽高。
4. 筛选层、SMD 属性或焊盘信息，并在右侧勾选需要点胶的位置。
5. 设置工件原点、轴镜像、XY 偏移以及 Z/E 运动参数。
6. Gerber 模式设置喷嘴直径、铺膏线距、焊盘内缩、铺膏 `E/mm` 和最短线段。
7. 检查路径顺序和预览结果，导出 `pcb_solder_paste.gcode`。
8. 把文件放到 SD 卡或内部 Flash，通过 `gcode -f /sd/pcb_solder_paste.gcode` 执行。

生成文件仅使用当前固件支持的 `G0/G1/G90/G91/G92/M17/M84`。坐标表点胶和小焊盘点胶使用相对 E 挤出；Gerber 线段铺膏会先 `G92 E0`，再用同一条 `G1 X... Y... E...` 同步移动和挤出。当前固件没有 `G4` 延时指令，因此工具暂不生成驻留命令。

Gerber 模式使用 `gerbonara` 优先解析 RS-274X 图元，并内置一个轻量备用解析器。支持常见矩形、圆形、椭圆、线绘制和 region 焊盘；复杂 aperture macro 会尽量按边界盒处理。不要默认导入 Copper 层，铜层包含走线、铺铜和过孔，通常不等于需要涂锡膏的焊盘图形。

仓库内的 `sample_paste.GTP` 是一个很小的测试 Paste 层，可以用于验证 Gerber 导入、预览和 G-code 输出。
