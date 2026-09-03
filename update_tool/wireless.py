# -*- coding: utf-8 -*-
"""
wireless.py — BG Bootloader 无线连接层（ESP8266 WiFi 透传桥接）

设计要点：
  从上位机视角，无线链路等价于“经过 WiFi 模块转发的串口”——升级协议帧（0xAA ...）
  原样发出，只是承载通道从 COM 口换成 TCP socket。ESP8266 (wireless.ino) 逐字节把
  TCP 数据透传到下位机 UART3，反向亦然；控制类文本以行首 @BPC 触发，升级二进制帧
  首字节是 0xAA 不会与之冲突。

  为了让 bl_core.BLComm 的协议逻辑零改动即可跑在无线上，这里提供 SocketSerialAdapter：
  把一个 TCP socket 适配成 pyserial.Serial 的最小子集
  (write / read / reset_input_buffer / close / is_open)。

协议兼容 wireless.ino：
  · UDP 发现端口 8267，查询串 "BANPCBTOOL?"，应答 "BANPCBTOOL NAME=.. IP=.. BRIDGE=8266 .."
  · TCP 桥接端口 8266
  · 链路握手 "@BPC PING\\r\\n" → "@BPC PONG"（或 "@BPC OK"）
"""

from __future__ import annotations

import socket
import time
from dataclasses import dataclass
from typing import Callable, List, Optional

# ─── 协议常量（与 wireless.ino / pcb_gcode_tool 保持一致）─────────────────────
DISCOVERY_PORT      = 8267
BRIDGE_PORT         = 8266
DEFAULT_DEVICE_IP   = "192.168.4.1"    # AP 模式固定 IP
DEFAULT_DEVICE_NAME = "BanPCBTool"     # 模块热点名称前缀
DISCOVERY_QUERY     = b"BANPCBTOOL?"
HELLO_CMD           = b"@BPC PING\r\n"

# SocketSerialAdapter 默认读超时：与 BLComm 串口的 timeout=0.05 对齐，
# 使 send_and_recv 的读循环在两种传输下时序一致。
_DEFAULT_READ_TIMEOUT = 0.05

LogFn = Optional[Callable[[str], None]]


# ─── 设备模型 ────────────────────────────────────────────────────────────────
@dataclass
class WirelessDevice:
    """一台被发现的 BanPCBTool 无线设备。"""
    name: str        = DEFAULT_DEVICE_NAME
    ip: str          = ""
    bridge_port: int = BRIDGE_PORT
    http_port: int   = 80
    wifi: str        = ""
    ap: bool         = False

    @property
    def label(self) -> str:
        suffix = "  AP" if self.ap else ""
        return f"{self.name}  {self.ip}:{self.bridge_port}{suffix}"


# ─── UDP 发现 ────────────────────────────────────────────────────────────────
def get_local_broadcast_addresses() -> List[str]:
    """返回本机所有 NIC 的定向子网广播地址 + 255.255.255.255 + 192.168.4.255(模块AP)。

    Windows 在多网卡时常静默丢弃 255.255.255.255，定向子网广播能显著提升发现命中率。
    netifaces 为可选依赖；缺失时回退到 gethostbyname_ex 推算子网广播。
    """
    result = {"255.255.255.255", "192.168.4.255"}
    try:
        import netifaces  # type: ignore
        for iface in netifaces.interfaces():
            for entry in netifaces.ifaddresses(iface).get(netifaces.AF_INET, []):
                ip   = entry.get("addr", "")
                mask = entry.get("netmask", "")
                if not ip or not mask or ip.startswith("127."):
                    continue
                try:
                    ip_b   = socket.inet_aton(ip)
                    mask_b = socket.inet_aton(mask)
                    bcast  = bytes(ib | (~mb & 0xFF) for ib, mb in zip(ip_b, mask_b))
                    result.add(socket.inet_ntoa(bcast))
                except OSError:
                    pass
        return list(result)
    except Exception:  # netifaces 未安装或异常 → 回退
        try:
            _, _, ips = socket.gethostbyname_ex(socket.gethostname())
            for ip in ips:
                if ip.startswith("127."):
                    continue
                parts = ip.split(".")
                if len(parts) == 4:
                    result.add(f"{parts[0]}.{parts[1]}.{parts[2]}.255")
        except (socket.herror, OSError):
            pass
        return list(result)


def parse_discovery_packet(data: bytes, source_ip: str) -> Optional[WirelessDevice]:
    """解析一条 UDP 发现应答；非 BanPCBTool 设备返回 None。"""
    try:
        text = data.decode("utf-8", errors="replace").strip()
    except UnicodeError:
        return None
    if not text.upper().startswith("BANPCBTOOL"):
        return None
    values = {}
    for token in text.split()[1:]:
        if "=" in token:
            key, value = token.split("=", 1)
            values[key.lower()] = value

    def _int(key: str, default: int) -> int:
        try:
            return int(values.get(key, default) or default)
        except (TypeError, ValueError):
            return default

    return WirelessDevice(
        name        = values.get("name", DEFAULT_DEVICE_NAME),
        ip          = values.get("ip", source_ip),
        bridge_port = _int("bridge", BRIDGE_PORT),
        http_port   = _int("http", 80),
        wifi        = values.get("wifi", ""),
        ap          = values.get("ap", "0") in {"1", "true", "yes", "on"},
    )


def discover_devices(rounds: int = 3,
                     recv_attempts: int = 12,
                     timeout: float = 0.25,
                     include_ap_fallback: bool = True,
                     log: LogFn = None) -> List[WirelessDevice]:
    """UDP 广播发现 BanPCBTool 设备（纯同步，宜在 QThread 中调用）。

    向每个 NIC 子网定向广播 + 255.255.255.255 + 192.168.4.255 发送 rounds 轮查询，
    随后在 recv_attempts 次窗口内收集应答，按 IP 去重返回。
    include_ap_fallback=True 时，若没发现任何设备则补一个 AP 固定 IP 兜底项。
    """
    found = {}
    udp = None
    try:
        targets = get_local_broadcast_addresses()
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp.settimeout(timeout)
        for _ in range(max(1, int(rounds))):
            for bcast in targets:
                try:
                    udp.sendto(DISCOVERY_QUERY, (bcast, DISCOVERY_PORT))
                except OSError:
                    continue
        for _ in range(max(1, int(recv_attempts))):
            try:
                data, address = udp.recvfrom(512)
            except (socket.timeout, TimeoutError):
                continue
            except OSError:
                break
            device = parse_discovery_packet(data, address[0])
            if device:
                found[device.ip] = device
    except OSError as exc:
        if log:
            log(f"发现过程出错: {exc}")
    finally:
        if udp is not None:
            try:
                udp.close()
            except OSError:
                pass

    if include_ap_fallback and DEFAULT_DEVICE_IP not in found:
        found[DEFAULT_DEVICE_IP] = WirelessDevice(
            name=DEFAULT_DEVICE_NAME, ip=DEFAULT_DEVICE_IP, ap=True)

    devices = list(found.values())
    # STA（局域网）设备排在 AP 兜底之前，便于自动优选
    devices.sort(key=lambda d: (d.ap, d.ip))
    if log:
        for device in devices:
            mode = "AP" if device.ap else "STA"
            log(f"发现设备: {device.label}  [{mode}]  wifi={device.wifi or '-'}")
    return devices


# ─── TCP → Serial 适配器 ─────────────────────────────────────────────────────
class SocketSerialAdapter:
    """把 TCP socket 适配成 pyserial.Serial 的最小子集，供 BLComm 无线复用。

    提供：write(data)->int / read(n)->bytes / reset_input_buffer() / close() / is_open
    read 语义与串口 timeout 一致：无数据在 read_timeout 内返回 b""；
    链路真正断开（ConnectionReset 等）时抛出 OSError，交由上层判定失败。
    """

    def __init__(self, sock: socket.socket, read_timeout: float = _DEFAULT_READ_TIMEOUT):
        self._sock         = sock
        self._read_timeout = read_timeout
        self._closed       = False
        try:
            self._sock.settimeout(self._read_timeout)
        except OSError:
            pass

    # ── pyserial 兼容属性/方法 ──
    @property
    def is_open(self) -> bool:
        return self._sock is not None and not self._closed

    @property
    def port(self) -> str:
        """供日志/显示：无线链路以 “ip:port” 标识。"""
        try:
            peer = self._sock.getpeername()
            return f"{peer[0]}:{peer[1]}"
        except (OSError, AttributeError):
            return "wireless"

    def write(self, data: bytes) -> int:
        if self._sock is None or self._closed:
            raise OSError("无线链路已关闭")
        self._sock.sendall(bytes(data))
        return len(data)

    def read(self, n: int = 1) -> bytes:
        if self._sock is None or self._closed:
            return b""
        try:
            return self._sock.recv(n)
        except (socket.timeout, TimeoutError):
            return b""
        # 其它 OSError（连接被复位/中断）向上抛，让 BLComm/worker 报错

    def reset_input_buffer(self) -> None:
        """清空接收缓冲中的残留字节（握手回显、shell 提示符等），best-effort。"""
        if self._sock is None or self._closed:
            return
        try:
            self._sock.setblocking(False)
            try:
                while True:
                    if not self._sock.recv(4096):
                        break
            except (BlockingIOError, InterruptedError, socket.timeout, TimeoutError, OSError):
                pass
            finally:
                self._sock.setblocking(True)
                self._sock.settimeout(self._read_timeout)
        except OSError:
            pass

    def close(self) -> None:
        self._closed = True
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def __repr__(self) -> str:
        return f"<SocketSerialAdapter {self.port}>"


# ─── 建链 + 握手 ─────────────────────────────────────────────────────────────
def _handshake(sock: socket.socket, timeout: float = 1.2, log: LogFn = None) -> None:
    """@BPC PING → @BPC PONG/@BPC OK 链路握手；失败抛 OSError。

    握手只验证到 ESP8266 的 TCP 链路（@BPC 由模块自身应答，不转发给 STM32）。
    """
    sock.settimeout(timeout)
    buf = b""
    try:
        sock.sendall(HELLO_CMD)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                chunk = sock.recv(256)
            except (socket.timeout, TimeoutError):
                if buf:
                    break
                continue
            if not chunk:
                break
            buf += chunk
            if b"@BPC PONG" in buf or b"@BPC OK" in buf:
                break
    finally:
        pass
    if b"@BPC PONG" not in buf and b"@BPC OK" not in buf:
        snippet = buf.strip().decode("utf-8", errors="replace")[:60]
        raise OSError(f"无线握手失败（未收到 @BPC PONG，实收: {snippet!r}）")
    if log:
        log(f"无线握手成功: {buf.strip().decode('utf-8', errors='replace')[:60]}")


def open_wireless(device, bridge_port: int = BRIDGE_PORT,
                  connect_timeout: float = 2.5,
                  handshake_timeout: float = 1.2,
                  log: LogFn = None) -> SocketSerialAdapter:
    """连接一台无线设备并握手，返回可直接交给 BLComm(transport=..) 的适配器。

    device 可为 WirelessDevice，或字符串 IP（配合 bridge_port）。
    握手成功后清空接收缓冲，去掉回显/提示符残留，准备跑二进制升级协议。
    失败抛 OSError。
    """
    if isinstance(device, WirelessDevice):
        ip, port = device.ip, device.bridge_port
    else:
        ip, port = str(device), bridge_port
    if not ip:
        raise OSError("无线设备 IP 为空")

    if log:
        log(f"正在连接无线设备 {ip}:{port} …")
    sock = socket.create_connection((ip, port), timeout=connect_timeout)
    try:
        _handshake(sock, timeout=handshake_timeout, log=log)
    except OSError:
        try:
            sock.close()
        except OSError:
            pass
        raise

    # 独占桥接槽：告知模块本次连接用于固件升级，期间拒绝其它 TCP 连接抢占。
    # 否则后台工具（如 pcb_gcode_tool 每 3s 的 @BPC PING 心跳 / 8266 端口探测）
    # 建连时会抢走模块唯一的 bridgeClient 槽，把正在进行的升级掐断
    # （上位机报 WinError 10053 你的主机中的软件中止了一个已建立的连接）。
    try:
        sock.sendall(b"@BPC LOCK\r\n")
        time.sleep(0.05)   # 给模块时间回 @BPC OK LOCK
    except OSError:
        pass

    adapter = SocketSerialAdapter(sock, read_timeout=_DEFAULT_READ_TIMEOUT)
    adapter.reset_input_buffer()   # 清握手 + LOCK 响应残留，避免污染首个协议帧
    return adapter
