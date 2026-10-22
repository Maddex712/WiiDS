"""Extract the owner's Super Mario Galaxy HUD art for the Thor lower screen.

Reads the owner's cooked game data (build/galaxy-thor/cooked/files/LayoutData: little-endian
"CRAR" archives from the upstream cooker, tools/cook), decodes the TPL textures (headers
swapped to little-endian by tools/cook/lyt.py swap_tpl; pixel and palette data keep the GX
big-endian tile layout) and writes PNGs plus manifest.json to build/galaxy-thor/lower-art.
The images are private: staged to the helper's external files directory (files/ThorHud) on the
device and never packaged into an APK or committed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'reference/galaxy-android/tools'))
import rarc  # noqa: E402  (upstream Yaz0/RARC/CRAR reader)

# archive -> textures (without .tpl) used by the lower screen
WANT = {
    'StarCounter.arc': ['imiconstar'],
    'StarPieceCounter.arc': ['myiconstarpiece', 'myicontotalstarpiece'],
    'CoinCounter.arc': ['myiconcoin'],
    'PurpleCoinCounter.arc': ['myiconpurplecoin'],
    'PlayerLeft.arc': ['mymarios', 'myluigi'],
    'IconComet.arc': ['mycometicon', 'mycomettail'],
    'Icon.arc': ['imicontitlelogoblack', 'mymapstar', 'imkira'],
    'TitleLogo.arc': ['mytitlelogo'],
    'ScenarioSelect.arc': ['mystar', 'myquestion', 'mycometmark'],
    'MapGalaxyIcon.arc': ['mygalaxyicon', 'mygalaxyiconstar'],
    'MapDomeIcon.arc': ['mymapdome1', 'mymapdome2', 'mymapdome3', 'mymapdome4', 'mymapdome5', 'mymapdome6'],
    # the settings menu (GalaxySettingsMenu.java): the A button and the Power Star
    'IconAButton.arc': ['myiconabutton00'],
    'PauseMenu.arc': ['imgetstaricon'],
}


def c565(v):
    return ((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, 255)


def c5a3(v):
    if v & 0x8000:
        return ((v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
    return ((v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17, (v >> 12 & 7) * 255 // 7)


# GX texture formats: (block width, block height, bits per pixel)
BLOCKS = {0: (8, 8, 4), 1: (8, 4, 8), 2: (8, 4, 8), 3: (4, 4, 16), 4: (4, 4, 16), 5: (4, 4, 16),
          6: (4, 4, 32), 8: (8, 8, 4), 9: (8, 4, 8), 14: (8, 8, 4)}


def decode(data: bytes, img_off: int, fmt: int, w: int, h: int, pal: list | None) -> Image.Image:
    """GX tiled pixel data (big-endian) -> RGBA. Intensity formats become white with that alpha."""
    bw, bh, bpp = BLOCKS[fmt]
    im = Image.new('RGBA', (w, h))
    px = im.load()
    p = img_off
    for by in range(0, h, bh):
        for bx in range(0, w, bw):
            if fmt == 14:  # CMPR: 2x2 DXT1 sub-blocks
                for sy in range(0, 8, 4):
                    for sx in range(0, 8, 4):
                        c0, c1 = struct.unpack_from('>HH', data, p)
                        bits = struct.unpack_from('>I', data, p + 4)[0]
                        p += 8
                        a, b = c565(c0), c565(c1)
                        if c0 > c1:
                            cols = [a, b, tuple((2 * a[i] + b[i]) // 3 for i in range(3)) + (255,),
                                    tuple((a[i] + 2 * b[i]) // 3 for i in range(3)) + (255,)]
                        else:
                            cols = [a, b, tuple((a[i] + b[i]) // 2 for i in range(3)) + (255,), (0, 0, 0, 0)]
                        for i in range(16):
                            x, y = bx + sx + i % 4, by + sy + i // 4
                            if x < w and y < h:
                                px[x, y] = cols[bits >> (30 - 2 * i) & 3]
                continue
            if fmt == 6:  # RGBA8: AR plane then GB plane
                ar, gb = data[p:p + 32], data[p + 32:p + 64]
                p += 64
                for i in range(16):
                    x, y = bx + i % 4, by + i // 4
                    if x < w and y < h:
                        px[x, y] = (ar[2 * i + 1], gb[2 * i], gb[2 * i + 1], ar[2 * i])
                continue
            for y in range(by, by + bh):
                for x in range(bx, bx + bw):
                    if bpp == 4:
                        v = data[p] >> 4 if (x - bx) % 2 == 0 else data[p] & 15
                        if (x - bx) % 2 == 1:
                            p += 1
                        c = pal[v] if fmt == 8 else (255, 255, 255, v * 17)          # CI4 / I4
                    elif bpp == 8:
                        v = data[p]
                        p += 1
                        if fmt == 1:
                            c = (255, 255, 255, v)                                       # I8
                        elif fmt == 2:
                            i = (v & 15) * 17
                            c = (i, i, i, (v >> 4) * 17)                                 # IA4
                        else:
                            c = pal[v]                                                   # CI8
                    else:
                        hi, lo = data[p], data[p + 1]
                        v = hi << 8 | lo
                        p += 2
                        c = (lo, lo, lo, hi) if fmt == 3 else c565(v) if fmt == 4 else c5a3(v)  # IA8 / 565 / 5A3
                    if x < w and y < h:
                        px[x, y] = c
    return im


def tpl_images(b: bytes) -> list[Image.Image]:
    """Cooked TPL: little-endian header/descriptors, big-endian GX pixel and palette data."""
    n, table = struct.unpack_from('<II', b, 4)
    out = []
    for i in range(n):
        tex, clut = struct.unpack_from('<II', b, table + 8 * i)
        h, w = struct.unpack_from('<HH', b, tex)
        fmt, data_off = struct.unpack_from('<II', b, tex + 4)
        pal = None
        if clut:
            count = struct.unpack_from('<H', b, clut)[0]
            pal_fmt, pal_off = struct.unpack_from('<II', b, clut + 4)
            pal = []
            for k in range(count):
                v = struct.unpack_from('>H', b, pal_off + 2 * k)[0]
                pal.append(c5a3(v) if pal_fmt == 2 else c565(v) if pal_fmt == 1 else (v & 255, v & 255, v & 255, v >> 8))
        out.append(decode(b, data_off, fmt, w, h, pal))
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--data', type=Path, default=ROOT / 'build/galaxy-thor/cooked/files/LayoutData')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/galaxy-thor/lower-art')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for archive, names in WANT.items():
        files = dict(rarc.rarc_list(rarc.yaz0_decompress((args.data / archive).read_bytes())))
        by_name = {p.split('/')[-1].lower(): d for p, d in files.items()}
        for name in names:
            data = by_name.get(name + '.tpl')
            if data is None:
                raise SystemExit(f'{archive}: {name}.tpl missing')
            images = tpl_images(data)
            key = f'{archive[:-4].lower()}-{name}'
            target = args.out / f'{key}.png'
            images[0].save(target)
            manifest[key] = {'archive': archive, 'texture': name + '.tpl', 'size': list(images[0].size),
                             'sha256': hashlib.sha256(target.read_bytes()).hexdigest()}
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'{len(manifest)} images -> {args.out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
