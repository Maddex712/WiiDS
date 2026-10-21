#!/usr/bin/env python3
"""Bundle the game's disc assets into the port executable.

usage: tools/bundle_disc.py --exe build/linux-32/sms --disc GAME.iso --out build/linux-32/sms-standalone

Reads a GMSE01 disc image (.iso/.gcm, or Dolphin .ciso), packs it into a
trimmed GameCube image (boot.bin, bi2.bin, apploader, main.dol, then the FST
and every file back to back, 32-byte aligned, with no padding or unused
areas), and writes OUT = the executable, that image, and a 32-byte trailer
{"SMSDISC1", u64 LE image offset, u64 LE image size, 8 zero bytes}.
platform/disc (gcdisc_open_embedded) finds the trailer at the end of the
running executable, so OUT runs with no disc image next to it.

The packed image is itself a valid disc image; --image-only writes just that.
"""
import argparse
import os
import shutil
import struct
import sys

ALIGN = 32
MAGIC = 0xC2339F3D
TRAILER_MAGIC = b"SMSDISC1"


def align(n, a=ALIGN):
    return (n + a - 1) // a * a


class Disc:
    """Random-access reader for a plain image or a CISO."""

    def __init__(self, path):
        self.f = open(path, "rb")
        head = self.f.read(0x8000)
        self.ciso = head[:4] == b"CISO"
        if self.ciso:
            self.block = struct.unpack_from("<I", head, 4)[0]
            pos, self.map = 0x8000, []
            for flag in head[8:0x8000]:
                self.map.append(pos if flag else None)
                if flag:
                    pos += self.block
        if head[:4] in (b"RVZ\x01", b"WIA\x01") or head[:4] == b"\xb1\x0b\xc0\x01":
            sys.exit("%s: RVZ/WIA/GCZ images are not supported; convert to .iso with Dolphin" % path)

    def read(self, off, n):
        if not self.ciso:
            self.f.seek(off)
            data = self.f.read(n)
            if len(data) != n:
                sys.exit("disc image is truncated (read 0x%x+0x%x)" % (off, n))
            return data
        out = bytearray()
        while n:
            blk, inb = divmod(off, self.block)
            take = min(n, self.block - inb)
            pos = self.map[blk] if blk < len(self.map) else None
            if pos is None:
                out += bytes(take)
            else:
                self.f.seek(pos + inb)
                out += self.f.read(take)
            off += take
            n -= take
        return bytes(out)


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def pack(disc, out, expect_id):
    """Writes the trimmed image to the open file `out` at its current
    position; returns the image size."""
    base = out.tell()
    boot = bytearray(disc.read(0, 0x440))
    if be32(boot, 0x1C) != MAGIC:
        sys.exit("not a GameCube disc image (no 0xC2339F3D at 0x1C)")
    game_id = boot[:6].decode("ascii", "replace")
    if expect_id and game_id != expect_id:
        sys.exit("disc is %s, expected %s (Super Mario Sunshine, North America)" % (game_id, expect_id))
    bi2 = disc.read(0x440, 0x2000)
    apl_head = disc.read(0x2440, 0x20)
    apl_size = 0x20 + be32(apl_head, 0x14) + be32(apl_head, 0x18)
    apploader = disc.read(0x2440, apl_size)

    dol_off = be32(boot, 0x420)
    dol_head = disc.read(dol_off, 0x100)
    dol_size = max(be32(dol_head, i * 4) + be32(dol_head, 0x90 + i * 4) for i in range(18))
    dol = disc.read(dol_off, dol_size)

    fst_off, fst_size = be32(boot, 0x424), be32(boot, 0x428)
    fst = bytearray(disc.read(fst_off, fst_size))
    count = be32(fst, 8)

    new_dol = align(0x2440 + apl_size, 0x100)
    new_fst = align(new_dol + dol_size)
    pos = align(new_fst + fst_size)
    files = []
    for i in range(1, count):
        e = i * 12
        if fst[e]:
            continue
        off, size = be32(fst, e + 4), be32(fst, e + 8)
        files.append((off, size, pos))
        struct.pack_into(">I", fst, e + 4, pos)
        pos = align(pos + size)
    total = pos
    if total >= 1 << 32:
        sys.exit("packed image does not fit 32-bit disc offsets")

    struct.pack_into(">III", boot, 0x420, new_dol, new_fst, fst_size)
    struct.pack_into(">I", boot, 0x42C, fst_size)

    def put(at, data):
        cur = out.tell() - base
        if at < cur:
            raise AssertionError("overlapping layout")
        out.write(bytes(at - cur))
        out.write(data)

    put(0, boot)
    put(0x440, bi2)
    put(0x2440, apploader)
    put(new_dol, dol)
    put(new_fst, fst)
    done = 0
    for off, size, at in files:
        chunk = 8 << 20
        first = True
        for o in range(0, size, chunk):
            data = disc.read(off + o, min(chunk, size - o))
            if first:
                put(at, data)
                first = False
            else:
                out.write(data)
        if size == 0:
            put(at, b"")
        done += size
    put(total, b"")
    print("bundle_disc: %s, %d files, %.1f MiB of assets, image %.1f MiB"
          % (game_id, len(files), done / 1048576.0, total / 1048576.0))
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", help="port executable to copy (omit with --image-only)")
    ap.add_argument("--disc", required=True, help="GMSE01 disc image (.iso/.gcm/.ciso)")
    ap.add_argument("--out", required=True, help="output executable (or image with --image-only)")
    ap.add_argument("--image-only", action="store_true", help="write only the trimmed disc image")
    ap.add_argument("--game-id", default="GMSE01", help="required game ID ('' accepts any)")
    a = ap.parse_args()
    if not a.image_only and not a.exe:
        ap.error("--exe is required unless --image-only")
    disc = Disc(a.disc)
    tmp = a.out + ".tmp"
    try:
        with open(tmp, "wb") as out:
            if not a.image_only:
                with open(a.exe, "rb") as exe:
                    shutil.copyfileobj(exe, out, 8 << 20)
                out.write(bytes(align(out.tell()) - out.tell()))
            start = out.tell()
            size = pack(disc, out, a.game_id)
            if not a.image_only:
                out.write(TRAILER_MAGIC + struct.pack("<QQ", start, size) + bytes(8))
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise
    if not a.image_only:
        os.chmod(tmp, os.stat(a.exe).st_mode | 0o111)
    os.replace(tmp, a.out)
    print("bundle_disc: wrote %s (%.1f MiB)" % (a.out, os.path.getsize(a.out) / 1048576.0))


if __name__ == "__main__":
    main()
