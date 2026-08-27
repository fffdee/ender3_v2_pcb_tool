# 驱动框架系统组件

## 1. 定位与配置

驱动框架只定义通用设备模型和驱动文件系统，不包含具体硬件驱动。源码位于 `02_system_components/driver_framework`，组件名 `driver_framework`，开关 `DRV_DEVICE_EN`。实际 SD、UART、EEPROM、步进电机等驱动位于 `01_driver`。

设备使用 `DrvDevice_t` 描述，提供 `init/open/close/read/write/ioctl/deinit` 回调、总线类型、参数表和私有数据。最多注册 `DRV_DEVICE_MAX` 个设备。

## 2. 运行逻辑

1. `DrvFramework_Init()` 严格按 `Vfs_Init -> DrvFs_Init -> DrvDevice_Init` 初始化。
2. `DrvFs_Init()` 在 VFS 根目录创建 `/driver`。
3. 产品层 `BanuxDriver_RegisterAll()` 逐个调用 `DrvDevice_Register()`。
4. 注册时先执行设备 `init`；失败则不创建设备节点，也不会标记为已注册。
5. 成功后按总线创建 `/driver/<bus>`，再创建设备节点和参数子节点。
6. 注销时移除节点、调用 `deinit` 并清除注册状态。

框架不主动轮询设备；设备运行依赖中断、应用调用或平台自己的 process 回调。

## 3. 调用链

```text
Banux_Init
  -> DrvFramework_Init
     -> Vfs_Init
     -> DrvFs_Init -> 创建 /driver
     -> DrvDevice_Init
  -> config.driverInit
     -> BanuxDriver_RegisterAll
        -> DrvDevice_Register(device)
           -> device.init(privData)
           -> DrvFs_GetBusDir(bus)
           -> DrvFs_CreateDevice
           -> DrvFs_CreateParam（逐项）
```

应用访问设备：

```text
banux_write("/driver/gpio/stepper_group", data, len)
  -> Vfs_FindNode
  -> DrvDevice_t.write(privData, data, len)
```

## 4. 使用方法

定义并注册一个驱动：

```c
static int demo_init(void *priv) { return 0; }
static int demo_read(void *priv, uint8_t *buf, uint32_t len) { return 0; }

static DrvDevice_t demo = {
    .name = "demo0",
    .bus = DRV_BUS_GPIO,
    .init = demo_init,
    .read = demo_read,
    .privData = NULL
};

int BanuxDriver_RegisterAll(void)
{
    return DrvDevice_Register(&demo);
}
```

Shell 调试：

```text
drivers
ls /driver
ls /driver/gpio
cat /driver/gpio/stepper_x/position
```

新增驱动时必须放在 `01_driver`，不要把硬件寄存器、HAL 句柄或板级引脚放回驱动框架目录。

## 5. 诊断

- 注册返回失败：先看设备 `init` 返回值，再检查 VFS 节点容量和重名。
- 设备不存在：`init` 失败的设备不会挂到 `/driver`。
- 参数不可写：确认参数描述符是否提供 setter/写回调。
- `banux_*` 返回 `-4`：对应设备没有实现该操作回调。

