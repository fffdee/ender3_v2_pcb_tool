# 文件读写系统组件

## 1. 定位与配置

文件读写系统向应用提供统一的路径式接口：`banux_open/close/read/read_at/write/ioctl`。组件名 `file_io`，开关 `BANUX_IO_EN`。它是应用层与 VFS、设备回调之间的分发层。

## 2. 运行逻辑

`BanuxIo_Init()` 只检查 VFS 根节点是否存在并更新组件状态。每次调用时重新使用 `Vfs_FindNode()` 查找路径，再根据节点类型分派：

- 设备节点：调用 `DrvDevice_t` 对应二进制回调。
- 参数节点：调用 `Vfs_ReadParam/Vfs_WriteParam`，数据按 UTF-8 文本处理。
- 文件节点：读取调用 `Vfs_ReadFile`；当前 `banux_write()` 不支持普通文件节点写入。

`banux_open/close` 只适用于设备节点，并维护共享的 `device->isOpened` 状态。

## 3. 调用链

```text
应用
  -> banux_read_at(path, data, len, offset)
     -> Vfs_FindNode(path)
        -> DEV   -> device.read(privData, data, len)
        -> PARAM -> Vfs_ReadParam
        -> FILE  -> Vfs_ReadFile(offset)

应用
  -> banux_ioctl(devicePath, command, argument)
     -> Vfs_FindNode -> device.ioctl(privData, command, argument)
```

## 4. 使用方法

读取设备状态：

```c
DrvStepperStatus_t status;
int count = banux_read("/driver/gpio/stepper_x", &status, sizeof(status));
```

写设备命令：

```c
DrvStepperMoveCommand_t move = {0};
move.steps[0] = 800;
move.pulseUs = 500;
if (banux_write("/driver/gpio/stepper_group", &move, sizeof(move)) < 0) {
    /* 处理错误 */
}
```

分块读取文件：

```c
char buffer[64];
int n = banux_read_at("/flash/job.txt", buffer, sizeof(buffer), offset);
```

错误码：`-1` 参数无效，`-2` 路径不存在，`-3` 节点类型错误，`-4` 操作不支持，`-5` 组件关闭，`-6` 驱动错误。成功读取返回字节数；设备写入的成功返回值由具体驱动定义。

## 5. 注意事项

- 设备数据是二进制结构，调用方和驱动必须使用相同类型与长度。
- 参数写入长度必须小于 `VFS_MAX_PARAM_LEN`。
- 普通文件创建、覆盖、删除应使用 FatFs/VFS 文件命令或文件系统适配 API，不要使用 `banux_write()`。
- `banux_read()` 等价于偏移为 0 的 `banux_read_at()`。

