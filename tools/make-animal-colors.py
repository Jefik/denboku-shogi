#!/usr/bin/env python3
"""The colors of the Animals piece set, as a font: make-animal-colors.py
For each animal of data/fonts/NotoEmoji-shogi.ttf, the areas inside its black outline that take a
color (the white body, small white eyes and highlights, thick black parts such as a mane or a
comb, and a few marked spots) are traced into glyphs of data/fonts/NotoEmoji-shogi-colors.otf, in
the emoji font's own units, so the game paints them under the outline glyph at the same place
(the style's [Layers]). Prints the [Layers] group for data/styles/dobutsu.ini.
Needs fontTools, numpy, Pillow, rsvg-convert and potrace."""
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections import deque

import numpy as np
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import parse_path
from fontTools.ttLib import TTFont
from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EMOJI = f"{ROOT}/data/fonts/NotoEmoji-shogi.ttf"
OUT = f"{ROOT}/data/fonts/NotoEmoji-shogi-colors.otf"
SIZE, PAD = 512, 16
# pieces, character, body color, color of the thick black areas ("" keeps them black), {(x, y) in 0-1: color of the area there,
# or None for none (a gap between legs, where the tile shows)}, and spots [(x, y, radius in 0-1, color)] painted inside the
# animal (over its thick parts too), not on its outline; radius 0 paints the whole thick part at that point (a leg, a comb)
ANIMALS = [
    ("GY OU", "🦁", "#fbd673", "#e8902a", {}),
    ("HI", "🦒", "#f7cf45", "#a8642a", {}),
    ("KA", "🐘", "#c4c8cc", "#8e959c", {}),
    ("FU", "🐥", "#ffd83a", "#f08a24", {}),
    ("TO", "🐔", "#ffffff", "#e5443a", {(0.22, 0.455): "#f6c12e"}),
    ("KI", "🐕", "#e2b07a", "#8a5a2b", {}),
    ("GI", "🐈", "#f4a95c", "#c4661e", {}),
    ("NG", "🐅", "#f29a3a", "", {}),
    ("KE", "🐇", "#f1ebe2", "#b9a48c", {}),
    ("NK", "🦘", "#cc8c5c", "#8a5432", {}),
    ("KY", "🐗", "#a57c5c", "#5e4030", {}),
    ("NY", "🦏", "#a3abb3", "#6b737a", {}),
    ("RY", "🐉", "#6cc46c", "#2e7d32", {}),
    ("UM", "🐎", "#b9814f", "#5b3a20", {}),
    # Tori shōgi, in the birds' own colors (Unicode has no crane, pheasant, swallow, wild goose or falcon; the
    # emoji nearest in shape): the phoenix golden in red flames (U+E000: make-fonts.sh gives the ligature's
    # glyph a code of its own), the crane the flamingo's long legs and neck in the red-crowned crane's
    # (丹頂) white with its red crown, the pheasant the cock in the copper pheasant's (ヤマドリ) copper with a
    # chestnut breast, a red face and comb and yellow legs, the quails small brown birds, the swallow a navy bird in flight (not between its
    # beak and the branch), the wild goose (雁) grey-brown, the falcon grey, perched with a hooked beak, and
    # the eagle with its wings spread.
    ("PX", "\ue000", "#f6c445", "#e0442f", {(0.45, 0.64): "#f6c445"}),
    ("CR", "🦩", "#ffffff", "", {(0.587, 0.66): None}, [(0.36, 0.085, 0.045, "#e0442f")]),
    ("PH", "🐓", "#e9a25b", "#9a4f2c", {}, [(0.243, 0.14, 0, "#e0442f"), (0.179, 0.394, 0, "#e0442f"),
                                           (0.343, 0.796, 0, "#f2c94a"), (0.478, 0.88, 0, "#f2c94a"), (0.272, 0.843, 0, "#f2c94a")]),
    ("LQ RQ", "🐦", "#c8a27a", "#7a5534", {}),
    ("SW", "🕊", "#3b4a6b", "#1d2433", {(0.148, 0.389): None}),
    ("GO", "🪿", "#b7a98f", "#e8902a", {}),
    ("FA", "🦜", "#a7b0ba", "#4a525c", {}),
    ("EA", "🦅", "#8a5a2b", "#3b2614", {}),
]


def components(mask):
    """Labels of the 4-connected areas of `mask`, and their sizes."""
    h, w = mask.shape
    label = np.zeros((h, w), np.int32)
    sizes = [0]
    for y in range(h):
        for x in range(w):
            if mask[y, x] and not label[y, x]:
                n = len(sizes)
                label[y, x] = n
                q, count = deque([(y, x)]), 0
                while q:
                    cy, cx = q.popleft()
                    count += 1
                    for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                        if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not label[ny, nx]:
                            label[ny, nx] = n
                            q.append((ny, nx))
                sizes.append(count)
    return label, sizes


def outline(font, ch, work):
    """The emoji drawn black on white, and the view box (font units) it was drawn in."""
    glyphs = font.getGlyphSet()
    name = font.getBestCmap()[ord(ch)]
    pen = SVGPathPen(glyphs)
    glyphs[name].draw(pen)
    from fontTools.pens.boundsPen import BoundsPen
    b = BoundsPen(glyphs)
    glyphs[name].draw(b)
    x0, y0, x1, y1 = b.bounds
    side = max(x1 - x0, y1 - y0) * SIZE / (SIZE - 2 * PAD)
    vx, vy = (x0 + x1) / 2 - side / 2, -(y0 + y1) / 2 - side / 2
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{SIZE}" height="{SIZE}" viewBox="{vx} {vy} {side} {side}">'
           f'<rect x="{vx}" y="{vy}" width="{side}" height="{side}" fill="white"/>'
           f'<path transform="scale(1,-1)" d="{pen.getCommands()}"/></svg>')
    with open(f"{work}/o.svg", "w") as fp:
        fp.write(svg)
    subprocess.run(["rsvg-convert", f"{work}/o.svg", "-o", f"{work}/o.png"], check=True)
    return Image.open(f"{work}/o.png").convert("L"), (vx, vy, side)


# Thick black parts left black though they pass the opening test (the phoenix's flames end in two stubs at its waist, the
# rabbit has a spot at the base of its ear): a point (x, y in 0-1) in each.
KEEP_BLACK = {"\ue000": [(0.277, 0.616), (0.731, 0.616)], "🐇": [(0.49, 0.24)]}


def areas(gray, body, thick, seeds, keep_black=(), spots=()):
    """[(color, mask, over the outline)] of the areas to paint."""
    a = np.array(gray)
    ink = a < 128
    label, sizes = components(~ink)
    outside = {label[0, 0], label[0, -1], label[-1, 0], label[-1, -1]}
    out = {body: np.zeros_like(ink)}  # the body first, the details over it
    for n in range(1, len(sizes)):
        if n in outside:
            continue
        c = body if sizes[n] > SIZE * SIZE // 2500 else "#ffffff"  # eyes and highlights stay white
        out[c] = out.get(c, np.zeros_like(ink)) | (label == n)
    for (sx, sy), c in seeds.items():
        at = int(sy * SIZE), int(sx * SIZE)
        area = label == label[at]
        if label[at] in outside:
            # An open outline (the phoenix's body): the area with its gaps closed by a thicker outline,
            # then grown back to the outline's own edge.
            closed = np.array(gray.filter(ImageFilter.MinFilter(21))) < 128
            clabel, _ = components(~closed)
            if not clabel[at] or clabel[at] == clabel[0, 0]:
                continue
            grown = Image.fromarray(np.where(clabel == clabel[at], 0, 255).astype(np.uint8)).filter(ImageFilter.MinFilter(21))
            area = (np.array(grown) < 128) & ~ink & (label == label[at])
        for k in out:
            out[k] &= ~area
        if c:
            out[c] = out.get(c, np.zeros_like(ink)) | area
    if thick:
        # Thick black (a mane, flames), painted over the outline within a black rim: the ink 7 pixels in from
        # its edge, where a 29-pixel disc fits (an opening), so the outline's own strokes stay black.
        core = np.array(gray.filter(ImageFilter.MaxFilter(15))) < 128
        opened = np.array(gray.filter(ImageFilter.MaxFilter(29)).filter(ImageFilter.MinFilter(29))) < 128
        out[thick] = core & opened
        label_t, _ = components(out[thick])
        for (px, py) in keep_black:
            n = label_t[int(py * SIZE), int(px * SIZE)]
            if n:
                out[thick] &= label_t != n
    inside = np.any(list(out.values()), axis=0)
    under = [(c, m, c == thick) for c, m in out.items()]
    yy, xx = np.mgrid[0:SIZE, 0:SIZE]
    label_t = components(out[thick])[0] if thick in out else None
    for x, y, r, c in spots:
        if not r:  # the whole thick part at that point (a leg, a comb)
            n = label_t[int(y * SIZE), int(x * SIZE)] if label_t is not None else 0
            if n:
                under.append((c, label_t == n, True))
            continue
        disc = inside & (((xx - x * SIZE) ** 2 + (yy - y * SIZE) ** 2) < (r * SIZE) ** 2)
        under += [(c, disc & ~ink, False), (c, disc & out.get(thick, ~inside), True)]
    return [(c, m, top) for c, m, top in under if m.any()]


def trace(mask, view, work):
    """The mask, a little larger so no gap shows at the outline, as paths in font units."""
    grown = Image.fromarray(np.where(mask, 0, 255).astype(np.uint8)).filter(ImageFilter.MinFilter(5))
    grown.save(f"{work}/m.pbm")
    subprocess.run(["potrace", "-s", "--turdsize", "4", "-o", f"{work}/m.svg", f"{work}/m.pbm"], check=True)
    vx, vy, side = view
    k = side / SIZE
    # potrace's paths: tenths of a pixel, y up from the bottom of the image.
    to_font = (k / 10, 0, 0, k / 10, vx, -(vy + side))
    return [(e.get("d"), to_font) for e in ET.parse(f"{work}/m.svg").getroot().iter() if e.tag.endswith("path")]


def main():
    emoji = TTFont(EMOJI)
    upm = emoji["head"].unitsPerEm
    names, charstrings, cmap, layers = [".notdef"], {".notdef": T2CharStringPen(upm, None).getCharString()}, {}, []
    point = 0xE000
    with tempfile.TemporaryDirectory() as work:
        for codes, ch, body, thick, seeds, *spots in ANIMALS:
            gray, view = outline(emoji, ch, work)
            entries = []
            for c, mask, top in areas(gray, body, thick, seeds, KEEP_BLACK.get(ch, ()), spots[0] if spots else ()):
                pen = T2CharStringPen(upm, None)
                for d, t in trace(mask, view, work):
                    parse_path(d, TransformPen(pen, t))
                name = f"uni{point:04X}"
                names.append(name)
                charstrings[name] = pen.getCharString()
                cmap[point] = name
                entries.append(f"U+{point:04X} {c}" + (" top" if top else ""))
                point += 1
            for code in codes.split():
                layers.append(f"{code}={', '.join(entries)}")
    fb = FontBuilder(upm, isTTF=False)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap(cmap)
    fb.setupCFF("NotoEmojiShogiColors", {"FullName": "NotoEmojiShogiColors"}, charstrings, {})
    fb.setupHorizontalMetrics({n: (upm, 0) for n in names})
    fb.setupHorizontalHeader(ascent=emoji["hhea"].ascent, descent=emoji["hhea"].descent)
    fb.setupNameTable({"familyName": "NotoEmojiShogiColors", "styleName": "Regular",
                       "copyright": "Color areas traced from Noto Emoji (c) Google, SIL Open Font License 1.1"})
    fb.setupOS2()
    fb.setupPost()
    fb.save(OUT)
    print("[Layers]")
    print("\n".join(layers))


if __name__ == "__main__":
    sys.exit(main())
