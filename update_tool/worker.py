"""
worker.py — 后台线程
1. AutoScanWorker  — 启动时自动扫描所有串口，发送握手探测
2. UpgradeWorker   — 耗时操作（握手、擦除、写 Flash、跳转、查询、设置分区、重启）
"""

import time
from PyQt5.QtCore import QThread, pyqtSignal
from bl_core import (
    BLComm, Bootloader, list_ports, list_bootloader_ports,
    list_bootloader_devices, probe_port, Cmd, build_packet,
    is_serial_disconnect,
)


# ═══════════════════════════════════════════════════════════════════════════════
#  AutoScanWorker — 扫描所有串口，逐个发握手包，找到 Bootloader 即返回
# ═══════════════════════════════════════════════════════════════════════════════
class AutoScanWorker(QThread):
    """
    信号:
        log(str)                           — 日志
        found(str, str, int)               — (port, description, protocol_ver)
        scan_finished(bool, str)           — (found_any?, message)
    """
    log             = pyqtSignal(str)
    found           = pyqtSignal(str, str, int)
    scan_finished   = pyqtSignal(bool, str)

    def __init__(self, baudrate: int = 2000000, parent=None):
        super().__init__(parent)
        self._baud = baudrate
        self._abort = False

    def abort(self):
        self._abort = True

    def run(self):
        # ── Step 1: 先通过 USB 身份协议 (PID=0x4247 + VID 产品表) 快速查找 ──
        bl_ports = list_bootloader_ports()
        bl_devs = list_bootloader_devices()
        if bl_ports:
            for d in bl_devs:
                self.log.emit(
                    f"发现 {d['product']} Bootloader "
                    f"(VID=0x{d['vid']:04X} PID=0x{d['pid']:04X}) …")
            for device, desc in bl_ports:
                if self._abort:
                    break
                self.log.emit(f"  探测 {device} ({desc}) …")
                ver = probe_port(device, self._baud, timeout=0.5)
                if ver is not None:
                    product = next((x["product"] for x in bl_devs if x["device"] == device), "BG")
                    self.log.emit(f"  ✓ {device} — {product} Bootloader v{ver}")
                    self.found.emit(device, desc, ver)
                    self.scan_finished.emit(True, "已找到设备")
                    return

        # ── Step 2: 全量扫描（VID/PID 不可用时回退） ──
        ports = list_ports()
        if not ports:
            self.log.emit("未发现任何串口")
            self.scan_finished.emit(False, "无可用串口")
            return

        self.log.emit(f"正在扫描 {len(ports)} 个串口 …")
        for device, desc in ports:
            if self._abort:
                break
            # 跳过已尝试的 bootloader 端口
            if any(d == device for d, _ in bl_ports):
                continue
            self.log.emit(f"  探测 {device} ({desc}) …")
            ver = probe_port(device, self._baud, timeout=0.5)
            if ver is not None:
                self.log.emit(f"  ✓ {device} — Bootloader v{ver}")
                self.found.emit(device, desc, ver)
                self.scan_finished.emit(True, "已找到设备")
                return
            else:
                self.log.emit(f"  ✗ {device} 无应答")

        # 未找到 Bootloader 设备，不主动尝试 APP→Bootloader 转换
        self.scan_finished.emit(False, "未发现 Bootloader 设备")


class UpgradeWorker(QThread):
    """
    信号:
        log(str)              — 一行日志文本
        progress(int, int)    — (已完成字节, 总字节)
        info(dict)            — 设备信息 (query_info 返回的字典)
        finished(bool, str)   — (成功?, 结果消息)
    """

    log      = pyqtSignal(str)
    progress = pyqtSignal(int, int)
    info     = pyqtSignal(dict)
    finished = pyqtSignal(bool, str)

    # 操作类型常量
    OP_PING       = "ping"
    OP_QUERY      = "query"
    OP_ERASE      = "erase"
    OP_UPGRADE    = "upgrade"
    OP_JUMP       = "jump"
    OP_REBOOT     = "reboot"
    OP_SET_PART_A = "set_part_a"
    OP_SET_PART_B = "set_part_b"
    OP_ENTER_BOOT = "enter_boot"

    def __init__(self, port: str, baud: int, operation: str,
                 firmware: bytes = b"", auto_jump: bool = True,
                 parent=None):
        super().__init__(parent)
        self._port      = port
        self._baud      = baud
        self._operation = operation
        self._firmware  = firmware
        self._auto_jump = auto_jump
        self._abort     = False

    def abort(self):
        """请求中止（当前正执行的 transact 会在超时后停止）。"""
        self._abort = True

    # ─── 内部辅助 ─────────────────────────────────────────────────────────────

    def _emit_log(self, msg: str):
        self.log.emit(msg)

    def _emit_progress(self, sent: int, total: int):
        self.progress.emit(sent, total)

    def _emit_info(self, info: dict):
        self.info.emit(info)

    # ─── 线程入口 ─────────────────────────────────────────────────────────────

    def run(self):
        comm = None
        try:
            self._emit_log(f"正在连接 {self._port} @ {self._baud} bps …")
            comm = BLComm(self._port, self._baud)
            # 让 Bootloader 把 info 字典通过信号回传 UI
            bl = Bootloader(
                comm,
                progress_cb=self._emit_progress,
                log_cb=self._emit_log,
                info_cb=self._emit_info,
            )

            if self._operation == self.OP_PING:
                bl.ping()

            elif self._operation == self.OP_QUERY:
                bl.ping()
                bl.query_info()

            elif self._operation == self.OP_ERASE:
                bl.ping()
                bl.erase()

            elif self._operation == self.OP_UPGRADE:
                if not self._firmware:
                    raise RuntimeError("未选择固件文件")
                bl.ping()
                bl.upgrade(self._firmware)
                # 仅在 upgrade() 完整成功（含 FINISH）后才允许 JUMP。
                # 中途断线会抛异常，绝不能再跳转到半截固件。
                if self._auto_jump:
                    try:
                        bl.jump()
                    except Exception as exc:
                        if is_serial_disconnect(exc):
                            self._emit_log(
                                f"设备已跳转（串口断开属正常）: {exc}")
                        else:
                            raise

            elif self._operation == self.OP_JUMP:
                bl.jump()

            elif self._operation == self.OP_REBOOT:
                bl.ping()
                bl.reboot()

            elif self._operation == self.OP_SET_PART_A:
                bl.ping()
                bl.set_partition(0)

            elif self._operation == self.OP_SET_PART_B:
                bl.ping()
                bl.set_partition(1)

            elif self._operation == self.OP_ENTER_BOOT:
                bl.enter_boot_from_app()

            else:
                raise RuntimeError(f"未知操作: {self._operation}")

            self.finished.emit(True, "操作成功完成 ✓")

        except Exception as exc:
            # 升级/跳转/复位后 CDC 会掉线，Windows 报 ClearCommError，属假失败
            if is_serial_disconnect(exc) and self._operation in (
                self.OP_UPGRADE, self.OP_JUMP,
                self.OP_REBOOT, self.OP_ENTER_BOOT,
            ):
                self._emit_log(f"设备已断开（属正常）: {exc}")
                self.finished.emit(True, "操作成功完成 ✓")
            else:
                self.finished.emit(False, f"操作失败: {exc}")
        finally:
            if comm:
                comm.close()
