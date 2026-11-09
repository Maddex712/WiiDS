#!/usr/bin/env python3
"""contact.py out.png cols in1.png in2.png ... -- tile PNG frames (uses macOS sips to shrink)."""
import os, struct, subprocess, sys, tempfile, zlib

def load(p):
    d = open(p, 'rb').read(); i = 8; idat = b''; w = h = ct = 0
    while i < len(d):
        n = struct.unpack('>I', d[i:i+4])[0]; t = d[i+4:i+8]
        if t == b'IHDR': w, h = struct.unpack('>II', d[i+8:i+16]); ct = d[i+17]
        if t == b'IDAT': idat += d[i+8:i+8+n]
        i += 12 + n
    raw = zlib.decompress(idat); bpp = {2: 3, 6: 4}[ct]; stride = w * bpp
    out = bytearray(); prev = bytearray(stride); p = 0
    for y in range(h):
        f = raw[p]; line = bytearray(raw[p+1:p+1+stride]); p += 1 + stride
        if f:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0; b = prev[x]; c = prev[x-bpp] if x >= bpp else 0
                if f == 1: line[x] = (line[x] + a) & 255
                elif f == 2: line[x] = (line[x] + b) & 255
                elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
                elif f == 4:
                    pp = a + b - c; pa, pb, pc = abs(pp-a), abs(pp-b), abs(pp-c)
                    line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line; prev = line
    rgb = bytearray()
    for i in range(0, len(out), bpp): rgb += out[i:i+3]
    return w, h, rgb

out, cols, files = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
tmp = tempfile.mkdtemp()
ims = []
files = [f for f in files if os.path.exists(f)]
for f in files:
    t = os.path.join(tmp, os.path.basename(f))
    subprocess.run(['sips', '-Z', '320', f, '--out', t], capture_output=True)
    ims.append(load(t))
w = max(i[0] for i in ims); h = max(i[1] for i in ims)
rows = (len(ims) + cols - 1) // cols
W, H = w * cols, h * rows
canvas = bytearray(W * H * 3)
for k, (iw, ih, rgb) in enumerate(ims):
    ox, oy = (k % cols) * w, (k // cols) * h
    for y in range(ih):
        canvas[((oy+y)*W+ox)*3:((oy+y)*W+ox+iw)*3] = rgb[y*iw*3:(y+1)*iw*3]
raw = b''.join(b'\0' + bytes(canvas[y*W*3:(y+1)*W*3]) for y in range(H))
def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
