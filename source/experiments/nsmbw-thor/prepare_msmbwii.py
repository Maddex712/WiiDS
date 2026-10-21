"""Prepare a MSMBWii ("Just a Little Adventure") translation input from the clean E1 preparation.

The mod's Riivolution XML installs the Another SMBW loader, which reads AnotherRes/SystemUS_1.bin
(the USA rev 1 table; every site matches this disc's E1 layout) and writes plain words into the
DOL and the boot RELs. A static recompilation cannot take run-time code writes, so the same words
are applied here, before translation, to copies of main.dol, the pre-linked REL image and the flat
image. The loader hooks themselves (0x80328130/0x80328478 -> 0x80001800) are not applied.

One System entry branches from continueFromFlagObjCheck (0x807EBC5C) to a 6-word cave at
0x80A02610 that only lets profile 0x193 stop the search. The cave is written in place instead:
the function's four float bound checks lose their cror (fcmpo + bgt/blt is the same test for
non-NaN positions), which frees the three words the profile test needs. Original game bytes in
the clean preparation are never modified.
"""
import argparse, hashlib, json, shutil, struct
from pathlib import Path

LOADER_HOOKS = {0x80328130, 0x80328478}
CAVE_HOOK, CAVE = 0x807EBC5C, 0x80A02610


def parse_system(data):
    """Another SMBW System table: (addr, word) pairs, or (count, addr, words...) blocks; ends at -1."""
    out, i = [], 0
    while i + 8 <= len(data):
        a, v = struct.unpack_from('>II', data, i)
        if a == 0xFFFFFFFF:
            break
        if a < 0x80000000:
            words = [struct.unpack_from('>I', data, i + 8 + 4 * k)[0] for k in range(a)]
            out.append((v, words)); i += 8 + 4 * a
        else:
            out.append((a, [v])); i += 8
    return out


def b(src, dst, op=0x48000000):
    return op | ((dst - src) & 0x03FFFFFC)


def bc(src, dst, op):
    return op | ((dst - src) & 0xFFFC)


def flag_obj_rewrite():
    """continueFromFlagObjCheck 0x807EBC1C..0x807EBC60 with the cave's profile test inlined."""
    base, skip, ret = 0x807EBC1C, 0x807EBC64, 0x807EBC7C
    BGT, BLT, BNE = 0x41810000, 0x41800000, 0x40820000
    words = [0xC0210008, 0xC00300AC, 0xFC010040, None, 0xFC00F840, None,
             0xC021000C, 0xC00300B0, 0xFC010040, None, 0xFC00F040, None,
             0xA0030008, 0x28000193, None, 0x38600000, None, 0x60000000]
    ops = {3: BGT, 5: BGT, 9: BLT, 11: BLT, 14: BNE}
    for k, op in ops.items():
        words[k] = bc(base + 4 * k, skip, op)
    words[16] = b(base + 64, ret)
    return base, words


ORIGINAL_FLAG_OBJ = [0xC0210008, 0xC00300AC, 0xFC010040, 0x4C401382, 0x40820038, 0xFC00F840, 0x4C401382,
                     0x4082002C, 0xC021000C, 0xC00300B0, 0xFC010040, 0x4C411382, 0x40820018, 0xFC00F040,
                     0x4C411382, 0x4082000C, 0x38600000, 0x4800001C]


class Image:
    def __init__(self, prepared):
        self.dol = bytearray((prepared / 'main.dol').read_bytes())
        self.rel = bytearray((prepared / 'nsmbw_linked.rel').read_bytes())
        self.flat = bytearray((prepared / 'nsmbw_flat.bin').read_bytes())
        layout = json.loads((prepared / 'nsmbw_rel_layout.json').read_text())
        self.rel_start = int(layout['image_start'], 16)
        self.sections = []
        offs = struct.unpack_from('>18I', self.dol, 0)
        addrs = struct.unpack_from('>18I', self.dol, 0x48)
        sizes = struct.unpack_from('>18I', self.dol, 0x90)
        for o, a, s in zip(offs, addrs, sizes):
            if s:
                self.sections.append((a, s, o))

    def read(self, addr):
        return struct.unpack_from('>I', self.flat, addr - 0x80000000)[0]

    def write(self, addr, word):
        struct.pack_into('>I', self.flat, addr - 0x80000000, word)
        for a, s, o in self.sections:
            if a <= addr < a + s:
                struct.pack_into('>I', self.dol, o + addr - a, word); return 'dol'
        if self.rel_start <= addr < self.rel_start + len(self.rel):
            struct.pack_into('>I', self.rel, addr - self.rel_start, word); return 'rel'
        raise ValueError(f'{addr:08x} is in neither the DOL nor the REL image')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--clean', required=True, type=Path)
    ap.add_argument('--mod', required=True, type=Path, help='msmbwiijala folder')
    ap.add_argument('--out', required=True, type=Path)
    args = ap.parse_args()
    clean_game = args.clean / 'game'
    img = Image(clean_game)
    log = []
    for addr, words in parse_system((args.mod / 'AnotherRes' / 'SystemUS_1.bin').read_bytes()):
        if addr == CAVE_HOOK or addr == CAVE:
            continue
        for k, w in enumerate(words):
            a = addr + 4 * k
            before = img.read(a)
            where = img.write(a, w)
            log.append({'addr': f'{a:08x}', 'before': f'{before:08x}', 'after': f'{w:08x}', 'image': where})
    base, rewrite = flag_obj_rewrite()
    current = [img.read(base + 4 * k) for k in range(len(rewrite))]
    if current != ORIGINAL_FLAG_OBJ:
        raise SystemExit('continueFromFlagObjCheck does not match the expected E1 instructions')
    for k, w in enumerate(rewrite):
        img.write(base + 4 * k, w)
    log.append({'addr': f'{base:08x}', 'words': len(rewrite), 'note': 'cave 0x80A02610 inlined'})
    # The XML's save-name words (only where the original bytes are present).
    for addr in (0x802F148C, 0x802F118C):
        if img.read(addr) == 0x7769696D and img.read(addr + 4) == 0x6A32642E:
            for k, w in enumerate((0x616E6F74, 0x6865722E)):
                img.write(addr + 4 * k, w)
            log.append({'addr': f'{addr:08x}', 'note': 'save name wiimj2d. -> another.'})
    out_game = args.out / 'game'
    if args.out.exists():
        shutil.rmtree(args.out)
    shutil.copytree(clean_game, out_game)
    (out_game / 'main.dol').write_bytes(img.dol)
    (out_game / 'nsmbw_linked.rel').write_bytes(img.rel)
    (out_game / 'nsmbw_flat.bin').write_bytes(img.flat)
    for name in ('MAP-e1.txt',):
        shutil.copy2(args.clean / name, args.out / name)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    receipt = {'mod': "MSMBWii: Just A Little Adventure (MidiGuyDP)", 'table': 'AnotherRes/SystemUS_1.bin',
               'patched_sha256': {n: sha(out_game / n) for n in ('main.dol', 'nsmbw_linked.rel', 'nsmbw_flat.bin')},
               'clean_unchanged_sha256': {n: sha(clean_game / n) for n in ('main.dol', 'nsmbw_linked.rel')},
               'writes': log}
    (args.out / 'msmbwii-preparation.json').write_text(json.dumps(receipt, indent=1))
    for entry in log:
        print(entry)


if __name__ == '__main__':
    main()
