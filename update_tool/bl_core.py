"""
bl_core.py — BG Bootloader 协议核心
包含 CRC 计算、数据包构建/解析、串口通信、Bootloader 操作。
GUI 和 CLI 共享该模块，不依赖任何 UI 框架。
"""

import time
import struct
from typing import Optional, Tuple, Callable

try:
    import serial
    import serial.tools.list_ports
except ImportError as e:
    raise ImportError("请先安装 pyserial: pip install pyserial") from e

# ─── 协议常量 ────────────────────────────────────────────────────────────────
SOF        = 0xAA
CHUNK_SIZE = 256    # 每个 DATA 包携带的固件字节数
TIMEOUT_S  = 5.0   # 等待响应的超时秒数
MAX_RETRY  = 3      # NACK / 超时后的最大重试次数


class Cmd:
    SYNC        = 0x01
    START       = 0x02
    DATA        = 0x03
    FINISH      = 0x04
    JUMP        = 0x05
    ERASE       = 0x06
    QUERY_INFO  = 0x07   # v2: 查询设备分区信息
    SET_PART    = 0x08   # v2: 设置活跃分区
    REBOOT      = 0x09   # v2: 请求设备重启
    ENTER_BOOT  = 0x0B   # APP → bootloader: reboot & stay in BL


class Rsp:
    ACK  = 0xA1
    NACK = 0xA2


class ErrCode:
    _names = {
        0x01: "CRC_MISMATCH",
        0x02: "FLASH_ERROR",
        0x03: "SIZE_OVERFLOW",
        0x04: "STATE_INVALID",
        0x05: "BAD_PARAM",
    }

    @classmethod
    def name(cls, code: int) -> str:
        return cls._names.get(code, f"0x{code:02X}")


# ─── CRC16-CCITT (poly=0x1021, init=0xFFFF) ─────────────────────────────────
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = (crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


# ─── 数据包构建 / 解析 ────────────────────────────────────────────────────────
def build_packet(cmd: int, seq: int, data: bytes = b"") -> bytes:
    """构建一个完整的协议帧。"""
    header    = struct.pack(">BBHH", SOF, cmd, seq, len(data))
    crc_input = struct.pack(">BHH", cmd, seq, len(data)) + data
    return header + data + struct.pack(">H", crc16(crc_input))


def parse_packet(raw: bytes) -> Optional[Tuple[int, int, bytes]]:
    """
    从 raw 字节流中解析一个完整帧。
    - 成功: 返回 (cmd, seq, data)
    - 数据不足: 返回 None
    - CRC 错误: 抛出 ValueError
    """
    if len(raw) < 8:
        return None
    if raw[0] != SOF:
        raise ValueError("Bad SOF byte")
    cmd, seq, length = struct.unpack(">BHH", raw[1:6])
    if len(raw) < 8 + length:
        return None
    data     = raw[6 : 6 + length]
    recv_crc, = struct.unpack(">H", raw[6 + length : 8 + length])
    exp_crc  = crc16(struct.pack(">BHH", cmd, seq, length) + data)
    if recv_crc != exp_crc:
        raise ValueError(f"CRC mismatch: got 0x{recv_crc:04X}, expected 0x{exp_crc:04X}")
    return cmd, seq, data


# ─── 串口通信层 ───────────────────────────────────────────────────────────────

def is_serial_disconnect(exc: BaseException) -> bool:
    """USB CDC 设备复位/跳转后，Windows 上常见的假失败。"""
    text = str(exc)
    needles = (
        "ClearCommError",
        "设备不识别此命令",
        "PermissionError",
        "Access is denied",
        "WriteFile failed",
        "ReadFile failed",
        "GetOverlappedResult failed",
        "GetCommState failed",
        "SetCommState failed",
        "句柄无效",
        "The device does not recognize",
        "device disconnected",
        "OSError(22",
        "Errno 22",
    )
    if any(n in text for n in needles):
        return True
    for arg in getattr(exc, "args", ()):
        if isinstance(arg, BaseException) and arg is not exc:
            if is_serial_disconnect(arg):
                return True
    return False


class BLComm:
    """封装串口读写，提供"发送并等待响应"语义。"""

    def __init__(self, port: str, baudrate: int = 2000000):
        self._ser = serial.Serial(port, baudrate=baudrate, timeout=0.05)
        self._buf = bytearray()

    def close(self):
        try:
            if self._ser and getattr(self._ser, "is_open", False):
                self._ser.close()
        except Exception:
            pass

    def flush_rx(self):
        try:
            self._ser.reset_input_buffer()
        except Exception as exc:
            if is_serial_disconnect(exc):
                self._buf.clear()
                return
            raise
        self._buf.clear()

    def send_and_recv(self, pkt: bytes,
                      timeout: float = TIMEOUT_S) -> Tuple[int, int, bytes]:
        """
        发送 pkt，等待并返回 (cmd, seq, data)。
        超时时抛出 TimeoutError；端口因复位断开时抛出 serial.SerialException。
        """
        try:
            self._ser.write(pkt)
        except Exception as exc:
            if is_serial_disconnect(exc):
                raise serial.SerialException(str(exc)) from exc
            raise

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                chunk = self._ser.read(256)
            except Exception as exc:
                if is_serial_disconnect(exc):
                    raise serial.SerialException(str(exc)) from exc
                raise
            if chunk:
                self._buf.extend(chunk)
            while len(self._buf) >= 8:
                idx = self._buf.find(SOF)
                if idx < 0:
                    self._buf.clear()
                    break
                if idx > 0:
                    del self._buf[:idx]
                try:
                    result = parse_packet(bytes(self._buf))
                    if result is None:
                        break
                    rsp_cmd, rsp_seq, rsp_data = result
                    del self._buf[: 8 + len(rsp_data)]
                    return rsp_cmd, rsp_seq, rsp_data
                except ValueError:
                    del self._buf[0]
        raise TimeoutError("等待设备响应超时")


# ─── Bootloader 操作层 ────────────────────────────────────────────────────────
class Bootloader:
    """
    封装全部 bootloader 操作。
    progress_cb: 可选回调 progress_cb(sent, total) 用于汇报进度。
    log_cb:      可选回调 log_cb(msg: str) 用于输出日志。
    info_cb:     可选回调 info_cb(info: dict) 用于汇报设备信息。
    """

    def __init__(self, comm: BLComm,
                 progress_cb: Optional[Callable[[int, int], None]] = None,
                 log_cb:      Optional[Callable[[str], None]] = None,
                 info_cb:     Optional[Callable[[dict], None]] = None):
        self._comm       = comm
        self._seq        = 0
        self._progress   = progress_cb or (lambda s, t: None)
        self._log        = log_cb       or print
        self._info       = info_cb      or (lambda i: None)

    def _next_seq(self) -> int:
        s = self._seq
        self._seq = (self._seq + 1) & 0xFFFF
        return s

    def _transact(self, cmd: int, data: bytes = b"",
                  timeout: float = TIMEOUT_S, label: str = "") -> bytes:
        """发送命令，等待 ACK，按需重试。失败时抛出 RuntimeError。"""
        seq      = self._next_seq()
        pkt      = build_packet(cmd, seq, data)
        last_err = "unknown"

        for attempt in range(1, MAX_RETRY + 1):
            if attempt > 1:
                self._log(f"  [重试 {attempt}/{MAX_RETRY}]")
                try:
                    self._comm.flush_rx()
                except Exception as exc:
                    if is_serial_disconnect(exc):
                        raise serial.SerialException(str(exc)) from exc
                    raise

            try:
                rsp_cmd, rsp_seq, rsp_data = self._comm.send_and_recv(pkt, timeout)
            except TimeoutError:
                last_err = "超时"
                continue
            except serial.SerialException as exc:
                # 设备已跳转/复位导致端口失效：向上抛出，由 JUMP/REBOOT 判定为成功
                if is_serial_disconnect(exc):
                    raise
                last_err = str(exc)
                continue

            # 必须匹配序号，否则会把重试前残留的旧 ACK/NACK 当成当前命令成功
            if rsp_seq != seq:
                last_err = f"序号不匹配 (expect {seq}, got {rsp_seq})"
                continue

            if rsp_cmd == Rsp.ACK:
                return rsp_data
            if rsp_cmd == Rsp.NACK:
                code     = rsp_data[0] if rsp_data else 0xFF
                last_err = f"NACK({ErrCode.name(code)})"
                if code != 0x01:   # 非 CRC 错误不再重试
                    break
            else:
                last_err = f"未知响应 cmd=0x{rsp_cmd:02X}"

        tag = f" [{label}]" if label else ""
        raise RuntimeError(f"命令 0x{cmd:02X}{tag} 失败: {last_err}")

    # ── 对外接口 ─────────────────────────────────────────────────────────────

    def ping(self) -> int:
        """握手；返回设备协议版本号。"""
        resp    = self._transact(Cmd.SYNC, label="SYNC")
        version = resp[0] if resp else 0
        self._log(f"设备在线，协议版本 v{version}")
        return version

    def enter_boot_from_app(self) -> bool:
        """让 APP 模式的设备跳回 Bootloader。

        流程:
          1. 直接发送 CMD_ENTER_BOOT 协议包（0xAA SOF + 0x0B CMD）
          2. APP 的 CDC_Upgrade_CheckEnter() 自动嗅探 0xAA 进入升级模式
          3. App_Upgrade 引擎处理 CMD_ENTER_BOOT，写 BURN_FLAG_MAGIC 到 Flash 后复位
          4. 等待设备重新枚举为 Bootloader

        返回 True 如果 CMD_ENTER_BOOT 收到 ACK（设备即将复位）。
        """
        self._log("发送 CMD_ENTER_BOOT 协议包 …")
        try:
            resp = self._transact(Cmd.ENTER_BOOT, timeout=2.0,
                                  label="ENTER_BOOT")
            if resp:
                self._log("APP 已确认，正在重启到 Bootloader …")
                return True
        except Exception as e:
            # APP 复位会导致串口断开，这是正常的
            self._log(f"APP 已重启（串口断开属正常）: {e}")
            return True

        return False

    def erase(self):
        """擦除整个应用 Flash 区域（最长 30 s）。"""
        self._log("正在擦除应用区 Flash …")
        self._transact(Cmd.ERASE, timeout=30.0, label="ERASE")
        self._log("擦除完成 ✓")

    def upgrade(self, firmware: bytes):
        """完整升级流程：QUERY_INFO → START → DATA chunks → FINISH。

        单分区模式 (boot_mode == 0): 直接写入 Part A（覆盖当前固件）。
        双分区模式 (boot_mode == 1): 写入非活跃分区（备份分区）。
        """
        total = len(firmware)
        self._log(f"固件大小: {total} 字节  ({total / 1024:.1f} KB)")

        # ── 先查询分区信息，校验大小并显示目标分区 ──────────────────────────
        info = self.query_info()

        if info["boot_mode"] == 0:
            # 单分区：直接写 Part A
            target_base = info["part_a_base"]
            target_size = info["part_a_size"]
            target_lbl  = "A (单分区)"
        else:
            # 双分区：写非活跃分区
            active_part = info["active_part"]
            if active_part == 0:
                target_base = info["part_b_base"]
                target_size = info["part_b_size"]
                target_lbl  = "B"
            else:
                target_base = info["part_a_base"]
                target_size = info["part_a_size"]
                target_lbl  = "A"

        self._log(
            f"将写入分区 {target_lbl}  地址=0x{target_base:06X}  "
            f"容量={target_size // 1024} KB"
        )
        if total > target_size:
            raise RuntimeError(
                f"固件大小 {total:,} 字节 ({total/1024:.1f} KB) "
                f"超出分区 {target_lbl} 容量 {target_size:,} 字节 "
                f"({target_size//1024} KB)"
            )

        self._transact(Cmd.START, struct.pack(">I", total),
                       timeout=30.0, label="START")
        self._log("Flash 已擦除，开始传输 …")

        offset = 0
        try:
            while offset < total:
                chunk   = firmware[offset : offset + CHUNK_SIZE]
                payload = struct.pack(">I", offset) + chunk
                self._transact(Cmd.DATA, payload, label=f"DATA@0x{offset:X}")
                offset += len(chunk)
                self._progress(offset, total)

            self._transact(Cmd.FINISH, struct.pack(">I", total), label="FINISH")
        except (TimeoutError, serial.SerialException, OSError, PermissionError) as exc:
            # 中途断线 ≠ 升级成功。半截固件若被误判成功再 JUMP 会砖机。
            if is_serial_disconnect(exc):
                raise RuntimeError(
                    f"升级中断（已传输 {offset}/{total}，串口断开）: {exc}。"
                    f"请勿跳转应用；重新上电应留在 Bootloader，再重试升级。"
                ) from exc
            raise
        except RuntimeError as exc:
            if is_serial_disconnect(exc):
                raise RuntimeError(
                    f"升级中断（已传输 {offset}/{total}，串口断开）: {exc}。"
                    f"请勿跳转应用；重新上电应留在 Bootloader，再重试升级。"
                ) from exc
            raise
        if offset != total:
            raise RuntimeError(f"升级不完整: 已传输 {offset}/{total}")
        self._log("升级完成 ✓")

    def jump(self):
        """发送 JUMP，设备跳转到应用。

        设备收到 JUMP 后会立刻切走 USB CDC，Windows 上常出现
        ClearCommError / PermissionError(13)，属正常现象，视为成功。
        """
        self._log("正在跳转到应用 …")
        try:
            self._transact(Cmd.JUMP, timeout=2.0, label="JUMP")
        except (TimeoutError, serial.SerialException) as exc:
            self._log(f"设备已跳转（串口断开属正常）: {exc}")
            return
        except RuntimeError as exc:
            if is_serial_disconnect(exc):
                self._log(f"设备已跳转（串口断开属正常）: {exc}")
                return
            raise
        self._log("设备已跳转 ✓")

    def query_info(self) -> dict:
        """
        查询设备分区信息（协议 v2+）。
        解析 DevInfo_t 结构体（20 字节）:
          [0]  boot_mode       uint8
          [1]  active_part     uint8
          [2]  boot_fail_cnt   uint8
          [3]  protocol_ver    uint8
          [4-7]  part_a_base   uint32 BE
          [8-11] part_a_size   uint32 BE
          [12-15] part_b_base  uint32 BE
          [16-19] part_b_size  uint32 BE
        """
        resp = self._transact(Cmd.QUERY_INFO, label="QUERY_INFO")
        info = {
            "protocol_ver":  0,
            "boot_mode":     0,
            "active_part":   0,
            "boot_fail_cnt": 0,
            "part_a_base":   0x00040000,
            "part_a_size":   0x00200000,   # default 2 MB
            "part_b_base":   0x00240000,
            "part_b_size":   0x00200000,   # default 2 MB
        }
        if len(resp) >= 4:
            info["boot_mode"]     = resp[0]
            info["active_part"]   = resp[1]
            info["boot_fail_cnt"] = resp[2]
            info["protocol_ver"]  = resp[3]
        if len(resp) >= 20:
            (info["part_a_base"],
             info["part_a_size"],
             info["part_b_base"],
             info["part_b_size"]) = struct.unpack("<IIII", resp[4:20])
        active_lbl  = 'B' if info['active_part'] else 'A'
        is_single   = info['boot_mode'] == 0

        if is_single:
            self._log(
                f"设备信息: 协议v{info['protocol_ver']}  "
                f"模式=单分区  "
                f"容量={info['part_a_size']//1024} KB  "
                f"启动失败次数={info['boot_fail_cnt']}"
            )
        else:
            backup_lbl  = 'A' if info['active_part'] else 'B'
            backup_size = info['part_a_size'] if info['active_part'] else info['part_b_size']
            self._log(
                f"设备信息: 协议v{info['protocol_ver']}  "
                f"模式=双分区A/B  "
                f"当前运行分区={active_lbl}  "
                f"备份分区={backup_lbl}({backup_size//1024} KB)  "
                f"启动失败次数={info['boot_fail_cnt']}"
            )
        # 通过回调把 info 字典传给 UI 层（用于更新设备信息卡）
        self._info(info)
        return info

    def set_partition(self, part: int):
        """
        设置活跃分区（协议 v2+）。
        part: 0 = A 区（出厂固件），1 = B 区（用户升级固件）
        """
        if part not in (0, 1):
            raise ValueError("part 必须为 0(A) 或 1(B)")
        self._transact(Cmd.SET_PART, bytes([part]), label="SET_PART")
        self._log(f"活跃分区已设置为 {'B' if part else 'A'} ✓")

    def reboot(self):
        """请求设备立即重启（协议 v2+）。"""
        self._log("正在请求设备重启 …")
        # 设备重启后 ACK/串口都可能立即失效，属正常现象
        try:
            self._transact(Cmd.REBOOT, timeout=2.0, label="REBOOT")
        except (TimeoutError, serial.SerialException):
            pass
        except RuntimeError as exc:
            if not is_serial_disconnect(exc):
                raise
        self._log("重启命令已发送 ✓")


# ─── USB 身份协议（Bootloader）──────────────────────────────────────────────
# PID = 0x4247 ('BG') 标识 BG Bootloader 家族
# VID = 产品编号：
#   0x0001 BanBox
#   0x0002 BanAirBundy
#
# 上位机据此识别平台，再走 CDC 升级协议。

BG_USB_PID = 0x4247

PRODUCT_BY_VID = {
    0x0001: "BanBox",
    0x0002: "BanAirBundy",
}

# 兼容旧工具默认值（非身份协议产品）；仅作文档/回退，正式识别以 PID=0x4247 为准
LEGACY_BL_VID = 0x8888
LEGACY_BL_PID = 0x1722

# 向后兼容旧导入名：默认指向本仓库产品 BanBox
BL_VID = 0x0001
BL_PID = BG_USB_PID


def is_bg_bootloader_id(vid, pid) -> bool:
    """是否为 BG Bootloader 身份（PID=0x4247 且 VID 在产品表中）。"""
    try:
        vid_i = int(vid) if vid is not None else -1
        pid_i = int(pid) if pid is not None else -1
    except (TypeError, ValueError):
        return False
    return pid_i == BG_USB_PID and vid_i in PRODUCT_BY_VID


def product_name_from_vid(vid) -> str:
    try:
        vid_i = int(vid) if vid is not None else -1
    except (TypeError, ValueError):
        return "Unknown"
    return PRODUCT_BY_VID.get(vid_i, f"Unknown(VID=0x{vid_i:04X})")


def identify_usb_ids(vid, pid) -> Optional[dict]:
    """
    根据 VID/PID 识别设备。
    返回 dict: brand/product/role/vid/pid；无法识别返回 None。
    """
    try:
        vid_i = int(vid) if vid is not None else -1
        pid_i = int(pid) if pid is not None else -1
    except (TypeError, ValueError):
        return None

    if is_bg_bootloader_id(vid_i, pid_i):
        return {
            "brand": "BG",
            "product": product_name_from_vid(vid_i),
            "role": "bootloader",
            "vid": vid_i,
            "pid": pid_i,
        }

    # 可选：旧版硬编码 ID
    if vid_i == LEGACY_BL_VID and pid_i == LEGACY_BL_PID:
        return {
            "brand": "BG",
            "product": "Legacy Bootloader",
            "role": "bootloader",
            "vid": vid_i,
            "pid": pid_i,
        }

    return None


def identify_port(device: str) -> Optional[dict]:
    """按串口设备名识别 USB 身份；找不到端口或非 BL 返回 None。"""
    if not device:
        return None
    for p in serial.tools.list_ports.comports():
        if p.device == device:
            info = identify_usb_ids(getattr(p, "vid", None), getattr(p, "pid", None))
            if info is None:
                return None
            info = dict(info)
            info["device"] = p.device
            info["description"] = p.description or p.device
            return info
    return None


def list_ports():
    """返回 [(device, description), ...] 列表。"""
    return [(p.device, p.description or p.device)
            for p in sorted(serial.tools.list_ports.comports())]


def list_bootloader_ports():
    """返回 BG Bootloader 串口 [(device, description), ...]。"""
    result = []
    for p in serial.tools.list_ports.comports():
        vid = getattr(p, "vid", None)
        pid = getattr(p, "pid", None)
        if identify_usb_ids(vid, pid) is not None:
            result.append((p.device, p.description or p.device))
    return sorted(result)


def list_bootloader_devices():
    """返回已识别的 Bootloader 设备详情列表 [dict, ...]。"""
    result = []
    for p in serial.tools.list_ports.comports():
        info = identify_usb_ids(getattr(p, "vid", None), getattr(p, "pid", None))
        if info is None:
            continue
        item = dict(info)
        item["device"] = p.device
        item["description"] = p.description or p.device
        result.append(item)
    return sorted(result, key=lambda x: x["device"])


def probe_port(port: str, baudrate: int = 2000000,
               timeout: float = 0.5) -> Optional[int]:
    """
    向指定端口发送 SYNC 握手包，快速探测是否有 Bootloader 应答。
    返回协议版本号；探测失败返回 None。
    """
    try:
        ser = serial.Serial(port, baudrate=baudrate, timeout=0.05)
    except (serial.SerialException, OSError):
        return None
    try:
        pkt = build_packet(Cmd.SYNC, 0)
        ser.reset_input_buffer()
        ser.write(pkt)
        buf = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = ser.read(256)
            if chunk:
                buf.extend(chunk)
            while len(buf) >= 8:
                idx = buf.find(SOF)
                if idx < 0:
                    buf.clear()
                    break
                if idx > 0:
                    del buf[:idx]
                try:
                    result = parse_packet(bytes(buf))
                    if result is None:
                        break
                    rsp_cmd, _, rsp_data = result
                    if rsp_cmd == Rsp.ACK:
                        return rsp_data[0] if rsp_data else 0
                    del buf[:8 + len(rsp_data)]
                except ValueError:
                    del buf[0]
        return None
    except Exception:
        return None
    finally:
        try:
            ser.close()
        except Exception:
            pass
