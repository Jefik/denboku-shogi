#!/usr/bin/env python3
"""Make data/fonts/DenbokuSans-{Regular,Bold}.ttf from Noto Sans JP (OFL, variable):
    make-ui-font.py <NotoSansJP[wght].ttf> <NotoSansSymbols2-Regular.ttf>
Static weights (old FreeType on the Kindle) cut to Latin, kana, symbols and the kanji the game
writes, renamed so they never clash with an installed Noto. ⏮ ⏭, which Noto Sans JP lacks, come
from Noto Sans Symbols 2 (OFL, same 1000 units per em). Needs fontTools."""
import glob, os, sys
from fontTools.subset import Options, Subsetter
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
RANGES = [(0x20, 0x7E), (0xA0, 0x17F), (0x2000, 0x206F), (0x2190, 0x21FF), (0x2460, 0x24FF), (0x25A0, 0x25FF),
          (0x2600, 0x26FF), (0x3000, 0x30FF), (0xFF00, 0xFFEF)]
written = set()
for path in glob.glob(f"{ROOT}/src/**/*.[ch]", recursive=True) + glob.glob(f"{ROOT}/data/styles/*.ini") + glob.glob(f"{ROOT}/data/lessons/*.txt"):
    written |= {ord(c) for c in open(path, encoding="utf-8").read()}
written = {u for u in written if u <= 0xFFFF}  # the emoji are pieces, not text
unicodes = {u for u in written if u >= 0x80} | {u for a, b in RANGES for u in range(a, b + 1)}
symbols = TTFont(sys.argv[2])
FROM_SYMBOLS = sorted(u for u in written if u not in TTFont(sys.argv[1]).getBestCmap() and u in symbols.getBestCmap())

for weight, style in ((400, "Regular"), (700, "Bold")):
    font = instantiateVariableFont(TTFont(sys.argv[1]), {"wght": weight})
    options = Options()
    options.layout_features = ["ccmp", "locl", "kern", "liga", "clig", "calt", "mark", "mkmk", "rlig"]  # what Pango shapes horizontal text with
    sub = Subsetter(options)
    sub.populate(unicodes=unicodes)
    sub.subset(font)
    for u in FROM_SYMBOLS:
        name = symbols.getBestCmap()[u]
        font.setGlyphOrder(font.getGlyphOrder() + [name])
        font["glyf"].glyphOrder = font.getGlyphOrder()
        font["glyf"][name] = symbols["glyf"][name]
        font["hmtx"][name] = symbols["hmtx"][name]
        if "vmtx" in font:
            font["vmtx"][name] = (1000, font["vmtx"].metrics[".notdef"][1])
        for table in font["cmap"].tables:
            if table.isUnicode():
                table.cmap[u] = name
    names = font["name"]
    for rec in list(names.names):
        if rec.nameID in (1, 2, 3, 4, 6, 16, 17, 21, 22, 25):
            names.removeNames(nameID=rec.nameID)
    for nid, value in ((1, "Denboku Sans"), (2, style), (3, f"Denboku Sans {style}"), (4, f"Denboku Sans {style}"),
                       (6, f"DenbokuSans-{style}")):
        names.setName(value, nid, 3, 1, 0x409)
        names.setName(value, nid, 1, 0, 0)
    os2 = font["OS/2"]
    os2.usWeightClass = weight
    bold = weight == 700
    os2.fsSelection = (os2.fsSelection & ~0x61) | (0x20 if bold else 0x40)
    font["head"].macStyle = (font["head"].macStyle & ~1) | bold
    if "STAT" in font:
        del font["STAT"]
    font.save(f"{ROOT}/data/fonts/DenbokuSans-{style}.ttf")
