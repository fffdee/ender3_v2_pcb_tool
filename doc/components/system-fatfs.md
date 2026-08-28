# FatFs 系统组件

## 1. 定位与配置

FatFs 组件名 `fatfs`，开关 `BANUX_FATFS_EN`。目录包含 Elm-Chan FatFs 核心、STM32Cube 驱动注册层、SD diskio 和板级 SDIO BSP。它提供逻辑卷；VFS 挂载适配再把卷映射为 Banux 路径。

当前卷约定：SD 为 `0:` 并挂到 `/sd`，内部 Flash 为 `1:` 并挂到 `/flash`。两者可同时存在，Flash 不是 SD 失败后的切换后端。

## 2. 运行逻辑

1. `Banux_Init()` 通过 `config.filesystemInit()` 调用 `MX_FATFS_Init()`。
2. `MX_FATFS_Init()` 调用 `FATFS_LinkDriver()` 注册 SD 驱动和 Flash 驱动，得到 `SDPath`、`FlashPath` 及返回状态。
3. SD 具体驱动注册阶段调用 `f_mount()`；FatFs 首次访问再经 `disk_initialize/read/write/ioctl` 进入 `SD_Driver`。
4. `sd_diskio` 使用 BSP/HAL SDIO 完成块操作并等待卡状态；写失败向上返回 `RES_ERROR`，FatFs 转为 `FR_DISK_ERR`。
5. VFS 适配把 FatFs 目录按需转换成 VFS 文件节点，文件操作最终回到 `f_open/f_read/f_write/f_unlink` 等 API。

## 3. 调用链

```text
Banux_Init -> MX_FATFS_Init
  -> FATFS_LinkDriver(SD_Driver)    -> 0:
  -> FATFS_LinkDriver(FLASH_Driver) -> 1:

touch /sd/a.txt
  -> Shell 文件命令 -> FatFsVfs
  -> f_open/f_close
  -> disk_write(0, ...)
  -> SD_Driver.disk_write
  -> BSP_SD_WriteBlocks -> HAL_SD_WriteBlocks
```

## 4. 使用方法

Shell：

```text
ls /sd
mkdir /sd/jobs
touch /sd/jobs/test.gcode
cat /sd/jobs/test.gcode
rm /sd/jobs/test.gcode
```

FatFs 原生 API：

```c
FIL file;
UINT written;
if (f_open(&file, "0:/job.gcode", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
    f_write(&file, data, length, &written);
    f_sync(&file);
    f_close(&file);
}
```

应用优先使用 `/sd/...` 和 `/flash/...` 的 VFS 路径；只有文件系统适配和底层诊断代码应直接依赖 `0:`、`1:`。

## 5. 诊断与维护

- `retSD/retFlash != 0`：对应 diskio 驱动未成功链接。
- `FR_NOT_READY`：检查卡检测、SD 初始化和供电。
- `FR_DISK_ERR`：检查 diskio 打印的 sector、HAL error、state 和 SDIO STA。
- 文件操作后目录乱码：优先排查块写成功判定、缓存同步和卡状态，而不是字符编码。
- CubeMX 重新生成 FatFs 后，应合并到本组件，不能在工程中同时链接第二套 FatFs。

