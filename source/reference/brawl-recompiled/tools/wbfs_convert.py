#!/usr/bin/env python3
"""Convert a single-game .wbfs into a raw (still encrypted) Wii .iso and a .ciso, for testing the launcher's
readers (launcher/WiiDisc.cs). Usage: wbfs_convert.py game.wbfs outdir   -> outdir/<stem>.iso, outdir/<stem>.ciso
Blocks the WBFS does not store are zeros in the .iso and absent from the .ciso. Needs only the standard library."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_wbfs import Wbfs

WII_SINGLE_LAYER = 0x118240000  # bytes on a single-layer Wii disc


def convert(src, outdir):
    disc = Wbfs(src)
    used = [i for i, w in enumerate(disc.wlba) if w]
    size = max(WII_SINGLE_LAYER, (used[-1] + 1) * disc.blk)
    stem = os.path.join(outdir, os.path.splitext(os.path.basename(src))[0])
    os.makedirs(outdir, exist_ok=True)

    with open(stem + ".iso", "wb") as iso:
        iso.truncate(size)  # unstored blocks stay zero
        for i in used:
            iso.seek(i * disc.blk)
            iso.write(disc.read(i * disc.blk, disc.blk))

    # CISO: "CISO", block size (LE u32), one byte per block (1 = stored), padded to 0x8000; then stored blocks in order.
    assert disc.blk % 0x8000 == 0 and size // disc.blk <= 0x8000 - 8
    present = bytearray(0x8000 - 8)
    for i in used:
        present[i] = 1
    with open(stem + ".ciso", "wb") as ciso:
        ciso.write(b"CISO" + struct.pack("<I", disc.blk) + present)
        for i in used:
            ciso.write(disc.read(i * disc.blk, disc.blk))
    print(f"{len(used)} stored blocks of {disc.blk} bytes; iso {size} bytes; holes read {disc.holes}")
    return stem + ".iso", stem + ".ciso"


if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
