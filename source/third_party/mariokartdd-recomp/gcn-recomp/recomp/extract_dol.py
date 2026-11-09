#!/usr/bin/env python3
"""Extract main.dol from a GameCube disc image and verify it is the expected build.

Usage: extract_dol.py <image.iso|image.ciso> <out main.dol> <disc id> <main.dol sha1>
"""
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from dol import DiscImage  # noqa: E402


def main():
    img_path, out_path, disc_id, expected_sha1 = sys.argv[1:5]
    expected_id = disc_id.encode()
    img = DiscImage(img_path)
    hdr = img.read(0, 0x440)
    if hdr[:6] != expected_id:
        sys.exit(f'error: {img_path} is disc {hdr[:6]!r}, not {expected_id!r}.\n'
                 'Note: only uncompressed .iso and .ciso images are supported; convert .rvz/.gcz first (see README).')
    dol_off = struct.unpack('>I', hdr[0x420:0x424])[0]
    dh = img.read(dol_off, 0x100)
    offs = struct.unpack('>18I', dh[0x00:0x48])
    sizes = struct.unpack('>18I', dh[0x90:0xD8])
    dol_size = max(o + s for o, s in zip(offs, sizes) if s)
    dol = img.read(dol_off, dol_size)
    sha1 = hashlib.sha1(dol).hexdigest()
    if sha1 != expected_sha1:
        sys.exit(f'error: main.dol SHA-1 {sha1} does not match the supported build ({expected_sha1}).')
    with open(out_path, 'wb') as f:
        f.write(dol)


if __name__ == '__main__':
    main()
