# update_tool — BG Bootloader 上位机

USB CDC / 无线(WiFi 桥接) 固件升级工具（PyQt5 GUI），用于识别 BG Bootloader 设备并烧录 APP 固件。无线模式把 ESP8266 的 TCP 透传链路当作“经 WiFi 转发的串口”，复用同一套升级协议。

## 依赖

```bash
pip install pyserial PyQt5
# 可选：提升无线设备(UDP 广播)发现命中率（多网卡子网定向广播）
pip install netifaces
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
- **设备连接**：
  - **连接方式**下拉：`无线 (WiFi 桥接)` / `串口 (USB 直连)`，切换后自动记忆到 `device_connection.json`
  - 无线模式：「搜索设备」UDP 广播发现 BanPCBTool，设备下拉选择目标（IP:8266）
  - 串口模式：串口选择、波特率、刷新、自动扫描
  - 「进入 Boot 模式」：两种模式通用（串口发 `boot`，无线经 TCP 透传发 `boot`）
- **固件操作**：选择 `.bin` → 升级；双分区模式额外支持握手/查询/擦除/跳转/重启
- **日志**：实时操作日志

## 典型流程

### 串口直连（USB CDC）
1. 设备进入 Bootloader（烧录 BL，或 APP 串口发 `boot` / 点「进入 Boot 模式」）
2. 打开本工具，自动扫描或手动选串口
3. Banner 显示绿色产品名后，选择固件并升级

### 无线（WiFi 桥接透传）
1. 连接方式选「无线 (WiFi 桥接)」→「搜索设备」发现目标（或直接用上次记忆的 IP）
2. 选中设备后点「升级」：上位机 TCP 连到模块 `IP:8266`，`@BPC PING` 握手，随后升级协议帧经模块逐字节透传到下位机 UART3
3. 与串口模式行为一致（握手/查询/擦除/升级/跳转），仅承载通道不同

## 目录结构

```
update_tool/
├── README.md               ← 本说明
├── bg_bootloader.py        ← GUI 入口（无线/串口双模式）
├── bl_core.py              ← CDC 升级协议 + USB 身份识别（传输后端可切换）
├── wireless.py             ← 无线连接层：UDP 发现 + TCP 握手 + SocketSerialAdapter
├── worker.py               ← 后台扫描 / 升级线程（含 WirelessScanWorker）
└── device_connection.json  ← 连接偏好（运行时生成，与 pcb_gcode_tool 结构兼容）
```

## 协议概要

- 帧头 SOF：`0xAA`
- 主要命令：SYNC / START / DATA / FINISH / JUMP / ERASE / QUERY_INFO / SET_PART / REBOOT
- 详情实现见 `bl_core.py`

## 无线升级原理

从上位机视角，**无线链路等价于“经过 WiFi 模块转发的串口”**：升级协议帧（`0xAA …`）原样发出，只是承载通道从 COM 口换成 TCP socket。

```
上位机 BLComm  ──0xAA 协议帧──▶  TCP socket (IP:8266)
                                     │  ESP8266 wireless.ino 逐字节透传
                                     ▼
                               下位机 UART3 ──▶ app_bl_poll 嗅探
```

- **传输抽象**：`wireless.SocketSerialAdapter` 把 TCP socket 适配成 pyserial.Serial 的最小子集（`write`/`read`/`reset_input_buffer`/`close`/`is_open`），因此 `BLComm` 协议层零改动即可跑在无线上（`BLComm(transport=adapter)`）。
- **连接兼容 `wireless.ino`**：UDP 发现端口 `8267`（查询串 `BANPCBTOOL?`）、TCP 桥接端口 `8266`、链路握手 `@BPC PING`→`@BPC PONG`。升级二进制帧首字节是 `0xAA`，不会与模块的行首 `@BPC` 控制前缀冲突。

> **固件侧前提**：无线升级要求下位机固件能通过 **UART3** 处理完整升级协议。当前 `app/Core/Src/app_bl.c` 的 `app_bl_poll()` 对 UART3 仅嗅探 `ENTER_BOOT`(0x0B) 帧与 `boot` 文本；若要在无线下完成 SYNC/START/DATA/FINISH 全流程，需在固件侧为 UART3 增加升级通道（参考 `app_upgrade.c` 的 `UpgradeChannel_t` 抽象）。上位机侧已按“串口式”就绪，固件通道补齐后即可直接无线升级。

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
