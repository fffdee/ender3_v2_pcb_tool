# 固件升级应用组件

## 1. 当前状态与适用边界

组件名 `firmware_upgrade`。当前 `component_manifest.c` 的 enabled 参数固定为 `0`，因此 `banux -i` 应显示 `disabled`，`BanuxComponent_StartType()` 不会调用它。

该目录来自另一套 Bootloader、分区和 Flash 模型：代码使用 `PART_A_BASE=0x00040000`、外部 `spi_flash` 接口、CDC/BLE 升级通道以及 2 MB/8 MB 运行时布局。当前 Ender-3 V2 STM32F103 APP 起始地址和内部 Flash 布局不同。禁止仅把 enabled 改为 1 后直接使用，否则可能擦除错误地址或破坏 Bootloader。

当前主板实际可用的 `boot`、`ENTER_BOOT` 和 APP/Bootloader 升级通路与此组件不是同一套实现。

## 2. 运行逻辑（目标平台设计）

在目标平台完成移植后，设计流程如下：

1. 早期启动调用 `FwUpgrade_BootInit()`，检测 Flash 容量、计算 A/B 分区和标志地址，并执行启动分区判断。
2. APP 基础功能就绪后调用 `FwUpgrade_ConfirmBootSuccess()` 清除启动失败计数。
3. `FwUpgrade_Init()` 初始化共享升级状态机和 CDC 桥。
4. 主机发送 `SYNC/QUERY/START/DATA/FINISH` 数据包；状态机校验协议、大小和 CRC，擦除并写入目标分区。
5. `FINISH` 校验固件有效标志并更新活动分区，调用方随后复位。
6. 连续启动失败达到 `BOOT_FAIL_MAX=3` 时，Boot 决策逻辑用于回退。
7. `FwUpgrade_RebootToBootloader()` 写一次性 stay flag 后系统复位，Bootloader 读取并清除该标志。

## 3. 调用链

预期启动链：

```text
main early boot
  -> FwUpgrade_BootInit
     -> DualPart_Init
     -> Boot_CheckAndJump

application init
  -> FwUpgrade_ConfirmBootSuccess -> Boot_ConfirmSuccess
  -> FwUpgrade_Init
     -> App_Upgrade_Init
     -> CDC_Upgrade_Init
```

CDC 升级链：

```text
main loop
  -> FwUpgrade_CheckCdcEnter
     -> CDC_Upgrade_CheckEnter（检测 0xAA SOF）
  -> FwUpgrade_ProcessCdc
     -> CDC_Upgrade_Process
     -> App_Upgrade_ProcessChannel
     -> packet parser/state machine
     -> SpiFlashErase/SpiFlashWrite
     -> FINISH -> reset
```

BLE 链：

```text
BLE GATT RX -> App_OTA_OnData -> App_OTA_Process
  -> 同一 App_Upgrade 协议状态机
```

## 4. 使用方法（仅限移植完成后）

```c
FwUpgrade_BootInit();

/* 基础硬件初始化成功后 */
FwUpgrade_ConfirmBootSuccess();
FwUpgrade_Init();

while (1) {
    if (FwUpgrade_InCdcMode()) {
        FwUpgrade_ProcessCdc();
    } else {
        (void)FwUpgrade_CheckCdcEnter();
        /* normal process */
    }
}
```

查询布局：

```c
FwUpgradeInfo_t info;
if (FwUpgrade_GetInfo(&info)) {
    /* flags valid */
}
```

进入 Bootloader 的 API `FwUpgrade_RebootToBootloader()` 成功后不会返回。

## 5. 在当前工程启用前必须完成

1. 重新定义并审核 STM32F103 的 Bootloader、APP、备用区、配置页和 `/flash` 20 KB 区域，确保互不重叠。
2. 用 STM32 内部 Flash HAL 替换 `spi_flash` 地址和擦写接口，或明确接入真实外部 Flash。
3. 校验向量表重定位、链接脚本、固件有效标志位置和复位跳转。
4. 对齐现有 Bootloader 的协议和配置结构，避免同时维护两套活动分区标志。
5. 在断电、CRC 错误、越界包、重复包和连续启动失败场景下完成台架测试。
6. 最后才把组件描述符 enabled 改为配置宏，并接入 `init/process` 生命周期。

未经上述移植和验证，不应把本组件作为当前产品的升级使用说明。
