#!/usr/bin/env python3
"""Freeze MD3 geometry and Roboto bitmaps for an integer-only C renderer.

No AndroidX/Pillow/FreeType dependency is used by the kernel module. Geometry
is deliberately a flattened, quantized approximation of the pinned cubics.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re

from PIL import ImageFont, features
import PIL

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "kernel/md3/generated"
SHAPES = ROOT / "third_party/material_shapes/material_shapes.json"
FONT = ROOT / "tests/md3_web/fonts/Roboto-400.ttf"
UNITS = 4096
CURVE_ERROR = 1 / 1024
PILLOW_VERSION = "12.3.0"
FREETYPE_VERSION = "2.14.3"


def mid(a, b):
    return ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)


def flatten(p, tolerance, out, depth=0):
    a, b, c, d = p
    line1 = ((2 * a[0] + d[0]) / 3, (2 * a[1] + d[1]) / 3)
    line2 = ((a[0] + 2 * d[0]) / 3, (a[1] + 2 * d[1]) / 3)
    error = max(math.dist(b, line1), math.dist(c, line2)) * .75
    if error <= tolerance:
        out.append(d)
        return
    if depth >= 16:
        raise ValueError("Cubic exceeded flattening depth bound")
    ab, bc, cd = mid(a, b), mid(b, c), mid(c, d)
    abc, bcd = mid(ab, bc), mid(bc, cd)
    center = mid(abc, bcd)
    flatten((a, ab, abc, center), tolerance, out, depth + 1)
    flatten((center, bcd, cd, d), tolerance, out, depth + 1)


def quantize(points, unit=1):
    result = []
    for x, y in points:
        point = (round(x * UNITS / unit), round(y * UNITS / unit))
        if not result or result[-1] != point:
            result.append(point)
    if len(result) > 1 and result[-1] == result[0]:
        result.pop()
    return result


def emit_points(name, points):
    lines = [f"static const struct qm_point {name}[] = {{"]
    for start in range(0, len(points), 6):
        lines.append("    " + ", ".join("{%d, %d}" % p for p in points[start:start + 6]) + ",")
    lines.append("};")
    return "\n".join(lines)


def make_shapes():
    source = json.loads(SHAPES.read_text())
    if len(source) != 35:
        raise ValueError("Expected the 35 pinned MaterialShapes presets")
    texts, entries, counts = [], [], {}
    for shape in source:
        points = [tuple(shape["cubics"][0][:2])]
        for cubic in shape["cubics"]:
            flatten(list(zip(cubic[::2], cubic[1::2])), CURVE_ERROR, points)
        points = quantize(points)
        if not 3 <= len(points) <= 384:
            raise ValueError(f"Invalid contour size: {shape['name']} {len(points)}")
        if any(not (0 <= x <= UNITS and 0 <= y <= UNITS) for x, y in points):
            raise ValueError(f"Contour outside normalized square: {shape['name']}")
        ident = "qm_shape_" + shape["name"].lower()
        texts.append(emit_points(ident, points))
        entries.append(f'    {{"{shape["name"]}", {ident}, {len(points)}}},')
        counts[shape["name"]] = len(points)
    texts.append("static const struct qm_shape_data qm_shapes[] = {\n" + "\n".join(entries) + "\n};")
    return "\n\n".join(texts), counts


def make_font():
    font = ImageFont.truetype(str(FONT), 18, layout_engine=ImageFont.Layout.BASIC)
    ascent, descent = font.getmetrics()
    coverage, glyphs = bytearray(), []
    for codepoint in range(32, 127):
        char = chr(codepoint)
        left, top, right, bottom = font.getbbox(char, anchor="ls")
        mask = font.getmask(char, mode="L")
        width, height = mask.size
        if width != right - left or height != bottom - top:
            raise ValueError("Glyph mask dimensions disagree with metrics")
        glyphs.append((codepoint, len(coverage), width, height, left, top, round(font.getlength(char))))
        coverage.extend(bytes(mask))
    lines = ["static const qm_u8 qm_roboto_coverage[] = {"]
    for start in range(0, len(coverage), 24):
        lines.append("    " + ",".join(str(b) for b in coverage[start:start + 24]) + ",")
    lines.append("};\nstatic const struct qm_glyph qm_roboto_glyphs[] = {")
    lines.extend("    {%d, %d, %d, %d, %d, %d, %d}," % g for g in glyphs)
    lines.append("};")
    lines.append("static const struct qm_font qm_roboto18 = {\n"
                 f"    qm_roboto_coverage, {len(coverage)}, qm_roboto_glyphs, {len(glyphs)}, {ascent + descent}, {ascent}\n"
                 "};")
    return "\n".join(lines), len(coverage)


class Icon:
    def __init__(self, token):
        self.token = token
        self.paths = []

    def path(self, points, *, closed=False, filled=False):
        self.paths.append((quantize(points, 24), closed, filled))
        return self

    def line(self, *coordinates):
        return self.path(list(zip(coordinates[::2], coordinates[1::2])))

    def circle(self, x, y, radius, filled=False):
        return self.arc(x, y, radius, 0, 2 * math.pi, 40, closed=True, filled=filled)

    def arc(self, x, y, radius, start, end, steps=40, *, closed=False, filled=False):
        return self.path([(x + math.cos(start + (end - start) * i / steps) * radius,
                           y + math.sin(start + (end - start) * i / steps) * radius)
                          for i in range(steps + 1)], closed=closed, filled=filled)

    def rect(self, left, top, right, bottom, filled=False):
        return self.path([(left, top), (right, top), (right, bottom), (left, bottom)],
                         closed=True, filled=filled)

    def curves(self, start, segments, closed=False, filled=False):
        points = [start]
        for segment in segments:
            flatten((points[-1], *segment), 24 * CURVE_ERROR, points)
        return self.path(points, closed=closed, filled=filled)


def make_icons():
    # These paths follow jni/src/ui/md3/md3_icons.cpp's 24-unit native symbols.
    # Curves/circles are sampled offline; raster output can differ from ImGui AA.
    icons = []
    def icon(token):
        item = Icon(token)
        icons.append(item)
        return item
    icon("home").line(3,10,12,3,21,10).line(5,9,5,21,10,21,10,15,14,15,14,21,19,21,19,9)
    item = icon("widgets")
    for y in (0, 10):
        for x in (0, 10): item.rect(4+x,4+y,10+x,10+y)
    item = icon("settings")
    item.path([(12+math.cos(i*math.pi/16-math.pi/2)*(10 if i%4 in (1,2) else 8),
                12+math.sin(i*math.pi/16-math.pi/2)*(10 if i%4 in (1,2) else 8)) for i in range(32)], closed=True)
    item.circle(12,12,3.25)
    icon("speed").arc(12,13,9,math.pi*.75,math.pi*2.25).line(12,13,17,8).circle(12,13,1.7,True)
    icon("info").circle(12,12,9).line(12,11,12,17).circle(12,7,1,True)
    icon("menu").line(4,6,20,6).line(4,12,20,12).line(4,18,20,18)
    icon("close").line(6,6,18,18).line(6,18,18,6)
    icon("minimize").line(4,12,20,12)
    item = icon("sun").circle(12,12,4)
    for i in range(8):
        x,y=math.cos(i*math.pi/4),math.sin(i*math.pi/4)
        item.line(12+8*x,12+8*y,12+10*x,12+10*y)
    icon("moon").curves((14.5,3.5), [((12.5,7),(13.3,12.2),(17,14)),
        ((18.3,14.7),(19.6,15),(21,14.8)), ((19.6,19.7),(14.5,22.2),(9.8,20.6)),
        ((5.1,19.3),(2.5,14.3),(3.8,9.6)), ((5.1,5),(9.7,2.4),(14.5,3.5))], closed=True)
    icon("expand").line(4,10,4,4,10,4).line(14,4,20,4,20,10).line(20,14,20,20,14,20).line(10,20,4,20,4,14)
    icon("format_bold").line(7,4,7,20).line(7,4,12.5,4).curves((12.5,4), [((18,4),(18,11.5),(12.5,11.5))]).line(12.5,11.5,7,11.5).curves((12.5,11.5), [((19.5,11.5),(19.5,20),(12.5,20))]).line(12.5,20,7,20)
    icon("format_italic").line(10,4,19,4).line(5,20,14,20).line(15,4,9,20)
    icon("format_underlined").line(6,4,6,11.5).curves((6,11.5), [((6,19),(18,19),(18,11.5))]).line(18,11.5,18,4).line(5,21,19,21)
    icon("format_color_text").line(6,16,12,3,18,16).line(8.5,11,15.5,11).rect(4,20,20,23,True)
    icon("format_color_fill").line(7,3,11,7).path([(10,5),(17,12),(10,19),(3,12)],closed=True).line(4,12,16,12).curves((20,12), [((21,14),(22.5,15.5),(22.5,17)),((22.5,20.5),(17.5,20.5),(17.5,17)),((17.5,15.5),(19,14),(20,12))],closed=True,filled=True)
    icon("person_add").circle(8,7,3).line(2,20,2,18).curves((2,18), [((2,12),(14,12),(14,18))]).line(14,18,14,20,2,20).line(16,10,22,10).line(19,7,19,13)
    icon("share").line(8.2,10.7,15.8,6.3).line(8.2,13.3,15.8,17.7).circle(6,12,2.5).circle(18,5,2.5).circle(18,19,2.5)
    icon("alarm").circle(12,13,8).line(12,8,12,13,15.5,15.5).line(2.5,5.5,5.5,2.5).line(18.5,2.5,21.5,5.5).line(6,19.5,4.5,21.5).line(18,19.5,19.5,21.5)
    icon("clock").circle(12,12,9).line(12,6,12,12,16,15)
    icon("timer").rect(5,2,19,4,True).rect(5,20,19,22,True).path([(7,4),(17,4),(17,8),(13.5,12),(10.5,12),(7,8)],closed=True,filled=True).path([(10.5,12),(13.5,12),(17,16),(17,20),(7,20),(7,16)],closed=True,filled=True)
    icon("stopwatch").circle(12,14,7).line(10,2,14,2).line(12,2,12,5).line(17.5,8,19.5,6).line(12,9,12,14)
    icon("play").path([(7,4),(20,12),(7,20)],closed=True,filled=True)
    icon("pause").rect(6,4,10,20,True).rect(14,4,18,20,True)
    icon("reset").arc(12,13,8,-math.pi*.75,math.pi*.95).line(6.3,7.3,6.3,3).line(6.3,7.3,10.6,7.3)
    icon("plus").line(5,12,19,12).line(12,5,12,19)
    icon("check").line(5,12,10,17,20,7)
    icon("chevron_down").line(6,9,12,15,18,9)
    texts, paths, entries = [], [], []
    path_index = 0
    for item in icons:
        offset = path_index
        for points, closed, filled in item.paths:
            if not 2 <= len(points) <= 128:
                raise ValueError("Icon path bound exceeded")
            name=f"qm_icon_path_{path_index}"
            texts.append(emit_points(name, points))
            paths.append(f"    {{{name}, {len(points)}, {int(closed)}, {int(filled)}}},")
            path_index += 1
        entries.append(f'    {{"@{item.token}", {offset}, {len(item.paths)}}},')
    texts.append("static const struct qm_icon_path qm_icon_paths[] = {\n" + "\n".join(paths) + "\n};")
    texts.append("static const struct qm_icon_data qm_icons[] = {\n" + "\n".join(entries) + "\n};")
    return "\n\n".join(texts), [item.token for item in icons]


def render():
    if PIL.__version__ != PILLOW_VERSION or features.version("freetype2") != FREETYPE_VERSION:
        raise SystemExit(f"Use Pillow {PILLOW_VERSION} / FreeType {FREETYPE_VERSION} to reproduce frozen font pixels")
    shape_text, counts = make_shapes()
    font_text, coverage_size = make_font()
    icons_text, icons = make_icons()
    preamble = "/* Generated by scripts/generate_kernel_md3_assets.py; do not edit.\n" \
               " * MaterialShapes: Apache-2.0 (third_party/material_shapes/LICENSE).\n" \
               " * Roboto: SIL OFL-1.1 (tests/md3_web/fonts/OFL.txt).\n" \
               " * Integer runtime data; flattened/quantized shape and icon paths. */\n\n"
    manifest = {
        "generator": "scripts/generate_kernel_md3_assets.py",
        "shapes_source": str(SHAPES.relative_to(ROOT)),
        "shapes_sha256": hashlib.sha256(SHAPES.read_bytes()).hexdigest(),
        "shape_units": UNITS,
        "cubic_flattening_error_normalized": CURVE_ERROR,
        "point_quantization_error_per_axis_normalized": 1 / (2 * UNITS),
        "shape_point_counts": counts,
        "font_source": str(FONT.relative_to(ROOT)),
        "font_sha256": hashlib.sha256(FONT.read_bytes()).hexdigest(),
        "font_pixel_size": 18,
        "font_codepoints": [32, 126],
        "font_coverage_bytes": coverage_size,
        "font_layout_engine": "BASIC",
        "pillow_version": PIL.__version__,
        "freetype_version": features.version("freetype2"),
        "icon_source": "jni/src/ui/md3/md3_icons.cpp",
        "icons": icons,
        "runtime_floating_point": False,
    }
    return {
        "shapes.inc": preamble + shape_text + "\n",
        "font.inc": preamble + font_text + "\n",
        "icons.inc": preamble + icons_text + "\n",
        "provenance.json": json.dumps(manifest, indent=2) + "\n",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Validate checked-in generated files without rewriting")
    args = parser.parse_args()
    files = render()
    if args.check:
        stale = [name for name, content in files.items()
                 if not (DEST / name).exists() or (DEST / name).read_text() != content]
        if stale:
            raise SystemExit("Generated assets differ: " + ", ".join(stale))
    else:
        DEST.mkdir(parents=True, exist_ok=True)
        for name, content in files.items():
            (DEST / name).write_text(content)
    print("PASS: 35 quantized MaterialShapes, 95 Roboto glyphs, 28 vector icons")


if __name__ == "__main__":
    main()
