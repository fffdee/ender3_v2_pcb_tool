# update_tool — BG Bootloader 上位机

USB CDC 固件升级工具（PyQt5 GUI），用于识别 BG Bootloader 设备并烧录 APP 固件。

## 依赖

```bash
pip install pyserial PyQt5
```

Python 3.8+ 推荐。

## 运行

```bash
cd update_tool
python bg_bootloader.py
```

默认波特率：**2000000**（USB CDC 实际速率由主机栈决定，下拉框可改）。

## USB 身份协议

上位机通过 USB **VID/PID** 识别 Bootloader 所属产品：

| 字段 | 值 | 含义 |
|------|-----|------|
| PID | `0x4247` | BG（`'B''G'`）Bootloader 家族 |
| VID | `0x0001` | BanBox |
| VID | `0x0002` | BanAirBundy |

本仓库 Bootloader 枚举为：`VID=0x0001` / `PID=0x4247` → **BanBox**。

固件侧常量见：`bootloader/src/usb_identity.h`。

## UI 说明

- **顶部 Banner**
  - 未连接：红色「未连接」
  - 已连接：绿色「已连接 · \<产品名\> (COMx)」
- **设备连接**：串口选择、刷新、自动扫描、「进入 Boot 模式」
- **固件操作**：选择 `.bin` → 升级；双分区模式额外支持握手/查询/擦除/跳转/重启
- **日志**：实时操作日志

## 典型流程

1. 设备进入 Bootloader（烧录 BL，或 APP 串口发 `boot` / 点「进入 Boot 模式」）
2. 打开本工具，自动扫描或手动选串口
3. Banner 显示绿色产品名后，选择固件并升级

## 目录结构

```
update_tool/
├── README.md           ← 本说明
├── bg_bootloader.py    ← GUI 入口
├── bl_core.py          ← CDC 升级协议 + USB 身份识别
└── worker.py           ← 后台扫描 / 升级线程
```

## 协议概要

- 帧头 SOF：`0xAA`
- 主要命令：SYNC / START / DATA / FINISH / JUMP / ERASE / QUERY_INFO / SET_PART / REBOOT
- 详情实现见 `bl_core.py`

## 防变砖逻辑（升级中断 / 拔线）

单分区设备升级时，**第一包 DATA 就会写入 `BGPF` 有效魔数**。若中途失败或拔 USB，旧逻辑会把半截镜像当成合法 APP 并跳转 → 卡死，断电仍无法恢复。

### 设备侧（`bootloader`）

| 机制 | 说明 |
|------|------|
| sticky `upgrade-pending` | 地址 `0x3F004`（bootloader 区，升级擦 APP 不会清掉），魔数 `"PEND"` |
| `CMD_START` / `CMD_ERASE` | **先置位** pending，再擦写 APP |
| `CMD_FINISH` | 仅当 `written == total` 且存在 `BGPF` 时成功，然后**清除** pending |
| 开机检查 | pending 仍置位 → **强制留在 Bootloader**，绝不跳 APP |
| `CMD_JUMP` | pending 未清或会话未完成 → 拒绝跳转 |

实现见：`bootloader/src/upgrade.h`（`UPG_PENDING_*`）、`bootloader/src/upgrade.c`。

### 上位机侧（本工具）

| 行为 | 说明 |
|------|------|
| 中途断线 | 判定为**升级失败**，不再误报“升级完成” |
| 自动 JUMP | **仅**在 `upgrade()` 完整成功（含 FINISH）后执行 |
| 不完整传输 | `offset != total` 直接报错，禁止跳转 |
| 响应序号 | ACK/NACK 必须匹配请求 seq，忽略重试残留的旧包 |

### 已知坑（已修）

DATA 超时重传时，若设备用 `written += len` 累加，会把同一 offset 计两次 → FINISH 报 `SIZE_OVERFLOW`、pending 无法清除 → 重上电一直停在 Bootloader。  
现改为 **high-water**（`written = max(written, offset+len)`）；JUMP 在 pending 残留但 Part A 已有合法 `BGPF` 时允许清 pending 并跳转（救砖）。

### 正常 / 异常对照

1. **完整升级**：START → DATA… → FINISH（清 pending）→ JUMP → APP  
2. **中途拔线**：pending 保留 → 重上电进 BL → 用本工具重刷即可  
3. **已砖旧板**（无 pending 的半截镜像）：需烧录器擦 Part A，或先烧含本逻辑的新 bootloader 后再救砖

> 防变砖保护**必须先烧录带本逻辑的 bootloader** 才生效。

## 与固件对应关系

| 工程 | 角色 |
|------|------|
| `bootloader` | USB CDC 升级模式，暴露身份 VID/PID；含 upgrade-pending 防变砖 |
| `BanBox` | 应用；可通过 Shell `boot` 写 burn flag 后复位进 BL |
| `update_tool` | 主机端识别产品并烧录 |

## 兼容说明

仍识别旧 ID `VID=0x8888 / PID=0x1722`（显示为 Legacy Bootloader），正式产品以 `PID=0x4247` 身份协议为准。
