#!/usr/bin/env python3
"""Make platform/gx/src/gx_shader_keys.inc, the shader warm-up's program keys.

    python3 tools/gx/shader_keys.py RECORDING... [--cache gx-programs-64.bin...]

Each RECORDING is a file the game wrote with SMS_GX_SHADER_KEYS=file: one
line per program key a stage drew with, "<stage> <word>=<hex value> ...".
Run the game with it into each stage (tools/gx/record_shader_keys.sh does it
headless, through SMS_WARP) and play; recordings add up, so several runs can
go into one file or many.

--cache adds the keys of program cache files (gx-programs-<32|64>.bin in the
port's cache folder) under no stage: the warm-up compiles those in the
background only. Their keys are only usable while the key layout is the one
they were made with.

The keys are in the layout of gx_shader.cpp's kShaderKeyLayout; when that
changes, record them again.
"""
import argparse
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHADER = os.path.join(ROOT, 'platform', 'gx', 'src', 'gx_shader.cpp')
OUT = os.path.join(ROOT, 'platform', 'gx', 'src', 'gx_shader_keys.inc')
KEY_SIZE = 468  # sizeof(ProgramKey)
ANY = 0xFF


def layout():
    m = re.search(r'kShaderKeyLayout = (\d+);', open(SHADER).read())
    if not m:
        sys.exit('kShaderKeyLayout not found in ' + SHADER)
    return int(m.group(1))


def read_recording(path, keys):
    for n, line in enumerate(open(path), 1):
        parts = line.split()
        if not parts:
            continue
        try:
            stage = int(parts[0])
            words = {}
            for p in parts[1:]:
                i, v = p.split('=')
                words[int(i)] = int(v, 16)
        except ValueError:
            sys.exit(f'{path}:{n}: not a key line')
        if not 0 <= stage <= 0xFE or any(not 0 <= i < KEY_SIZE // 4 for i in words):
            sys.exit(f'{path}:{n}: stage or word out of range')
        keys.add((stage, tuple(sorted((i, v) for i, v in words.items() if v))))


def read_cache(path, keys):
    d = open(path, 'rb').read()
    if d[:8] != b'SMSGXPB1' or len(d) < 20:
        sys.exit(f'{path}: not a program cache')
    size = struct.unpack_from('<I', d, 16)[0]
    if size != KEY_SIZE:
        sys.exit(f'{path}: keys of {size} bytes, not {KEY_SIZE}')
    p = 20
    while p + 8 + size + 8 <= len(d):
        words = struct.unpack_from(f'<{size // 4}I', d, p + 8)
        length = struct.unpack_from('<I', d, p + 8 + size + 4)[0]
        p += 8 + size + 8 + length
        keys.add((ANY, tuple((i, v) for i, v in enumerate(words) if v)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('recordings', nargs='*')
    ap.add_argument('--cache', nargs='*', default=[])
    ap.add_argument('-o', '--output', default=OUT)
    a = ap.parse_args()
    keys = set()
    for r in a.recordings:
        read_recording(r, keys)
    staged = {k for s, k in keys if s != ANY}
    for c in a.cache:
        found = set()
        read_cache(c, found)
        keys |= {(s, k) for s, k in found if k not in staged}
    # Each program once: its count of nonzero words, then each as its index
    # and its value (LEB128); then each stage: its number, its count of
    # programs (16 bits) and their indices (16 bits each). The rest are any
    # stage's (kept under none).
    programs = sorted({k for _, k in keys})
    index = {k: i for i, k in enumerate(programs)}
    out = bytearray(struct.pack('<H', len(programs)))
    for words in programs:
        out.append(len(words))
        for i, v in words:
            out.append(i)
            while True:
                b = v & 0x7F
                v >>= 7
                out.append(b | (0x80 if v else 0))
                if not v:
                    break
    for stage in sorted({s for s, _ in keys if s != ANY}):
        mine = sorted(index[k] for s, k in keys if s == stage)
        out += struct.pack('<BH', stage, len(mine))
        for i in mine:
            out += struct.pack('<H', i)
    stages = sorted({s for s, _ in keys if s != ANY})
    with open(a.output, 'w') as f:
        f.write('// The program keys the game\'s stages draw with, for the shader warm-up\n'
                '// (gx_shader.cpp). Made by tools/gx/shader_keys.py: do not edit.\n'
                f'// {len({k for _, k in keys})} programs; stages {", ".join(map(str, stages)) or "none"}\n'
                f'static const uint32_t kWarmLayout = {layout()}, kWarmKeySize = {KEY_SIZE}, kWarmFormat = 2;\n'
                'static const uint8_t kWarmKeys[] = {\n')
        for i in range(0, len(out), 32):
            f.write('    ' + ','.join(str(b) for b in out[i:i + 32]) + ',\n')
        if not out:
            f.write('    0,\n')
        f.write('};\n')
    print(f'{a.output}: {len(programs)} programs, {len(stages)} stages, {len(out)} bytes')


if __name__ == '__main__':
    main()
