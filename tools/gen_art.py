#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The screen's art as C tables: app/art.h.
- the logo (the boot animation, ui_draw.c): the fm1 app's stacked lockup from the logotypes repository (../logotypes,
  beside this repository: apps/fm1/svg), laid out for the 240 x 240 screen: its mark (the ring, the body: the disc with
  its stepped cut, the slash's red ends) over "null" "wave" "fm1", six shapes as coverage masks; the colours are the
  theme's, drawn by the firmware
- the knobs' ring (app.c knob): each pixel of a 19 x 19 box, its coverage of a ring 3 px thick at radius 9 that runs
  from 135 to 405 degrees (0 is right, clockwise on the screen), and where along the ring it lies, 0..255
  tools/gen_art.py
"""
import math
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "app" / "art.h"


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


LOGO_SVG = ROOT.parent / "logotypes" / "apps" / "fm1" / "svg" / "nullwave-fm1-lockup-stacked-dark.svg"   # (light: the
                                                                                                    # same shapes)
PARTS = ("ring", "body", "tails", "null", "wave", "fm1")    # the svg's paths, in its order: the mark's three, the words
MARK = (14, 132)                         # the mark on the screen: its top, the ring's diameter (px of 240)
WORDS = (156, 216)                       # "null" "wave" "fm1": their top, their width; the version's lines under them


def splash_shapes(svg=LOGO_SVG):
    """the logo's shapes laid out on the 240 x 240 screen: [(name, [sub-path's points])], the mark and the words each
    scaled and centred on their own"""
    import re
    s = svg.read_text()
    paths = [[[tuple(float(v) for v in p.split()) for p in sp.split("L")]
              for sp in re.findall(r"M([^MZ]*)Z", re.search(r'd="([^"]*)"', m.group(1)).group(1))]
             for m in re.finditer(r"<path ([^>]*)>", s)]
    assert len(paths) == len(PARTS), "the fm1 lockup svg's paths changed"

    def box(group):
        pts = [q for sub in group for q in sub]
        return min(x for x, y in pts), min(y for x, y in pts), max(x for x, y in pts), max(y for x, y in pts)

    out = []
    for group, (top, size), wide in ((paths[:3], MARK, False), (paths[3:], WORDS, True)):
        x0, y0, x1, y1 = box(paths[0] if not wide else [q for p in group for q in p])
        k = size / (x1 - x0)
        dx, dy = 120 - (x0 + x1) / 2 * k, top - y0 * k
        for subs in group:
            out.append([[(x * k + dx, y * k + dy) for x, y in sub] for sub in subs])
    return list(zip(PARTS, out))


def shape_cover(subs, size, k):
    """a shape's coverage on a size x size screen, drawn at k times and scaled down (fill-rule evenodd: xored)"""
    from PIL import ImageChops, ImageDraw
    im = Image.new("1", (size * k, size * k), 0)
    scale = size * k / 240
    for sub in subs:
        m1 = Image.new("1", im.size, 0)
        ImageDraw.Draw(m1).polygon([(x * scale, y * scale) for x, y in sub], fill=1)
        im = ImageChops.logical_xor(im, m1)
    return im.convert("L").resize((size, size), Image.LANCZOS)


def splash_parts(svg=LOGO_SVG, k=4):
    """the logo's shapes as 16-level coverage masks, each in its own box, run-length coded: a byte is a level (high
    nibble) and a run of 1..16 pixels (low nibble + 1)"""
    out = []
    for name, subs in splash_shapes(svg):
        cov = shape_cover(subs, 240, k)
        bx = cov.getbbox()
        crop = cov.crop(bx)
        data = [round(v * 15 / 255) for v in crop.tobytes()]
        rle, i = [], 0
        while i < len(data):
            c, n = data[i], 1
            while i + n < len(data) and data[i + n] == c and n < 16:
                n += 1
            rle.append(c << 4 | (n - 1))
            i += n
        out.append((bx, rle))
    return out


def ring():
    R0, R1, N = 6.5, 9.5, 19
    cov, pos = [], []
    for y in range(N):
        for x in range(N):
            hit = 0
            for sy in range(4):
                for sx in range(4):
                    dx, dy = x + (sx + .5) / 4 - N / 2, y + (sy + .5) / 4 - N / 2
                    d = math.hypot(dx, dy)
                    a = (math.degrees(math.atan2(dy, dx)) - 135) % 360
                    hit += R0 <= d <= R1 and a <= 270
            cov.append(round(hit * 15 / 16))
            dx, dy = x + .5 - N / 2, y + .5 - N / 2
            a = (math.degrees(math.atan2(dy, dx)) - 135) % 360
            pos.append(min(255, round(a / 270 * 255)) if a <= 270 else 255)
    return N, cov, pos


def arr(name, ctype, vals, per=24):
    lines = [", ".join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
    return f"static const {ctype} {name}[{len(vals)}] = {{\n    " + ",\n    ".join(lines) + "\n};\n"


def main():
    parts = splash_parts()
    n, cov, pos = ring()
    txt = ""
    for name, (bx, rle) in zip(PARTS, parts):
        up = name.upper()
        txt += (f"#define LOGO_{up}_X {bx[0]}\n#define LOGO_{up}_Y {bx[1]}\n#define LOGO_{up}_W {bx[2] - bx[0]}\n"
                f"#define LOGO_{up}_H {bx[3] - bx[1]}\n" + arr(f"LOGO_{up}", "uint8_t", rle))
    OUT.write_text(
        "/* SPDX-License-Identifier: MPL-2.0\n * Copyright (C) 2026 nullwave contributors */\n"
        "/* generated by tools/gen_art.py: the logo's shapes (the boot animation) and the knobs' ring. Do not edit */\n"
        "#pragma once\n#include <stdint.h>\n\n"
        "/* the logo (the fm1 app's stacked lockup, laid out on the 240 x 240 screen): each shape's box and its coverage, 16 levels,\n"
        " * run-length coded (a byte: the level in the high nibble, a run of 1..16 pixels in the low) */\n"
        + txt
        + f"\n#define RING_N {n}\n"
        + arr("RING_COV", "uint8_t", cov, n) + arr("RING_POS", "uint8_t", pos, n))
    print(f"{OUT}: logo " + ", ".join(f"{nm} {len(r)} B" for nm, (b, r) in zip(PARTS, parts)))


if __name__ == "__main__":
    main()
