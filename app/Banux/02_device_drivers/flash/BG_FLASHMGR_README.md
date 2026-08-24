# BG_FlashMgr - Flash管理器应用层接口

## 概述

BG_FlashMgr 是一个高层次的Flash管理接口，模仿 `bg_flash_manager` 的设计风格，提供简单易用的API来操作Flash存储设备。

### 特性

- ✅ **简单易用** - 类似 `bg_flash_manager` 的函数指针结构体设计
- ✅ **分区管理** - 支持System、Looper、Storage三个分区
- ✅ **线程安全** - 内置互斥锁保护
- ✅ **自动对齐** - 自动处理页/扇区/块对齐
- ✅ **错误处理** - 统一的错误码定义
- ✅ **状态查询** - 实时设备状态和空间统计
- ✅ **调试友好** - 丰富的日志和测试接口
- ✅ **Shell集成** - 完整的Shell命令支持

## 已集成到工程 ✅

BG_FlashMgr已成功集成到你的工程中：

### 1. 初始化位置
- **文件**: `src/main.c`
- **位置**: `EffectTask()` 函数中
- **代码**: 
  ```c
  /* Initialize BG_FlashMgr */
  if (BG_FlashMgr.Init() == BG_FLASH_OK) {
      DBG("[Main] BG_FlashMgr initialized successfully\n");
      BG_FlashMgr.PrintInfo();
  } else {
      DBG("[Main] BG_FlashMgr init failed!\n");
  }
  ```

### 2. Shell命令已添加
- **文件**: `src/BanGUI/base_func/bg_shell_commands.c`
- **模块**: `flash`
- **可用命令**:
  ```
  flash -i          显示Flash信息
  flash -s          显示Flash状态
  flash -t [dev]    测试设备(0或1)
  flash -r <off> <len>  从Looper读取数据
  flash -e <offset> 擦除Looper扇区
  flash -f [dev]    格式化设备
  ```

## 架构层次

```
┌─────────────────────────────────────┐
│  BG_FlashMgr (应用层接口)            │  ← 你在这里
│  - ReadLooper/WriteLooper          │
│  - ReadStorage/WriteStorage        │
│  - 线程安全保护                     │
├─────────────────────────────────────┤
│  flash_devices (设备注册层)         │
│  - FlashPartition_xxx()            │
│  - CS引脚控制                       │
├─────────────────────────────────────┤
│  flash_bus (总线管理层)             │
│  - 设备注册/遍历                    │
│  - Shell命令支持                    │
├─────────────────────────────────────┤
│  flash_nor_w25qxx (驱动层)         │
│  - Read/Write/Erase               │
│  - JEDEC ID识别                    │
├─────────────────────────────────────┤
│  SPI Flash 硬件                    │
│  - W25Q64 x 2                      │
└─────────────────────────────────────┘
```

## 分区布局

### Flash #0 (8MB, CS=GPIOA21)
```
+--------------------------------+
| System Partition (1MB)         | 0x000000 - 0x0FFFFF
|   - 固件/配置                  |
+--------------------------------+
| Looper Partition (7MB)         | 0x100000 - 0x7FFFFF
|   - 音频循环数据               |
+--------------------------------+
```

### Flash #1 (8MB, CS=GPIOA23)
```
+--------------------------------+
| Storage Partition (8MB)        | 0x000000 - 0x7FFFFF
|   - 文件存储                   |
+--------------------------------+
```

## 快速开始

### 在代码中使用

```c
#include "BG_FlashMgr.h"

void your_function(void)
{
    uint8_t buffer[256];
    
    // 检查Flash是否就绪
    if (!BG_FlashMgr.IsReady()) {
        return;
    }
    
    // Looper分区读写
    BG_FlashMgr.EraseLooperSector(0);      // 擦除扇区
    BG_FlashMgr.WriteLooper(0, buffer, 256); // 写入数据
    BG_FlashMgr.ReadLooper(0, buffer, 256);  // 读取数据
    
    // Storage分区读写
    BG_FlashMgr.ReadStorage(0, buffer, 256);
    BG_FlashMgr.WriteStorage(0, buffer, 256);
}
```

### 使用Shell命令

连接Shell后可以使用以下命令：

```bash
# 显示Flash信息
flash -i

# 显示详细状态
flash -s

# 测试Flash设备
flash -t 0    # 测试设备0(System Flash)
flash -t 1    # 测试设备1(Storage Flash)

# 读取数据
flash -r 0x0 64    # 从Looper偏移0读取64字节

# 擦除扇区
flash -e 0x1000    # 擦除偏移0x1000处的扇区

# 格式化(危险!)
flash -f 0    # 格式化Looper分区
flash -f 1    # 格式化Storage分区
```

## API参考

### 初始化

| 函数 | 说明 |
|------|------|
| `BG_FlashMgr.Init()` | 初始化Flash管理器 |
| `BG_FlashMgr.DeInit()` | 反初始化 |
| `BG_FlashMgr.IsReady()` | 检查是否就绪 |

### System分区 (1MB)

| 函数 | 说明 |
|------|------|
| `BG_FlashMgr.ReadSystem(offset, buf, size)` | 读取数据 |
| `BG_FlashMgr.WriteSystem(offset, data, size)` | 写入数据 |
| `BG_FlashMgr.EraseSystemSector(offset)` | 擦除扇区(4KB) |

### Looper分区 (7MB)

| 函数 | 说明 |
|------|------|
| `BG_FlashMgr.ReadLooper(offset, buf, size)` | 读取数据 |
| `BG_FlashMgr.WriteLooper(offset, data, size)` | 写入数据 |
| `BG_FlashMgr.EraseLooperSector(offset)` | 擦除扇区(4KB) |
| `BG_FlashMgr.EraseLooperBlock(offset)` | 擦除块(64KB) |
| `BG_FlashMgr.EraseLooperAll()` | 擦除整个分区 |

### Storage分区 (8MB)

| 函数 | 说明 |
|------|------|
| `BG_FlashMgr.ReadStorage(offset, buf, size)` | 读取数据 |
| `BG_FlashMgr.WriteStorage(offset, data, size)` | 写入数据 |
| `BG_FlashMgr.EraseStorageSector(offset)` | 擦除扇区(4KB) |
| `BG_FlashMgr.EraseStorageBlock(offset)` | 擦除块(64KB) |
| `BG_FlashMgr.EraseStorageAll()` | 擦除整个分区 |

### 状态查询

| 函数 | 说明 |
|------|------|
| `BG_FlashMgr.GetStatus(&status)` | 获取详细状态 |
| `BG_FlashMgr.GetLooperFreeSpace()` | 获取Looper空闲空间 |
| `BG_FlashMgr.GetStorageFreeSpace()` | 获取Storage空闲空间 |
| `BG_FlashMgr.PrintInfo()` | 打印设备信息 |

### 测试与调试

| 函数 | 说明 |
|------|------|
| `BG_FlashMgr.TestDevice(id)` | 测试指定设备 |
| `BG_FlashMgr.Format(id)` | 格式化分区 |

## 错误码

| 错误码 | 值 | 说明 |
|--------|-----|------|
| `BG_FLASH_OK` | 0 | 成功 |
| `BG_FLASH_ERROR` | -1 | 一般错误 |
| `BG_FLASH_ERROR_PARAM` | -2 | 参数错误 |
| `BG_FLASH_ERROR_NOT_INIT` | -3 | 未初始化 |
| `BG_FLASH_ERROR_ERASE` | -4 | 擦除失败 |
| `BG_FLASH_ERROR_WRITE` | -5 | 写入失败 |
| `BG_FLASH_ERROR_READ` | -6 | 读取失败 |
| `BG_FLASH_ERROR_VERIFY` | -7 | 验证失败 |
| `BG_FLASH_ERROR_TIMEOUT` | -8 | 超时 |
| `BG_FLASH_ERROR_BUSY` | -9 | 设备忙 |

## 使用注意事项

### ⚠️ 写入前必须擦除

Flash存储器的特性决定了**写入前必须先擦除**：

```c
// ✅ 正确
BG_FlashMgr.EraseLooperSector(0);      // 先擦除
BG_FlashMgr.WriteLooper(0, data, 256); // 后写入

// ❌ 错误
BG_FlashMgr.WriteLooper(0, data, 256); // 直接写入会失败
```

### 📏 地址对齐

- **扇区擦除**: 地址自动对齐到4KB边界
- **块擦除**: 地址自动对齐到64KB边界
- **页写入**: 自动处理页(256B)边界跨越

### 🔒 线程安全

所有API都使用互斥锁保护，可以在多线程环境中安全使用。

### 🎯 分区边界保护

所有操作都会检查分区边界，防止越界访问：

```c
// System分区只有1MB,超出会返回错误
ret = BG_FlashMgr.WriteSystem(2*1024*1024, data, 256);
// 返回: BG_FLASH_ERROR_PARAM
```

## 性能参考

基于W25Q64 @ 80MHz SPI:

| 操作 | 典型时间 | 速度 |
|------|----------|------|
| 读取 | ~1ms/页(256B) | ~250 KB/s |
| 写入 | ~5ms/页(256B) | ~50 KB/s |
| 擦除扇区(4KB) | ~100ms | - |
| 擦除块(64KB) | ~400ms | - |
| 擦除全片(8MB) | ~100s | - |

## 示例代码

完整示例请参考 `BG_FlashMgr_example.h`：

- 示例1: 基本初始化和信息查询
- 示例2: Looper分区读写
- 示例3: 大数据块写入
- 示例4: 存储分区操作
- 示例5: 格式化分区
- 示例6: 设备测试
- 示例7: 使用便捷宏

运行所有示例：
```c
BG_FlashMgr_RunAllExamples();
```

## 文件清单

### 应用层接口
- `BG_FlashMgr.h` - 应用层接口头文件
- `BG_FlashMgr.c` - 应用层接口实现
- `BG_FlashMgr_example.h` - 使用示例

### 底层实现
- `flash_api.h` - 统一API入口
- `flash_devices.h/c` - 设备注册
- `flash_bus.h/c` - 总线管理
- `flash_nor_w25qxx.h/c` - W25Qxx驱动

## 集成到项目

### 1. 在main.c中初始化

```c
#include "BG_FlashMgr.h"

int main(void)
{
    // ... 其他初始化 ...
    
    // 初始化Flash管理器
    BG_FlashMgr.Init();
    
    // ... 启动任务 ...
}
```

### 2. 在应用中使用

```c
#include "BG_FlashMgr.h"

void audio_looper_save(uint8_t *audio_data, uint32_t size)
{
    static uint32_t write_offset = 0;
    
    if (!BG_FlashMgr.IsReady()) {
        return;
    }
    
    // 检查空间
    if (write_offset + size > BG_FLASH_PARTITION_LOOPER_SIZE) {
        write_offset = 0;  // 循环写入
    }
    
    // 擦除必要的扇区
    uint32_t erase_addr = (write_offset / BG_FLASH_SECTOR_SIZE) * BG_FLASH_SECTOR_SIZE;
    BG_FlashMgr.EraseLooperSector(erase_addr);
    
    // 写入数据
    BG_FlashMgr.WriteLooper(write_offset, audio_data, size);
    
    write_offset += size;
}
```

### 3. 添加Shell命令 (可选)

在 `shell_commands.c` 中添加：

```c
#include "flash_bus.h"

static shell_cmd_t shell_cmds[] = {
    // ... 其他命令 ...
    {"flash", FlashBus_ShellCmd, "Flash operations"},
};
```

## 常见问题

### Q: Flash#1的CS引脚需要确认吗？
A: 是的，当前代码中Flash#1使用GPIOA23，请根据实际硬件原理图确认并修改 `flash_devices.h` 中的 `FLASH1_CS_PIN` 定义。

### Q: 如何添加NAND Flash支持？
A: 参考 `flash_nor_w25qxx.c` 创建 `flash_nand_w25nxx.c`，实现相同的 `FlashOps_t` 接口，然后在 `flash_devices.c` 中注册即可。

### Q: 性能不够怎么办？
A: 可以考虑：
1. 使用DMA进行SPI传输
2. 使用块擦除代替扇区擦除
3. 批量写入减少CS切换次数
4. 使用Quad SPI模式

### Q: 如何实现文件系统？
A: 可以在BG_FlashMgr之上集成FatFS或LittleFS等轻量级文件系统。

## 版本历史

- **v1.0** (2025-12-17)
  - 初始版本
  - 支持双W25Q64 NOR Flash
  - 三分区管理(System/Looper/Storage)
  - 线程安全保护
  - Shell命令支持
  - 已集成到工程 main.c 和 bg_shell_commands.c

## 集成清单 ✅

完成的工作：
- [x] 创建应用层接口 BG_FlashMgr.h/c
- [x] 创建总线-驱动架构 flash_bus/flash_nor_w25qxx
- [x] 在 main.c 的 EffectTask() 中初始化
- [x] 在 bg_shell_commands.c 中添加 flash 模块
- [x] 删除示例文件 BG_FlashMgr_example.h
- [x] 更新文档

## License

Copyright (c) 2025 BanGUI Project
