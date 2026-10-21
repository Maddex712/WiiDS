#!/usr/bin/env python3
"""Make an app icon from the game's memory-card icon.

usage: tools/extract_icon.py --disc GAME.iso [--icns OUT.icns] [--ico OUT.ico] [--png OUT.png]

Reads /data/common.szs from a GMSE01 disc image (.iso/.gcm, or Dolphin
.ciso), unpacks card/mario_icon.bti (the 32x32 Mario head the game writes
with its saves; C8 with an RGB5A3 palette, two animation frames stacked) and
scales frame 0 up without smoothing. --icns writes a macOS icon (PNG
entries, 16 to 1024 px), --ico a Windows icon (16 to 256 px, for the .exe
resource), --png the 1024 px macOS image. The art comes from
the user's disc at build time, so none of it is kept in this repository.
"""
import argparse
import os
import struct
import sys
import zlib

sys.dont_write_bytecode = True  # no tools/__pycache__ from the import below
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bundle_disc import Disc, be32  # noqa: E402

ICON_PATH = ("data", "common.szs")
ARC_ENTRY = ("card", "mario_icon.bti")
FRAME = 32
# macOS icons leave a margin around the artwork: the sprite covers 25/32 of
# the canvas (800 px of 1024). Windows icons use the whole square.
MAC_FILL = 25 / 32.0


def be16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def disc_file(disc, path):
    boot = disc.read(0, 0x440)
    fst = disc.read(be32(boot, 0x424), be32(boot, 0x428))
    count = be32(fst, 8)

    def name(i):
        o = count * 12 + (be32(fst, i * 12) & 0xFFFFFF)
        return fst[o:fst.index(b"\0", o)].decode("latin1")

    i, end = 1, count
    for depth, part in enumerate(path):
        while i < end:
            is_dir = fst[i * 12]
            if name(i).lower() == part:
                break
            i = be32(fst, i * 12 + 8) if is_dir else i + 1
        else:
            sys.exit("%s not found on the disc" % "/".join(path))
        if depth < len(path) - 1:
            end, i = be32(fst, i * 12 + 8), i + 1
    return disc.read(be32(fst, i * 12 + 4), be32(fst, i * 12 + 8))


def yaz0(src):
    if src[:4] != b"Yaz0":
        return src
    size, out, s = be32(src, 4), bytearray(), 16
    while len(out) < size:
        code = src[s]
        s += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if code & (0x80 >> bit):
                out.append(src[s])
                s += 1
                continue
            b1, b2 = src[s], src[s + 1]
            s += 2
            dist = ((b1 & 0xF) << 8 | b2) + 1
            n = b1 >> 4
            if n == 0:
                n = src[s] + 0x12
                s += 1
            else:
                n += 2
            for _ in range(n):
                out.append(out[-dist])
    return bytes(out)


def rarc_file(arc, path):
    if arc[:4] != b"RARC":
        sys.exit("common.szs is not a RARC archive")
    info = 0x20
    nodes_n, nodes_off, _, ents_off, _, str_off = struct.unpack_from(">IIIIII", arc, info)
    nodes_off, ents_off, str_off = nodes_off + info, ents_off + info, str_off + info
    data = be32(arc, 0xC) + info

    def string(o):
        o += str_off
        return arc[o:arc.index(b"\0", o)].decode("latin1").lower()

    for n in range(nodes_n):
        node = nodes_off + n * 16
        if string(be32(arc, node + 4)) != path[0]:
            continue
        first = be32(arc, node + 12)
        for e in range(first, first + be16(arc, node + 10)):
            ent = ents_off + e * 20
            if string(be32(arc, ent + 4) & 0xFFFFFF) == path[1]:
                off, size = be32(arc, ent + 8), be32(arc, ent + 12)
                return arc[data + off:data + off + size]
    sys.exit("%s not found in common.szs" % "/".join(path))


def rgb5a3(v):
    if v & 0x8000:
        r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
        return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, 255)
    a, r, g, b = (v >> 12) & 7, (v >> 8) & 15, (v >> 4) & 15, v & 15
    return (r * 17, g * 17, b * 17, a << 5 | a << 2 | a >> 1)


def decode_c8(bti):
    """Returns rows of RGBA tuples for a C8 BTI with an RGB5A3 palette."""
    if bti[0] != 9 or bti[9] != 2:
        sys.exit("mario_icon.bti is format %d / palette %d, expected C8 / RGB5A3" % (bti[0], bti[9]))
    w, h = be16(bti, 2), be16(bti, 4)
    pal_off, img = be32(bti, 12), be32(bti, 0x1C)
    pal = [rgb5a3(be16(bti, pal_off + 2 * i)) for i in range(be16(bti, 10))]
    rows = [[None] * w for _ in range(h)]
    o = img
    for ty in range(0, h, 4):
        for tx in range(0, w, 8):
            for y in range(4):
                for x in range(8):
                    rows[ty + y][tx + x] = pal[bti[o]]
                    o += 1
    return rows


def sample(frame, x0, x1, y0, y1):
    """The source pixels [x0, x1) x [y0, y1): the one pixel when enlarging,
    their alpha-weighted average when shrinking."""
    if x1 - x0 <= 1 and y1 - y0 <= 1:
        return bytes(frame[y0][x0])
    px = [frame[y][x] for y in range(y0, y1) for x in range(x0, x1)]
    a = sum(p[3] for p in px)
    if not a:
        return bytes(4)
    return bytes([sum(p[c] * p[3] for p in px) // a for c in range(3)] + [a // len(px)])


def scaled(frame, size, fill):
    """The frame centered on a size x size canvas, without smoothing."""
    art = max(1, round(size * fill))
    pad = (size - art) // 2
    clear = bytes(4)
    out = []
    for y in range(size):
        dy = y - pad
        y0, y1 = dy * FRAME // art, max(dy * FRAME // art + 1, (dy + 1) * FRAME // art)
        row = bytearray()
        for x in range(size):
            dx = x - pad
            if 0 <= dy < art and 0 <= dx < art:
                x0, x1 = dx * FRAME // art, max(dx * FRAME // art + 1, (dx + 1) * FRAME // art)
                row += sample(frame, x0, x1, y0, y1)
            else:
                row += clear
        out.append(bytes(row))
    return out


def png(rows):
    w, h = len(rows[0]) // 4, len(rows)

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    raw = b"".join(b"\0" + r for r in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# (ICNS type, pixel size); PNG payloads are accepted for all of these.
ICNS_TYPES = [(b"icp4", 16), (b"icp5", 32), (b"icp6", 64), (b"ic07", 128), (b"ic08", 256),
              (b"ic09", 512), (b"ic10", 1024), (b"ic11", 32), (b"ic12", 64), (b"ic13", 256),
              (b"ic14", 512)]


def icns(frame):
    pngs = {}
    body = b""
    for tag, size in ICNS_TYPES:
        if size not in pngs:
            pngs[size] = png(scaled(frame, size, MAC_FILL))
        body += tag + struct.pack(">I", 8 + len(pngs[size])) + pngs[size]
    return b"icns" + struct.pack(">I", 8 + len(body)) + body


ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def ico(frame):
    """A Windows icon with a PNG image per size (Vista and later)."""
    images = [png(scaled(frame, size, 1.0)) for size in ICO_SIZES]
    head = struct.pack("<HHH", 0, 1, len(images))
    off = len(head) + 16 * len(images)
    entries = b""
    for size, data in zip(ICO_SIZES, images):
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), off)
        off += len(data)
    return head + entries + b"".join(images)


def write(path, data):
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--disc", required=True, help="GMSE01 disc image (.iso/.gcm/.ciso)")
    ap.add_argument("--icns", help="macOS icon to write")
    ap.add_argument("--ico", help="Windows icon to write")
    ap.add_argument("--png", help="1024 px PNG to write")
    a = ap.parse_args()
    if not (a.icns or a.ico or a.png):
        ap.error("pass --icns, --ico and/or --png")
    arc = yaz0(disc_file(Disc(a.disc), ICON_PATH))
    frame = decode_c8(rarc_file(arc, ARC_ENTRY))[:FRAME]
    if a.icns:
        write(a.icns, icns(frame))
        print("extract_icon: wrote %s" % a.icns)
    if a.ico:
        write(a.ico, ico(frame))
        print("extract_icon: wrote %s" % a.ico)
    if a.png:
        write(a.png, png(scaled(frame, 1024, MAC_FILL)))
        print("extract_icon: wrote %s" % a.png)


if __name__ == "__main__":
    main()
