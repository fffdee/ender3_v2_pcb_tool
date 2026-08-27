# VFS 系统组件

## 1. 定位与配置

VFS 提供内存中的统一路径树，源码位于 `02_system_components/driver_framework/vfs`，组件名为 `vfs`，配置开关为 `VFS_EN`。它本身不读写硬件；设备节点、参数节点和文件挂载点都建立在这棵树上。

关键容量由 `banux_config.h` 控制：`VFS_MAX_NODES=128`、路径最大 64 字节、节点名最大 16 字节、单目录最多 24 个子节点、参数文本最大 32 字节。

## 2. 运行逻辑

1. `Vfs_Init()` 清空静态节点池，分配根节点 `/`，把当前目录设为根目录。
2. 驱动框架随后创建 `/driver` 及总线/设备/参数节点。
3. FatFs 适配层创建 `/sd`、`/flash` 等挂载目录，并为目录配置懒加载回调。
4. `Vfs_FindNode()` 从根目录或当前目录逐段解析路径；遇到尚未加载的动态目录时调用 `Vfs_RefreshDir()`。
5. `Vfs_Cd()` 修改全局当前目录；Shell 的相对路径和 `banux_*` 相对路径均以该目录为起点。

VFS 使用固定内存池，不在运行时调用堆分配。节点耗尽、目录子节点达到上限或名称过长都会直接返回错误。

## 3. 调用链

初始化：

```text
Banux_Init
  -> DrvFramework_Init
     -> Vfs_Init
        -> AllocNode -> InitNode("/", DIR)
        -> BanuxComponent_SetState("vfs", READY)
```

路径查询：

```text
Shell/banux_read/banux_write
  -> Vfs_FindNode(path)
     -> ParsePath
     -> 从 root 或 cwd 遍历 children
     -> 必要时 Vfs_RefreshDir
     -> 返回 VfsNode_t
```

## 4. 使用方法

应用层通常不直接操作节点，而是使用 `banux_read()`、`banux_write()` 或 Shell：

```text
ls /
cd /driver/gpio
ls
pwd
cat /driver/sdio/sd/size_kb
```

框架或挂载适配层可直接创建节点：

```c
VfsNode_t *root = Vfs_GetRoot();
VfsNode_t *dir = Vfs_CreateNode("example", VFS_NODE_DIR);
Vfs_AddChild(root, dir);
VfsNode_t *node = Vfs_FindNode("/example");
```

使用规则：绝对路径以 `/` 开头；相对路径受当前目录影响；支持 `.` 和 `..`；不要保存动态目录刷新前取得的文件子节点指针作为长期句柄。

## 5. 诊断

- `Vfs_GetRoot()==NULL`：VFS 初始化失败或尚未初始化。
- 找不到相对路径：先用 `pwd` 确认当前目录。
- 创建节点失败：检查节点池、子节点上限、名称长度和重名。
- 挂载目录存在但内容为空：检查目录加载回调和底层 FatFs 状态。

