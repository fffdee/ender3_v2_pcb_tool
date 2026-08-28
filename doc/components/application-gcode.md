# G-code 解析与运动适配应用组件

## 1. 定位、依赖与配置

组件名 `gcode`，开关 `BANUX_GCODE_EN`，当前启用。它把文本 G-code 转成 X/Y/Z/E 四轴步数，通过路径式文件读写系统调用步进电机驱动。

依赖项：命令行系统、文件读写系统、消息订阅系统，以及已注册的 `/driver/gpio/stepper_x`、`stepper_y`、`stepper_z`、`stepper_e` 和 `stepper_group`。每毫米步数由 `GCODE_X/Y/Z/E_STEPS_PER_MM` 配置，当前默认值分别为 80、80、400、95。

## 2. 运行逻辑

### 2.1 初始化

`Gcode_Init()` 清零状态，设为绝对坐标模式和默认进给速度，通过四个单轴设备读取实际位置，然后注册 `gcode` Shell 模块和 `EVT_GCODE_STOP` 紧急停止订阅。任一必要驱动不存在都会导致组件进入 `failed`。

### 2.2 解析

解析器逐词读取 `G/M/N/X/Y/Z/E/F`，数值统一保存为 0.001 mm 或 0.001 mm/min 的定点整数。支持空格、Tab、`;` 行尾注释、`(...)` 注释、可选行号和 `*checksum` 异或校验。未知字母、重复 G/M 主码、非法小数或不支持命令会返回明确错误码。

### 2.3 运动

`G0/G1` 先根据 `G90/G91` 计算目标毫米位置，再按各轴 steps/mm 换算相对步数。四轴步数写入 `DrvStepperMoveCommand_t`，用最大距离和进给速度计算脉冲周期，经 `banux_write("/driver/gpio/stepper_group", ...)` 交给同步 DDA 驱动。完成后重新读取四轴位置。

注意：当前 `G90/G91` 同时作用于 X/Y/Z/E，没有独立的 E 轴绝对模式。需要相对挤出时，应使用 `G91 -> G1 E... -> G90` 并立即恢复。

### 2.4 文件执行与事件

`Gcode_ExecuteFile()` 每次读取 64 字节，支持 UTF-8 BOM，逐行执行；单行含终止符最多 96 字节。任意一行失败立即停止并打印行号。每条命令发布 `EVT_GCODE_COMMAND`，成功发布 `EVT_GCODE_COMPLETE`，失败发布 `EVT_GCODE_ERROR`。收到 `EVT_GCODE_STOP` 时调用 group 驱动 STOP ioctl。

## 3. 调用链

组件初始化：

```text
Banux_Init
  -> BanuxComponent_StartType(APPLICATION)
     -> Gcode_Init
        -> banux_read(stepper_x/y/z/e)
        -> Shell_RegisterModule(gcode)
        -> BG_Event_SubscribeNamed(EVT_GCODE_STOP, ...)
```

单行运动：

```text
gcode G1 X10 Y20 F1200
  -> shell_gcode_line
  -> Gcode_ExecuteLine
     -> BG_Event_Publish(EVT_GCODE_COMMAND)
     -> parse_line
     -> execute_move
        -> mm 转 steps
        -> 计算 pulseUs
        -> banux_write(stepper_group)
        -> group.write -> 同步 DDA 脉冲
        -> refresh_position
     -> BG_Event_Publish(COMPLETE/ERROR)
```

文件执行：

```text
gcode -f /sd/job.gcode
  -> Gcode_ExecuteFile
     -> banux_read_at(path, 64-byte chunk, offset)
     -> Gcode_ExecuteLine（逐行）
```

## 4. 使用方法

Shell 单行：

```text
gcode M17
gcode G90
gcode G92 X0 Y0 Z0 E0
gcode G0 X10 Y20 Z5 F1200
gcode M114
gcode M84
```

执行文件：

```text
gcode -f /sd/pcb_solder_paste.gcode
gcode -f /flash/test.gcode
gcode -s
```

锡膏单点示例：

```gcode
M17
G90
G92 X0 Y0 Z0 E0
G0 Z5 F300
G0 X10 Y20 F1200
G1 Z0.2 F300
G91
G1 E0.5 F60
G1 E-0.05 F60
G90
G0 Z5 F300
M84
```

代码调用：

```c
int result = Gcode_ExecuteLine("G0 X10 Y20 F1200");
GcodeState_t state;
Gcode_GetState(&state);
```

## 5. 支持范围与错误码

支持 `G0/G1/G90/G91/G92/M17/M18/M84/M114`。当前不支持 `G4` 驻留、圆弧、回零、加减速规划、温控和独立 E 模式。

| 返回值 | 含义 |
| --- | --- |
| `0` | 成功 |
| `-1` | 语法或参数非法 |
| `-2` | checksum 错误 |
| `-3` | 未知字母 |
| `-4` | 命令不支持 |
| `-5` | 驱动或文件读失败 |
| `-6` | 行过长 |

正式运动前必须先低速、抬高 Z、确认坐标方向和限位状态；软件坐标不能替代机械急停。

