"""Extract Brawl's NW4R RFNT fonts (big-endian, as on the disc) to glyph sheets plus metrics, in the JSON layout
the Galaxy add-on's GameFont reads (PORTING/experiments/galaxy-thor/Extract-LowerFonts.py writes the same).

Usage: python -I extract_fonts.py <extracted disc root> <out dir>
Writes <name>_sheetNN.png and <name>.json for files/system/font/font_melee.brfnt (and any other .brfnt there).
"""
import glob
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from brres_tex import BLOCK, decode  # noqa: E402


def parse(d):
    sig, bom, ver, size, hsz, nblk = struct.unpack_from(">4sHHIHH", d, 0)
    assert sig == b"RFNT" and bom == 0xFEFF, (sig, hex(bom))
    o, finf = hsz, None
    for _ in range(nblk):
        kind, bsz = struct.unpack_from(">4sI", d, o)
        if kind == b"FINF":
            finf = o + 8
        o += bsz
    (font_type, linefeed, alter, dl, dgw, dcw, encoding, p_glyph, p_width, p_map, height, width,
     ascent) = struct.unpack_from(">BbHbBbBIIIBBB", d, finf)
    (cell_w, cell_h, baseline, max_cw, sheet_size, sheet_num, sheet_fmt, sheet_row, sheet_line, sheet_w, sheet_h,
     sheet_img) = struct.unpack_from(">BBbBIHHHHHHI", d, p_glyph)
    widths, w = [], p_width
    while w:
        ib, ie, nxt = struct.unpack_from(">HHI", d, w)
        widths.append((ib, ie, [struct.unpack_from(">bBb", d, w + 8 + 3 * i) for i in range(ie - ib + 1)]))
        w = nxt
    cmap, m = {}, p_map
    while m:
        cb, ce, method, _r, nxt = struct.unpack_from(">HHHHI", d, m)
        info, entries = m + 12, {}
        if method == 0:
            first = struct.unpack_from(">H", d, info)[0]
            entries = {c: first + c - cb for c in range(cb, ce + 1)}
        elif method == 1:
            for c in range(cb, ce + 1):
                idx = struct.unpack_from(">H", d, info + 2 * (c - cb))[0]
                if idx != 0xFFFF:
                    entries[c] = idx
        else:
            n = struct.unpack_from(">H", d, info)[0]
            for i in range(n):
                c, idx = struct.unpack_from(">HH", d, info + 2 + 4 * i)
                entries[c] = idx
        for c, idx in entries.items():
            cmap.setdefault(c, idx)
        m = nxt
    return dict(lineFeed=linefeed, alter=alter, defaults=(dl, dgw, dcw), encoding=encoding, cellW=cell_w,
                cellH=cell_h, baseline=baseline, sheetSize=sheet_size, sheetNum=sheet_num, sheetFmt=sheet_fmt & 0x7FFF,
                sheetRow=sheet_row, sheetLine=sheet_line, sheetW=sheet_w, sheetH=sheet_h, sheetImg=sheet_img,
                widths=widths, cmap=cmap)


def widths_of(f, idx):
    for ib, ie, table in f["widths"]:
        if ib <= idx <= ie:
            return table[idx - ib]
    return f["defaults"]


def main():
    disc, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for path in sorted(glob.glob(os.path.join(disc, "files", "system", "font", "*.brfnt"))):
        name = os.path.splitext(os.path.basename(path))[0]
        d = open(path, "rb").read()
        f = parse(d)
        if f["sheetFmt"] not in BLOCK:
            print(f"skip {name}: sheet format {f['sheetFmt']}")
            continue
        files = []
        for s in range(f["sheetNum"]):
            im = decode(d[f["sheetImg"] + s * f["sheetSize"]:], f["sheetFmt"], f["sheetW"], f["sheetH"])
            if f["sheetFmt"] in (0, 1):  # intensity: white glyphs, coverage in alpha
                im.putalpha(im.getchannel("R"))
                im = im.point(lambda v: 255, None) if False else im
                white = im.copy()
                white.paste((255, 255, 255), (0, 0, im.width, im.height))
                white.putalpha(im.getchannel("A"))
                im = white
            elif f["sheetFmt"] in (2, 3):  # intensity + alpha: keep the alpha, white glyphs
                white = im.copy()
                white.paste((255, 255, 255), (0, 0, im.width, im.height))
                white.putalpha(im.getchannel("A"))
                im = white
            fn = f"{name}_sheet{s:02d}.png"
            im.save(os.path.join(out, fn))
            files.append(fn)
        per = f["sheetRow"] * f["sheetLine"]
        glyphs = {}
        for code, idx in f["cmap"].items():
            sheet, cell = divmod(idx, per)
            if sheet >= f["sheetNum"]:
                continue
            left, gw, adv = widths_of(f, idx)
            uni = code if f["encoding"] in (0, 1) else ord(bytes([code]).decode("cp1252", "replace")) if code < 256 else code
            glyphs[str(uni)] = {"sheet": sheet, "x": (cell % f["sheetRow"]) * (f["cellW"] + 1),
                                "y": (cell // f["sheetRow"]) * (f["cellH"] + 1), "w": gw, "h": f["cellH"],
                                "left": left, "advance": adv}
        meta = {"font": name, "baseline": f["baseline"], "lineFeed": f["lineFeed"], "cellHeight": f["cellH"],
                "sheet": {"files": files, "format": f["sheetFmt"]}, "glyphs": glyphs}
        with open(os.path.join(out, f"{name}.json"), "w", encoding="utf-8") as o:
            json.dump(meta, o)
        print(f"{name}: {len(glyphs)} glyphs, {f['sheetNum']} sheet(s) {f['sheetW']}x{f['sheetH']} format "
              f"{f['sheetFmt']}, cell {f['cellW']}x{f['cellH']}, baseline {f['baseline']}")


if __name__ == "__main__":
    main()
