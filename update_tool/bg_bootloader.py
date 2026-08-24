#!/usr/bin/env python3
"""
bg_bootloader.py — BG Bootloader Host Tool v2.1  (PyQt5 GUI)
USB CDC 固件升级 & 跳转上位机

功能:
  - 自动扫描串口探测 Bootloader 设备
  - 握手 / 擦除 / 升级 / 跳转 / 重启 / 查询分区信息 / 设置活跃分区
  - 实时进度与日志显示
  - 升级后自动跳转可选

依赖:
  pip install pyserial PyQt5
"""

import sys
import os
import time
from pathlib import Path

from PyQt5.QtCore import Qt, QTimer, QSize
from PyQt5.QtGui import QFont, QColor, QIcon, QPalette, QLinearGradient, QBrush, QGuiApplication
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget,
    QVBoxLayout, QHBoxLayout, QGridLayout, QFormLayout,
    QLabel, QComboBox, QPushButton, QProgressBar,
    QTextEdit, QFileDialog, QCheckBox, QMessageBox,
    QFrame, QSizePolicy, QGroupBox,
)

from bl_core import (
    list_ports, list_bootloader_ports, identify_port, identify_usb_ids,
    is_bg_bootloader_id, BL_VID, BL_PID, BG_USB_PID,
)
from worker import AutoScanWorker, UpgradeWorker


# ═══════════════════════════════════════════════════════════════════════════════
#  样式表 (QSS) — 层次分明、主次分明
# ═══════════════════════════════════════════════════════════════════════════════
QSS = """
QWidget {
    color: #1f2933;
    font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
    font-size: 13px;
}

QMainWindow {
    background-color: #f5f7fa;
}

/* ── 卡片容器：白色圆角 + 阴影感 ── */
QFrame#Card {
    background-color: #ffffff;
    border: 1px solid #e1e7ef;
    border-radius: 8px;
}

/* ── 分组标题（H1 / H2 风格） ── */
QLabel#H1 {
    font-size: 16px;
    font-weight: 600;
    color: #1f2933;
    padding: 2px 0px;
}
QLabel#H2 {
    font-size: 13px;
    font-weight: 600;
    color: #4b5563;
    padding: 0px;
}
QLabel#Caption {
    font-size: 11px;
    color: #6b7280;
}
QLabel#Muted {
    color: #6b7280;
}

/* ── 状态标签 ── */
QLabel#StatusConnected {
    color: #10b981;
    font-weight: 600;
}
QLabel#StatusDisconnected {
    color: #9ca3af;
    font-weight: 600;
}
QLabel#StatusError {
    color: #ef4444;
    font-weight: 600;
}
QLabel#StatusBusy {
    color: #f59e0b;
    font-weight: 600;
}

/* ── 顶部产品身份 Banner ── */
QLabel#ProductBannerDisconnected {
    background-color: #ef4444;
    color: #ffffff;
    font-size: 15px;
    font-weight: 700;
    padding: 10px 16px;
    border-radius: 8px;
}
QLabel#ProductBannerConnected {
    background-color: #10b981;
    color: #ffffff;
    font-size: 15px;
    font-weight: 700;
    padding: 10px 16px;
    border-radius: 8px;
}
QLabel#ProductBannerBusy {
    background-color: #f59e0b;
    color: #ffffff;
    font-size: 15px;
    font-weight: 700;
    padding: 10px 16px;
    border-radius: 8px;
}

/* ── 数据展示（信息卡） ── */
QLabel#InfoKey {
    color: #6b7280;
    font-size: 11px;
}
QLabel#InfoVal {
    color: #1f2933;
    font-weight: 500;
    font-family: "Consolas", "Cascadia Mono", monospace;
}

/* ── 按钮 ── */
QPushButton {
    background-color: #ffffff;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 6px 14px;
    color: #374151;
}
QPushButton:hover {
    background-color: #f3f4f6;
    border-color: #9ca3af;
}
QPushButton:pressed {
    background-color: #e5e7eb;
}
QPushButton:disabled {
    color: #9ca3af;
    background-color: #f9fafb;
    border-color: #e5e7eb;
}

/* ── 主按钮（升级 / 跳转） ── */
QPushButton#Primary {
    background-color: #2563eb;
    border: 1px solid #2563eb;
    color: #ffffff;
    font-weight: 600;
}
QPushButton#Primary:hover {
    background-color: #1d4ed8;
    border-color: #1d4ed8;
}
QPushButton#Primary:pressed {
    background-color: #1e40af;
}
QPushButton#Primary:disabled {
    background-color: #93c5fd;
    border-color: #93c5fd;
    color: #ffffff;
}

/* ── 危险按钮（擦除） ── */
QPushButton#Danger {
    background-color: #ffffff;
    border: 1px solid #fca5a5;
    color: #dc2626;
}
QPushButton#Danger:hover {
    background-color: #fef2f2;
    border-color: #ef4444;
}

/* ── 成功按钮（跳转） ── */
QPushButton#Success {
    background-color: #ffffff;
    border: 1px solid #6ee7b7;
    color: #047857;
}
QPushButton#Success:hover {
    background-color: #ecfdf5;
    border-color: #10b981;
}

/* ── Boot 模式按钮（醒目橙色） ── */
QPushButton#BootMode {
    background-color: #fff7ed;
    border: 1px solid #fdba74;
    color: #c2410c;
    font-weight: 600;
}
QPushButton#BootMode:hover {
    background-color: #ffedd5;
    border-color: #f97316;
}
QPushButton#BootMode:pressed {
    background-color: #fed7aa;
}

/* ── 输入控件 ── */
QComboBox {
    background-color: #ffffff;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 4px 8px;
    min-height: 22px;
}
QComboBox:hover {
    border-color: #9ca3af;
}
QComboBox::drop-down {
    border: none;
    width: 22px;
}
QComboBox QAbstractItemView {
    background-color: #ffffff;
    border: 1px solid #d1d5db;
    selection-background-color: #dbeafe;
    selection-color: #1e40af;
}

/* ── 进度条 ── */
QProgressBar {
    background-color: #f3f4f6;
    border: 1px solid #e5e7eb;
    border-radius: 6px;
    text-align: center;
    color: #1f2933;
    font-weight: 600;
    min-height: 22px;
}
QProgressBar::chunk {
    background-color: #2563eb;
    border-radius: 5px;
}

/* ── 日志区 ── */
QTextEdit#LogView {
    background-color: #0f172a;
    color: #e2e8f0;
    border: 1px solid #1e293b;
    border-radius: 6px;
    font-family: "Consolas", "Cascadia Mono", monospace;
    font-size: 12px;
    padding: 6px;
}

/* ── 复选框 ── */
QCheckBox {
    color: #374151;
    spacing: 6px;
}
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid #d1d5db;
    border-radius: 3px;
    background-color: #ffffff;
}
QCheckBox::indicator:checked {
    background-color: #2563eb;
    border-color: #2563eb;
}
"""


# ═══════════════════════════════════════════════════════════════════════════════
#  Helper: 卡片容器
# ═══════════════════════════════════════════════════════════════════════════════
def make_card() -> QFrame:
    """创建一个白色卡片容器。"""
    frame = QFrame()
    frame.setObjectName("Card")
    frame.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
    return frame


def make_h1(text: str) -> QLabel:
    lbl = QLabel(text)
    lbl.setObjectName("H1")
    return lbl


def make_h2(text: str) -> QLabel:
    lbl = QLabel(text)
    lbl.setObjectName("H2")
    return lbl


def make_caption(text: str) -> QLabel:
    lbl = QLabel(text)
    lbl.setObjectName("Caption")
    return lbl


def make_info_pair(key: str, value: str = "—") -> tuple:
    """创建一对 (Key, Value) 标签，用于设备信息卡。"""
    k = QLabel(key)
    k.setObjectName("InfoKey")
    v = QLabel(value)
    v.setObjectName("InfoVal")
    v.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
    v.setMinimumWidth(80)
    return k, v


# ═══════════════════════════════════════════════════════════════════════════════
#  MainWindow
# ═══════════════════════════════════════════════════════════════════════════════
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("BG Bootloader — USB CDC 升级工具 v2.1")
        self.setMinimumSize(820, 640)

        self._fw_path: str = ""
        self._worker: UpgradeWorker | None = None
        self._scan_worker: AutoScanWorker | None = None
        self._boot_mode: int = 0   # 0 = single, 1 = dual
        self._connected_product: str = ""
        self._connected_port: str = ""

        self._build_ui()
        self._connect_signals()
        self._set_banner_disconnected()

        # 启动后自动扫描
        QTimer.singleShot(200, self._on_auto_scan)

    # ────────────────────────────────────────────────────────────────────────
    #  UI 构建 — 三层结构：①连接 ②操作 ③日志
    # ────────────────────────────────────────────────────────────────────────
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(16, 16, 16, 16)
        root.setSpacing(12)

        # 顶部产品身份 Banner：未连接红 / 已连接绿+产品名
        self.lbl_banner = QLabel("未连接")
        self.lbl_banner.setObjectName("ProductBannerDisconnected")
        self.lbl_banner.setAlignment(Qt.AlignCenter)
        self.lbl_banner.setMinimumHeight(40)
        root.addWidget(self.lbl_banner)

        # ════════════════════════════════════════════════════════════════════
        #  Layer 1: 连接状态卡
        # ════════════════════════════════════════════════════════════════════
        card_conn = make_card()
        card_conn_layout = QVBoxLayout(card_conn)
        card_conn_layout.setContentsMargins(16, 12, 16, 12)
        card_conn_layout.setSpacing(8)

        # 标题行：H1 + 状态标签（右对齐）
        title_row = QHBoxLayout()
        title_row.addWidget(make_h1("设备连接"))
        title_row.addStretch()
        self.lbl_status = QLabel("● 未连接")
        self.lbl_status.setObjectName("StatusDisconnected")
        title_row.addWidget(self.lbl_status, alignment=Qt.AlignRight | Qt.AlignVCenter)
        card_conn_layout.addLayout(title_row)

        # 控件行：串口 / 波特率 / 刷新 / 自动扫描
        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(8)

        ctrl_row.addWidget(make_caption("串口"))
        self.combo_port = QComboBox()
        self.combo_port.setMinimumWidth(240)
        ctrl_row.addWidget(self.combo_port, 1)

        ctrl_row.addSpacing(12)
        ctrl_row.addWidget(make_caption("波特率"))
        self.combo_baud = QComboBox()
        self.combo_baud.addItems(["9600", "19200", "38400", "57600",
                                  "115200", "230400", "460800", "921600",
                                  "2000000"])
        self.combo_baud.setCurrentText("2000000")
        self.combo_baud.setFixedWidth(96)
        ctrl_row.addWidget(self.combo_baud)

        ctrl_row.addSpacing(12)
        self.btn_refresh = QPushButton("刷新")
        self.btn_refresh.setFixedWidth(64)
        ctrl_row.addWidget(self.btn_refresh)

        self.btn_scan = QPushButton("自动扫描")
        self.btn_scan.setFixedWidth(88)
        ctrl_row.addWidget(self.btn_scan)

        self.btn_enter_boot = QPushButton("进入 Boot 模式")
        self.btn_enter_boot.setObjectName("BootMode")
        self.btn_enter_boot.setFixedWidth(120)
        self.btn_enter_boot.setVisible(False)  # 默认隐藏，非 Bootloader 设备才显示
        self.btn_enter_boot.setToolTip("发送 'boot' 命令让 APP 重启到 Bootloader 升级模式")
        ctrl_row.addWidget(self.btn_enter_boot)

        card_conn_layout.addLayout(ctrl_row)
        root.addWidget(card_conn)

        # ════════════════════════════════════════════════════════════════════
        #  Layer 2: 固件 + 操作 (主操作区)
        # ════════════════════════════════════════════════════════════════════
        card_op = make_card()
        card_op_layout = QVBoxLayout(card_op)
        card_op_layout.setContentsMargins(16, 12, 16, 12)
        card_op_layout.setSpacing(10)

        # ── 2.1 固件选择 ──
        fw_row = QHBoxLayout()
        fw_row.setSpacing(8)
        self.lbl_fw = QLabel("未选择固件文件")
        self.lbl_fw.setObjectName("Muted")
        self.lbl_fw.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        fw_row.addWidget(make_h2("固件"))
        fw_row.addWidget(self.lbl_fw, 1)
        self.btn_browse = QPushButton("浏览 …")
        self.btn_browse.setFixedWidth(88)
        fw_row.addWidget(self.btn_browse)
        card_op_layout.addLayout(fw_row)

        # 分隔线
        sep = QFrame()
        sep.setFrameShape(QFrame.HLine)
        sep.setStyleSheet("color: #e1e7ef; background-color: #e1e7ef; max-height: 1px;")
        card_op_layout.addWidget(sep)

        # ── 2.2 操作按钮（主次分明）──
        op_row = QHBoxLayout()
        op_row.setSpacing(8)

        # 次操作（左侧，灰底）— 双分区模式才显示
        self.btn_ping = QPushButton("握手")
        self.btn_ping.setFixedHeight(36)
        self.btn_ping.setVisible(False)
        op_row.addWidget(self.btn_ping)

        self.btn_query = QPushButton("查询信息")
        self.btn_query.setFixedHeight(36)
        self.btn_query.setVisible(False)
        op_row.addWidget(self.btn_query)

        self.btn_erase = QPushButton("擦除")
        self.btn_erase.setObjectName("Danger")
        self.btn_erase.setFixedHeight(36)
        self.btn_erase.setVisible(False)
        op_row.addWidget(self.btn_erase)

        op_row.addStretch()

        # 主操作（右侧，彩色突出）
        self.btn_jump = QPushButton("跳转")
        self.btn_jump.setObjectName("Success")
        self.btn_jump.setFixedHeight(36)
        self.btn_jump.setVisible(False)
        op_row.addWidget(self.btn_jump)

        self.btn_upgrade = QPushButton("▶ 升级")
        self.btn_upgrade.setObjectName("Primary")
        self.btn_upgrade.setFixedHeight(36)
        self.btn_upgrade.setMinimumWidth(120)
        op_row.addWidget(self.btn_upgrade)

        card_op_layout.addLayout(op_row)

        # ── 2.3 升级选项 ──
        opt_row = QHBoxLayout()
        self.chk_auto_jump = QCheckBox("升级后自动跳转到应用")
        self.chk_auto_jump.setChecked(True)
        self.chk_auto_jump.setVisible(False)
        opt_row.addWidget(self.chk_auto_jump)
        opt_row.addStretch()

        self.btn_set_part_a = QPushButton("设为 A 区")
        self.btn_set_part_a.setFixedHeight(28)
        self.btn_set_part_a.setFixedWidth(80)
        self.btn_set_part_a.setVisible(False)
        opt_row.addWidget(self.btn_set_part_a)

        self.btn_set_part_b = QPushButton("设为 B 区")
        self.btn_set_part_b.setFixedHeight(28)
        self.btn_set_part_b.setFixedWidth(80)
        self.btn_set_part_b.setVisible(False)
        opt_row.addWidget(self.btn_set_part_b)

        self.btn_reboot = QPushButton("重启设备")
        self.btn_reboot.setFixedHeight(28)
        self.btn_reboot.setFixedWidth(80)
        self.btn_reboot.setVisible(False)
        opt_row.addWidget(self.btn_reboot)

        card_op_layout.addLayout(opt_row)
        root.addWidget(card_op)

        # ════════════════════════════════════════════════════════════════════
        #  Layer 2.5: 进度条 + 设备信息
        # ════════════════════════════════════════════════════════════════════
        info_row = QHBoxLayout()
        info_row.setSpacing(12)

        # 进度条 (左侧，主要)
        prog_col = QVBoxLayout()
        prog_col.setSpacing(2)
        prog_col.addWidget(make_caption("升级进度"))
        self.progress = QProgressBar()
        self.progress.setTextVisible(True)
        self.progress.setValue(0)
        self.progress.setFormat("准备就绪")
        prog_col.addWidget(self.progress)
        info_row.addLayout(prog_col, 3)

        # 设备信息 (右侧，紧凑)
        info_card = make_card()
        info_grid = QGridLayout(info_card)
        info_grid.setContentsMargins(12, 8, 12, 8)
        info_grid.setHorizontalSpacing(10)
        info_grid.setVerticalSpacing(4)

        self.lbl_info_protocol = make_info_pair("协议版本")
        self.lbl_info_mode = make_info_pair("启动模式")
        self.lbl_info_size = make_info_pair("固件容量")
        self.lbl_info_active = make_info_pair("当前分区")
        self.lbl_info_backup = make_info_pair("备份分区")
        self.lbl_info_failcnt = make_info_pair("失败次数")

        for row, (k, v) in enumerate([
            self.lbl_info_protocol,
            self.lbl_info_mode,
            self.lbl_info_size,
            self.lbl_info_active,
            self.lbl_info_backup,
            self.lbl_info_failcnt,
        ]):
            info_grid.addWidget(k, row, 0)
            info_grid.addWidget(v, row, 1)
        info_grid.setColumnStretch(1, 1)

        info_row.addWidget(info_card, 2)

        # 默认隐藏，查询后根据模式显示
        self._info_card = info_card
        info_card.setVisible(False)

        root.addLayout(info_row)

        # ════════════════════════════════════════════════════════════════════
        #  Layer 3: 日志区 (底部，占满剩余空间)
        # ════════════════════════════════════════════════════════════════════
        log_header = QHBoxLayout()
        log_header.addWidget(make_h2("日志"))
        log_header.addStretch()
        self.btn_clear_log = QPushButton("清空")
        self.btn_clear_log.setFixedWidth(64)
        self.btn_clear_log.setFixedHeight(24)
        log_header.addWidget(self.btn_clear_log)

        root.addLayout(log_header)

        self.txt_log = QTextEdit()
        self.txt_log.setObjectName("LogView")
        self.txt_log.setReadOnly(True)
        root.addWidget(self.txt_log, 1)   # stretch=1 让日志区占满剩余空间

    # ────────────────────────────────────────────────────────────────────────
    #  信号绑定
    # ────────────────────────────────────────────────────────────────────────
    def _connect_signals(self):
        self.btn_refresh.clicked.connect(self._refresh_ports)
        self.btn_scan.clicked.connect(self._on_auto_scan)
        self.btn_browse.clicked.connect(self._browse_firmware)
        self.btn_enter_boot.clicked.connect(self._on_enter_boot)
        self.combo_port.currentIndexChanged.connect(self._on_port_changed)

        self.btn_ping.clicked.connect(lambda: self._start_op(UpgradeWorker.OP_PING))
        self.btn_query.clicked.connect(lambda: self._start_op(UpgradeWorker.OP_QUERY))
        self.btn_erase.clicked.connect(lambda: self._start_op(UpgradeWorker.OP_ERASE))
        self.btn_upgrade.clicked.connect(lambda: self._start_op(UpgradeWorker.OP_UPGRADE))
        self.btn_jump.clicked.connect(lambda: self._start_op(UpgradeWorker.OP_JUMP))
        self.btn_reboot.clicked.connect(lambda: self._start_op(UpgradeWorker.OP_REBOOT))
        self.btn_set_part_a.clicked.connect(lambda: self._start_op(UpgradeWorker.OP_SET_PART_A))
        self.btn_set_part_b.clicked.connect(lambda: self._start_op(UpgradeWorker.OP_SET_PART_B))

        self.btn_clear_log.clicked.connect(self.txt_log.clear)

    # ────────────────────────────────────────────────────────────────────────
    #  串口列表
    # ────────────────────────────────────────────────────────────────────────
    def _refresh_ports(self):
        current = self.combo_port.currentData()
        self.combo_port.clear()
        ports = list_ports()
        for device, desc in ports:
            self.combo_port.addItem(f"{device}  —  {desc}", userData=device)
        # 尝试恢复之前的选择
        if current:
            for i in range(self.combo_port.count()):
                if self.combo_port.itemData(i) == current:
                    self.combo_port.setCurrentIndex(i)
                    break
        self._on_port_changed()

    def _on_port_changed(self):
        """端口选择变化时，根据 USB 身份协议决定是否显示'进入 Boot 模式'按钮。"""
        import serial.tools.list_ports as sp
        port_data = self.combo_port.currentData()
        if not port_data:
            self.btn_enter_boot.setVisible(False)
            return

        is_bootloader = False
        for p in sp.comports():
            if p.device == port_data:
                vid = getattr(p, 'vid', None)
                pid = getattr(p, 'pid', None)
                is_bootloader = is_bg_bootloader_id(vid, pid) or (
                    identify_usb_ids(vid, pid) is not None)
                break

        # 非 Bootloader 设备 → 显示"进入 Boot 模式"按钮
        self.btn_enter_boot.setVisible(not is_bootloader)

    def _on_enter_boot(self):
        """发送 'boot' shell 命令让 APP 进入 Bootloader 烧录模式。"""
        import serial as pyserial
        port_data = self.combo_port.currentData()
        if not port_data:
            QMessageBox.warning(self, "未选择串口", "请先选择串口")
            return

        baud = int(self.combo_baud.currentText())
        self._append_log(f"发送 'boot' 命令到 {port_data} …")
        self._set_busy(True)
        self._set_status("● 正在进入 Boot 模式 …", "StatusBusy")

        try:
            ser = pyserial.Serial(port_data, baudrate=baud, timeout=0.5)
        except (pyserial.SerialException, OSError) as e:
            self._append_log(f"✗ 无法打开 {port_data}: {e}")
            self._set_busy(False)
            self._set_status("● 连接失败", "StatusError")
            return

        try:
            # 发送 shell 命令让 APP 写 burn flag 并复位
            ser.write(b"boot\r\n")
            ser.flush()
            time.sleep(0.3)
            # 读取可能的响应
            try:
                resp = ser.read(256)
                if resp:
                    text = resp.decode('utf-8', errors='replace').strip()
                    if text:
                        self._append_log(f"  响应: {text}")
            except Exception:
                pass
        finally:
            ser.close()

        # 等待设备重新枚举（APP→复位→Bootloader，VID/PID 变化）
        self._append_log("  等待设备重新枚举 (3s) …")
        QTimer.singleShot(3000, self._after_enter_boot)

    def _after_enter_boot(self):
        """进入 Boot 模式后重新扫描设备。"""
        self._refresh_ports()

        # 通过 VID/PID 查找 Bootloader
        bl_ports = list_bootloader_ports()
        if bl_ports:
            for bdev, bdesc in bl_ports:
                info = identify_port(bdev)
                product = info["product"] if info else "BG"
                self._append_log(f"  发现 {product} Bootloader: {bdev} ({bdesc})")
                # 选中该端口
                for i in range(self.combo_port.count()):
                    if self.combo_port.itemData(i) == bdev:
                        self.combo_port.setCurrentIndex(i)
                        break
                self._set_connected(bdev, product)
                break
            # 自动查询信息
            QTimer.singleShot(300, lambda: self._start_op(UpgradeWorker.OP_QUERY))
            self._set_status("● 已连接 (Bootloader)", "StatusConnected")
        else:
            self._append_log("  ✗ 未发现 Bootloader 设备，请确认设备已重启")
            self._set_status("● 未连接", "StatusDisconnected")
            self._set_banner_disconnected()

        self._set_busy(False)

    # ────────────────────────────────────────────────────────────────────────
    #  自动扫描
    # ────────────────────────────────────────────────────────────────────────
    def _on_auto_scan(self):
        if self._scan_worker and self._scan_worker.isRunning():
            return
        self._refresh_ports()
        baud = int(self.combo_baud.currentText())
        self._scan_worker = AutoScanWorker(baud, parent=self)
        self._scan_worker.log.connect(self._append_log)
        self._scan_worker.found.connect(self._on_device_found)
        self._scan_worker.scan_finished.connect(self._on_scan_finished)
        self._set_busy(True, scanning=True)
        self._scan_worker.start()

    def _on_device_found(self, port: str, desc: str, ver: int):
        # 选中找到的端口
        for i in range(self.combo_port.count()):
            if self.combo_port.itemData(i) == port:
                self.combo_port.setCurrentIndex(i)
                break
        info = identify_port(port)
        product = info["product"] if info else "BG"
        self._set_connected(port, product, ver)
        self._set_status(f"● 已连接  {port}  (协议 v{ver})", "StatusConnected")
        # 自动查询分区信息，触发 UI 模式适配
        QTimer.singleShot(300, lambda: self._start_op(UpgradeWorker.OP_QUERY))

    def _on_scan_finished(self, found: bool, msg: str):
        self._set_busy(False)
        if not found:
            self._set_status("● 未连接", "StatusDisconnected")
            self._set_banner_disconnected()
            self._append_log(msg)

    # ────────────────────────────────────────────────────────────────────────
    #  固件选择
    # ────────────────────────────────────────────────────────────────────────
    def _browse_firmware(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "选择固件文件", "",
            "二进制文件 (*.bin);;所有文件 (*)")
        if path:
            self._fw_path = path
            size = os.path.getsize(path)
            name = Path(path).name
            self.lbl_fw.setText(f"{name}  ({size:,} 字节 / {size/1024:.1f} KB)")
            self.lbl_fw.setStyleSheet("color: #1f2933; font-weight: 500;")

    # ────────────────────────────────────────────────────────────────────────
    #  操作执行
    # ────────────────────────────────────────────────────────────────────────
    def _start_op(self, operation: str):
        if self._worker and self._worker.isRunning():
            QMessageBox.warning(self, "操作进行中", "请等待当前操作完成")
            return

        port_data = self.combo_port.currentData()
        if not port_data:
            QMessageBox.warning(self, "未选择串口", "请先选择串口或等待自动扫描完成")
            return

        firmware = b""
        if operation == UpgradeWorker.OP_UPGRADE:
            if not self._fw_path:
                QMessageBox.warning(self, "未选择固件", "请先选择固件文件")
                return
            with open(self._fw_path, "rb") as f:
                firmware = f.read()
            if not firmware:
                QMessageBox.warning(self, "固件为空", "固件文件内容为空")
                return

        # 危险操作确认
        if operation == UpgradeWorker.OP_ERASE:
            ret = QMessageBox.question(
                self, "确认擦除",
                "擦除将清除应用区所有固件数据。\n确定继续？",
                QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
            if ret != QMessageBox.Yes:
                return

        baud = int(self.combo_baud.currentText())
        auto_jump = self.chk_auto_jump.isChecked()

        self.progress.setValue(0)
        self.progress.setFormat("进行中 …")
        self._set_busy(True)
        self._set_status("● 操作中", "StatusBusy")
        self._set_banner_busy()

        self._worker = UpgradeWorker(
            port_data, baud, operation,
            firmware=firmware, auto_jump=auto_jump, parent=self)
        self._worker.log.connect(self._append_log)
        self._worker.progress.connect(self._on_progress)
        self._worker.info.connect(self._on_device_info)
        self._worker.finished.connect(self._on_op_finished)
        self._worker.start()

    def _on_progress(self, sent: int, total: int):
        if total > 0:
            self.progress.setMaximum(total)
            self.progress.setValue(sent)
            pct = sent * 100 // total
            self.progress.setFormat(f"{pct}%  ({sent:,} / {total:,} 字节)")

    def _on_device_info(self, info: dict):
        """接收 worker 报告的设备信息并更新 UI。"""
        self._boot_mode = info.get('boot_mode', 0)
        is_single = self._boot_mode == 0

        self.lbl_info_protocol[1].setText(f"v{info.get('protocol_ver', '?')}")
        self.lbl_info_mode[1].setText(
            "单分区" if is_single else "双分区 A/B")

        part_a_size = info.get('part_a_size', 0)
        self.lbl_info_size[1].setText(
            f"{part_a_size // 1024} KB" if part_a_size else "—")

        active = info.get('active_part', 0)
        self.lbl_info_active[1].setText("B" if active == 1 else "A")
        self.lbl_info_backup[1].setText("A" if active == 1 else "B")
        self.lbl_info_failcnt[1].setText(str(info.get('boot_fail_cnt', 0)))

        # 根据模式切换 UI
        self._apply_ui_mode()

    def _apply_ui_mode(self):
        """根据 boot_mode 显示/隐藏 UI 元素。"""
        is_single = self._boot_mode == 0

        # 双分区专属按钮
        self.btn_ping.setVisible(not is_single)
        self.btn_query.setVisible(not is_single)
        self.btn_erase.setVisible(not is_single)
        self.btn_jump.setVisible(not is_single)
        self.chk_auto_jump.setVisible(not is_single)
        self.btn_set_part_a.setVisible(not is_single)
        self.btn_set_part_b.setVisible(not is_single)
        self.btn_reboot.setVisible(not is_single)

        # 双分区信息行（当前分区、备份分区）
        self.lbl_info_active[0].setVisible(not is_single)
        self.lbl_info_active[1].setVisible(not is_single)
        self.lbl_info_backup[0].setVisible(not is_single)
        self.lbl_info_backup[1].setVisible(not is_single)

        # 单分区显示信息卡（精简版），双分区也显示
        self._info_card.setVisible(True)

    def _on_op_finished(self, success: bool, msg: str):
        self._set_busy(False)
        self._append_log(msg)
        if success:
            self._set_status("● 已连接", "StatusConnected")
            if self._connected_product:
                self._set_banner_connected(self._connected_product, self._connected_port)
            else:
                port = self.combo_port.currentData() or ""
                info = identify_port(port) if port else None
                product = info["product"] if info else "BG"
                self._set_connected(port, product)
            if self.progress.value() == 0 or self.progress.value() == self.progress.maximum():
                self.progress.setFormat("完成")
        else:
            self._set_status("● 操作失败", "StatusError")
            self.progress.setFormat("失败")
            if self._connected_product:
                self._set_banner_connected(self._connected_product, self._connected_port)

    # ────────────────────────────────────────────────────────────────────────
    #  产品 Banner / 状态
    # ────────────────────────────────────────────────────────────────────────
    def _polish_label(self, lbl: QLabel, object_name: str):
        lbl.setObjectName(object_name)
        lbl.style().unpolish(lbl)
        lbl.style().polish(lbl)

    def _set_banner_disconnected(self):
        self._connected_product = ""
        self._connected_port = ""
        self.lbl_banner.setText("未连接")
        self._polish_label(self.lbl_banner, "ProductBannerDisconnected")

    def _set_banner_connected(self, product: str, port: str = "", ver=None):
        text = f"已连接 · {product}"
        if port:
            text += f"  ({port})"
        if ver is not None:
            text += f"  · 协议 v{ver}"
        self.lbl_banner.setText(text)
        self._polish_label(self.lbl_banner, "ProductBannerConnected")

    def _set_banner_busy(self):
        product = self._connected_product or "设备"
        self.lbl_banner.setText(f"操作中 · {product}")
        self._polish_label(self.lbl_banner, "ProductBannerBusy")

    def _set_connected(self, port: str, product: str, ver=None):
        self._connected_port = port or ""
        self._connected_product = product or "BG"
        self._set_banner_connected(self._connected_product, self._connected_port, ver)

    def _set_status(self, text: str, object_name: str):
        self.lbl_status.setText(text)
        self._polish_label(self.lbl_status, object_name)
        if object_name == "StatusDisconnected":
            self._set_banner_disconnected()
        elif object_name == "StatusBusy":
            self._set_banner_busy()
        elif object_name == "StatusConnected":
            if self._connected_product:
                self._set_banner_connected(self._connected_product, self._connected_port)
            else:
                port = self.combo_port.currentData() or ""
                info = identify_port(port) if port else None
                if info:
                    self._set_connected(port, info["product"])
                elif port:
                    self._set_banner_connected("BG Bootloader", port)

    def _append_log(self, msg: str):
        # 简单着色：成功/失败/警告
        color = "#e2e8f0"
        if msg.startswith("✓") or "完成" in msg or "成功" in msg:
            color = "#86efac"
        elif msg.startswith("✗") or "失败" in msg or "错误" in msg or "异常" in msg:
            color = "#fca5a5"
        elif msg.startswith("  [") or msg.startswith("  探测"):
            color = "#94a3b8"
        elif msg.startswith("正在"):
            color = "#93c5fd"
        self.txt_log.append(f'<span style="color:{color};">{msg}</span>')
        sb = self.txt_log.verticalScrollBar()
        sb.setValue(sb.maximum())

    def _set_busy(self, busy: bool, scanning: bool = False):
        """禁用 / 启用操作按钮。"""
        enabled = not busy
        self.btn_ping.setEnabled(enabled)
        self.btn_query.setEnabled(enabled)
        self.btn_erase.setEnabled(enabled)
        self.btn_upgrade.setEnabled(enabled)
        self.btn_jump.setEnabled(enabled)
        self.btn_reboot.setEnabled(enabled)
        self.btn_set_part_a.setEnabled(enabled)
        self.btn_set_part_b.setEnabled(enabled)
        self.btn_browse.setEnabled(enabled)
        self.btn_scan.setEnabled(enabled)
        self.btn_refresh.setEnabled(enabled)
        self.btn_enter_boot.setEnabled(enabled)

    def closeEvent(self, event):
        # 确保后台线程退出
        if self._scan_worker and self._scan_worker.isRunning():
            self._scan_worker.abort()
            self._scan_worker.wait(2000)
        if self._worker and self._worker.isRunning():
            self._worker.abort()
            self._worker.wait(2000)
        event.accept()


# ═══════════════════════════════════════════════════════════════════════════════
#  入口
# ═══════════════════════════════════════════════════════════════════════════════
def main():
    # 高 DPI 支持
    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling, True)
    QApplication.setAttribute(Qt.AA_UseHighDpiPixmaps, True)

    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    app.setStyleSheet(QSS)

    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
