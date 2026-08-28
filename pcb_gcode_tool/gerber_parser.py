from __future__ import annotations

import math
import re
import zipfile
from dataclasses import dataclass
from pathlib import Path
from tempfile import TemporaryDirectory


Point = tuple[float, float]


@dataclass(frozen=True)
class GerberAperture:
    code: int
    shape: str
    params: tuple[float, ...]
    raw: str


@dataclass
class GerberPad:
    pad_id: int
    x: float
    y: float
    width: float
    height: float
    shape: str
    vertices: list[Point]
    source_layer: str
    aperture: str
    rotation: float = 0.0
    label: str = ""


@dataclass
class GerberParseResult:
    pads: list[GerberPad]
    warnings: list[str]
    unit: str
    coordinate_format: str


@dataclass
class _CoordFormat:
    zero: str = "L"
    x_int: int = 2
    x_dec: int = 4
    y_int: int = 2
    y_dec: int = 4


def parse_gerber_file(path: Path) -> GerberParseResult:
    if path.suffix.lower() == ".zip":
        return _parse_gerber_zip(path)
    result = _parse_with_gerbonara(path)
    if result and result.pads:
        return result
    text = path.read_text(encoding="utf-8", errors="replace")
    parser = _GerberParser(path)
    return parser.parse(text)


def _parse_gerber_zip(path: Path) -> GerberParseResult:
    with zipfile.ZipFile(path) as archive:
        members = [name for name in archive.namelist() if not name.endswith("/")]
        candidates = sorted((name for name in members if _is_paste_gerber_name(name)),
                            key=_paste_priority)
        if not candidates:
            raise ValueError("Gerber zip does not contain a Paste layer (.GTP/.GBP or paste-named Gerber).")
        selected = candidates[0]
        suffix = Path(selected).suffix or ".gbr"
        with TemporaryDirectory() as directory:
            temp_path = Path(directory) / ("selected_paste" + suffix)
            temp_path.write_bytes(archive.read(selected))
            result = parse_gerber_file(temp_path)
        result.warnings.insert(0, f"Gerber zip member selected: {selected}")
        return result


def _is_paste_gerber_name(name: str) -> bool:
    lower = name.lower()
    suffix = Path(lower).suffix
    return suffix in (".gtp", ".gbp") or "paste" in lower and suffix in (".gbr", ".ger", ".pho", ".art")


def _paste_priority(name: str) -> tuple[int, str]:
    lower = name.lower()
    if lower.endswith(".gtp") or "toppaste" in lower or "top_paste" in lower:
        return 0, lower
    if lower.endswith(".gbp") or "bottompaste" in lower or "bottom_paste" in lower:
        return 1, lower
    return 2, lower


def _parse_with_gerbonara(path: Path) -> GerberParseResult | None:
    try:
        from gerbonara.rs274x import GerberFile
        from gerbonara.utils import MM
    except Exception:
        return None
    try:
        gerber = GerberFile.open(path)
    except Exception:
        return None
    pads: list[GerberPad] = []
    warnings: list[str] = []
    layer = _GerberParser._guess_layer_name(path)
    for obj in getattr(gerber, "objects", []):
        if not getattr(obj, "polarity_dark", True):
            continue
        try:
            converted = obj.converted(MM)
        except Exception:
            converted = obj
        pad = _pad_from_gerbonara_object(converted, len(pads), layer)
        if pad:
            pads.append(pad)
    unit = str(getattr(getattr(gerber, "file_settings", None), "unit", "mm"))
    if not pads:
        return None
    return GerberParseResult(pads, warnings, "mm", f"gerbonara:{unit}")


def _pad_from_gerbonara_object(obj: object, pad_id: int, layer: str) -> GerberPad | None:
    name = type(obj).__name__
    if name == "Flash":
        return _pad_from_gerbonara_flash(obj, pad_id, layer)
    if name == "Region":
        outline = getattr(obj, "outline", None)
        if outline and len(outline) >= 3:
            vertices = _dedupe_closed([(float(x), float(y)) for x, y in outline])
            return _pad_from_vertices(pad_id, vertices, "region", layer, "region")
    if name == "Line":
        x1, y1 = float(getattr(obj, "x1")), float(getattr(obj, "y1"))
        x2, y2 = float(getattr(obj, "x2")), float(getattr(obj, "y2"))
        width = float(getattr(obj, "width", 0.15))
        vertices = _capsule_polygon((x1, y1), (x2, y2), width)
        return _pad_from_vertices(pad_id, vertices, "draw", layer, "line")
    bbox = _gerbonara_bbox(obj)
    if bbox:
        (min_x, min_y), (max_x, max_y) = bbox
        vertices = _rect_polygon((min_x + max_x) / 2.0, (min_y + max_y) / 2.0,
                                 max_x - min_x, max_y - min_y)
        return _pad_from_vertices(pad_id, vertices, "macro", layer, name)
    return None


def _pad_from_gerbonara_flash(obj: object, pad_id: int, layer: str) -> GerberPad | None:
    aperture = getattr(obj, "aperture", None)
    aperture_name = type(aperture).__name__
    x, y = float(getattr(obj, "x")), float(getattr(obj, "y"))
    aperture_number = getattr(aperture, "original_number", None)
    aperture_label = f"D{aperture_number}" if aperture_number is not None else aperture_name
    if aperture_name == "RectangleAperture":
        width, height = float(getattr(aperture, "w")), float(getattr(aperture, "h"))
        return _pad_from_vertices(pad_id, _rect_polygon(x, y, width, height),
                                  "rect", layer, aperture_label)
    if aperture_name == "CircleAperture":
        diameter = float(getattr(aperture, "diameter"))
        return _pad_from_vertices(pad_id, _circle_polygon(x, y, diameter / 2.0, 48),
                                  "circle", layer, aperture_label)
    if aperture_name == "ObroundAperture":
        width, height = float(getattr(aperture, "w")), float(getattr(aperture, "h"))
        return _pad_from_vertices(pad_id, _obround_polygon(x, y, width, height),
                                  "obround", layer, aperture_label)
    if aperture_name == "PolygonAperture":
        diameter = float(getattr(aperture, "diameter"))
        sides = int(getattr(aperture, "n_vertices", 6))
        rotation = float(getattr(aperture, "rotation", 0.0))
        vertices = _regular_polygon(x, y, diameter / 2.0, sides, rotation)
        return _pad_from_vertices(pad_id, vertices, "polygon", layer, aperture_label, rotation)
    bbox = _gerbonara_bbox(obj)
    if bbox:
        (min_x, min_y), (max_x, max_y) = bbox
        return _pad_from_vertices(pad_id, _rect_polygon((min_x + max_x) / 2.0,
                                                        (min_y + max_y) / 2.0,
                                                        max_x - min_x, max_y - min_y),
                                  "macro", layer, aperture_label)
    return None


def _gerbonara_bbox(obj: object) -> tuple[Point, Point] | None:
    try:
        (min_x, min_y), (max_x, max_y) = obj.bounding_box()
        return (float(min_x), float(min_y)), (float(max_x), float(max_y))
    except Exception:
        return None


def _pad_from_vertices(pad_id: int, vertices: list[Point], shape: str,
                       layer: str, aperture: str, rotation: float = 0.0) -> GerberPad:
    min_x, max_x, min_y, max_y = _bounds(vertices)
    return GerberPad(pad_id, (min_x + max_x) / 2.0, (min_y + max_y) / 2.0,
                     max_x - min_x, max_y - min_y, shape, vertices,
                     layer, aperture, rotation, f"PAD{pad_id + 1}")


class _GerberParser:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.unit = "mm"
        self.fmt = _CoordFormat()
        self.apertures: dict[int, GerberAperture] = {}
        self.current_aperture: GerberAperture | None = None
        self.current_x = 0.0
        self.current_y = 0.0
        self.modal_d = 2
        self.region: list[Point] | None = None
        self.pads: list[GerberPad] = []
        self.warnings: list[str] = []
        self.polarity_dark = True
        self.layer_name = self._guess_layer_name(path)

    def parse(self, text: str) -> GerberParseResult:
        for raw in text.replace("\r", "\n").split("*"):
            token = raw.replace("%", "").strip()
            if not token:
                continue
            self._handle_token(token)
        fmt = f"{self.fmt.zero} X{self.fmt.x_int}.{self.fmt.x_dec} Y{self.fmt.y_int}.{self.fmt.y_dec}"
        return GerberParseResult(self.pads, self.warnings, self.unit, fmt)

    def _handle_token(self, token: str) -> None:
        if not token or token.startswith("G04") or token.startswith("M02"):
            return
        if token.startswith("MO"):
            if "IN" in token:
                self.unit = "inch"
            elif "MM" in token:
                self.unit = "mm"
            return
        if token.startswith("FS"):
            self._parse_format(token)
            return
        if token.startswith("ADD"):
            self._parse_aperture(token)
            return
        if token.startswith("LP"):
            self.polarity_dark = "C" not in token[2:4]
            return
        if token.startswith("SR") and len(token) > 2:
            self.warnings.append("Step-repeat (SR) is declared; only the base image is parsed.")
            return
        if "G36" in token:
            self.region = []
            return
        if "G37" in token:
            self._finish_region()
            return
        self._handle_graphic(token)

    def _parse_format(self, token: str) -> None:
        match = re.search(r"FS([LT])A?X(\d)(\d)Y(\d)(\d)", token)
        if not match:
            self.warnings.append(f"Unsupported coordinate format: {token}")
            return
        self.fmt = _CoordFormat(match.group(1), int(match.group(2)),
                                int(match.group(3)), int(match.group(4)),
                                int(match.group(5)))

    def _parse_aperture(self, token: str) -> None:
        match = re.match(r"ADD(\d+)([A-Za-z_.$][A-Za-z0-9_.$]*)(?:,([^%]*))?$", token)
        if not match:
            self.warnings.append(f"Unsupported aperture definition: {token}")
            return
        code = int(match.group(1))
        shape = match.group(2).upper()
        raw_params = [float(value) for value in re.findall(r"[+-]?(?:\d+\.?\d*|\.\d+)",
                                                           match.group(3) or "")]
        if shape == "P":
            params = tuple(([self._unit(raw_params[0])] if raw_params else [])
                           + raw_params[1:])
        else:
            params = tuple(self._unit(value) for value in raw_params)
        self.apertures[code] = GerberAperture(code, shape, params, token)

    def _handle_graphic(self, token: str) -> None:
        d_codes = [int(value) for value in re.findall(r"D(\d+)", token)]
        coord_match = re.search(r"[XYIJ][+-]?\d+", token)
        if d_codes and not coord_match and d_codes[-1] >= 10:
            self.current_aperture = self.apertures.get(d_codes[-1])
            if self.current_aperture is None:
                self.warnings.append(f"Aperture D{d_codes[-1]} selected before definition.")
            return

        d_code = d_codes[-1] if d_codes else self.modal_d
        if d_code in (1, 2, 3):
            self.modal_d = d_code

        x = self._coord_from_token(token, "X", self.current_x)
        y = self._coord_from_token(token, "Y", self.current_y)

        if d_code == 2:
            self.current_x, self.current_y = x, y
            if self.region is not None:
                self.region.append((x, y))
            return

        if d_code == 1:
            start = (self.current_x, self.current_y)
            end = (x, y)
            if self.region is not None:
                if not self.region:
                    self.region.append(start)
                self.region.append(end)
            elif self.polarity_dark and self.current_aperture:
                self._add_draw(start, end, self.current_aperture)
            self.current_x, self.current_y = x, y
            return

        if d_code == 3:
            self.current_x, self.current_y = x, y
            if self.polarity_dark and self.current_aperture:
                self._add_flash(x, y, self.current_aperture)
            return

        if d_code >= 10:
            self.current_aperture = self.apertures.get(d_code)
            if self.current_aperture is None:
                self.warnings.append(f"Aperture D{d_code} selected before definition.")

    def _coord_from_token(self, token: str, axis: str, default: float) -> float:
        match = re.search(axis + r"([+-]?\d+)", token)
        if not match:
            return default
        decimals = self.fmt.x_dec if axis == "X" else self.fmt.y_dec
        integers = self.fmt.x_int if axis == "X" else self.fmt.y_int
        return self._parse_coord(match.group(1), integers, decimals)

    def _parse_coord(self, value: str, integers: int, decimals: int) -> float:
        sign = -1 if value.startswith("-") else 1
        digits = value.lstrip("+-")
        total = integers + decimals
        if self.fmt.zero == "L":
            digits = digits.rjust(total, "0")
            number = int(digits) / (10 ** decimals)
        else:
            missing = max(0, total - len(digits))
            number = int(digits) * (10 ** missing) / (10 ** decimals)
        return sign * self._unit(number)

    def _unit(self, value: float) -> float:
        return value * 25.4 if self.unit == "inch" else value

    def _add_flash(self, x: float, y: float, aperture: GerberAperture) -> None:
        shape = aperture.shape
        params = aperture.params
        vertices: list[Point]
        rotation = 0.0
        if shape == "C" and len(params) >= 1:
            diameter = params[0]
            width = height = diameter
            vertices = _circle_polygon(x, y, diameter / 2.0, 48)
            out_shape = "circle"
        elif shape == "R" and len(params) >= 2:
            width, height = params[0], params[1]
            vertices = _rect_polygon(x, y, width, height)
            out_shape = "rect"
        elif shape == "O" and len(params) >= 2:
            width, height = params[0], params[1]
            vertices = _obround_polygon(x, y, width, height)
            out_shape = "obround"
        elif shape == "P" and len(params) >= 2:
            diameter = params[0]
            sides = max(3, int(round(params[1])))
            rotation = params[2] if len(params) >= 3 else 0.0
            width = height = diameter
            vertices = _regular_polygon(x, y, diameter / 2.0, sides, rotation)
            out_shape = "polygon"
        else:
            size = params[0] if params else 0.2
            width = height = size
            vertices = _rect_polygon(x, y, width, height)
            out_shape = "unknown"
            self.warnings.append(f"Unsupported aperture D{aperture.code} ({aperture.raw}); using a small bbox.")
        self._append_pad(x, y, width, height, out_shape, vertices, f"D{aperture.code}", rotation)

    def _add_draw(self, start: Point, end: Point, aperture: GerberAperture) -> None:
        if start == end:
            self._add_flash(end[0], end[1], aperture)
            return
        diameter = aperture.params[0] if aperture.shape == "C" and aperture.params else 0.15
        vertices = _capsule_polygon(start, end, diameter)
        min_x, max_x, min_y, max_y = _bounds(vertices)
        self._append_pad((min_x + max_x) / 2.0, (min_y + max_y) / 2.0,
                         max_x - min_x, max_y - min_y, "draw", vertices, f"D{aperture.code}", 0.0)

    def _finish_region(self) -> None:
        if self.region and len(self.region) >= 3 and self.polarity_dark:
            vertices = _dedupe_closed(self.region)
            min_x, max_x, min_y, max_y = _bounds(vertices)
            self._append_pad((min_x + max_x) / 2.0, (min_y + max_y) / 2.0,
                             max_x - min_x, max_y - min_y, "region", vertices, "region", 0.0)
        self.region = None

    def _append_pad(self, x: float, y: float, width: float, height: float,
                    shape: str, vertices: list[Point], aperture: str,
                    rotation: float) -> None:
        pad_id = len(self.pads)
        self.pads.append(GerberPad(pad_id, x, y, width, height, shape, vertices,
                                   self.layer_name, aperture, rotation, f"PAD{pad_id + 1}"))

    @staticmethod
    def _guess_layer_name(path: Path) -> str:
        suffix = path.suffix.lower()
        name = path.name.lower()
        if suffix == ".gtp" or "paste" in name and ("top" in name or "front" in name):
            return "Top Paste"
        if suffix == ".gbp" or "paste" in name and ("bottom" in name or "back" in name):
            return "Bottom Paste"
        if "paste" in name:
            return "Paste"
        return "Gerber"


def _rect_polygon(cx: float, cy: float, width: float, height: float) -> list[Point]:
    hw, hh = width / 2.0, height / 2.0
    return [(cx - hw, cy - hh), (cx + hw, cy - hh), (cx + hw, cy + hh), (cx - hw, cy + hh)]


def _circle_polygon(cx: float, cy: float, radius: float, segments: int) -> list[Point]:
    return [(cx + math.cos(2.0 * math.pi * i / segments) * radius,
             cy + math.sin(2.0 * math.pi * i / segments) * radius)
            for i in range(segments)]


def _regular_polygon(cx: float, cy: float, radius: float, sides: int, rotation_deg: float) -> list[Point]:
    rotation = math.radians(rotation_deg)
    return [(cx + math.cos(rotation + 2.0 * math.pi * i / sides) * radius,
             cy + math.sin(rotation + 2.0 * math.pi * i / sides) * radius)
            for i in range(sides)]


def _obround_polygon(cx: float, cy: float, width: float, height: float) -> list[Point]:
    if width >= height:
        radius = height / 2.0
        left, right = cx - width / 2.0 + radius, cx + width / 2.0 - radius
        points = []
        for i in range(12):
            angle = math.pi / 2.0 + math.pi * i / 11.0
            points.append((left + math.cos(angle) * radius, cy + math.sin(angle) * radius))
        for i in range(12):
            angle = -math.pi / 2.0 + math.pi * i / 11.0
            points.append((right + math.cos(angle) * radius, cy + math.sin(angle) * radius))
        return points
    radius = width / 2.0
    bottom, top = cy - height / 2.0 + radius, cy + height / 2.0 - radius
    points = []
    for i in range(12):
        angle = math.pi + math.pi * i / 11.0
        points.append((cx + math.cos(angle) * radius, bottom + math.sin(angle) * radius))
    for i in range(12):
        angle = math.pi * i / 11.0
        points.append((cx + math.cos(angle) * radius, top + math.sin(angle) * radius))
    return points


def _capsule_polygon(start: Point, end: Point, diameter: float) -> list[Point]:
    sx, sy = start
    ex, ey = end
    angle = math.atan2(ey - sy, ex - sx)
    radius = diameter / 2.0
    points = []
    for i in range(16):
        a = angle + math.pi / 2.0 + math.pi * i / 15.0
        points.append((sx + math.cos(a) * radius, sy + math.sin(a) * radius))
    for i in range(16):
        a = angle - math.pi / 2.0 + math.pi * i / 15.0
        points.append((ex + math.cos(a) * radius, ey + math.sin(a) * radius))
    return points


def _bounds(points: list[Point]) -> tuple[float, float, float, float]:
    return (min(x for x, _ in points), max(x for x, _ in points),
            min(y for _, y in points), max(y for _, y in points))


def _dedupe_closed(points: list[Point]) -> list[Point]:
    result: list[Point] = []
    for point in points:
        if not result or math.hypot(point[0] - result[-1][0], point[1] - result[-1][1]) > 1e-6:
            result.append(point)
    if len(result) > 1 and math.hypot(result[0][0] - result[-1][0], result[0][1] - result[-1][1]) < 1e-6:
        result.pop()
    return result
