#!/usr/bin/env python3
"""Draw the art as SVG, its originals, from a brush font that has 電墨将棋 and a title font:
    make-art.py <brush font> <title font>
The current art uses Yuji Syuku (../denboku-shogi-investigation/fonts-candidates/YujiSyuku-Regular.ttf)
and, for "Denboku Shōgi" in the banner, Marcellus (fonts-candidates/banner-latin/Marcellus-Regular.ttf, OFL);
the tagline uses data/fonts/DenbokuSans-Regular.ttf. The letters are outlines taken from the fonts, so the
SVGs look the same anywhere. Writes
    data/art/banner.svg     README and GitHub preview; banner.png from it for the About dialog
    data/art/icon.svg       black square with a white 墨 piece; icon.png (Linux, Windows, Flatpak, AppImage),
                            windows/denboku.ico and the Android app's (android/app/src/main/res/drawable/icon.png)
    kindle/cover.svg        vertical title and the 電墨 piece; the Kindle library's cover in
                            kindle/shortcut_denboku.sh, as the shortcut's "# Icon:" PNG
Needs fontTools, Pillow (the .ico) and rsvg-convert (librsvg)."""
import base64, io, os, subprocess, sys
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.ttLib import TTFont
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ART = f"{ROOT}/data/art"
FONTS = {"brush": TTFont(sys.argv[1]), "title": TTFont(sys.argv[2]), "sans": TTFont(f"{ROOT}/data/fonts/DenbokuSans-Regular.ttf")}


def outline(font, text, size):
    """`text` at `size` px as one SVG path from (0, 0) on the baseline, and its ink box (x0, y0, x1, y1)."""
    f = FONTS[font]
    glyphs, cmap, upm = f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm
    k, x, d = size / upm, 0, []
    box = [float("inf"), float("inf"), float("-inf"), float("-inf")]
    for ch in text:
        name = cmap.get(ord(ch))
        if not name:
            continue
        g = glyphs[name]
        t = (k, 0, 0, -k, x, 0)  # font units up, SVG down
        pen = SVGPathPen(glyphs, lambda v: f"{v:.1f}".rstrip("0").rstrip("."))
        g.draw(TransformPen(pen, t))
        d.append(pen.getCommands())
        bp = BoundsPen(glyphs)
        g.draw(TransformPen(bp, t))
        if bp.bounds:
            box = [min(box[0], bp.bounds[0]), min(box[1], bp.bounds[1]), max(box[2], bp.bounds[2]), max(box[3], bp.bounds[3])]
        x += g.width * k
    return " ".join(d), box


def text(font, s, size, cx, cy, fill, opacity=1.0):
    """`s` centered by its ink at (cx, cy)."""
    d, (x0, y0, x1, y1) = outline(font, s, size)
    op = f' fill-opacity="{opacity:g}"' if opacity < 1 else ""
    return f'<path transform="translate({cx - (x0 + x1) / 2:.1f} {cy - (y0 + y1) / 2:.1f})" d="{d}" fill="{fill}"{op}/>'


def fit(font, s, width, start):
    """The largest size up to `start` at which `s` is at most `width` wide."""
    size = start
    while size > 20:
        _, (x0, _, x1, _) = outline(font, s, size)
        if x1 - x0 <= width:
            break
        size -= 2
    return size


def piece(x, y, w, h, chars, fill, ink, edge, shadow=False):
    """A koma in (x, y, w, h) with `chars` placed as the game places them (centers at 63/110 for one
    glyph, 38/110 and 83/110 for two), each centered by its ink."""
    px, py, pw, ph = x + w * 0.05, y + h * 0.03, w * 0.9, h * 0.92
    pts = [(px + pw * 0.5, py), (px + pw * 0.85, py + ph * 0.14), (px + pw, py + ph), (px, py + ph), (px + pw * 0.15, py + ph * 0.14)]
    poly = " ".join(f"{a:.1f},{b:.1f}" for a, b in pts)
    out = []
    if shadow:
        out.append(f'<polygon points="{poly}" transform="translate({w * 0.02:.1f} {h * 0.025:.1f})" fill="#000" '
                   f'fill-opacity="0.47" filter="url(#shadow)"/>')
    out.append(f'<polygon points="{poly}" fill="{fill}" stroke="{edge}" stroke-width="{max(1.0, w * 0.012):.1f}" stroke-linejoin="round"/>')
    centers = [0.573] if len(chars) == 1 else [0.345, 0.755]
    size = ph * (0.62 if len(chars) == 1 else 0.40)
    for ch, c in zip(chars, centers):
        out.append(text("brush", ch, size, x + w * 0.5, py + ph * c, ink))
    return "\n".join(out)


def svg(w, h, body, defs=""):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">\n'
            f"{f'<defs>{defs}</defs>' if defs else ''}\n{body}\n</svg>\n")


def icon():
    return svg(256, 256, '<rect x="6" y="6" width="244" height="244" rx="40" fill="#000"/>\n'
               + piece(22, 26, 212, 212, "墨", "#fff", "#000", "#000"))


def cover(W=1200, H=1800):
    """Vertical title on the right, as on a Japanese book, the 電墨 piece on the left."""
    body = [f'<rect width="{W}" height="{H}" fill="#f8f8f8"/>']
    for i, ch in enumerate("電墨将棋"):
        body.append(text("brush", ch, W * 0.25, W * 0.78, H * (0.17 + 0.22 * i), "#000"))
    body.append(f'<line x1="{W * 0.6:g}" y1="{H * 0.06:g}" x2="{W * 0.6:g}" y2="{H * 0.94:g}" stroke="#000" stroke-width="{W * 0.005:g}"/>')
    pw, ph = W * 0.5, W * 0.56
    body.append(piece(W * 0.05, H * 0.5 - ph / 2, pw, ph, "電墨", "#fff", "#000", "#000"))
    return svg(W, H, "\n".join(body))


def banner(W=1280, H=640):
    cx, cy = 875, 290
    body = [f'<rect width="{W}" height="{H}" fill="#f4f1ea"/>',
            text("brush", "墨", 540, 300, 320, "#e2ddd3"),
            piece(165, 170, 270, 300, "電墨", "#fafafa", "#181412", "#141414", shadow=True),
            # 電墨将棋 faint behind the name, the tagline below it.
            text("brush", "電墨将棋", fit("brush", "電墨将棋", 720, 220), cx, cy, "#0f0f0f", 0.12),
            text("title", "Denboku Shōgi", fit("title", "Denboku Shōgi", 640, 120), cx, cy, "#0f0f0f"),
            text("sans", "Shōgi Everywhere", 34, cx, cy + 165, "#505050")]
    return svg(W, H, "\n".join(body), '<filter id="shadow" x="-10%" y="-10%" width="130%" height="130%">'
               '<feGaussianBlur stdDeviation="4.9"/></filter>')


def render(svg_path, w, h):
    png = subprocess.run(["rsvg-convert", "-w", str(w), "-h", str(h), svg_path], check=True, capture_output=True).stdout
    return Image.open(io.BytesIO(png))


os.makedirs(ART, exist_ok=True)
for path, make in ((f"{ART}/icon.svg", icon), (f"{ROOT}/kindle/cover.svg", cover), (f"{ART}/banner.svg", banner)):
    with open(path, "w", encoding="utf-8") as f:
        f.write(make())
ic = render(f"{ART}/icon.svg", 256, 256)
ic.save(f"{ART}/icon.png")
ic.save(f"{ROOT}/android/app/src/main/res/drawable/icon.png")
ic.save(f"{ROOT}/windows/denboku.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
render(f"{ART}/banner.svg", 1280, 640).convert("RGB").save(f"{ART}/banner.png", optimize=True)
# The library shows the shortcut's "# Icon:" PNG as its cover (256×384, as the Kindle's own).
small = io.BytesIO()
render(f"{ROOT}/kindle/cover.svg", 256, 384).convert("L").save(small, "PNG", optimize=True)
with open(f"{ROOT}/kindle/shortcut_denboku.sh", "w") as f:
    f.write("#!/bin/sh\n# Name: 電墨将棋 Denboku Shōgi\n# Author: jefik\n"
            f"# Icon: data:image/png;base64,{base64.b64encode(small.getvalue()).decode()}\n"
            "# DontUseFBInk\n/mnt/us/extensions/denboku/launch.sh &\n")
