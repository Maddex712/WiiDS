"""Extract Super Mario Galaxy's NW4R RFNT fonts from the owner's cooked Font.arc.

Cooked RFNT (tools/cook/lyt.py swap_rfnt / swap_file_header): file header, block headers
(kind + size), FINF/TGLP/CWDH/CMAP numeric fields are little-endian; the TGLP sheet image data
stays big-endian GX tiled texture data and is decoded with decode() from Extract-LowerArt.py.

Usage: python -I extract_fonts.py <Font.arc> <out_dir>
Writes per font: <name>_sheetNN.png, <name>.json (Canvas metrics), <name>_atlas.png
(labelled glyph table, for inspection only).
"""
from __future__ import annotations

import importlib.util
import json
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

PORTING = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PORTING / 'reference/galaxy-android/tools'))
import rarc  # noqa: E402

_spec = importlib.util.spec_from_file_location(
    'extract_lowerart', str(PORTING / 'experiments/galaxy-thor/Extract-LowerArt.py'))
_lowerart = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lowerart)
decode = _lowerart.decode

GX_FORMATS = {0: 'I4', 1: 'I8', 2: 'IA4', 3: 'IA8', 4: 'RGB565', 5: 'RGB5A3', 6: 'RGBA8',
              8: 'CI4', 9: 'CI8', 14: 'CMPR'}
ENCODINGS = {0: 'UTF-8', 1: 'UTF-16', 2: 'Shift-JIS', 3: 'CP1252'}
MAP_METHODS = {0: 'direct', 1: 'table', 2: 'scan'}


def code_to_unicode(code: int, encoding: int) -> int:
    if encoding in (0, 1):
        return code
    if encoding == 2:
        raw = bytes([code]) if code < 0x100 else bytes([code >> 8, code & 0xFF])
        return ord(raw.decode('shift_jis'))
    if encoding == 3:
        return ord(bytes([code]).decode('cp1252'))
    raise ValueError(encoding)


def parse_rfnt(d: bytes) -> dict:
    sig, bom, ver, size, hsz, nblk = struct.unpack_from('<4sHHIHH', d, 0)
    assert sig == b'TNFR' and bom == 0xFEFF, (sig, hex(bom))
    blocks = []
    o = hsz
    for _ in range(nblk):
        kind = d[o:o + 4][::-1].decode('ascii')   # kind was swapped as a u32
        bsz = struct.unpack_from('<I', d, o + 4)[0]
        blocks.append((kind, o, bsz))
        o += bsz
    finf_o = next(o for k, o, _ in blocks if k == 'FINF') + 8
    (font_type, linefeed, alter_idx, dl, dgw, dcw, encoding, p_glyph, p_width, p_map,
     height, width, ascent) = struct.unpack_from('<BbHbBbBIIIBBB', d, finf_o)
    g = p_glyph  # pointers are file offsets to block data (header + 8)
    (cell_w, cell_h, baseline, max_cw, sheet_size, sheet_num, sheet_fmt, sheet_row, sheet_line,
     sheet_w, sheet_h, sheet_img) = struct.unpack_from('<BBbBIHHHHHHI', d, g)
    widths = []
    w = p_width
    while w:
        ib, ie, nxt = struct.unpack_from('<HHI', d, w)
        table = [struct.unpack_from('<bBb', d, w + 8 + 3 * i) for i in range(ie - ib + 1)]
        widths.append((ib, ie, table))
        w = nxt
    maps = []
    m = p_map
    while m:
        cb, ce, method, _res, nxt = struct.unpack_from('<HHHHI', d, m)
        info = m + 12
        entries = {}
        if method == 0:
            first = struct.unpack_from('<H', d, info)[0]
            for c in range(cb, ce + 1):
                entries[c] = first + (c - cb)
        elif method == 1:
            for c in range(cb, ce + 1):
                idx = struct.unpack_from('<H', d, info + 2 * (c - cb))[0]
                if idx != 0xFFFF:
                    entries[c] = idx
        elif method == 2:
            n = struct.unpack_from('<H', d, info)[0]
            for i in range(n):
                c, idx = struct.unpack_from('<HH', d, info + 2 + 4 * i)
                entries[c] = idx
        maps.append({'begin': cb, 'end': ce, 'method': MAP_METHODS.get(method, method), 'entries': entries})
        m = nxt
    return {
        'version': ver, 'blocks': blocks,
        'fontType': font_type, 'lineFeed': linefeed, 'alterCharIndex': alter_idx,
        'defaultWidths': (dl, dgw, dcw), 'encoding': encoding,
        'height': height, 'width': width, 'ascent': ascent,
        'cellW': cell_w, 'cellH': cell_h, 'baseline': baseline, 'maxCharWidth': max_cw,
        'sheetSize': sheet_size, 'sheetNum': sheet_num, 'sheetFmt': sheet_fmt,
        'sheetRow': sheet_row, 'sheetLine': sheet_line, 'sheetW': sheet_w, 'sheetH': sheet_h,
        'sheetImg': sheet_img, 'widths': widths, 'maps': maps,
    }


def glyph_widths(f: dict, idx: int):
    for ib, ie, table in f['widths']:
        if ib <= idx <= ie:
            return table[idx - ib]
    return f['defaultWidths']


def glyph_cell(f: dict, idx: int):
    per_sheet = f['sheetRow'] * f['sheetLine']
    sheet, cell = divmod(idx, per_sheet)
    # nw4r::ut::ResFontBase::GetGlyphFromIndex: 1-texel gutter between cells
    x = (cell % f['sheetRow']) * (f['cellW'] + 1)
    y = (cell // f['sheetRow']) * (f['cellH'] + 1)
    return sheet, x, y


def code_map(f: dict) -> dict[int, int]:
    """Font code -> glyph index, honouring NW4R's 'first CMAP whose range holds the code' rule."""
    out = {}
    for i, mp in enumerate(f['maps']):
        earlier = f['maps'][:i]
        for c, idx in mp['entries'].items():
            if any(e['begin'] <= c <= e['end'] for e in earlier):
                continue
            out[c] = idx
    return out


def main() -> int:
    arc, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    files = dict(rarc.rarc_list(rarc.yaz0_decompress(arc.read_bytes())))
    label_font = ImageFont.load_default()
    summary = {}
    for path, data in sorted(files.items()):
        if not path.lower().endswith('.brfnt'):
            continue
        name = Path(path).stem
        d = bytes(data)
        f = parse_rfnt(d)
        fmt = f['sheetFmt'] & 0x7FFF
        sheets = []
        sheet_files = []
        for s in range(f['sheetNum']):
            im = decode(d, f['sheetImg'] + s * f['sheetSize'], fmt, f['sheetW'], f['sheetH'], None)
            fn = f'{name}_sheet{s:02d}.png'
            im.save(out / fn)
            sheets.append(im)
            sheet_files.append(fn)
        cmap = code_map(f)
        glyphs = {}
        for code in sorted(cmap):
            idx = cmap[code]
            left, gw, adv = glyph_widths(f, idx)
            sheet, x, y = glyph_cell(f, idx)
            if sheet >= f['sheetNum']:
                continue
            uni = code_to_unicode(code, f['encoding'])
            glyphs[str(uni)] = {
                'ch': chr(uni), 'code': code, 'index': idx, 'sheet': sheet,
                'x': x, 'y': y, 'w': gw, 'h': f['cellH'],
                'left': left, 'glyphWidth': gw, 'advance': adv,
            }
        a_sheet, a_x, a_y = glyph_cell(f, f['alterCharIndex'])
        a_l, a_gw, a_adv = glyph_widths(f, f['alterCharIndex'])
        # colour mode from pixels
        if fmt in (0, 1):
            colour = 'intensity mask: decoded as white RGB, alpha = intensity (tint with Paint colour / SRC_IN)'
        elif fmt in (2, 3):
            colour = 'intensity + alpha: RGB = grey intensity (game maps it between a black colour and a white colour), alpha = coverage'
        else:
            colour = 'full colour RGBA (RGB5A3: 3-bit alpha in translucent texels); draw as-is'
        meta = {
            'font': name,
            'source': f'LayoutData/Font.arc:{path}',
            'rfntVersion': hex(f['version']),
            'encoding': ENCODINGS.get(f['encoding'], f['encoding']),
            'fontType': f['fontType'],
            'lineFeed': f['lineFeed'],
            'fontHeight': f['height'], 'fontWidth': f['width'], 'ascent': f['ascent'],
            'cellWidth': f['cellW'], 'cellHeight': f['cellH'],
            'baseline': f['baseline'],
            'maxCharWidth': f['maxCharWidth'],
            'defaultWidths': dict(zip(('left', 'glyphWidth', 'advance'), f['defaultWidths'])),
            'alterChar': {'index': f['alterCharIndex'], 'sheet': a_sheet, 'x': a_x, 'y': a_y,
                          'w': a_gw, 'h': f['cellH'], 'left': a_l, 'glyphWidth': a_gw, 'advance': a_adv},
            'sheet': {'count': f['sheetNum'], 'format': GX_FORMATS.get(fmt, fmt), 'gxFormat': fmt,
                      'width': f['sheetW'], 'height': f['sheetH'], 'cols': f['sheetRow'],
                      'rows': f['sheetLine'], 'cellPitchX': f['cellW'] + 1, 'cellPitchY': f['cellH'] + 1,
                      'files': sheet_files},
            'colourMode': colour,
            'codeMaps': [{'begin': f'U+{m["begin"]:04X}', 'end': f'U+{m["end"]:04X}', 'method': m['method'],
                          'count': len(m['entries'])} for m in f['maps']],
            'render': ('src = sheets[sheet] rect (x, y, x+w, y+h); dst left = penX + left*scale, '
                       'dst top = lineTop; dst size = (w*scale, h*scale); penX += advance*scale; '
                       'baseline y = lineTop + baseline*scale; next line: lineTop += lineFeed*scale. '
                       'Missing code point -> alterChar.'),
            'glyphCount': len(glyphs),
            'glyphs': glyphs,
        }
        (out / f'{name}.json').write_text(json.dumps(meta, indent=1, ensure_ascii=False), encoding='utf-8')
        # labelled atlas for inspection
        items = sorted(glyphs.items(), key=lambda kv: int(kv[0]))
        cols = 16
        cw = max(f['cellW'], 40) + 6
        ch = f['cellH'] + 16
        rows = (len(items) + cols - 1) // cols
        atlas = Image.new('RGBA', (cols * cw, rows * ch), (24, 28, 56, 255))
        dr = ImageDraw.Draw(atlas)
        for i, (k, g) in enumerate(items):
            ox, oy = (i % cols) * cw + 3, (i // cols) * ch
            cell = sheets[g['sheet']].crop((g['x'], g['y'], g['x'] + max(g['w'], 1), g['y'] + g['h']))
            dr.rectangle((ox, oy + 12, ox + max(g['w'], 1) - 1, oy + 12 + g['h'] - 1), outline=(60, 70, 110, 255))
            atlas.alpha_composite(cell, (ox, oy + 12))
            dr.line((ox, oy + 12 + f['baseline'], ox + g['advance'], oy + 12 + f['baseline']), fill=(255, 80, 80, 255))
            dr.text((ox, oy), f'{int(k):04X}', fill=(200, 200, 200, 255), font=label_font)
        atlas.save(out / f'{name}_atlas.png')
        summary[name] = {k: meta[k] for k in ('encoding', 'lineFeed', 'fontHeight', 'ascent', 'cellWidth',
                                              'cellHeight', 'baseline', 'maxCharWidth', 'defaultWidths',
                                              'sheet', 'codeMaps', 'glyphCount')}
        print(name, json.dumps(summary[name]))
    (out / 'fonts_summary.json').write_text(json.dumps(summary, indent=1), encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
