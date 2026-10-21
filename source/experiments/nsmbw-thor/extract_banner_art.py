"""Decode the owner's NSMB Wii disc channel banner/icon textures into private PNGs.

opening.bnr = IMET header + U8 {meta/banner.bin, meta/icon.bin, meta/sound.bin};
banner.bin/icon.bin = IMD5 header + LZ77 (0x10) + U8 {arc/timg/*.tpl, arc/blyt/*.brlyt}.
Output goes to ignored build/nsmbw-thor/banner-art only; nothing here is redistributable.
  python extract_banner_art.py [opening.bnr] [out_dir]
"""
import importlib.util
import json
import hashlib
import struct
import sys
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BNR = ROOT / 'build/nsmbw-thor/e1/extracted/files/opening.bnr'
DEFAULT_OUT = ROOT / 'build/nsmbw-thor/banner-art'
sys.path[:0] = [str(ROOT / 'reference/mkw-3ds/tools'), str(ROOT / 'reference/mkw-3ds/vendor/gclib')]
# imagequant is only needed for encoding; decoding never touches it.
sys.modules.setdefault('imagequant', types.ModuleType('imagequant'))
spec = importlib.util.spec_from_file_location('thor_map_bake', ROOT / 'scripts/Bake-ThorMaps.py')
bake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bake)


def u8(data):
    magic, root, header_size, _ = struct.unpack_from('>IIII', data, 0)
    if magic != 0x55AA382D:
        raise ValueError('not a U8 archive')
    total = struct.unpack_from('>I', data, root + 8)[0]
    strings = root + total * 12
    out, ends, stack = {}, [], []
    for i in range(1, total):
        kind_name, offset, size = struct.unpack_from('>III', data, root + i * 12)
        is_dir, name_at = kind_name >> 24, kind_name & 0xFFFFFF
        name = data[strings + name_at:data.index(b'\0', strings + name_at)].decode()
        while ends and i >= ends[-1]:
            ends.pop(); stack.pop()
        if is_dir:
            stack.append(name); ends.append(size)
        else:
            out['/'.join(stack + [name])] = data[offset:offset + size]
    return out


def lz77(data):
    if data[0] != 0x10:
        raise ValueError('expected LZ77 type 0x10')
    size = int.from_bytes(data[1:4], 'little')
    out, at = bytearray(), 4
    while len(out) < size:
        flags = data[at]; at += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                b1, b2 = data[at], data[at + 1]; at += 2
                length, back = (b1 >> 4) + 3, ((b1 & 0xF) << 8 | b2) + 1
                for _ in range(length):
                    out.append(out[-back])
            else:
                out.append(data[at]); at += 1
    return bytes(out)


def inner(blob):
    if blob[:4] == b'IMD5':
        blob = blob[0x20:]
    if blob[:4] == b'LZ77':
        blob = blob[4:]
    if blob[0] == 0x10:
        blob = lz77(blob)
    return u8(blob)


def main():
    bnr = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_BNR
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_OUT
    data = bnr.read_bytes()
    at = data.find(b'IMET')
    if at < 0:
        raise ValueError('no IMET header')
    outer = u8(data[at - 0x40 + 0x600:])
    out.mkdir(parents=True, exist_ok=True)
    records = {}
    for part in ('banner', 'icon'):
        files = inner(outer[f'meta/{part}.bin'])
        for name, payload in sorted(files.items()):
            if not name.endswith('.tpl'):
                continue
            tmp = out / '_tmp.tpl'; tmp.write_bytes(payload)
            try:
                image = bake.decode_tpl(tmp)
            except Exception as error:  # palette/multi-image TPLs are reported, not guessed
                records[f'{part}/{name}'] = {'error': str(error)}
                continue
            target = out / f'{part}_{Path(name).stem}.png'
            image.save(target)
            records[f'{part}/{name}'] = {'png': target.name, 'size': list(image.size)}
        (out / '_tmp.tpl').unlink(missing_ok=True)
    (out / 'banner-art.json').write_text(json.dumps({
        'source': bnr.name, 'source_sha256': hashlib.sha256(data).hexdigest(), 'textures': records}, indent=2) + '\n')
    for k, v in records.items():
        print(k, v)


if __name__ == '__main__':
    main()
