# 内部 Flash 文件系统组件

## 1. 定位与配置

组件名 `internal_flash_fs`，开关 `BANUX_INTERNAL_FLASH_FS_EN`，默认跟随 `BANUX_FATFS_EN`。它把 STM32 内部 Flash 的 20 KB 区域作为独立 FAT 逻辑盘 `1:`，固定挂载到根目录 `/flash`。

当前区域为 `0x0807A000..0x0807EFFF`。常规 APP 升级擦除终点在它之前，Bootloader 配置页在它之后；Keil 全片擦除仍会清空该区域。

## 2. 运行逻辑

1. `MX_FATFS_Init()` 先把内部 Flash diskio 注册为 `1:`。
2. 驱动注册完成后，`Banux_Init()` 无条件调用 `InternalFlashFs_Init()`。
3. 初始化检查 `retFlash`，然后执行 `f_mount(&FlashFatFS, FlashPath, 1)`。
4. 挂载成功后调用 `FatFsVfs_Mount("flash", 1)` 创建 `/flash`。
5. 首次为空白介质时，底层适配负责格式化/初始化；后续文件通过 FatFs 扇区读写映射到 Flash 擦写。

## 3. 调用链

```text
Banux_Init
  -> config.filesystemInit -> MX_FATFS_Init -> Link FLASH_Driver as 1:
  -> config.driverInit
  -> InternalFlashFs_Init
     -> f_mount(1:)
     -> FatFsVfs_Mount("flash", 1)
     -> /flash ready
```

## 4. 使用方法

```text
mkdir /flash/jobs
touch /flash/jobs/start.txt
vim /flash/jobs/start.txt
run /flash/jobs/start.txt
gcode -f /flash/job.gcode
```

代码中可通过 VFS 读取：

```c
char data[64];
int count = banux_read_at("/flash/config.txt", data, sizeof(data), 0);
```

适合存放配置、小型命令脚本、少量 G-code 和恢复信息。不适合高频日志、数据库式更新或持续流式写入，因为 MCU Flash 擦除寿命有限且擦除粒度较大。

## 5. 诊断

- `/flash` 不存在：检查组件状态、`retFlash`、FatFs 开关和 diskio 是否加入 Keil。
- 挂载失败：查看 `[FlashFs] f_mount failed` 的 `FRESULT`。
- 升级后数据丢失：确认升级工具没有执行全片擦除。
- 写入频繁失败：检查是否跨越保留地址、是否满足 Flash 擦除/编程对齐要求。

