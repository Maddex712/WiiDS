"""Extract the TEX0 textures in Brawl .brres files to PNG (CMPR, RGB5A3, RGBA8, RGB565, I4, I8, IA4, IA8).

Usage: python -I brres_tex.py <out dir> <file.brres> ...
Each texture is written as <out dir>/<brres stem>__<texture name>.png. C4/C8 use the PLT0 palette of the same name.
"""
import os
import struct
import sys

from PIL import Image

BLOCK = {0: (8, 8, 4), 1: (8, 4, 8), 2: (8, 4, 8), 3: (4, 4, 16), 4: (4, 4, 16), 5: (4, 4, 16), 6: (4, 4, 32),
         8: (8, 8, 4), 9: (8, 4, 8), 14: (8, 8, 4)}


def rgb565(v):
    return ((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31


def rgb5a3(v):
    if v & 0x8000:
        return ((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31, 255
    return ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 7) * 255 // 7


def cmpr_block(data, p):
    c0, c1 = struct.unpack(">HH", data[p:p + 4])
    a, b = rgb565(c0), rgb565(c1)
    if c0 > c1:
        pal = [a + (255,), b + (255,), tuple((2 * x + y) // 3 for x, y in zip(a, b)) + (255,),
               tuple((x + 2 * y) // 3 for x, y in zip(a, b)) + (255,)]
    else:
        pal = [a + (255,), b + (255,), tuple((x + y) // 2 for x, y in zip(a, b)) + (255,), (0, 0, 0, 0)]
    bits = struct.unpack(">I", data[p + 4:p + 8])[0]
    return [pal[(bits >> (30 - 2 * i)) & 3] for i in range(16)]


def palette_color(fmt, v):
    if fmt == 0:
        return (v & 255, v & 255, v & 255, v >> 8)
    return rgb565(v) + (255,) if fmt == 1 else rgb5a3(v)


def decode(data, fmt, w, h, palette=None):
    bw, bh, bpp = BLOCK[fmt]
    img = Image.new("RGBA", (w, h))
    px = img.load()
    p = 0

    def put(x, y, c):
        if x < w and y < h:
            px[x, y] = c

    for by in range(0, h, bh):
        for bx in range(0, w, bw):
            if fmt == 14:  # CMPR: 2x2 sub-blocks of 4x4
                for sub in range(4):
                    colors = cmpr_block(data, p)
                    p += 8
                    ox, oy = bx + (sub & 1) * 4, by + (sub >> 1) * 4
                    for i, c in enumerate(colors):
                        put(ox + (i & 3), oy + (i >> 2), c)
            elif fmt == 6:  # RGBA8: AR block then GB block
                ar, gb = data[p:p + 32], data[p + 32:p + 64]
                p += 64
                for i in range(16):
                    put(bx + (i & 3), by + (i >> 2), (ar[2 * i + 1], gb[2 * i], gb[2 * i + 1], ar[2 * i]))
            else:
                for y in range(bh):
                    for x in range(bw):
                        if fmt == 8:
                            byte = data[p + (y * bw + x) // 2]
                            c = palette[byte >> 4 if x % 2 == 0 else byte & 15]
                        elif fmt == 9:
                            c = palette[data[p + y * bw + x]]
                        elif fmt == 0:
                            byte = data[p + (y * bw + x) // 2]
                            v = (byte >> 4 if x % 2 == 0 else byte & 15) * 17
                            c = (v, v, v, 255)
                        elif fmt == 1:
                            v = data[p + y * bw + x]
                            c = (v, v, v, 255)
                        elif fmt == 2:
                            byte = data[p + y * bw + x]
                            v = (byte & 15) * 17
                            c = (v, v, v, (byte >> 4) * 17)
                        elif fmt == 3:
                            a, v = data[p + 2 * (y * bw + x)], data[p + 2 * (y * bw + x) + 1]
                            c = (v, v, v, a)
                        else:
                            v = struct.unpack(">H", data[p + 2 * (y * bw + x):p + 2 * (y * bw + x) + 2])[0]
                            c = rgb565(v) + (255,) if fmt == 4 else rgb5a3(v)
                        put(bx + x, by + y, c)
                p += bw * bh * bpp // 8
    return img


def palettes(blob):
    found = {}
    at = blob.find(b"PLT0")
    while at >= 0:
        try:  # "PLT0" can also occur by chance inside other data (archives hold compressed files)
            data_off, name_off, fmt, count = struct.unpack(">iiIH", blob[at + 16:at + 30])
            name_at = at + name_off
            name = blob[name_at:blob.index(bytes(1), name_at)].decode("ascii", "replace")
            raw = blob[at + data_off:at + data_off + 2 * count]
            found[name] = [palette_color(fmt, v) for v in struct.unpack(f">{count}H", raw)]
        except (struct.error, ValueError):
            pass
        at = blob.find(b"PLT0", at + 4)
    return found


def textures(blob):
    at = blob.find(b"TEX0")
    while at >= 0:
        try:
            version = struct.unpack(">I", blob[at + 8:at + 12])[0]
            data_off, name_off = struct.unpack(">ii", blob[at + 16:at + 24])
            head = at + (0x1C if version >= 3 else 0x18)
            w, h, fmt = struct.unpack(">HHI", blob[head + 4:head + 12])
            name_at = at + name_off
            name = blob[name_at:blob.index(bytes(1), name_at)].decode("ascii", "replace")
            if 0 < name_at < len(blob) and data_off > 0:
                yield name, fmt, w, h, blob[at + data_off:]
        except (struct.error, ValueError):
            pass
        at = blob.find(b"TEX0", at + 4)


def lz77(data):
    """Nintendo LZ77 (type 0x10), the compression Brawl's .pac archives use."""
    size = data[1] | data[2] << 8 | data[3] << 16
    out = bytearray()
    p = 4
    while len(out) < size:
        flags = data[p]
        p += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                length = (data[p] >> 4) + 3
                back = ((data[p] & 15) << 8 | data[p + 1]) + 1
                p += 2
                for _ in range(length):
                    out.append(out[-back])
            else:
                out.append(data[p])
                p += 1
    return bytes(out)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for path in sys.argv[2:]:
        blob = open(path, "rb").read()
        if blob[:1] == b"\x10" and blob[4:8] != b"bres":
            blob = lz77(blob)
        stem = os.path.splitext(os.path.basename(path))[0]
        pals = palettes(blob)
        for name, fmt, w, h, data in textures(blob):
            if fmt not in BLOCK or not (0 < w <= 1024 and 0 < h <= 1024) or (fmt in (8, 9) and name not in pals):
                print(f"skip {stem}/{name} format {fmt} {w}x{h}".encode("ascii", "replace").decode())
                continue
            decode(data, fmt, w, h, pals.get(name)).save(os.path.join(out, f"{stem}__{name}.png"))
            print(f"{stem}/{name} format {fmt} {w}x{h}".encode("ascii", "replace").decode())


if __name__ == "__main__":
    main()
