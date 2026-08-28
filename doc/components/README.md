# Banux 组件详细文档

本文档集以当前 `app/Banux` 源码为准，覆盖所有已进入静态组件注册表的系统组件和应用组件。每份文档固定说明运行逻辑、调用链和使用方法。

## 组件总调用关系

```text
main
  -> Banux_Init(config)
     -> BanuxComponent_Init()
     -> config.platformInit()
     -> config.filesystemInit() -> MX_FATFS_Init()
     -> BG_Event_Init()
     -> DrvFramework_Init()
        -> Vfs_Init() -> DrvFs_Init() -> DrvDevice_Init()
     -> BanuxIo_Init()
     -> config.driverInit() -> BanuxDriver_RegisterAll()
     -> InternalFlashFs_Init()
     -> Shell_Init() -> Shell_SetIO() -> Shell_RegisterAllModules()
     -> CommandParser_Init()
     -> BanuxComponent_StartType(APPLICATION)
        -> Gcode_Init()（当前启用）
        -> firmware_upgrade（当前禁用）

main loop
  -> Banux_Process()
     -> platformProcess()
     -> BanuxComponent_ProcessAll()
     -> BanuxScheduler_Process()
        -> Shell_Process()
        -> CommandParser_Process()
```

## 系统组件

| 组件 | 文档 | 配置开关 |
| --- | --- | --- |
| VFS | [VFS 系统](system-vfs.md) | `VFS_EN` |
| 驱动框架 | [驱动框架](system-driver-framework.md) | `DRV_DEVICE_EN` |
| 文件读写 | [文件读写系统](system-file-io.md) | `BANUX_IO_EN` |
| 命令行 | [命令行系统](system-command-line.md) | 当前描述符固定启用 |
| 命令解析器 | [命令解析器](system-command-parser.md) | `COMMAND_PARSER_EN` |
| 消息订阅 | [消息订阅系统](system-event.md) | `BG_EVENT_EN` |
| FatFs | [FatFs 系统](system-fatfs.md) | `BANUX_FATFS_EN` |
| 内部 Flash FS | [内部 Flash 文件系统](system-internal-flash-fs.md) | `BANUX_INTERNAL_FLASH_FS_EN` |

## 应用组件

| 组件 | 文档 | 配置开关/状态 |
| --- | --- | --- |
| G-code | [G-code 解析与运动适配](application-gcode.md) | `BANUX_GCODE_EN`，当前启用 |
| 固件升级 | [固件升级](application-firmware-upgrade.md) | 描述符当前固定为禁用 |

## 通用组件状态检查

```text
banux -i
```

状态含义：`disabled` 未编译启用，`registered` 已注册未就绪，`ready` 初始化成功，`failed` 初始化失败。组件目录存在不代表当前固件已经启用，最终以该命令和 `banux_config.h` 为准。

