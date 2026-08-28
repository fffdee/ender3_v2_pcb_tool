from __future__ import annotations

import argparse
import base64
import json
import math
import re
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import openpyxl
from PyQt6.QtCore import QPointF, QRectF, Qt, QTimer
from PyQt6.QtGui import QAction, QColor, QFont, QPainter, QPainterPath, QPen, QPolygonF
from PyQt6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QDialog, QDoubleSpinBox, QFileDialog, QFormLayout,
    QFrame, QGridLayout, QGroupBox, QHBoxLayout, QHeaderView, QLabel, QLineEdit,
    QMainWindow, QMessageBox, QPlainTextEdit, QPushButton, QScrollArea,
    QSizePolicy, QSplitter, QStackedWidget, QStatusBar, QStyle, QTableWidget,
    QTableWidgetItem, QToolBar, QVBoxLayout, QWidget,
)

try:
    from gerber_parser import GerberPad, parse_gerber_file
except ImportError:
    from .gerber_parser import GerberPad, parse_gerber_file


ROOT = Path(__file__).resolve().parent
DEFAULT_WORKBOOK = ROOT.parent / "PickAndPlace_PCB_PCB_1048_looper_2_2025_10_07_2_2026_08_27.xlsx"
DISCOVERY_PORT = 8267
BRIDGE_PORT = 8266
UPLOAD_CHUNK_SIZE = 48


@dataclass
class WirelessDevice:
    name: str
    ip: str
    bridge_port: int = BRIDGE_PORT
    http_port: int = 80
    wifi: str = ""
    ap: bool = False


@dataclass
class SheetData:
    name: str
    headers: list[str]
    rows: list[dict[str, Any]]


@dataclass
class PointData:
    row_id: int
    raw: dict[str, Any]
    x: float
    y: float


@dataclass
class DispenseSegment:
    pad_id: int
    label: str
    start: tuple[float, float]
    end: tuple[float, float]

    @property
    def length(self) -> float:
        return math.hypot(self.end[0] - self.start[0], self.end[1] - self.start[1])


def load_workbook(path: Path) -> list[SheetData]:
    workbook = openpyxl.load_workbook(path, data_only=True, read_only=True)
    sheets: list[SheetData] = []
    try:
        for sheet in workbook.worksheets:
            iterator = sheet.iter_rows(values_only=True)
            first = next(iterator, None)
            if first is None:
                sheets.append(SheetData(sheet.title, [], []))
                continue
            headers = [str(value).strip() if value is not None else f"Column {index + 1}"
                       for index, value in enumerate(first)]
            rows: list[dict[str, Any]] = []
            for source_row in iterator:
                if not any(value is not None and str(value).strip() for value in source_row):
                    continue
                rows.append({headers[index]: value for index, value in enumerate(source_row)
                             if index < len(headers) and value is not None})
            sheets.append(SheetData(sheet.title, headers, rows))
    finally:
        workbook.close()
    if not sheets:
        raise ValueError("工作簿中没有工作表")
    return sheets


def parse_number(value: Any) -> float | None:
    if isinstance(value, (int, float)):
        return float(value)
    match = re.search(r"[+-]?(?:\d+\.?\d*|\.\d+)", str(value or "").replace(",", "."))
    return float(match.group(0)) if match else None


def gcode_number(value: float, digits: int = 3) -> str:
    return f"{value:.{digits}f}".rstrip("0").rstrip(".") or "0"


def gcode_label(value: Any, limit: int = 40) -> str:
    label = re.sub(r"[^A-Za-z0-9_.-]", "_", str(value or ""))[:limit]
    return label or "item"


class PathPreview(QWidget):
    def __init__(self) -> None:
        super().__init__()
        self.points: list[PointData] = []
        self.pads: list[GerberPad] = []
        self.segments: list[DispenseSegment] = []
        self.setMinimumSize(420, 280)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

    def set_points(self, points: list[PointData]) -> None:
        self.set_geometry(points, [], [])

    def set_geometry(self, points: list[PointData],
                     pads: list[GerberPad],
                     segments: list[DispenseSegment]) -> None:
        self.points = points
        self.pads = pads
        self.segments = segments
        self.update()

    def paintEvent(self, event) -> None:  # noqa: N802
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.fillRect(self.rect(), QColor("#ffffff"))
        painter.setPen(QPen(QColor("#e6eaeb"), 1))
        for x in range(0, self.width(), 24):
            painter.drawLine(x, 0, x, self.height())
        for y in range(0, self.height(), 24):
            painter.drawLine(0, y, self.width(), y)
        if not self.points and not self.pads:
            painter.setPen(QColor("#68777d"))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "没有选中的点胶对象")
            return

        xs = [p.x for p in self.points]
        ys = [p.y for p in self.points]
        for pad in self.pads:
            xs.extend(x for x, _ in pad.vertices)
            ys.extend(y for _, y in pad.vertices)
        for segment in self.segments:
            xs.extend([segment.start[0], segment.end[0]])
            ys.extend([segment.start[1], segment.end[1]])
        min_x, max_x = min(xs), max(xs)
        min_y, max_y = min(ys), max(ys)
        range_x, range_y = max(1.0, max_x - min_x), max(1.0, max_y - min_y)
        drawing = self.rect().adjusted(28, 22, -28, -22)
        scale = min(drawing.width() / range_x, drawing.height() / range_y)
        board_w, board_h = range_x * scale, range_y * scale
        left = drawing.center().x() - board_w / 2
        top = drawing.center().y() - board_h / 2

        def map_xy(x: float, y: float) -> QPointF:
            return QPointF(left + (x - min_x) * scale, top + (max_y - y) * scale)

        painter.setPen(QPen(QColor("#dbe2e3"), 1, Qt.PenStyle.DashLine))
        painter.setBrush(QColor(246, 249, 248, 190))
        painter.drawRect(QRectF(left, top, board_w, board_h))
        if self.pads:
            painter.setPen(QPen(QColor("#087f5b"), 1))
            painter.setBrush(QColor(8, 127, 91, 46))
            for pad in self.pads:
                painter.drawPolygon(QPolygonF([map_xy(x, y) for x, y in pad.vertices]))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.setPen(QPen(QColor("#d94841"), 1.35))
            for segment in self.segments:
                painter.drawLine(map_xy(*segment.start), map_xy(*segment.end))
        if self.points:
            path = QPainterPath(map_xy(self.points[0].x, self.points[0].y))
            for point in self.points[1:]:
                path.lineTo(map_xy(point.x, point.y))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.setPen(QPen(QColor(224, 122, 34, 145), 1.15))
            painter.drawPath(path)
            for index, point in enumerate(self.points):
                painter.setPen(QPen(QColor("#ffffff"), 1))
                painter.setBrush(QColor("#d94841" if index == 0 else "#087f5b"))
                painter.drawEllipse(map_xy(point.x, point.y), 3.8, 3.8)


class MainWindow(QMainWindow):
    def __init__(self, initial_file: Path | None = None, initial_gerber: Path | None = None) -> None:
        super().__init__()
        self.sheets: list[SheetData] = []
        self.rows: list[dict[str, Any]] = []
        self.selected_ids: set[int] = set()
        self.gerber_pads: list[GerberPad] = []
        self.selected_pad_ids: set[int] = set()
        self.current_gerber: Path | None = None
        self.gerber_warnings: list[str] = []
        self.current_file: Path | None = None
        self._updating_table = False
        self.devices: list[WirelessDevice] = []
        self.connected_device: WirelessDevice | None = None
        self.bridge_socket: socket.socket | None = None
        self.setWindowTitle("Banux PCB 锡膏路径生成器")
        self.resize(1440, 860)
        self.setMinimumSize(1050, 680)
        self._build_ui()
        self._connect_signals()
        self._apply_style()
        target = initial_file
        if target:
            QTimer.singleShot(0, lambda: self.import_workbook(target, show_error=False))
        if initial_gerber:
            QTimer.singleShot(50, lambda: self.import_gerber(initial_gerber, show_error=False))

    def _build_ui(self) -> None:
        toolbar = QToolBar("主工具栏")
        toolbar.setMovable(False)
        toolbar.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        self.addToolBar(toolbar)
        title = QLabel("PCB 锡膏路径生成器")
        title.setObjectName("appTitle")
        toolbar.addWidget(title)
        spacer = QWidget()
        spacer.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        toolbar.addWidget(spacer)
        self.import_action = QAction(self.style().standardIcon(QStyle.StandardPixmap.SP_DialogOpenButton), "导入 XLSX", self)
        self.import_gerber_action = QAction(self.style().standardIcon(QStyle.StandardPixmap.SP_FileDialogDetailedView), "导入 Gerber", self)
        self.export_action = QAction(self.style().standardIcon(QStyle.StandardPixmap.SP_DialogSaveButton), "导出 G-code", self)
        self.start_action = QAction(self.style().standardIcon(QStyle.StandardPixmap.SP_MediaPlay), "开始", self)
        self.settings_action = QAction(self.style().standardIcon(QStyle.StandardPixmap.SP_FileDialogDetailedView), "无线设置", self)
        toolbar.addAction(self.import_gerber_action)
        toolbar.addAction(self.export_action)
        toolbar.addAction(self.start_action)
        toolbar.addAction(self.settings_action)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.setChildrenCollapsible(False)
        splitter.addWidget(self._build_settings())
        splitter.addWidget(self._build_center())
        splitter.addWidget(self._build_points())
        splitter.setSizes([290, 760, 390])
        splitter.setStretchFactor(1, 1)
        self.prepare_page = splitter
        self.pages = QStackedWidget()
        self.pages.addWidget(self.prepare_page)
        self.start_page = self._build_start_page()
        self.pages.addWidget(self.start_page)
        self.setCentralWidget(self.pages)
        self.setStatusBar(QStatusBar())
        self.statusBar().showMessage("请选择 Gerber Paste 文件")
        self.update_start_enabled()

    def _spin(self, value: float, minimum: float = -100000.0,
              maximum: float = 100000.0, step: float = 0.1,
              decimals: int = 3) -> QDoubleSpinBox:
        box = QDoubleSpinBox()
        box.setRange(minimum, maximum)
        box.setDecimals(decimals)
        box.setSingleStep(step)
        box.setValue(value)
        box.setKeyboardTracking(False)
        return box

    def _build_settings(self) -> QWidget:
        content = QWidget()
        layout = QVBoxLayout(content)
        layout.setContentsMargins(12, 12, 12, 16)
        layout.setSpacing(12)
        coordinate_group = QGroupBox("坐标设置")
        form = QFormLayout(coordinate_group)
        self.mode_combo = QComboBox()
        self.mode_combo.addItems(["坐标表点胶", "Gerber 焊盘铺膏"])
        self.sheet_combo = QComboBox()
        self.source_combo = QComboBox()
        self.source_combo.addItems(["Mid X / Mid Y", "Ref X / Ref Y", "Pad X / Pad Y"])
        self.origin_combo = QComboBox()
        self.origin_combo.addItems(["左下角归零", "保留原坐标", "指定工件原点"])
        self.offset_x, self.offset_y = self._spin(0), self._spin(0)
        self.origin_x, self.origin_y = self._spin(0), self._spin(0)
        self.flip_x, self.flip_y = QCheckBox("X 轴镜像"), QCheckBox("Y 轴镜像")
        mirror_row = QWidget()
        mirror_layout = QHBoxLayout(mirror_row)
        mirror_layout.setContentsMargins(0, 0, 0, 0)
        mirror_layout.addWidget(self.flip_x)
        mirror_layout.addWidget(self.flip_y)
        form.addRow("路径来源", self.mode_combo)
        form.addRow("工作表", self.sheet_combo)
        form.addRow("坐标列", self.source_combo)
        form.addRow("原点", self.origin_combo)
        form.addRow("X 偏移 (mm)", self.offset_x)
        form.addRow("Y 偏移 (mm)", self.offset_y)
        form.addRow("原点 X (mm)", self.origin_x)
        form.addRow("原点 Y (mm)", self.origin_y)
        form.addRow("镜像", mirror_row)

        motion_group = QGroupBox("运动参数")
        motion = QFormLayout(motion_group)
        self.safe_z = self._spin(5, 0, 1000, 0.1)
        self.paste_z = self._spin(0.2, -1000, 1000, 0.05)
        self.travel_feed = self._spin(1200, 1, 100000, 50, 0)
        self.z_feed = self._spin(300, 1, 100000, 10, 0)
        self.extrude = self._spin(0.5, 0, 1000, 0.01)
        self.retract = self._spin(0.05, 0, 1000, 0.01)
        self.e_feed = self._spin(60, 1, 100000, 5, 0)
        self.nozzle_diameter = self._spin(0.35, 0.05, 10, 0.05)
        self.line_spacing = self._spin(0.30, 0.05, 10, 0.05)
        self.edge_inset = self._spin(0.08, 0, 10, 0.02)
        self.e_per_mm = self._spin(0.08, 0, 1000, 0.005, 4)
        self.min_stroke_length = self._spin(0.25, 0, 1000, 0.05)
        self.order_combo = QComboBox()
        self.order_combo.addItems(["坐标表原顺序", "最近点优先"])
        for label, widget in [("安全 Z (mm)", self.safe_z), ("点胶 Z (mm)", self.paste_z),
                              ("XY 速度", self.travel_feed), ("Z 速度", self.z_feed),
                              ("单点挤出 E", self.extrude), ("回抽 E", self.retract),
                              ("E 轴速度", self.e_feed), ("喷嘴直径 (mm)", self.nozzle_diameter),
                              ("铺膏线距 (mm)", self.line_spacing), ("焊盘内缩 (mm)", self.edge_inset),
                              ("铺膏 E/mm", self.e_per_mm), ("最短线段 (mm)", self.min_stroke_length),
                              ("路径顺序", self.order_combo)]:
            motion.addRow(label, widget)
        layout.addWidget(coordinate_group)
        layout.addWidget(motion_group)
        layout.addStretch()
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setWidget(content)
        scroll.setMinimumWidth(270)
        return scroll

    def _build_center(self) -> QWidget:
        panel = QWidget()
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(9)
        title_row = QHBoxLayout()
        title = QLabel("路径预览")
        title.setObjectName("sectionTitle")
        self.board_size_label = QLabel("--")
        self.board_size_label.setObjectName("muted")
        title_row.addWidget(title)
        title_row.addStretch()
        title_row.addWidget(self.board_size_label)
        layout.addLayout(title_row)
        self.preview = PathPreview()
        self.preview.setObjectName("preview")
        layout.addWidget(self.preview, 3)
        stats = QFrame()
        stats.setObjectName("stats")
        stats_layout = QGridLayout(stats)
        stats_layout.setContentsMargins(10, 8, 10, 8)
        self.point_count, self.distance_label = QLabel("0"), QLabel("0 mm")
        self.time_label, self.line_count = QLabel("0 s"), QLabel("0")
        for column, (label, value) in enumerate([
            ("点胶点", self.point_count), ("空移距离", self.distance_label),
            ("预计耗时", self.time_label), ("G-code 行数", self.line_count),
        ]):
            caption = QLabel(label)
            caption.setObjectName("muted")
            value.setObjectName("statValue")
            stats_layout.addWidget(caption, 0, column)
            stats_layout.addWidget(value, 1, column)
        layout.addWidget(stats)
        code_header = QHBoxLayout()
        code_title = QLabel("G-code 预览")
        code_title.setObjectName("sectionTitle")
        self.copy_button = QPushButton("复制")
        self.go_start_button = QPushButton("开始")
        code_header.addWidget(code_title)
        code_header.addStretch()
        code_header.addWidget(self.copy_button)
        code_header.addWidget(self.go_start_button)
        layout.addLayout(code_header)
        self.gcode_preview = QPlainTextEdit()
        self.gcode_preview.setReadOnly(True)
        self.gcode_preview.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.gcode_preview.setFont(QFont("Consolas", 9))
        layout.addWidget(self.gcode_preview, 2)
        return panel

    def _build_start_page(self) -> QWidget:
        page = QWidget()
        page_layout = QHBoxLayout(page)
        page_layout.setContentsMargins(18, 16, 18, 16)
        page_layout.setSpacing(12)
        left_panel = QWidget()
        layout = QVBoxLayout(left_panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(12)

        connection = QGroupBox("无线连接")
        grid = QGridLayout(connection)
        self.device_combo = QComboBox()
        self.device_info_label = QLabel("未搜索")
        self.device_info_label.setObjectName("muted")
        self.search_device_button = QPushButton("搜索设备")
        self.connect_device_button = QPushButton("连接")
        self.disconnect_device_button = QPushButton("断开")
        grid.addWidget(QLabel("设备"), 0, 0)
        grid.addWidget(self.device_combo, 0, 1, 1, 3)
        grid.addWidget(self.search_device_button, 0, 4)
        grid.addWidget(self.connect_device_button, 0, 5)
        grid.addWidget(self.disconnect_device_button, 0, 6)
        grid.addWidget(self.device_info_label, 1, 1, 1, 6)

        transfer = QGroupBox("G-code 传输与执行")
        transfer_grid = QGridLayout(transfer)
        self.storage_combo = QComboBox()
        self.storage_combo.addItems(["/flash", "/sd"])
        self.remote_path_edit = QLineEdit("/flash/pcb_solder_paste.gcode")
        self.upload_button = QPushButton("传到设备")
        self.execute_button = QPushButton("执行")
        self.transfer_progress = QLabel("0%")
        self.transfer_progress.setObjectName("statValue")
        transfer_grid.addWidget(QLabel("存储"), 0, 0)
        transfer_grid.addWidget(self.storage_combo, 0, 1)
        transfer_grid.addWidget(QLabel("路径"), 0, 2)
        transfer_grid.addWidget(self.remote_path_edit, 0, 3, 1, 3)
        transfer_grid.addWidget(self.upload_button, 0, 6)
        transfer_grid.addWidget(self.execute_button, 0, 7)
        transfer_grid.addWidget(QLabel("进度"), 1, 0)
        transfer_grid.addWidget(self.transfer_progress, 1, 1, 1, 7)

        self.connection_log = QPlainTextEdit()
        self.connection_log.setReadOnly(True)
        self.connection_log.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.connection_log.setFont(QFont("Consolas", 9))

        layout.addWidget(connection)
        layout.addWidget(transfer)
        layout.addWidget(QLabel("通信日志"))
        layout.addWidget(self.connection_log, 1)
        page_layout.addWidget(left_panel, 1)
        page_layout.addWidget(self._build_manual_console())
        return page

    def _build_manual_console(self) -> QWidget:
        panel = QGroupBox("手动操作台")
        panel.setMinimumWidth(280)
        layout = QVBoxLayout(panel)
        layout.setSpacing(10)

        motion = QFormLayout()
        self.jog_step = self._spin(1.0, 0.01, 100.0, 0.1)
        self.jog_feed = self._spin(600.0, 1.0, 10000.0, 50.0, 0)
        self.extruder_step = self._spin(0.2, 0.001, 100.0, 0.01, 3)
        motion.addRow("步距 (mm)", self.jog_step)
        motion.addRow("速度", self.jog_feed)
        motion.addRow("挤出量 E", self.extruder_step)
        layout.addLayout(motion)

        xyz_grid = QGridLayout()
        self.y_plus_button = QPushButton("Y+")
        self.y_minus_button = QPushButton("Y-")
        self.x_minus_button = QPushButton("X-")
        self.x_plus_button = QPushButton("X+")
        self.z_plus_button = QPushButton("Z+")
        self.z_minus_button = QPushButton("Z-")
        xyz_grid.addWidget(self.y_plus_button, 0, 1)
        xyz_grid.addWidget(self.x_minus_button, 1, 0)
        xyz_grid.addWidget(self.x_plus_button, 1, 2)
        xyz_grid.addWidget(self.y_minus_button, 2, 1)
        xyz_grid.addWidget(self.z_plus_button, 0, 3)
        xyz_grid.addWidget(self.z_minus_button, 2, 3)
        layout.addLayout(xyz_grid)

        extruder_row = QHBoxLayout()
        self.e_minus_button = QPushButton("E-")
        self.e_plus_button = QPushButton("E+")
        extruder_row.addWidget(self.e_minus_button)
        extruder_row.addWidget(self.e_plus_button)
        layout.addLayout(extruder_row)

        utility_row = QHBoxLayout()
        self.motor_on_button = QPushButton("使能")
        self.motor_off_button = QPushButton("释放")
        self.status_button = QPushButton("状态")
        utility_row.addWidget(self.motor_on_button)
        utility_row.addWidget(self.motor_off_button)
        utility_row.addWidget(self.status_button)
        layout.addLayout(utility_row)
        layout.addStretch()
        return panel

    def _build_points(self) -> QWidget:
        panel = QWidget()
        panel.setMinimumWidth(340)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(10, 12, 10, 10)
        layout.setSpacing(8)
        header = QHBoxLayout()
        title = QLabel("点胶对象")
        title.setObjectName("sectionTitle")
        self.summary_label = QLabel("0 / 0")
        self.summary_label.setObjectName("muted")
        header.addWidget(title)
        header.addStretch()
        header.addWidget(self.summary_label)
        layout.addLayout(header)
        self.search_edit = QLineEdit()
        self.search_edit.setPlaceholderText("搜索位号、封装、器件或焊盘")
        layout.addWidget(self.search_edit)
        filters = QHBoxLayout()
        self.layer_combo = QComboBox()
        self.layer_combo.addItems(["全部层", "顶层", "底层"])
        self.smd_only = QCheckBox("仅 SMD")
        self.smd_only.setChecked(True)
        filters.addWidget(self.layer_combo)
        filters.addWidget(self.smd_only)
        filters.addStretch()
        layout.addLayout(filters)
        selection = QHBoxLayout()
        self.select_button, self.clear_button = QPushButton("选择可见"), QPushButton("清除可见")
        selection.addWidget(self.select_button)
        selection.addWidget(self.clear_button)
        selection.addStretch()
        layout.addLayout(selection)
        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(["选", "对象", "坐标 (mm)", "信息"])
        self.table.verticalHeader().setVisible(False)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.table.setColumnWidth(0, 42)
        self.table.setColumnWidth(1, 70)
        self.table.setColumnWidth(2, 112)
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        layout.addWidget(self.table)
        return panel

    def _connect_signals(self) -> None:
        self.import_action.triggered.connect(self.choose_workbook)
        self.import_gerber_action.triggered.connect(self.choose_gerber)
        self.export_action.triggered.connect(self.export_gcode)
        self.start_action.triggered.connect(self.show_start_page)
        self.settings_action.triggered.connect(self.show_wireless_settings)
        self.sheet_combo.currentIndexChanged.connect(self.sheet_changed)
        for widget in [self.mode_combo, self.source_combo, self.origin_combo, self.order_combo, self.layer_combo]:
            widget.currentIndexChanged.connect(self.refresh)
        for widget in [self.flip_x, self.flip_y, self.smd_only]:
            widget.toggled.connect(self.refresh)
        for spin in [self.offset_x, self.offset_y, self.origin_x, self.origin_y,
                     self.safe_z, self.paste_z, self.travel_feed, self.z_feed,
                     self.extrude, self.retract, self.e_feed, self.nozzle_diameter,
                     self.line_spacing, self.edge_inset, self.e_per_mm, self.min_stroke_length]:
            spin.valueChanged.connect(self.refresh)
        self.search_edit.textChanged.connect(self.refresh_table)
        self.table.itemChanged.connect(self.table_item_changed)
        self.select_button.clicked.connect(lambda: self.select_visible(True))
        self.clear_button.clicked.connect(lambda: self.select_visible(False))
        self.copy_button.clicked.connect(self.copy_gcode)
        self.go_start_button.clicked.connect(self.show_start_page)
        self.search_device_button.clicked.connect(self.discover_wireless_devices)
        self.connect_device_button.clicked.connect(self.connect_wireless_device)
        self.disconnect_device_button.clicked.connect(self.disconnect_wireless_device)
        self.storage_combo.currentTextChanged.connect(self.update_remote_path_root)
        self.upload_button.clicked.connect(self.upload_gcode_to_device)
        self.execute_button.clicked.connect(self.execute_remote_gcode)
        self.x_minus_button.clicked.connect(lambda _checked=False: self.jog_axis("X", -1.0))
        self.x_plus_button.clicked.connect(lambda _checked=False: self.jog_axis("X", 1.0))
        self.y_minus_button.clicked.connect(lambda _checked=False: self.jog_axis("Y", -1.0))
        self.y_plus_button.clicked.connect(lambda _checked=False: self.jog_axis("Y", 1.0))
        self.z_minus_button.clicked.connect(lambda _checked=False: self.jog_axis("Z", -1.0))
        self.z_plus_button.clicked.connect(lambda _checked=False: self.jog_axis("Z", 1.0))
        self.e_minus_button.clicked.connect(lambda _checked=False: self.jog_axis("E", -1.0))
        self.e_plus_button.clicked.connect(lambda _checked=False: self.jog_axis("E", 1.0))
        self.motor_on_button.clicked.connect(lambda _checked=False: self.send_manual_command("gcode M17"))
        self.motor_off_button.clicked.connect(lambda _checked=False: self.send_manual_command("gcode M84"))
        self.status_button.clicked.connect(lambda _checked=False: self.send_manual_command("gcode -s"))

    def _apply_style(self) -> None:
        self.setStyleSheet("""
            QMainWindow, QWidget { background: #f5f7f7; color: #1c282d; font-family: "Microsoft YaHei UI"; font-size: 12px; }
            QToolBar { min-height: 52px; padding: 4px 12px; spacing: 8px; background: #202b30; border-bottom: 3px solid #087f5b; }
            QToolBar QToolButton { min-height: 30px; padding: 3px 10px; color: white; background: #2e3b40; border: 1px solid #5b696e; border-radius: 4px; }
            QToolBar QToolButton:hover { background: #3a494f; }
            QToolBar QWidget { background: transparent; }
            QLabel#appTitle { min-width: 220px; color: white; background: transparent; font-size: 18px; font-weight: 700; }
            QLabel#sectionTitle { font-size: 14px; font-weight: 700; }
            QLabel#muted { color: #68777d; }
            QLabel#statValue { font-size: 16px; font-weight: 700; }
            QGroupBox { margin-top: 9px; padding-top: 11px; font-weight: 700; border: 1px solid #ccd4d6; border-radius: 5px; background: white; }
            QGroupBox::title { subcontrol-origin: margin; left: 9px; padding: 0 4px; }
            QLineEdit, QComboBox, QDoubleSpinBox { min-height: 28px; padding: 0 6px; background: white; border: 1px solid #bdc7ca; border-radius: 4px; }
            QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus { border: 1px solid #087f5b; }
            QPushButton { min-height: 29px; padding: 0 11px; background: white; border: 1px solid #aeb9bc; border-radius: 4px; }
            QPushButton:hover { background: #edf7f3; border-color: #087f5b; }
            QWidget#preview { border: 1px solid #c8d0d2; }
            QFrame#stats { background: white; border: 1px solid #d5dcde; }
            QPlainTextEdit { color: #bdebdc; background: #182126; border: 1px solid #34444a; }
            QTableWidget { background: white; alternate-background-color: #f7f9f9; border: 1px solid #ccd4d6; gridline-color: #e5e9ea; }
            QHeaderView::section { padding: 6px; color: #526168; background: #eef2f2; border: 0; border-bottom: 1px solid #cbd3d5; }
            QSplitter::handle { background: #d5dcde; width: 1px; }
        """)

    def choose_workbook(self) -> None:
        initial = str(self.current_file.parent if self.current_file else ROOT.parent)
        file_name, _ = QFileDialog.getOpenFileName(self, "选择 PCB 坐标表", initial, "Excel 工作簿 (*.xlsx)")
        if file_name:
            self.import_workbook(Path(file_name))

    def choose_gerber(self) -> None:
        initial = str(self.current_gerber.parent if self.current_gerber else ROOT.parent)
        file_name, _ = QFileDialog.getOpenFileName(
            self, "选择 Gerber Paste 文件", initial,
            "Gerber Paste 或压缩包 (*.gtp *.gbp *.zip);;Gerber 文件 (*.gbr *.ger *.pho *.art);;所有文件 (*.*)")
        if file_name:
            self.import_gerber(Path(file_name))

    def import_workbook(self, path: Path, show_error: bool = True) -> bool:
        try:
            sheets = load_workbook(path)
            if not any({"Mid X", "Mid Y"}.issubset(set(sheet.headers)) for sheet in sheets):
                raise ValueError("找不到 Mid X / Mid Y 坐标列")
        except Exception as exc:
            if show_error:
                QMessageBox.critical(self, "导入失败", f"无法读取坐标表：\n{exc}")
            self.statusBar().showMessage(f"导入失败：{exc}")
            return False
        self.sheets, self.current_file = sheets, path
        self.sheet_combo.blockSignals(True)
        self.sheet_combo.clear()
        for sheet in sheets:
            self.sheet_combo.addItem(f"{sheet.name} ({len(sheet.rows)})")
        self.sheet_combo.blockSignals(False)
        self.sheet_combo.setCurrentIndex(0)
        self.sheet_changed(0)
        self.statusBar().showMessage(f"已导入 {path.name}，共 {sum(len(s.rows) for s in sheets)} 条记录")
        return True

    def import_gerber(self, path: Path, show_error: bool = True) -> bool:
        try:
            result = parse_gerber_file(path)
            if not result.pads:
                raise ValueError("没有解析到焊盘。请优先选择 Top Paste(.GTP) 或 Bottom Paste(.GBP) 层。")
        except Exception as exc:
            if show_error:
                QMessageBox.critical(self, "导入失败", f"无法读取 Gerber：\n{exc}")
            self.statusBar().showMessage(f"Gerber 导入失败：{exc}")
            return False
        self.gerber_pads = result.pads
        self.selected_pad_ids = {pad.pad_id for pad in result.pads}
        self.current_gerber = path
        self.gerber_warnings = result.warnings
        self.mode_combo.setCurrentIndex(1)
        self.refresh()
        warning = f"，{len(result.warnings)} 条警告" if result.warnings else ""
        self.statusBar().showMessage(
            f"已导入 {path.name}，解析 {len(result.pads)} 个焊盘，单位 {result.unit}，格式 {result.coordinate_format}{warning}")
        return True

    def sheet_changed(self, index: int) -> None:
        self.rows = self.sheets[index].rows if 0 <= index < len(self.sheets) else []
        self.selected_ids = {i for i, row in enumerate(self.rows)
                             if str(row.get("SMD", "")).lower() == "yes"}
        self.refresh()

    def transformed_points(self) -> list[PointData]:
        prefix = ("Mid", "Ref", "Pad")[self.source_combo.currentIndex()]
        points: list[PointData] = []
        for row_id, row in enumerate(self.rows):
            x, y = parse_number(row.get(f"{prefix} X")), parse_number(row.get(f"{prefix} Y"))
            if x is not None and y is not None:
                points.append(PointData(row_id, row, x, y))
        if not points:
            return []
        mode = self.origin_combo.currentIndex()
        flip_x, flip_y = self.flip_x.isChecked(), self.flip_y.isChecked()
        if mode == 0:
            min_x, max_x = min(p.x for p in points), max(p.x for p in points)
            min_y, max_y = min(p.y for p in points), max(p.y for p in points)
            for point in points:
                point.x = (max_x - point.x if flip_x else point.x - min_x) + self.offset_x.value()
                point.y = (max_y - point.y if flip_y else point.y - min_y) + self.offset_y.value()
        else:
            origin_x = self.origin_x.value() if mode == 2 else 0.0
            origin_y = self.origin_y.value() if mode == 2 else 0.0
            for point in points:
                point.x = (point.x - origin_x) * (-1 if flip_x else 1) + self.offset_x.value()
                point.y = (point.y - origin_y) * (-1 if flip_y else 1) + self.offset_y.value()
        return points

    def transformed_pads(self) -> list[GerberPad]:
        if not self.gerber_pads:
            return []
        all_x = [x for pad in self.gerber_pads for x, _ in pad.vertices]
        all_y = [y for pad in self.gerber_pads for _, y in pad.vertices]
        min_x, max_x = min(all_x), max(all_x)
        min_y, max_y = min(all_y), max(all_y)
        mode = self.origin_combo.currentIndex()
        flip_x, flip_y = self.flip_x.isChecked(), self.flip_y.isChecked()
        origin_x = self.origin_x.value() if mode == 2 else 0.0
        origin_y = self.origin_y.value() if mode == 2 else 0.0

        def mapped(x: float, y: float) -> tuple[float, float]:
            if mode == 0:
                tx = max_x - x if flip_x else x - min_x
                ty = max_y - y if flip_y else y - min_y
            else:
                tx = (x - origin_x) * (-1 if flip_x else 1)
                ty = (y - origin_y) * (-1 if flip_y else 1)
            return tx + self.offset_x.value(), ty + self.offset_y.value()

        pads: list[GerberPad] = []
        for pad in self.gerber_pads:
            vertices = [mapped(x, y) for x, y in pad.vertices]
            xs = [x for x, _ in vertices]
            ys = [y for _, y in vertices]
            cx, cy = mapped(pad.x, pad.y)
            pads.append(GerberPad(pad.pad_id, cx, cy, max(xs) - min(xs), max(ys) - min(ys),
                                  pad.shape, vertices, pad.source_layer, pad.aperture,
                                  pad.rotation, pad.label))
        return pads

    def visible_points(self) -> list[PointData]:
        query = self.search_edit.text().strip().lower()
        layer = (None, "T", "B")[self.layer_combo.currentIndex()]
        result = []
        for point in self.transformed_points():
            raw = point.raw
            if self.smd_only.isChecked() and str(raw.get("SMD", "")).lower() != "yes":
                continue
            if layer and str(raw.get("Layer", "")).upper() != layer:
                continue
            text = " ".join(str(raw.get(k, "")) for k in ("Designator", "Footprint", "Device", "Comment")).lower()
            if not query or query in text:
                result.append(point)
        return result

    def visible_pads(self) -> list[GerberPad]:
        query = self.search_edit.text().strip().lower()
        layer_index = self.layer_combo.currentIndex()
        result: list[GerberPad] = []
        for pad in self.transformed_pads():
            layer = pad.source_layer.lower()
            if layer_index == 1 and "bottom" in layer:
                continue
            if layer_index == 2 and "top" in layer:
                continue
            text = f"{pad.label} {pad.shape} {pad.aperture} {pad.source_layer} {pad.width:.3f} {pad.height:.3f}".lower()
            if not query or query in text:
                result.append(pad)
        return result

    def ordered_selected_points(self) -> list[PointData]:
        points = [point for point in self.transformed_points() if point.row_id in self.selected_ids]
        if self.order_combo.currentIndex() == 0 or len(points) < 2:
            return points
        pending, ordered = points[:], []
        ordered.append(pending.pop(0))
        while pending:
            previous = ordered[-1]
            best = min(range(len(pending)), key=lambda i: math.hypot(pending[i].x - previous.x,
                                                                      pending[i].y - previous.y))
            ordered.append(pending.pop(best))
        return ordered

    def ordered_selected_pads(self) -> list[GerberPad]:
        pads = [pad for pad in self.transformed_pads() if pad.pad_id in self.selected_pad_ids]
        if self.order_combo.currentIndex() == 0 or len(pads) < 2:
            return pads
        pending, ordered = pads[:], []
        ordered.append(pending.pop(0))
        while pending:
            previous = ordered[-1]
            best = min(range(len(pending)), key=lambda i: math.hypot(pending[i].x - previous.x,
                                                                      pending[i].y - previous.y))
            ordered.append(pending.pop(best))
        return ordered

    def pad_dispense_segments(self, pads: list[GerberPad] | None = None) -> list[DispenseSegment]:
        pads = self.ordered_selected_pads() if pads is None else pads
        segments: list[DispenseSegment] = []
        for pad in pads:
            segments.extend(self._segments_for_pad(pad))
        return segments

    def _segments_for_pad(self, pad: GerberPad) -> list[DispenseSegment]:
        spacing = max(0.05, self.line_spacing.value())
        inset = max(0.0, self.edge_inset.value())
        min_length = max(0.0, self.min_stroke_length.value())
        xs = [x for x, _ in pad.vertices]
        ys = [y for _, y in pad.vertices]
        min_x, max_x = min(xs) + inset, max(xs) - inset
        min_y, max_y = min(ys) + inset, max(ys) - inset
        if max_x <= min_x or max_y <= min_y or min(pad.width, pad.height) < self.nozzle_diameter.value() * 1.15:
            return [DispenseSegment(pad.pad_id, pad.label, (pad.x, pad.y), (pad.x, pad.y))]

        height = max_y - min_y
        line_count = max(1, int(math.floor(height / spacing)) + 1)
        if line_count == 1:
            y_values = [(min_y + max_y) / 2.0]
        else:
            margin = max(0.0, (height - spacing * (line_count - 1)) / 2.0)
            y_values = [min_y + margin + i * spacing for i in range(line_count)]
        result: list[DispenseSegment] = []
        reverse = False
        for y in y_values:
            spans = self._polygon_spans_at_y(pad.vertices, y)
            for left, right in spans:
                left += inset
                right -= inset
                if right - left < min_length:
                    continue
                start, end = ((right, y), (left, y)) if reverse else ((left, y), (right, y))
                result.append(DispenseSegment(pad.pad_id, pad.label, start, end))
                reverse = not reverse
        if not result:
            result.append(DispenseSegment(pad.pad_id, pad.label, (pad.x, pad.y), (pad.x, pad.y)))
        return result

    @staticmethod
    def _polygon_spans_at_y(vertices: list[tuple[float, float]], y: float) -> list[tuple[float, float]]:
        hits: list[float] = []
        if len(vertices) < 3:
            return []
        for index, (x1, y1) in enumerate(vertices):
            x2, y2 = vertices[(index + 1) % len(vertices)]
            if abs(y1 - y2) < 1e-9:
                continue
            if (y1 <= y < y2) or (y2 <= y < y1):
                t = (y - y1) / (y2 - y1)
                hits.append(x1 + t * (x2 - x1))
        hits.sort()
        spans: list[tuple[float, float]] = []
        for i in range(0, len(hits) - 1, 2):
            if hits[i + 1] > hits[i]:
                spans.append((hits[i], hits[i + 1]))
        return spans

    def refresh(self, *_args) -> None:
        custom = self.origin_combo.currentIndex() == 2
        self.origin_x.setEnabled(custom)
        self.origin_y.setEnabled(custom)
        gerber_mode = self.mode_combo.currentIndex() == 1
        self.sheet_combo.setEnabled(not gerber_mode)
        self.source_combo.setEnabled(not gerber_mode)
        self.smd_only.setEnabled(not gerber_mode)
        self.refresh_table()
        points = [] if gerber_mode else self.ordered_selected_points()
        pads = self.ordered_selected_pads() if gerber_mode else []
        segments = self.pad_dispense_segments(pads) if gerber_mode else []
        self.preview.set_geometry(points, pads, segments)
        self._update_board_size(points, pads)
        gcode = self.generate_gcode()
        self.gcode_preview.setPlainText(gcode)
        distance, seconds = self.path_stats(points, segments)
        self.point_count.setText(str(len(pads) if gerber_mode else len(points)))
        self.distance_label.setText(f"{distance:.1f} mm")
        self.time_label.setText(f"{seconds:.0f} s" if seconds < 60 else f"{seconds / 60:.1f} min")
        self.line_count.setText(str(len(gcode.rstrip().splitlines())))
        self.update_start_enabled()

    def _update_board_size(self, points: list[PointData], pads: list[GerberPad]) -> None:
        xs = [p.x for p in points]
        ys = [p.y for p in points]
        for pad in pads:
            xs.extend(x for x, _ in pad.vertices)
            ys.extend(y for _, y in pad.vertices)
        if xs and ys:
            self.board_size_label.setText(f"{max(xs) - min(xs):.2f} x {max(ys) - min(ys):.2f} mm")
        else:
            self.board_size_label.setText("--")

    def refresh_table(self, *_args) -> None:
        gerber_mode = self.mode_combo.currentIndex() == 1
        visible_points = [] if gerber_mode else self.visible_points()
        visible_pads = self.visible_pads() if gerber_mode else []
        self._updating_table = True
        self.table.setRowCount(len(visible_pads) if gerber_mode else len(visible_points))
        if gerber_mode:
            for table_row, pad in enumerate(visible_pads):
                check = QTableWidgetItem()
                check.setData(Qt.ItemDataRole.UserRole, pad.pad_id)
                check.setFlags(Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsUserCheckable)
                check.setCheckState(Qt.CheckState.Checked if pad.pad_id in self.selected_pad_ids else Qt.CheckState.Unchecked)
                name = QTableWidgetItem(pad.label)
                name.setToolTip(f"{pad.source_layer}  {pad.aperture}")
                coordinate = QTableWidgetItem(f"X {gcode_number(pad.x)}\nY {gcode_number(pad.y)}")
                info = QTableWidgetItem(f"{pad.shape} {pad.width:.3f} x {pad.height:.3f}")
                info.setToolTip(f"shape={pad.shape}, aperture={pad.aperture}, layer={pad.source_layer}")
                for column, item in enumerate((check, name, coordinate, info)):
                    self.table.setItem(table_row, column, item)
                self.table.setRowHeight(table_row, 43)
            selected_count = sum(pad.pad_id in self.selected_pad_ids for pad in visible_pads)
            self.summary_label.setText(f"{selected_count} / {len(visible_pads)} 可见")
            self._updating_table = False
            return
        for table_row, point in enumerate(visible_points):
            check = QTableWidgetItem()
            check.setData(Qt.ItemDataRole.UserRole, point.row_id)
            check.setFlags(Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsUserCheckable)
            check.setCheckState(Qt.CheckState.Checked if point.row_id in self.selected_ids else Qt.CheckState.Unchecked)
            raw = point.raw
            designation = QTableWidgetItem(str(raw.get("Designator", "-")))
            designation.setToolTip(f"层: {raw.get('Layer', '-')}  旋转: {raw.get('Rotation', 0)}°")
            coordinate = QTableWidgetItem(f"X {gcode_number(point.x)}\nY {gcode_number(point.y)}")
            footprint = QTableWidgetItem(str(raw.get("Footprint", raw.get("Device", "-"))))
            footprint.setToolTip(footprint.text())
            for column, item in enumerate((check, designation, coordinate, footprint)):
                self.table.setItem(table_row, column, item)
            self.table.setRowHeight(table_row, 43)
        self._updating_table = False
        self.summary_label.setText(f"{sum(p.row_id in self.selected_ids for p in visible_points)} / {len(visible_points)} 可见")

    def table_item_changed(self, item: QTableWidgetItem) -> None:
        if self._updating_table or item.column() != 0:
            return
        row_id = item.data(Qt.ItemDataRole.UserRole)
        target = self.selected_pad_ids if self.mode_combo.currentIndex() == 1 else self.selected_ids
        if item.checkState() == Qt.CheckState.Checked:
            target.add(row_id)
        else:
            target.discard(row_id)
        self.refresh()

    def select_visible(self, selected: bool) -> None:
        if self.mode_combo.currentIndex() == 1:
            for pad in self.visible_pads():
                self.selected_pad_ids.add(pad.pad_id) if selected else self.selected_pad_ids.discard(pad.pad_id)
        else:
            for point in self.visible_points():
                self.selected_ids.add(point.row_id) if selected else self.selected_ids.discard(point.row_id)
        self.refresh()

    def generate_gcode(self, points: list[PointData] | None = None) -> str:
        if self.mode_combo.currentIndex() == 1:
            return self.generate_pad_gcode()
        return self.generate_point_gcode(points)

    def generate_point_gcode(self, points: list[PointData] | None = None) -> str:
        points = self.ordered_selected_points() if points is None else points
        lines = ["; Banux PCB solder paste program", "; Source: PCB coordinate workbook",
                 f"; Points: {len(points)}", "M17", "G90", "G92 X0 Y0 Z0 E0",
                 f"G0 Z{gcode_number(self.safe_z.value())} F{gcode_number(self.z_feed.value())}"]
        for index, point in enumerate(points, 1):
            label = gcode_label(point.raw.get("Designator", "point"), 40)
            lines.extend([f"; {index} {label}",
                          f"G0 X{gcode_number(point.x)} Y{gcode_number(point.y)} F{gcode_number(self.travel_feed.value())}",
                          f"G1 Z{gcode_number(self.paste_z.value())} F{gcode_number(self.z_feed.value())}", "G91"])
            if self.extrude.value() > 0:
                lines.append(f"G1 E{gcode_number(self.extrude.value())} F{gcode_number(self.e_feed.value())}")
            if self.retract.value() > 0:
                lines.append(f"G1 E-{gcode_number(self.retract.value())} F{gcode_number(self.e_feed.value())}")
            lines.extend(["G90", f"G0 Z{gcode_number(self.safe_z.value())} F{gcode_number(self.z_feed.value())}"])
        lines.extend(["M84", "; End"])
        return "\n".join(lines) + "\n"

    def generate_pad_gcode(self) -> str:
        pads = self.ordered_selected_pads()
        segments = self.pad_dispense_segments(pads)
        source = gcode_label(self.current_gerber.name if self.current_gerber else "Gerber", 46)
        lines = ["; Banux PCB solder paste program", f"; Source: {source}",
                 f"; Pads: {len(pads)}", f"; Segments: {len(segments)}",
                 "M17", "G90", "G92 X0 Y0 Z0 E0",
                 f"G0 Z{gcode_number(self.safe_z.value())} F{gcode_number(self.z_feed.value())}"]
        for index, segment in enumerate(segments, 1):
            sx, sy = segment.start
            ex, ey = segment.end
            label = gcode_label(segment.label, 28)
            lines.extend([f"; {index} {label}",
                          f"G0 X{gcode_number(sx)} Y{gcode_number(sy)} F{gcode_number(self.travel_feed.value())}",
                          f"G1 Z{gcode_number(self.paste_z.value())} F{gcode_number(self.z_feed.value())}"])
            if segment.length <= 0.0005:
                lines.append("G91")
                if self.extrude.value() > 0:
                    lines.append(f"G1 E{gcode_number(self.extrude.value())} F{gcode_number(self.e_feed.value())}")
                if self.retract.value() > 0:
                    lines.append(f"G1 E-{gcode_number(self.retract.value())} F{gcode_number(self.e_feed.value())}")
                lines.append("G90")
            else:
                amount = max(0.0, segment.length * self.e_per_mm.value())
                lines.append("G92 E0")
                lines.append(f"G1 X{gcode_number(ex)} Y{gcode_number(ey)} E{gcode_number(amount, 4)} F{gcode_number(self.e_feed.value())}")
                if self.retract.value() > 0:
                    lines.extend(["G91",
                                  f"G1 E-{gcode_number(self.retract.value())} F{gcode_number(self.e_feed.value())}",
                                  "G90"])
            lines.append(f"G0 Z{gcode_number(self.safe_z.value())} F{gcode_number(self.z_feed.value())}")
        lines.extend(["M84", "; End"])
        return "\n".join(lines) + "\n"

    def path_stats(self, points: list[PointData],
                   segments: list[DispenseSegment] | None = None) -> tuple[float, float]:
        if segments is not None:
            travel = 0.0
            current: tuple[float, float] | None = None
            for segment in segments:
                if current is not None:
                    travel += math.hypot(segment.start[0] - current[0], segment.start[1] - current[1])
                current = segment.end
            dispense = sum(segment.length for segment in segments)
            z_distance = len(segments) * abs(self.safe_z.value() - self.paste_z.value()) * 2
            e_distance = dispense * self.e_per_mm.value() + len(segments) * self.retract.value()
            seconds = (travel / self.travel_feed.value() + z_distance / self.z_feed.value()
                       + e_distance / self.e_feed.value() + dispense / self.e_feed.value()) * 60
            return travel, seconds
        distance = sum(math.hypot(b.x - a.x, b.y - a.y) for a, b in zip(points, points[1:]))
        z_distance = len(points) * abs(self.safe_z.value() - self.paste_z.value()) * 2
        e_distance = len(points) * (self.extrude.value() + self.retract.value())
        seconds = (distance / self.travel_feed.value() + z_distance / self.z_feed.value()
                   + e_distance / self.e_feed.value()) * 60
        return distance, seconds

    def copy_gcode(self) -> None:
        QApplication.clipboard().setText(self.gcode_preview.toPlainText())
        self.statusBar().showMessage("G-code 已复制", 2500)

    def export_gcode(self) -> None:
        if self.mode_combo.currentIndex() == 1:
            if not self.selected_pad_ids:
                QMessageBox.warning(self, "无法导出", "请至少选择一个 Gerber 焊盘。")
                return
        elif not self.selected_ids:
            QMessageBox.warning(self, "无法导出", "请至少选择一个点胶坐标。")
            return
        source_file = self.current_gerber if self.mode_combo.currentIndex() == 1 else self.current_file
        default = (source_file.parent if source_file else ROOT) / "pcb_solder_paste.gcode"
        file_name, _ = QFileDialog.getSaveFileName(self, "导出 G-code", str(default),
                                                   "G-code (*.gcode *.nc);;文本文件 (*.txt)")
        if not file_name:
            return
        try:
            Path(file_name).write_text(self.generate_gcode(), encoding="utf-8", newline="\n")
        except OSError as exc:
            QMessageBox.critical(self, "导出失败", str(exc))
            return
        self.statusBar().showMessage(f"已导出 {file_name}", 4000)

    def closeEvent(self, event) -> None:  # noqa: N802
        self.disconnect_wireless_device(show_status=False)
        super().closeEvent(event)

    def has_prepared_job(self) -> bool:
        if self.mode_combo.currentIndex() == 1:
            return bool(self.current_gerber and self.selected_pad_ids and self.gcode_preview.toPlainText().strip())
        return bool(self.current_file and self.selected_ids and self.gcode_preview.toPlainText().strip())

    def update_start_enabled(self) -> None:
        enabled = self.has_prepared_job()
        self.start_action.setEnabled(enabled)
        self.go_start_button.setEnabled(enabled)

    def show_start_page(self) -> None:
        if not self.has_prepared_job():
            QMessageBox.information(self, "还不能开始", "请先导入 Gerber 并选择需要铺膏的焊盘。")
            self.update_start_enabled()
            return
        self.pages.setCurrentWidget(self.start_page)
        self.statusBar().showMessage("开始页面：搜索 BanPCBTool 并传输 G-code", 2500)

    def update_remote_path_root(self, root: str) -> None:
        current = self.remote_path_edit.text().strip()
        filename = Path(current).name or "pcb_solder_paste.gcode"
        self.remote_path_edit.setText(f"{root}/{filename}")

    def log_connection(self, text: str) -> None:
        self.connection_log.appendPlainText(text.rstrip())
        self.connection_log.verticalScrollBar().setValue(
            self.connection_log.verticalScrollBar().maximum())

    @staticmethod
    def parse_discovery_packet(data: bytes, source_ip: str) -> WirelessDevice | None:
        try:
            text = data.decode("utf-8", errors="replace").strip()
        except UnicodeError:
            return None
        if not text.upper().startswith("BANPCBTOOL"):
            return None
        values: dict[str, str] = {}
        for token in text.split()[1:]:
            if "=" in token:
                key, value = token.split("=", 1)
                values[key.lower()] = value
        return WirelessDevice(
            name=values.get("name", "BanPCBTool"),
            ip=values.get("ip", source_ip),
            bridge_port=int(values.get("bridge", str(BRIDGE_PORT)) or BRIDGE_PORT),
            http_port=int(values.get("http", "80") or 80),
            wifi=values.get("wifi", ""),
            ap=values.get("ap", "0") in {"1", "true", "yes", "on"},
        )

    def refresh_device_combo(self) -> None:
        self.device_combo.clear()
        for device in self.devices:
            suffix = " AP" if device.ap else ""
            self.device_combo.addItem(
                f"{device.name}  {device.ip}:{device.bridge_port}{suffix}", device)
        if self.devices:
            self.device_info_label.setText(f"发现 {len(self.devices)} 个 BanPCBTool 设备")
        else:
            self.device_info_label.setText("没有发现设备")

    def selected_wireless_device(self) -> WirelessDevice | None:
        data = self.device_combo.currentData()
        return data if isinstance(data, WirelessDevice) else None

    def discover_wireless_devices(self) -> None:
        self.log_connection("search: UDP broadcast BANPCBTOOL?")
        found: dict[str, WirelessDevice] = {}
        try:
            udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            udp.settimeout(0.25)
            udp.sendto(b"BANPCBTOOL?", ("255.255.255.255", DISCOVERY_PORT))
            for _ in range(8):
                try:
                    data, address = udp.recvfrom(512)
                except socket.timeout:
                    continue
                device = self.parse_discovery_packet(data, address[0])
                if device:
                    found[device.ip] = device
        except OSError as exc:
            QMessageBox.warning(self, "搜索失败", str(exc))
            self.log_connection(f"search error: {exc}")
            return
        finally:
            try:
                udp.close()
            except Exception:
                pass
        self.devices = list(found.values())
        self.refresh_device_combo()
        for device in self.devices:
            mode = "AP" if device.ap else "STA"
            self.log_connection(f"found: {device.name} {device.ip}:{device.bridge_port} {mode} wifi={device.wifi or '-'}")

    def connect_wireless_device(self) -> None:
        device = self.selected_wireless_device()
        if not device:
            QMessageBox.information(self, "未选择设备", "请先搜索并选择 BanPCBTool。")
            return
        self.disconnect_wireless_device(show_status=False)
        try:
            sock = socket.create_connection((device.ip, device.bridge_port), timeout=3.0)
            sock.settimeout(0.6)
            self.bridge_socket = sock
            self.connected_device = device
            self.log_connection(f"connect: {device.name} {device.ip}:{device.bridge_port}")
            sock.sendall(b"@BPC HELLO\r\n")
            response = self.read_bridge_response(1.2)
            if response:
                self.log_connection(response)
            if "BanPCBTool" not in response and "@BPC OK" not in response:
                self.log_connection("warning: ESP8266 did not return the expected name")
            self.device_info_label.setText(f"已连接 {device.name} ({device.ip})")
            self.statusBar().showMessage("无线模块已连接", 2500)
        except OSError as exc:
            self.bridge_socket = None
            self.connected_device = None
            QMessageBox.warning(self, "连接失败", str(exc))
            self.log_connection(f"connect error: {exc}")

    def disconnect_wireless_device(self, show_status: bool = True) -> None:
        if self.bridge_socket:
            try:
                self.bridge_socket.close()
            except OSError:
                pass
        self.bridge_socket = None
        self.connected_device = None
        if show_status:
            self.device_info_label.setText("已断开")
            self.statusBar().showMessage("无线连接已断开", 2000)

    def read_bridge_response(self, timeout_s: float = 2.0) -> str:
        if not self.bridge_socket:
            return ""
        chunks: list[bytes] = []
        self.bridge_socket.settimeout(0.2)
        stop_at = time.monotonic() + timeout_s
        while time.monotonic() < stop_at:
            try:
                chunk = self.bridge_socket.recv(1024)
            except socket.timeout:
                QApplication.processEvents()
                continue
            except OSError:
                break
            if not chunk:
                break
            chunks.append(chunk)
            text = b"".join(chunks).decode("utf-8", errors="replace")
            if "banux$ " in text or "OK block" in text or "OK clear" in text or "Error:" in text:
                return text.strip()
        return b"".join(chunks).decode("utf-8", errors="replace").strip()

    def send_bridge_command(self, command: str, timeout_s: float = 2.5) -> str:
        if not self.bridge_socket:
            raise OSError("无线设备未连接")
        self.log_connection(f"> {command}")
        self.bridge_socket.sendall(command.encode("ascii") + b"\r\n")
        response = self.read_bridge_response(timeout_s)
        if response:
            self.log_connection(response)
        if "Error:" in response or "failed" in response.lower():
            raise OSError(response or "命令执行失败")
        return response

    def _transfer_ready(self) -> bool:
        if self.mode_combo.currentIndex() == 1 and not self.selected_pad_ids:
            QMessageBox.warning(self, "无法传输", "请至少选择一个 Gerber 焊盘。")
            return False
        if self.mode_combo.currentIndex() != 1 and not self.selected_ids:
            QMessageBox.warning(self, "无法传输", "请至少选择一个点胶坐标。")
            return False
        if not self.bridge_socket:
            QMessageBox.information(self, "未连接", "请先搜索并连接 BanPCBTool。")
            return False
        return True

    def upload_gcode_to_device(self) -> None:
        if not self._transfer_ready():
            return
        path = self.remote_path_edit.text().strip()
        if not path.startswith(("/flash/", "/sd/")) or " " in path:
            QMessageBox.warning(self, "路径无效", "路径必须是 /flash/... 或 /sd/...，且不能包含空格。")
            return
        payload = self.generate_gcode().encode("ascii")
        try:
            self.send_bridge_command(f"recv -c {path}", 3.0)
            for offset in range(0, len(payload), UPLOAD_CHUNK_SIZE):
                chunk = payload[offset:offset + UPLOAD_CHUNK_SIZE]
                encoded = base64.b64encode(chunk).decode("ascii")
                self.send_bridge_command(f"recv -b {path} {offset} {encoded}", 3.0)
                percent = int(((offset + len(chunk)) * 100) / max(1, len(payload)))
                self.transfer_progress.setText(f"{percent}%  {offset + len(chunk)} / {len(payload)} bytes")
                QApplication.processEvents()
        except OSError as exc:
            QMessageBox.warning(self, "传输失败", str(exc))
            return
        self.statusBar().showMessage(f"已传输到 {path}", 3500)

    def execute_remote_gcode(self) -> None:
        if not self.bridge_socket:
            QMessageBox.information(self, "未连接", "请先连接 BanPCBTool。")
            return
        path = self.remote_path_edit.text().strip()
        try:
            self.send_bridge_command(f"gcode -f {path}", 4.0)
        except OSError as exc:
            QMessageBox.warning(self, "执行失败", str(exc))
            return
        self.statusBar().showMessage(f"已发送执行命令：{path}", 3000)

    def send_manual_command(self, command: str, timeout_s: float = 2.5) -> bool:
        if not self.bridge_socket:
            QMessageBox.information(self, "未连接", "请先连接 BanPCBTool。")
            return False
        try:
            self.send_bridge_command(command, timeout_s)
        except OSError as exc:
            QMessageBox.warning(self, "操作失败", str(exc))
            return False
        return True

    def jog_axis(self, axis: str, direction: float) -> None:
        amount = self.extruder_step.value() if axis == "E" else self.jog_step.value()
        value = gcode_number(amount * direction, 4 if axis == "E" else 3)
        feed = gcode_number(self.jog_feed.value(), 0)
        if not self.send_manual_command("gcode G91", 1.5):
            return
        if not self.send_manual_command(f"gcode G1 {axis}{value} F{feed}", 3.0):
            return
        self.send_manual_command("gcode G90", 1.5)

    def http_json(self, path: str, data: dict[str, str] | None = None) -> Any:
        device = self.connected_device or self.selected_wireless_device()
        if not device:
            raise OSError("请先选择或连接设备")
        url = f"http://{device.ip}:{device.http_port}{path}"
        body = None
        headers = {}
        if data is not None:
            body = urllib.parse.urlencode(data).encode("utf-8")
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        request = urllib.request.Request(url, data=body, headers=headers, method="POST" if data is not None else "GET")
        with urllib.request.urlopen(request, timeout=5.0) as response:
            text = response.read().decode("utf-8", errors="replace")
        return json.loads(text) if text.strip().startswith(("{", "[")) else text

    def show_wireless_settings(self) -> None:
        dialog = QDialog(self)
        dialog.setWindowTitle("无线设置")
        layout = QVBoxLayout(dialog)
        form = QFormLayout()
        device = self.connected_device or self.selected_wireless_device()
        host_edit = QLineEdit(device.ip if device else "")
        ssid_edit = QLineEdit()
        pass_edit = QLineEdit()
        pass_edit.setEchoMode(QLineEdit.EchoMode.Password)
        ota_edit = QLineEdit()
        scan_box = QComboBox()
        form.addRow("模块 IP", host_edit)
        form.addRow("WiFi", ssid_edit)
        form.addRow("密码", pass_edit)
        form.addRow("扫描结果", scan_box)
        form.addRow("OTA URL", ota_edit)
        layout.addLayout(form)
        button_row = QHBoxLayout()
        scan_button = QPushButton("扫描")
        save_button = QPushButton("保存配网")
        ap_button = QPushButton("进入配网")
        clear_button = QPushButton("清除 WiFi")
        ota_button = QPushButton("OTA")
        restart_button = QPushButton("重启模块")
        for button in (scan_button, save_button, ap_button, clear_button, ota_button, restart_button):
            button_row.addWidget(button)
        layout.addLayout(button_row)
        info = QPlainTextEdit()
        info.setReadOnly(True)
        layout.addWidget(info)

        def device_from_host() -> WirelessDevice:
            host = host_edit.text().strip()
            if not host:
                raise OSError("请输入模块 IP")
            if device:
                device.ip = host
                return device
            return WirelessDevice("BanPCBTool", host)

        def call(path: str, payload: dict[str, str] | None = None) -> Any:
            old_connected = self.connected_device
            old_devices = self.devices[:]
            temp = device_from_host()
            self.connected_device = temp
            try:
                result = self.http_json(path, payload)
            finally:
                self.connected_device = old_connected
                self.devices = old_devices
            info.appendPlainText(json.dumps(result, ensure_ascii=False, indent=2) if not isinstance(result, str) else result)
            return result

        def scan_wifi() -> None:
            try:
                result = call("/api/scan")
                scan_box.clear()
                for item in result if isinstance(result, list) else []:
                    name = str(item.get("ssid", ""))
                    if name:
                        scan_box.addItem(name)
            except (OSError, urllib.error.URLError, json.JSONDecodeError) as exc:
                QMessageBox.warning(dialog, "扫描失败", str(exc))

        def save_wifi() -> None:
            ssid = ssid_edit.text().strip() or scan_box.currentText().strip()
            if not ssid:
                QMessageBox.information(dialog, "缺少 WiFi", "请输入或选择 WiFi 名称。")
                return
            try:
                call("/api/wifi", {"ssid": ssid, "pass": pass_edit.text()})
                QMessageBox.information(dialog, "已保存", "模块会尝试连接新 WiFi。")
            except (OSError, urllib.error.URLError, json.JSONDecodeError) as exc:
                QMessageBox.warning(dialog, "保存失败", str(exc))

        def post_action(path: str) -> None:
            try:
                call(path, {})
            except (OSError, urllib.error.URLError, json.JSONDecodeError) as exc:
                QMessageBox.warning(dialog, "操作失败", str(exc))

        def ota_action() -> None:
            url = ota_edit.text().strip()
            if not url:
                QMessageBox.information(dialog, "缺少 URL", "请输入 ESP8266 固件 URL。")
                return
            try:
                call("/api/ota", {"url": url})
            except (OSError, urllib.error.URLError, json.JSONDecodeError) as exc:
                QMessageBox.warning(dialog, "OTA 失败", str(exc))

        scan_box.currentTextChanged.connect(ssid_edit.setText)
        scan_button.clicked.connect(scan_wifi)
        save_button.clicked.connect(save_wifi)
        ap_button.clicked.connect(lambda _checked=False: post_action("/api/ap"))
        clear_button.clicked.connect(lambda _checked=False: post_action("/api/clear"))
        ota_button.clicked.connect(ota_action)
        restart_button.clicked.connect(lambda _checked=False: post_action("/api/restart"))
        dialog.resize(640, 430)
        dialog.exec()


def run() -> int:
    parser = argparse.ArgumentParser(description="Banux PCB coordinate to G-code desktop tool")
    parser.add_argument("--file", type=Path, help="启动时导入的 XLSX 文件")
    parser.add_argument("--gerber", type=Path, help="启动时导入的 Gerber Paste 文件")
    parser.add_argument("--test", action="store_true", help="导入后输出测试结果并退出")
    parser.add_argument("--screenshot", type=Path, help="保存窗口截图")
    args = parser.parse_args()
    app = QApplication(sys.argv[:1])
    app.setStyle("Fusion")
    window = MainWindow(args.file, args.gerber)
    window.show()
    if args.test or args.screenshot:
        def finish() -> None:
            if args.screenshot:
                args.screenshot.parent.mkdir(parents=True, exist_ok=True)
                window.grab().save(str(args.screenshot))
            if args.test:
                gcode = window.generate_gcode()
                gerber_mode = window.mode_combo.currentIndex() == 1
                print(json.dumps({"file": str(window.current_file) if window.current_file else None,
                                  "gerber": str(window.current_gerber) if window.current_gerber else None,
                                  "mode": "gerber" if gerber_mode else "xlsx",
                                  "sheets": len(window.sheets), "rows": len(window.rows),
                                  "selected": len(window.selected_ids),
                                  "pads": len(window.gerber_pads),
                                  "selected_pads": len(window.selected_pad_ids),
                                  "segments": len(window.pad_dispense_segments()) if gerber_mode else 0,
                                  "gcode_lines": len(gcode.rstrip().splitlines()),
                                  "max_line_bytes": max(len(line.encode("ascii")) for line in gcode.splitlines())},
                                 ensure_ascii=False))
            app.quit()
        QTimer.singleShot(900, finish)
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(run())
