# Banux 使用指南

本文档面向 Ender-3 V2 主板 APP 开发，描述当前工程中已经接入并通过编译的 Banux 功能。

- 固件版本：`1.0.18`
- Banux 框架版本：`V2.0.1`
- MCU：STM32F103RET6
- APP 起始地址：`0x08008000`
- Shell 提示符：`banux$ `

> 本文档以当前 Keil 工程实际链接内容为准。仓库中存在但未加入当前工程的代码，不等于运行时已经启用；请使用 `banux -i` 查看最终状态。

## 1. 目录结构

当前使用的主要目录如下：

```text
app/Banux/
├── 00_core/                    Banux 内核、组件管理与协作式调度
│   ├── Banux.c/.h              框架入口和应用生命周期
│   ├── banux_scheduler.c/.h    系统服务调度器
│   ├── banux_component.c/.h    系统/应用组件注册和状态管理
│   └── banux_config.h          框架裁剪配置和容量限制
├── 01_driver/                  具体硬件驱动（依赖框架）
│   ├── driver_init.c/.h        Ender-3 V2 驱动注册入口
│   ├── eeprom/                 BL24C16A EEPROM
│   ├── internal_flash/         20 KB 内部 Flash 块设备
│   ├── sd/                     SDIO 和 FatFs/VFS 挂载适配
│   ├── stepper/                X/Y/Z/E 步进电机与限位
│   ├── timer/                  SysTick 1 ms 时基
│   ├── uart/                   UART1/UART3
│   └── library/                未启用的通用及 legacy 驱动
├── 02_system_components/
│   ├── file_io/                banux_read/write/open/close/ioctl
│   ├── command_line/           Shell、命令模块和 UART IO
│   ├── command_parser/         UTF-8 脚本与非阻塞定时调度
│   ├── event/                  发布/订阅消息系统
│   ├── internal_flash_fs/      独立 20 KB Flash FAT 系统组件
│   ├── fatfs/                  FatFs 核心、Cube 适配层与 SD diskio
│   │   ├── app/                MX_FATFS_Init 和逻辑盘对象
│   │   ├── target/             STM32F1 SDIO BSP 与 ffconf
│   │   └── src/                FatFs 核心、中间件驱动与可选模块
│   └── driver_framework/       可移植驱动框架（不含具体驱动）
│       ├── core/               设备模型与驱动文件系统
│       ├── vfs/                VFS 核心
│       └── drv_init.c/.h       纯框架初始化
├── 03_application_components/  应用组件
├── 04_application/             产品应用层
└── app_version.h              产品固件版本
```

依赖方向固定为 `core -> system component API <- driver`。驱动框架不包含、也不引用任何具体硬件驱动；产品通过 `01_driver/driver_init.c` 决定实际注册哪些设备。保留的旧 Flash 驱动位于 `01_driver/library/legacy_flash`，当前 APP 不链接这些源文件。

## 2. 系统架构

Banux 的主要系统组件按职责拆分如下：

1. 文件读写系统：向应用提供 `banux_read()`、`banux_write()` 等统一接口。
2. 命令行系统：处理 UART 输入、命令注册、参数拆分和交互输出。
3. 命令行解析器：解析 UTF-8 脚本并按顺序、非阻塞地调度命令。
4. 消息订阅系统：提供事件发布/订阅接口。
5. VFS 系统：维护目录、设备、参数和文件节点。
6. 驱动框架：提供通用设备模型并挂载到 `/driver/<bus>/<device>`。
7. FatFs：提供 SD 和内部 Flash 的 FAT12/FAT16/FAT32 文件系统支持。
8. 内部 Flash 文件系统：可裁剪的 20 KB 独立逻辑卷，挂载到 `/flash`。

具体硬件驱动不是系统组件。它们位于 `01_driver`，只依赖驱动框架公开的 `DrvDevice_t`、`DrvFs_*` 和 VFS 接口。移植 Banux 到其他主板时，可以完整复用 `00_core` 与 `02_system_components`，替换 `01_driver` 即可。

框架日志同样不依赖 Shell 或 UART。主板通过 `BanuxDebug_SetWriter()` 注入日志输出函数；未注入时框架保持静默，便于在其他平台独立链接。

典型调用链：

```text
应用或 Shell
    ↓
banux_read(path, data, len)
    ↓
VFS 查找节点
    ├── 设备节点 → DrvDevice.read()
    ├── 参数节点 → Vfs_ReadParam()
    └── 文件节点 → Vfs_ReadFile()
```

## 3. 当前启动顺序

当前 Ender-3 V2 APP 只通过 core 入口启动 Banux。平台相关函数以配置回调传入，依赖顺序由 `Banux_Init()` 内部维护：

```c
HAL_Init();
SystemClock_Config();

MX_GPIO_Init();
MX_SDIO_SD_Init();
MX_USART3_Init();

MX_USART1_Init();

const BanuxConfig_t config = {
    app_log,
    ShellIO_UartAll_Get(),
    MX_FATFS_Init,
    BanuxDriver_RegisterAll,
    app_bl_init,
    app_bl_poll
};
Banux_Init(&config);

while (1) {
    Banux_Process();
}
```

必须遵守以下依赖关系：

- `BanuxComponent_Init()` 必须早于组件状态更新。
- `DrvFramework_Init()` 必须早于任何设备注册。
- `Shell_Init()` 必须早于 `Shell_RegisterAllModules()`，否则 Shell 初始化会清空模块表。
- SDIO 驱动注册成功后才会创建 `/sd` 挂载点。
- `BANUX_INTERNAL_FLASH_FS_EN` 使能后始终创建 `/flash`，不依赖 SD 是否存在。

文件系统统一使用 `app/Banux/02_system_components/fatfs` 中的 STM32Cube/Elm-Chan FatFs；工程不再保留 Banux 外部或第二套 FAT32 实现。

FatFs 使用两个相互独立的逻辑卷：SD 固定为 `0:` 并挂载 `/sd`；内部 Flash 固定为 `1:` 并挂载 `/flash`。两者可以同时使用，SD 初始化失败不会改变 Flash 的卷号或挂载路径。Flash 区域为 `0x0807A000..0x0807EFFF`，位于 APP 升级区和 Bootloader 配置页之间。内部 Flash 擦写寿命有限，只适合配置、小脚本和低频文件更新，不适合作为连续日志盘。

可通过驱动属性确认 SD 状态：

```text
cat /driver/sdio/sd/backend
cat /driver/sdio/sd/size_kb
cat /driver/sdio/sd/free_kb
```

`backend` 固定返回 `sd`。内部 Flash 组件首次使用会自动格式化；Bootloader 常规 APP 升级只擦除到 `0x08078000`，不会清除 `/flash`。使用 Keil 全片擦除下载则会清空它。

## 4. 组件管理

### 4.1 查看组件

```text
banux -i
```

输出包含：

- Banux 框架版本
- APP 固件版本
- 组件总数、启用数和 ready 数
- 系统组件
- `03_application_components` 应用组件

组件状态：

| 状态 | 含义 |
| --- | --- |
| `disabled` | 当前构建未启用 |
| `registered` | 已进入静态组件表，但尚未完成初始化 |
| `ready` | 初始化完成，可使用 |
| `failed` | 已启用，但初始化失败 |

当前组件清单：

| 类型 | 名称 | 当前状态说明 |
| --- | --- | --- |
| 系统 | `vfs` | 当前启用 |
| 系统 | `driver_framework` | 当前启用 |
| 系统 | `file_io` | 当前启用 |
| 系统 | `shell` | 当前启用 |
| 系统 | `command_parser` | 当前启用，使用 1 ms 非阻塞调度 |
| 系统 | `fatfs` | 当前启用 |
| 系统 | `event_bus` | 描述符已链接，当前配置禁用 |
| 应用 | `firmware_upgrade` | 描述符已链接，当前配置禁用 |

Bootloader/APP 现有升级通道不等同于 `03_application_components/firmware_upgrade` 组件。后者显示 `disabled` 不代表 `boot` 命令不可用。

### 4.2 新增组件

组件在自己的目录中定义描述符：

```c
#include "banux_component.h"

BANUX_COMPONENT_DEFINE(g_banux_component_example,
                       "example",
                       "1.0.0",
                       BANUX_COMPONENT_APPLICATION,
                       EXAMPLE_COMPONENT_EN,
                       "example application component");
```

组件初始化完成后更新状态：

```c
int Example_Init(void)
{
    int ret = example_hardware_init();

    BanuxComponent_SetState("example", ret == 0
                            ? BANUX_COMPONENT_READY
                            : BANUX_COMPONENT_FAILED);
    return ret;
}
```

最后完成三项接入：

1. 将组件源文件加入 Keil 工程。
2. 在 `00_core/banux_component.c` 中使用 `BANUX_COMPONENT_DECLARE()` 声明描述符。
3. 将描述符地址加入 `g_static_components[]`。

组件名必须唯一，描述符和名称字符串必须具有静态生命周期。

## 5. VFS 与驱动路径

### 5.1 节点类型

VFS 支持以下主要节点：

| 节点 | 用途 |
| --- | --- |
| 目录 | 组织路径 |
| 设备 | 关联 `DrvDevice_t` |
| 参数 | 设备的文本属性，可只读或读写 |
| 文件 | 真实文件系统文件，例如 SD 卡文件 |

### 5.2 当前路径树

设备按总线挂载：

```text
/
├── driver/
│   ├── gpio/
│   │   ├── stepper_x/
│   │   ├── stepper_y/
│   │   ├── stepper_z/
│   │   └── stepper_e/
│   ├── i2c/
│   │   └── eeprom/
│   ├── sdio/
│   │   └── sd/
│   └── uart/
│       ├── uart1/
│       └── uart3/
└── sd/                         SD 卡 FatFs 挂载点
```

总线目录按需创建，没有设备时可能不会出现在目录树中。

### 5.3 VFS 限制

默认限制由 `00_core/banux_config.h` 控制：

| 配置 | 默认值 |
| --- | ---: |
| `VFS_MAX_PATH_LEN` | 64 |
| `VFS_MAX_NAME_LEN` | 16 |
| `VFS_MAX_CHILDREN` | 24 |
| `VFS_MAX_PARAM_LEN` | 32 |
| `VFS_MAX_NODES` | 128 |
| `DRV_DEVICE_MAX` | 16 |

这些限制使用静态内存，不依赖 `malloc`。增加数值会增加 SRAM 占用。

## 6. 路径式 IO API

包含统一头文件：

```c
#include "Banux.h"
```

也可以只包含：

```c
#include "banux_io.h"
```

### 6.1 API

```c
int banux_open(const char *path);
int banux_close(const char *path);
int banux_read(const char *path, void *data, uint32_t len);
int banux_write(const char *path, const void *data, uint32_t len);
int banux_ioctl(const char *path, uint32_t command, void *argument);
```

该 API 使用路径而不是 Linux 文件描述符：

- 设备节点：传递二进制数据，调用驱动 `read/write/ioctl`。
- 参数节点：传递文本值。
- 文件节点：`banux_read()` 从偏移 0 开始读取。
- 目录节点：返回类型错误。
- `open/close` 只作用于设备节点；没有回调的驱动视为无需额外打开。
- `banux_read/write` 当前不强制要求先调用 `banux_open()`。

当前通用路径 API 不支持文件节点写入，也不支持文件偏移读写。SD 文件写入请使用 `SdFs_WriteFile()`；分块读取请使用 `Vfs_ReadFile()`。

### 6.2 返回值

成功时：

- `read/write` 返回实际处理字节数。
- `open/close/ioctl` 通常返回 `0`。

框架错误码：

| 错误码 | 名称 | 含义 |
| ---: | --- | --- |
| `-1` | `BANUX_IO_ERR_INVALID` | 参数、长度或缓冲区无效 |
| `-2` | `BANUX_IO_ERR_NOT_FOUND` | 路径不存在 |
| `-3` | `BANUX_IO_ERR_WRONG_TYPE` | 节点类型不适用 |
| `-4` | `BANUX_IO_ERR_NOT_SUPPORTED` | 驱动没有对应操作，或参数只读 |
| `-5` | `BANUX_IO_ERR_DISABLED` | `BANUX_IO_EN=0` |
| `-6` | `BANUX_IO_ERR_DRIVER` | 底层参数或驱动操作失败 |

设备驱动自己的负返回值可能直接向上传递，调用方应将所有负值视为失败。

### 6.3 参数读写示例

```c
char value[16];
int len;

len = banux_read("/driver/gpio/stepper_x/limit", value, sizeof(value));
if (len >= 0) {
    /* value 为 "0" 或 "1" */
}

banux_write("/driver/gpio/stepper_x/pulse_us", "500", 3);
```

参数写入长度不应包含无关尾部数据。框架会复制数据并补 `\0`，最大长度受 `VFS_MAX_PARAM_LEN` 限制。

### 6.4 二进制设备读写示例

```c
DrvStepperStatus_t status;
DrvStepperCommand_t command;
int ret;

ret = banux_read("/driver/gpio/stepper_x", &status, sizeof(status));

command.steps = 100;
command.pulseUs = 500;
ret = banux_write("/driver/gpio/stepper_x", &command, sizeof(command));
```

设备读写使用二进制结构体，不要把 Shell 字符串直接写到设备节点。

## 7. Shell 使用

### 7.1 基本信息

Shell 同时使用 UART1 和 UART3：

- UART1：2,000,000 baud
- UART3：115,200 baud
- 提示符：`banux$ `
- 最大命令行：128 字节（含结尾空间，实际输入最多 127 字节）
- 最大参数数量：15
- 历史记录：10 条

常用帮助：

```text
help -a
help -m sys
help -m banux
help -l
help -h
```

### 7.2 命令表

| 命令 | 用途 |
| --- | --- |
| `sys -i` | MCU、时钟和 IO 信息 |
| `sys -m` | 内存配置 |
| `sys -v` | 固件版本 |
| `sys -r` | 软件复位 |
| `banux -i` | Banux 版本和组件状态 |
| `ls [path]` | 列目录 |
| `pwd` | 当前路径 |
| `cd [path]` | 切换路径，无参数回根目录 |
| `cat <path>` | 读取参数或文件 |
| `echo <path> <value>` | 写参数 |
| `echo <value> > <path>` | 重定向形式写参数 |
| `tree` | 显示驱动/VFS 树 |
| `drivers` | 显示已注册设备 |
| `touch <file...>` | 创建 SD 文件 |
| `mkdir <dir...>` | 创建 SD 目录 |
| `rm <path...>` | 删除 SD 文件或空目录 |
| `vim <file>` | 编辑 SD 文本文件 |
| `run <file>` | 启动非阻塞 UTF-8 命令脚本 |
| `delay <ms>` | 脚本内非阻塞等待后再执行下一行 |
| `boot` | 写入 Bootloader 标志并复位 |

### 7.3 UTF-8 脚本

脚本每行是一条 Shell 命令：

```text
sys -v
banux -i
cat /driver/i2c/eeprom/detected
echo /driver/gpio/stepper_x/pulse_us 500
delay 1000
echo /driver/gpio/stepper_x/steps 200
```

执行：

```text
run /sd/startup.txt
```

规则：

- 支持 UTF-8 和 UTF-8 BOM。
- 支持 `LF`、`CRLF` 和 `CR` 换行。
- 空行会跳过。
- 每行最长 127 字节。
- 嵌套 `run` 最大深度为 2。
- UTF-8 非法、命令未知或命令返回失败时立即停止，并报告行号。
- `run` 启动后立即返回，主循环每轮最多执行一条有效命令。
- `delay` 使用 SysTick/HAL 的 1 ms 硬件时基，不调用 `HAL_Delay()`，UART、升级监听和其他主循环任务保持运行。
- 单次 `delay` 范围为 `0` 到 `2147483647` ms；`delay` 只能在脚本执行期间使用。

定时脚本示例：

```text
echo /driver/gpio/stepper_x/enable 1
echo /driver/gpio/stepper_x/steps 200
delay 1000
echo /driver/gpio/stepper_x/steps -200
echo /driver/gpio/stepper_x/enable 0
```

1 ms 时基同时注册为标准驱动：

```text
/driver/timer/timer1ms/now_ms
```

可使用 `cat /driver/timer/timer1ms/now_ms` 查看启动后的毫秒计数，也可通过 `banux_read()` 读取设备节点获得原始 `uint32_t` 计数。

### 7.4 文本编辑器

`vim` 是轻量级串口编辑器，最大缓冲区 2048 字节。进入后支持：

```text
:p       显示内容
:a       进入追加模式
i        进入追加模式
:c       清空缓冲区
:d N     删除第 N 行
:w       写入
:q       退出
:q!      放弃修改退出
:wq      保存并退出
```

## 8. EEPROM 驱动

### 8.1 硬件

- 型号：BL24C16A
- 容量：2048 字节
- 页大小：16 字节
- SDA：PA11
- SCL：PA12
- 软件 I2C
- WP 接地

驱动支持总线恢复、ACK 探测、跨 256 字节块读取、分页写入和写完成 ACK 轮询。

### 8.2 VFS 路径

```text
/driver/i2c/eeprom/
├── type
├── size
├── page_size
├── address
├── value
├── detected
└── erase
```

Shell 示例：

```text
cat /driver/i2c/eeprom/detected
cat /driver/i2c/eeprom/type
echo /driver/i2c/eeprom/address 0
echo /driver/i2c/eeprom/value 90
cat /driver/i2c/eeprom/value
```

擦除整片 EEPROM：

```text
echo /driver/i2c/eeprom/erase 1
```

擦除会把全部 2048 字节写为 `0xFF`，会消耗 EEPROM 写入寿命。

应用层二进制访问：

```c
uint8_t data[16];

DrvEeprom_Read(0, data, sizeof(data));
DrvEeprom_Write(0, data, sizeof(data));
```

也可以先写 `address` 属性，再对 `/driver/i2c/eeprom` 使用 `banux_read/write`。

## 9. 步进电机驱动

### 9.1 引脚

| 轴 | STEP | DIR | 限位 |
| --- | --- | --- | --- |
| X | PB9 | PC2 | PC0 |
| Y | PB7 | PB8 | PC1 |
| Z | PB5 | PB6 | PA15 |
| E | PB3 | PB4 | 无 |

四路驱动共用 `PC3` 低电平使能。任意轴修改 `enable` 都会影响全部四路电机。

PB3、PB4 和 PA15 原本属于 JTAG 相关引脚。驱动初始化会关闭 JTAG 并保留 SWD 调试。

### 9.2 属性

四个轴都有：

```text
enable dir pulse_us steps position step_pin dir_pin en_pin
```

X/Y/Z 额外提供：

```text
limit limit_raw limit_active limit_pin
```

含义：

| 属性 | 含义 |
| --- | --- |
| `enable` | 共享使能，`1` 启用全部电机 |
| `dir` | DIR 原始输出电平 |
| `pulse_us` | STEP 高、低电平各自持续时间，范围 2～100000 μs |
| `steps` | 写入带符号步数，正负号决定方向 |
| `position` | 软件累计位置，可读写清零或校准 |
| `limit` | 按配置换算后的触发状态，`1` 表示触发 |
| `limit_raw` | GPIO 原始电平 |
| `limit_active` | 当前有效电平配置，`high` 或 `low` |

### 9.3 Shell 操作

```text
echo /driver/gpio/stepper_x/enable 1
echo /driver/gpio/stepper_x/pulse_us 500
echo /driver/gpio/stepper_x/steps 100
cat /driver/gpio/stepper_x/position
echo /driver/gpio/stepper_x/enable 0
```

限位检查：

```text
cat /driver/gpio/stepper_x/limit_pin
cat /driver/gpio/stepper_x/limit_active
cat /driver/gpio/stepper_x/limit_raw
cat /driver/gpio/stepper_x/limit
```

默认 `STEPPER_LIMIT_ACTIVE_HIGH=1`，适配 Creality 常见 NC 接法。如果实测按下开关后 `limit` 逻辑相反，在板级构建配置中定义：

```c
#define STEPPER_LIMIT_ACTIVE_HIGH 0
```

### 9.4 安全限制

- 上电初始化时 STEP 拉低，共享 EN 拉高，电机默认禁用。
- 单次命令最多 100000 步。
- 单次阻塞运动最长 5 秒。
- 当前脉冲生成是阻塞式软件延时，不适合多轴同步插补。
- 当前限位只提供状态，不会自动停止步进命令。
- 操作前应先确认机械方向和限位状态。

## 10. SDIO 与 SD 文件系统

### 10.1 设备节点

```text
/driver/sdio/sd/
├── detected
├── type
├── size_mb
├── free_mb
├── mount
└── block
```

```text
cat /driver/sdio/sd/detected
cat /driver/sdio/sd/type
cat /driver/sdio/sd/size_mb
cat /driver/sdio/sd/free_mb
cat /driver/sdio/sd/mount
```

启动日志会输出采样边沿和总线宽度：

```text
[SDIO] mounted ok, ..., edge=rising, bus=4-bit
```

`block` 控制设备节点二进制读写的原始扇区号。原始块写入会绕过 FatFs，可能破坏分区、FAT 或文件数据，除非明确知道目标扇区，否则不要写 `/driver/sdio/sd`。

### 10.2 文件操作

SD 挂载成功后使用 `/sd`：

```text
ls /sd
mkdir /sd/config
touch /sd/config/startup.txt
vim /sd/config/startup.txt
cat /sd/config/startup.txt
run /sd/config/startup.txt
rm /sd/config/startup.txt
```

内部 Flash 组件使能后可同时使用 `/flash`：

```text
ls /flash
mkdir /flash/config
touch /flash/config/startup.txt
vim /flash/config/startup.txt
cat /flash/config/startup.txt
rm /flash/config/startup.txt
```

`rm` 删除目录时要求目录为空。不要删除当前工作目录或它的父目录。

## 11. UART 驱动

```text
/driver/uart/uart1/
├── baud
└── rx_avail

/driver/uart/uart3/
├── baud
└── rx_avail
```

```text
cat /driver/uart/uart1/baud
cat /driver/uart/uart1/rx_avail
cat /driver/uart/uart3/baud
```

UART 设备节点支持二进制 `banux_read/write`。Shell 和升级协议也使用这些串口，应用直接读取可能与 Shell/升级数据竞争，应由上层统一管理访问时机。

## 12. 新增驱动

### 12.1 定义驱动操作

```c
static int example_init(void *priv)
{
    return 0;
}

static int example_read(void *priv, uint8_t *buf, uint32_t len)
{
    /* 返回实际读取字节数，失败返回负数 */
    return 0;
}

static int example_write(void *priv, const uint8_t *buf, uint32_t len)
{
    /* 返回实际写入字节数，失败返回负数 */
    return (int)len;
}
```

### 12.2 定义参数

```c
static int get_status(char *buf, uint16_t maxLen, void *userData)
{
    return snprintf(buf, maxLen, "%u", 1u);
}

static const FsParamDef_t example_params[] = {
    FS_PARAM_DEF("status", "device status", get_status, NULL),
    FS_PARAM_END
};
```

### 12.3 定义并注册设备

```c
static DrvDevice_t example_device = {
    .name = "example",
    .desc = "example device",
    .bus = DRV_BUS_GPIO,
    .init = example_init,
    .deinit = NULL,
    .open = NULL,
    .close = NULL,
    .read = example_read,
    .write = example_write,
    .ioctl = NULL,
    .params = example_params,
    .privData = NULL,
};

int Example_Register(void)
{
    return DrvDevice_Register(&example_device);
}
```

注册后路径为：

```text
/driver/gpio/example
/driver/gpio/example/status
```

每个可读写设备应提供标准 `read/write` 回调。天然只读的设备可以将 `write=NULL`，此时 `banux_write()` 返回 `BANUX_IO_ERR_NOT_SUPPORTED`。

## 13. 功能裁剪

主要配置位于 `00_core/banux_config.h`。建议通过板级头文件或编译器宏覆盖默认值，不要在多个源文件重复定义。

```c
#define VFS_EN                    1
#define DRV_DEVICE_EN             1
#define BANUX_IO_EN               1
#define COMMAND_PARSER_EN         1
#define TIMER1MS_EN               COMMAND_PARSER_EN
#define STEPPER_LIMIT_ACTIVE_HIGH 1
```

依赖关系：

- `BANUX_IO_EN` 依赖 VFS 路径查找和驱动框架。
- 关闭 `VFS_EN` 后，驱动路径、Shell 文件命令和路径 IO 均不可正常工作。
- 关闭 `BANUX_IO_EN` 后，API 保留但返回 `BANUX_IO_ERR_DISABLED`。
- 关闭 `COMMAND_PARSER_EN` 后，`run`、`delay` 和异步解析器为空实现。
- `TIMER1MS_EN` 默认跟随 `COMMAND_PARSER_EN`，也可以单独启用 1 ms 驱动。
- `banux -i` 中的组件状态由组件描述符和运行时初始化共同决定。

当前 `00_core/banux_config.h` 还保留了一些其他产品使用的通用开关。是否真正启用应同时检查 Keil 工程源文件和 `banux -i`，不要只根据宏名判断。

## 14. 版本管理

APP 固件版本定义：

```text
app/Banux/app_version.h
```

Banux 框架版本定义：

```text
app/Banux/00_core/banux_component.h
```

查看版本：

```text
sys -v
banux -i
```

版本建议：

- 修复实现但不改变接口：增加修订号。
- 新增兼容接口或组件：增加次版本号。
- 破坏现有 API/ABI：增加主版本号。
- 每次发布前同步更新固件版本，并重新生成 BIN/HEX。

## 15. 故障排查

### 15.1 `banux -i` 显示 `registered`

组件描述符已注册，但初始化函数没有执行或没有调用 `BanuxComponent_SetState()`。

### 15.2 `banux -i` 显示 `failed`

检查该组件之前的启动日志。VFS、FatFs 和硬件设备初始化失败都会保留对应错误信息。

### 15.3 路径不存在

```text
tree
drivers
ls /driver
```

设备只有在初始化和 `DrvDevice_Register()` 成功后才会出现在 VFS。

### 15.4 SD 卡未挂载

检查：

```text
cat /driver/sdio/sd/detected
cat /driver/sdio/sd/mount
```

如果 `/driver/sdio/sd` 本身不存在，说明 SDIO 初始化失败，设备按设计不会注册。结合启动日志中的 `hsd.ErrorCode`、`STA`、采样边沿和总线宽度定位。

### 15.5 SD 文件操作后复位或死机

当前工程为 FatFs 调用预留 `0x1000` 字节主栈，并把扇区缓冲、`FIL`、`DIR` 和 `FILINFO` 放在静态工作区。VFS 动态目录只在首次访问时枚举；`touch`、`mkdir`、`vim` 和 `rm` 成功后只增量更新对应节点，不再清空整棵子树。脚本解析器也会按路径重新取得文件节点，避免目录刷新后继续使用失效指针。

SD diskio 对所有读写使用 4 字节对齐的 512 字节中转缓冲，并逐扇区调用 STM32 HAL。Polling 写扇区期间只暂停 UART1/UART3 中断，避免 2 Mbaud 串口抢占造成 SDIO TX FIFO 下溢，SysTick 仍保持运行。SDIO 固定使用 rising 采样沿和较低总线时钟，不再因瞬态读错在运行中切换边沿。部分卡会在数据实际写入后让 STM32 SDIO 报数据 CRC 错误；驱动会等待卡就绪并回读比较该扇区，内容一致时按成功处理。目录枚举会拒绝非法 8.3 项并输出 `[FatFsVfs] skipped corrupt directory entry` 诊断；该日志表示介质上已有坏目录项，需要在电脑上运行 FAT 修复或重新格式化。

建议依次验证：

```text
ls /sd
mkdir /sd/kid
touch /sd/kid/a.txt
vim /sd/kid/a.txt
cat /sd/kid/a.txt
rm /sd/kid/a.txt
rm /sd/kid
```

### 15.6 限位状态相反

先对比按下前后的原始电平：

```text
cat /driver/gpio/stepper_x/limit_raw
```

然后调整 `STEPPER_LIMIT_ACTIVE_HIGH`，不要在应用层到处手工取反。

### 15.7 `banux_write()` 返回 `-4`

目标参数是只读属性、目标设备没有 `write` 回调，或者当前节点类型不支持写入。

### 15.8 Shell 脚本停止

`run` 使用遇错即停策略。错误信息会给出行号、返回码和命令内容。先在交互 Shell 单独执行失败行，再修正脚本。

### 15.9 `delay` 返回错误

`delay` 是脚本调度指令，不能直接在空闲的交互命令行中等待。请把它放在由 `run` 执行的 UTF-8 文件中；这项限制用于避免交互 Shell 产生含义不清的后台等待。

## 16. 发布检查清单

1. `sys -v` 输出预期固件版本。
2. `banux -i` 中启用组件均为 `ready`。
3. `drivers` 中 EEPROM、四路步进、SD 和 UART 状态符合预期。
4. X/Y/Z 限位按下和释放时 `limit_raw` 会变化，`limit` 语义正确。
5. SD 卡能挂载，并能执行 `touch/mkdir/vim/run/delay/rm`。
6. EEPROM 能完成一个地址的写入和回读。
7. 步进测试前确认共享使能和运动方向，测试后关闭 `enable`。
8. ARMCC 编译和 ARMLINK 完整链接无错误、无警告。
9. 重新生成 `.bin` 和 `.hex` 发布产物。
10. 定时脚本等待期间 UART 输入和 `app_bl_poll()` 仍能正常处理。
