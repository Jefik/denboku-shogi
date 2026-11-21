#!/usr/bin/env python3
"""Trace piece drawings into a font: build_trace_font.py <pieces dir> <work dir> <out.otf> [options]

Removes the piece body (the first path of each SVG), traces what is left with
potrace and maps each piece to its 1-kanji code point, so the result works like
any kanji font. Used for lishogi's intl_monochrome_2d (pictorial, CC BY 4.0),
orangain (cursive promoted pieces, CC BY-SA 3.0) and peanatsu's shogi_printout
(western symbols, CC BY-SA 3.0). Needs rsvg-convert, ImageMagick and potrace.

Options: --name NAME, --codes FU,KY,... (default all), --threshold 60 (percent;
darker pixels become ink), --copyright TEXT, --printout 1 (shogi_printout's
files: King.svg…, the body is the shogi_trapezoid element, texts dropped),
--xboard 1 (shogi_printout's xboard tiles: WhiteKing.svg…, the body and the
movement marks dropped, texts kept: --fontconfig FILE names the fonts they use),
--extra U+E000=FILE.svg (one more glyph, from an xboard tile, at that code point),
--birds 1 (tools/make-birds.py's <code>.svg, drawn as they are, at BIRDS' code points),
--grow N (strokes N pixels bolder on each side, out of 1024), --stroke R (xboard: every text
outlined by R times its size, so all pieces have the same weight), --font FILE (no pieces dir: each
piece's kanji set in that font, to make a thin brush font bolder with --grow), --tori FONT
(Tori shogi's kanji too, TORI_KANJI, set in that font, each as large as the set's own 金 against that
font's; --tori-grow N their --grow, by default the set's).
"""
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import parse_path

CHARS = {"FU": "歩", "KY": "香", "KE": "桂", "GI": "銀", "KI": "金", "KA": "角", "HI": "飛", "OU": "王",
         "GY": "玉", "TO": "と", "NY": "杏", "NK": "圭", "NG": "全", "UM": "馬", "RY": "龍"}
# Tori shogi's birds (tools/make-birds.py), each at its kanji; the quails, both 鶉, at 左 and 右.
BIRDS = {"SW": "燕", "GO": "鴈", "FA": "鷹", "EA": "鵰", "CR": "鶴", "PH": "雉", "LQ": "左", "RQ": "右", "PX": "鵬"}
TORI_KANJI = "燕鴈鷹鵰鶴雉鶉鵬左右"
PRINTOUT = {"FU": "Pawn", "KY": "Lance", "KE": "Knight", "GI": "Silver", "KI": "Gold", "KA": "Bishop", "HI": "Rook",
            "OU": "King", "GY": "King", "TO": "GoldPawn", "NY": "GoldLance", "NK": "GoldKnight", "NG": "GoldSilver",
            "UM": "CrownedBishop", "RY": "CrownedRook"}
PX = 1024
UPM = 1000
DESCENT = 120
SIZE = 900  # largest symbol, in font units; the others keep their relative size


def set_style(e, **props):
    """Sets (or with None removes) CSS properties in an element's style attribute."""
    style = dict(p.split(":", 1) for p in (e.get("style") or "").split(";") if ":" in p)
    for k, v in props.items():
        k = k.replace("_", "-")
        style.pop(k, None) if v is None else style.__setitem__(k, v)
    e.set("style", ";".join(f"{k}:{v}" for k, v in style.items()))


def xboard_tile(path, stroke=0):
    """An xboard tile without the piece body and the movement marks; with `stroke`, every text outlined
    by that fraction of its size (the tiles mix none, thin and thick)."""
    for prefix, uri in [("inkscape", "http://www.inkscape.org/namespaces/inkscape"),
                        ("sodipodi", "http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"), ("", "http://www.w3.org/2000/svg")]:
        ET.register_namespace(prefix, uri)
    tree = ET.parse(path)
    parents = {c: p for p in tree.getroot().iter() for c in p}
    for e in list(parents):
        if (e.get("id") or "").startswith(("shogi_trapezoid", "ShogiMovementMarkers")):
            parents[e].remove(e)
    if stroke:
        for e in tree.getroot().iter("{http://www.w3.org/2000/svg}text"):
            style = dict(p.split(":", 1) for p in e.get("style", "").split(";") if ":" in p)
            size = float(re.match(r"[\d.]+", style.get("font-size", "0")).group())
            color = style.get("stroke", "none")
            set_style(e, stroke=color if color != "none" else style.get("fill", "#000000"),
                      stroke_width=f"{stroke * size:.4f}px")
            for t in e.iter("{http://www.w3.org/2000/svg}tspan"):
                set_style(t, stroke=None, stroke_width=None)
    return ET.tostring(tree.getroot(), encoding="unicode")


def trace(src, work, code, threshold, mode, fontconfig=None, grow=0, stroke=0, text=None):
    if mode == "font":
        text = text or CHARS[code]
        lines = [(text, PX * 0.8, PX * 0.8)] if len(text) == 1 else [(text[0], PX * 0.46, PX * 0.42), (text[1], PX * 0.46, PX * 0.88)]
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{PX}" height="{PX}">'
               + "".join(f'<text x="{PX // 2}" y="{y:.0f}" text-anchor="middle" font-family="{src}" '
                         f'font-size="{size:.0f}">{t}</text>' for t, size, y in lines) + "</svg>")
    elif mode == "file":
        svg = xboard_tile(src, stroke)
    elif mode == "xboard":
        name = PRINTOUT[code]
        path = os.path.join(src, f"White{name}.svg")
        svg = xboard_tile(path if os.path.exists(path) else os.path.join(src, "WhiteAdvisor.svg"), stroke)  # the silver
        if code == "OU":  # the tiles have only 玉: the font's own 王, without the drop
            svg = svg.replace("玉", "王")
    elif mode == "plain":
        svg = open(os.path.join(src, f"{code}.svg")).read()
    elif mode == "printout":
        svg = open(os.path.join(src, f"{PRINTOUT[code]}.svg")).read()
        svg = re.sub(r'<(polygon|path)\b[^>]*id="shogi_trapezoid[^"]*"[^>]*/>', "", svg, count=1)
        svg = re.sub(r"<text\b.*?</text>", "", svg, flags=re.S)
    else:
        svg = re.sub(r"<path [^>]*/>", "", open(os.path.join(src, f"0{code}.svg")).read(), count=1)
    base = os.path.join(work, code)
    with open(base + ".svg", "w") as fp:
        fp.write(svg)
    env = dict(os.environ, FONTCONFIG_FILE=fontconfig) if fontconfig else None
    subprocess.run(["rsvg-convert", "-w", str(PX), "-h", str(PX), "-b", "white", base + ".svg", "-o", base + ".png"],
                   check=True, env=env)
    bolder = ["-morphology", "Erode", f"Disk:{grow}"] if grow else []  # the ink is black: eroding white grows it
    subprocess.run(["magick", base + ".png", "-colorspace", "Gray", "-threshold", f"{threshold}%", *bolder, base + ".pbm"],
                   check=True)
    subprocess.run(["potrace", "-s", "--turdsize", "4", "-o", base + "-trace.svg", base + ".pbm"], check=True)
    root = ET.parse(base + "-trace.svg").getroot()
    return [e.get("d") for e in root.iter() if e.tag.endswith("path")]


def only_font(path, work):
    """A font's family name and a fontconfig that sees only it."""
    from fontTools.ttLib import TTFont
    font = os.path.abspath(path)
    os.makedirs(work, exist_ok=True)
    fontconfig = os.path.join(work, "fonts.conf")
    with open(fontconfig, "w") as fp:
        fp.write(f"<?xml version='1.0'?><fontconfig><dir>{os.path.dirname(font)}</dir>"
                 f"<cachedir>{work}/cache</cachedir></fontconfig>")
    return TTFont(font, lazy=True)["name"].getDebugName(1), fontconfig


def extent(bounds):
    x0, y0, x1, y1 = bounds
    return max(x1 - x0, y1 - y0)


def traced(family, work, code, threshold, fontconfig, grow, text, k):
    rec = RecordingPen()
    for d in trace(family, work, code, threshold, "font", fontconfig, grow, 0, text):
        parse_path(d, TransformPen(rec, (k, 0, 0, k, 0, 0)))
    bounds = BoundsPen(None)
    rec.replay(bounds)
    return rec, bounds.bounds


def main():
    src, work, out = sys.argv[1:4]
    opts = dict(zip(sys.argv[4::2], sys.argv[5::2]))
    name = opts.get("--name", "ShogiIntl")
    codes = opts.get("--codes", ",".join(BIRDS if opts.get("--birds") == "1" else CHARS)).split(",")
    threshold = int(opts.get("--threshold", "60"))
    copyright = opts.get("--copyright", "Symbols by CouchTomato87, CC BY 4.0, traced from lishogi intl_monochrome_2d")
    os.makedirs(work, exist_ok=True)
    k = 0.1 * UPM / PX
    mode = ("xboard" if opts.get("--xboard") == "1" else "printout" if opts.get("--printout") == "1"
            else "plain" if opts.get("--birds") == "1" else "lishogi")
    chars = BIRDS if mode == "plain" else CHARS
    fontconfig = opts.get("--fontconfig")
    if "--font" in opts:
        src, fontconfig = only_font(opts["--font"], work)
        mode = "font"
    jobs = [(chars[code], src, code, mode) for code in codes]
    if "--extra" in opts:
        point, file = opts["--extra"].split("=")
        jobs.append((chr(int(point[2:], 16)), file, "extra", "file"))
    shapes = {}
    for ch, where, code, how in jobs:
        rec = RecordingPen()
        for d in trace(where, work, code, threshold, how, fontconfig, float(opts.get("--grow", 0)),
                       float(opts.get("--stroke", 0))):
            parse_path(d, TransformPen(rec, (k, 0, 0, k, 0, 0)))
        bounds = BoundsPen(None)
        rec.replay(bounds)
        shapes[ch] = (rec, bounds.bounds)
    scale = SIZE / max(max(x1 - x0, y1 - y0) for _, (x0, y0, x1, y1) in shapes.values())
    sizes = {ch: 1.0 for ch in shapes}
    if "--tori" in opts:
        grow = float(opts.get("--tori-grow", opts.get("--grow", 0)))
        family, fc = only_font(opts["--tori"], os.path.join(work, "tori"))
        gold = shapes.get("金")
        if gold:  # as large as the set's own 金 is against this font's
            _, ref = traced(family, work, "ref", threshold, fc, grow, "金", k)
        for ch in TORI_KANJI:
            rec, bounds = traced(family, work, f"t{ord(ch):x}", threshold, fc, grow, ch, k)
            if gold:
                sizes[ch] = extent(gold[1]) / extent(ref)
            shapes[ch] = (rec, bounds)
    names, charstrings, cmap = [".notdef"], {}, {}
    charstrings[".notdef"] = T2CharStringPen(UPM, None).getCharString()
    for ch, (rec, (x0, y0, x1, y1)) in shapes.items():
        pen = T2CharStringPen(UPM, None)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        s = scale * sizes.get(ch, 1.0)
        rec.replay(TransformPen(pen, (s, 0, 0, s, UPM / 2 - cx * s, (UPM / 2 - DESCENT) - cy * s)))
        glyph = f"uni{ord(ch):04X}"
        names.append(glyph)
        charstrings[glyph] = pen.getCharString()
        cmap[ord(ch)] = glyph
    fb = FontBuilder(UPM, isTTF=False)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap(cmap)
    fb.setupCFF(name, {"FullName": name}, charstrings, {})
    fb.setupHorizontalMetrics({n: (UPM, 0) for n in names})
    fb.setupHorizontalHeader(ascent=UPM - DESCENT, descent=-DESCENT)
    fb.setupNameTable({"familyName": name, "styleName": "Regular", "copyright": copyright})
    fb.setupOS2(sTypoAscender=UPM - DESCENT, sTypoDescender=-DESCENT, usWinAscent=UPM - DESCENT, usWinDescent=DESCENT)
    fb.setupPost()
    fb.save(out)


if __name__ == "__main__":
    main()
