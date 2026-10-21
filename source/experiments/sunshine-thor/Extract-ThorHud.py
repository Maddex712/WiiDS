"""Extract the owner's Super Mario Sunshine HUD/menu art for the Thor lower screen.

Reads the owner's extracted disc (build/sunshine-thor/extracted/files/data), decompresses
Yaz0 archives, walks RARC, decodes BTI textures and writes PNGs plus manifest.json to
build/sunshine-thor/thor-hud. The images are private: they are staged to the app's external
files directory on the device and never packaged into an APK or committed.
"""
from __future__ import annotations
import argparse, json, struct
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def yaz0(data: bytes) -> bytes:
    if data[:4] != b'Yaz0':
        return data
    size = struct.unpack_from('>I', data, 4)[0]
    out = bytearray(size); src, dst = 16, 0
    while dst < size:
        code = data[src]; src += 1
        for bit in range(8):
            if dst >= size: break
            if code & (0x80 >> bit):
                out[dst] = data[src]; dst += 1; src += 1
            else:
                b1, b2 = data[src], data[src + 1]; src += 2
                dist = ((b1 & 0x0F) << 8 | b2) + 1
                n = b1 >> 4
                if n == 0:
                    n = data[src] + 0x12; src += 1
                else:
                    n += 2
                for _ in range(n):
                    out[dst] = out[dst - dist]; dst += 1
    return bytes(out)


def rarc(data: bytes) -> dict[str, bytes]:
    assert data[:4] == b'RARC'
    data_off = struct.unpack_from('>I', data, 0xC)[0] + 0x20
    info = 0x20
    n_nodes, nodes_off, n_ent, ent_off, _, str_off = struct.unpack_from('>IIIIII', data, info)
    nodes_off += 0x20; ent_off += 0x20; str_off += 0x20
    files = {}
    def name(off):
        e = data.index(b'\0', str_off + off); return data[str_off + off:e].decode('ascii', 'replace')
    def walk(node, prefix):
        _, name_off, _, count, first = struct.unpack_from('>IIHHI', data, nodes_off + node * 16)
        for i in range(count):
            eid, _, typ, noff, a, b, _ = struct.unpack_from('>HHHHIII', data, ent_off + (first + i) * 20)
            nm = name(noff)
            if nm in ('.', '..'): continue
            if typ & 0x0200:  # directory: a is the node index
                walk(a, prefix + nm + '/')
            else:
                files[prefix + nm] = data[data_off + a:data_off + a + b]
    walk(0, '')
    return files


def c565(v):
    return ((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, 255)


def c5a3(v):
    if v & 0x8000:
        return ((v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
    return ((v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17, (v >> 12 & 7) * 255 // 7)


def bti(data: bytes) -> Image.Image:
    fmt, _, w, h = struct.unpack_from('>BBHH', data, 0)
    pal_fmt, pal_n, pal_off = data[9], struct.unpack_from('>H', data, 0xA)[0], struct.unpack_from('>I', data, 0xC)[0]
    img_off = struct.unpack_from('>I', data, 0x1C)[0]
    blocks = {0: (8, 8, 4), 1: (8, 4, 8), 2: (8, 4, 8), 3: (4, 4, 16), 4: (4, 4, 16), 5: (4, 4, 16), 6: (4, 4, 32),
              8: (8, 8, 4), 9: (8, 4, 8), 14: (8, 8, 4)}
    bw, bh, bpp = blocks[fmt]
    pal = []
    if fmt in (8, 9):
        for i in range(pal_n):
            v = struct.unpack_from('>H', data, pal_off + 2 * i)[0]
            pal.append(c5a3(v) if pal_fmt == 2 else c565(v) if pal_fmt == 1 else (v >> 8, v >> 8, v >> 8, v & 255))
    im = Image.new('RGBA', (w, h)); px = im.load(); p = img_off
    for by in range(0, h, bh):
        for bx in range(0, w, bw):
            if fmt == 14:  # CMPR: 2x2 sub-blocks of DXT1
                for sy in range(0, 8, 4):
                    for sx in range(0, 8, 4):
                        c0, c1 = struct.unpack_from('>HH', data, p); bits = struct.unpack_from('>I', data, p + 4)[0]; p += 8
                        a, b = c565(c0), c565(c1)
                        cols = [a, b] + ([tuple((2 * a[i] + b[i]) // 3 for i in range(3)) + (255,),
                                          tuple((a[i] + 2 * b[i]) // 3 for i in range(3)) + (255,)] if c0 > c1 else
                                         [tuple((a[i] + b[i]) // 2 for i in range(3)) + (255,), (0, 0, 0, 0)])
                        for i in range(16):
                            x, y = bx + sx + i % 4, by + sy + i // 4
                            if x < w and y < h: px[x, y] = cols[bits >> (30 - 2 * i) & 3]
                continue
            if fmt == 6:  # RGBA8: AR then GB planes
                ar = data[p:p + 32]; gb = data[p + 32:p + 64]; p += 64
                for i in range(16):
                    x, y = bx + i % 4, by + i // 4
                    if x < w and y < h: px[x, y] = (ar[2 * i + 1], gb[2 * i], gb[2 * i + 1], ar[2 * i])
                continue
            for y in range(by, by + bh):
                for x in range(bx, bx + bw):
                    if bpp == 4:
                        v = data[p] >> 4 if (x - bx) % 2 == 0 else data[p] & 15
                        if (x - bx) % 2 == 1: p += 1
                        c = pal[v] if fmt == 8 else (v * 17,) * 3 + (255,)
                    elif bpp == 8:
                        v = data[p]; p += 1
                        if fmt == 1: c = (v >> 4) * 17; c = (c, c, c, (v & 15) * 17)
                        elif fmt == 2: c = (v, v, v, v)
                        elif fmt == 9: c = pal[v]
                        else: c = (v >> 4) * 17; c = (c, c, c, (v & 15) * 17)
                    else:
                        v = struct.unpack_from('>H', data, p)[0]; p += 2
                        c = (data[p - 1], data[p - 1], data[p - 1], data[p - 2]) if fmt == 3 else c565(v) if fmt == 4 else c5a3(v)
                    if x < w and y < h: px[x, y] = c
    return im


WANT = {
    # The in-course HUD (game_6) and the area name plates from the file screen.
    'game_6.szs': None,
    'select.szs': ('sc_map_', 'sc_shine', 'sc_coin', 'sc_mario', 'sc_story', 'sc_number_', 'sc_question'),
    # Lower-screen theme: the Delfino Tours guide's postcards and paper, and the title logo/clouds.
    'guide.szs': ('guide_picture_', 'guide_map_pinna', 'guide_back', 'guide_draw_palmtree_1', 'guide_draw_ship_1'),
    'option.szs': ('title_logo', 'windows_kumo05', 'stage_airport', 'stage_dolpic'),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data', type=Path, default=ROOT / 'build/sunshine-thor/extracted/files/data')
    ap.add_argument('--out', type=Path, default=ROOT / 'build/sunshine-thor/thor-hud')
    ap.add_argument('--list', action='store_true')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for archive, prefixes in WANT.items():
        files = rarc(yaz0((args.data / archive).read_bytes()))
        for path, blob in sorted(files.items()):
            if not path.endswith('.bti'): continue
            stem = path.rsplit('/', 1)[-1][:-4]
            if prefixes and not stem.startswith(prefixes): continue
            if args.list:
                print(archive, path, blob[0], struct.unpack_from('>HH', blob, 2)); continue
            try:
                im = bti(blob)
            except Exception as e:
                print('skip', archive, path, e); continue
            name = f'{archive.split(".")[0]}-{stem}.png'
            im.save(args.out / name)
            manifest[name] = {'archive': archive, 'entry': path, 'size': list(im.size)}
    if not args.list:
        (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=1))
        print(len(manifest), 'images ->', args.out)


if __name__ == '__main__':
    main()
