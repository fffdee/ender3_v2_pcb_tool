# Banux PCB 锡膏路径生成器

使用 PyQt6 编写的 Windows 上位机，将 PCB Pick and Place XLSX 坐标表转换为 Banux 固件可执行的 G-code。

## 启动

双击 `start_tool.bat`，或者执行：

```powershell
& "C:\Users\admin\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" .\pcb_gcode_tool\main.py
```

程序启动时自动读取仓库根目录中的默认坐标表，也可以通过“导入 XLSX”读取其他文件。依赖记录在 `requirements.txt`。

## 使用流程

1. 选择 `Mid`、`Ref` 或 `Pad` 坐标，通常点胶使用元件中心 `Mid X/Y`。
2. 筛选层、SMD 属性或位号，并在右侧勾选需要点胶的位置。
3. 设置工件原点、轴镜像、XY 偏移以及 Z/E 运动参数。
4. 检查路径顺序和预览结果，导出 `pcb_solder_paste.gcode`。
5. 把文件放到 SD 卡或内部 Flash，通过 `gcode -f /sd/pcb_solder_paste.gcode` 执行。

生成文件仅使用当前固件支持的 `G0/G1/G90/G91/G92/M17/M84`。E 轴挤出时临时使用相对坐标，随后恢复绝对坐标，确保后续 XY 位置不受影响。当前固件没有 `G4` 延时指令，因此工具暂不生成驻留命令。

这类 Pick and Place 文件每行通常只有一个元件坐标，不包含每个焊盘的完整锡膏图形。程序目前把每行作为一个点胶点；精确到焊盘的路径需要后续增加 Gerber Paste 层解析。
