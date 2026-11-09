#!/usr/bin/env python3
"""Relocatable modules (the SDK's .rel files): extraction, parsing and the canonical link.

A game that loads RELs with OSLink puts them wherever its allocator says, and may vary that
from run to run. The recompiler compiles each at a canonical address instead: this links
every REL there, appends the result to the DOL the recompiler reads, and records which
instructions hold a relocated address, so that the generated code can compute those from
where OSLink actually placed each section (runtime/rels.cpp). See docs/rels.md.

Usage:
  rel.py extract <image> <outdir> <disc path>...
      copy the named files out of the disc image (for dtk's analysis, and the build)
  rel.py link <in.dol> <out.dol> <symbols dir> <file.rel>...
      write <out.dol> with the modules appended at canonical addresses, and beside it
      <out.dol>.rels (the modules and their sections), <out.dol>.relocs (the relocated
      instructions) and <out.dol>.syms (the modules' functions, from dtk's per-module
      symbols in <symbols dir>/<module>.txt, at their canonical addresses)
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from dol import DiscImage  # noqa: E402

# Canonical layout: above the 24 MB of RAM, where no guest pointer points. The code must
# stay within a relative branch (32 MB) of the DOL's, and it keeps the tables indexed by
# code address small; the data and bss go anywhere, since nothing branches there.
TEXT_BASE = 0x81800000
DATA_BASE = 0x82000000
BSS_BASE = 0x83000000

R_PPC_NONE, R_PPC_ADDR32, R_PPC_ADDR24, R_PPC_ADDR16, R_PPC_ADDR16_LO = 0, 1, 2, 3, 4
R_PPC_ADDR16_HI, R_PPC_ADDR16_HA, R_PPC_ADDR14, R_PPC_REL24, R_PPC_REL14 = 5, 6, 7, 10, 11
R_PPC_REL32 = 26
R_DOLPHIN_NOP, R_DOLPHIN_SECTION, R_DOLPHIN_END = 201, 202, 203


def disc_files(img):
    """{path: (offset, size)} for every file on the disc."""
    h = img.read(0, 0x440)
    fst_off, fst_size = struct.unpack('>II', h[0x424:0x42C])
    fst = img.read(fst_off, fst_size)
    count = struct.unpack('>I', fst[8:12])[0]
    strings = count * 12
    out, path, ends = {}, [], []
    for i in range(1, count):
        w0, a, b = struct.unpack('>III', fst[i * 12:i * 12 + 12])
        while ends and i >= ends[-1]:
            ends.pop()
            path.pop()
        o = strings + (w0 & 0xFFFFFF)
        name = fst[o:fst.index(b'\0', o)].decode('latin-1')
        if w0 >> 24:
            path.append(name)
            ends.append(b)
        else:
            out['/'.join(path + [name])] = (a, b)
    return out


def read_disc_file(img, files, path):
    if path not in files:
        sys.exit(f'error: {path} is not on the disc')
    off, size = files[path]
    return img.read(off, size)


class Rel:
    def __init__(self, name, data):
        self.name = name
        self.data = data
        (self.id, _, _, nsec, sec_off, _, _, self.version) = struct.unpack_from('>8I', data, 0)
        self.prolog_sec, self.epilog_sec, self.unres_sec, self.bss_sec = struct.unpack_from('>4B', data, 0x30)
        self.prolog, self.epilog, self.unresolved = struct.unpack_from('>3I', data, 0x34)
        imp_off, imp_size = struct.unpack_from('>2I', data, 0x28)
        self.sections = []  # (file offset or 0, size, executable)
        for k in range(nsec):
            off, size = struct.unpack_from('>2I', data, sec_off + k * 8)
            self.sections.append((off & ~1, size, bool(off & 1)))
        self.imports = []  # (module id, [(type, section, offset, target section, addend)])
        for k in range(imp_size // 8):
            mod, roff = struct.unpack_from('>2I', data, imp_off + k * 8)
            relocs, sec, off = [], 0, 0
            p = roff
            while True:
                delta, typ, tsec, addend = struct.unpack_from('>HBBI', data, p)
                p += 8
                off += delta
                if typ == R_DOLPHIN_END:
                    break
                if typ == R_DOLPHIN_SECTION:
                    sec, off = tsec, 0
                    continue
                if typ != R_DOLPHIN_NOP:
                    relocs.append((typ, sec, off, tsec, addend))
            self.imports.append((mod, relocs))


def write_dol(path, sections, bss, entry):
    """sections: [(addr, bytes, is_text)]. At most 7 text and 11 data."""
    text = [s for s in sections if s[2]]
    data = [s for s in sections if not s[2]]
    if len(text) > 7 or len(data) > 11:
        sys.exit('error: too many sections for a DOL')
    offs, addrs, sizes = [0] * 18, [0] * 18, [0] * 18
    body = bytearray()
    pos = 0x100
    for slot, (a, b, _) in list(enumerate(text)) + [(7 + k, s) for k, s in enumerate(data)]:
        offs[slot], addrs[slot], sizes[slot] = pos, a, len(b)
        body += b
        pos += len(b)
    hdr = struct.pack('>18I18I18I3I', *offs, *addrs, *sizes, bss[0], bss[1], entry)
    hdr += bytes(0x100 - len(hdr))
    open(path, 'wb').write(hdr + body)


def read_dol_sections(path):
    d = open(path, 'rb').read()
    offs = struct.unpack('>18I', d[0:0x48])
    addrs = struct.unpack('>18I', d[0x48:0x90])
    sizes = struct.unpack('>18I', d[0x90:0xD8])
    bss_addr, bss_size, entry = struct.unpack('>3I', d[0xD8:0xE4])
    secs = [(addrs[k], d[offs[k]:offs[k] + sizes[k]], k < 7) for k in range(18) if sizes[k]]
    return secs, (bss_addr, bss_size), entry


SYM_RE = re.compile(r'^(\S+) = (\.?\w+):0x([0-9A-Fa-f]+); // (.*)$')


def link(in_dol, out_dol, symdir, paths):
    rels = [Rel(os.path.basename(p), open(p, 'rb').read()) for p in paths]
    # A module that imports from one not given cannot be linked; leave it out, and any
    # that depend on it. This is the bootstrap build's case (docs/rels.md): it lacks the
    # modules the game assembles at run time, and needs only those that assemble them.
    while True:
        have = {r.id for r in rels}
        keep = [r for r in rels if all(m == 0 or m in have for m, _ in r.imports)]
        if len(keep) == len(rels):
            break
        for r in rels:
            if r not in keep:
                missing = sorted(m for m, _ in r.imports if m and m not in have)
                print(f'note: {r.name} left out: it imports from module(s) {missing}', file=sys.stderr)
        rels = keep
    ids = {}
    for r in rels:
        if r.id in ids:
            sys.exit(f'error: {r.name} and {ids[r.id].name} share module id {r.id}')
        ids[r.id] = r

    # Canonical addresses for every section of every module.
    text_at, data_at, bss_at = TEXT_BASE, DATA_BASE, BSS_BASE
    for r in rels:
        r.canon = []
        for k, (off, size, exe) in enumerate(r.sections):
            if not size:
                r.canon.append(0)
                continue
            if k == r.bss_sec or (off == 0 and not exe):
                bss_at = (bss_at + 31) & ~31
                r.canon.append(bss_at)
                bss_at += size
            elif exe:
                text_at = (text_at + 31) & ~31
                r.canon.append(text_at)
                text_at += size
            else:
                data_at = (data_at + 31) & ~31
                r.canon.append(data_at)
                data_at += size
        r.image = [bytearray(r.data[off:off + size]) if off else None
                   for off, size, _ in r.sections]

    relocs = []  # (canonical addr, kind, module index, section, addend)
    index = {r.id: k for k, r in enumerate(rels)}
    for r in rels:
        for mod, entries in r.imports:
            for typ, sec, off, tsec, addend in entries:
                buf = r.image[sec]
                if buf is None:
                    continue
                where = r.canon[sec] + off
                if mod == 0:
                    target = addend
                else:
                    target = ids[mod].canon[tsec] + addend
                if typ == R_PPC_ADDR32:
                    struct.pack_into('>I', buf, off, target & 0xFFFFFFFF)
                elif typ == R_PPC_REL32:
                    struct.pack_into('>I', buf, off, (target - where) & 0xFFFFFFFF)
                elif typ in (R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA, R_PPC_ADDR16):
                    v = {R_PPC_ADDR16_LO: target, R_PPC_ADDR16: target,
                         R_PPC_ADDR16_HI: target >> 16,
                         R_PPC_ADDR16_HA: (target + 0x8000) >> 16}[typ] & 0xFFFF
                    struct.pack_into('>H', buf, off, v)
                    if mod != 0 and r.sections[sec][2]:
                        kind = {R_PPC_ADDR16_LO: 'LO', R_PPC_ADDR16: 'LO',
                                R_PPC_ADDR16_HI: 'HI', R_PPC_ADDR16_HA: 'HA'}[typ]
                        relocs.append((where & ~3, kind, index[mod], tsec, addend))
                elif typ == R_PPC_REL24:
                    if not -0x2000000 <= target - where < 0x2000000:
                        sys.exit(f'error: {r.name}: branch at {where:08X} cannot reach {target:08X}')
                    w = struct.unpack_from('>I', buf, off)[0]
                    w = (w & 0xFC000003) | ((target - where) & 0x03FFFFFC)
                    struct.pack_into('>I', buf, off, w)
                elif typ == R_PPC_REL14:
                    w = struct.unpack_from('>I', buf, off)[0]
                    w = (w & 0xFFFF0003) | ((target - where) & 0xFFFC)
                    struct.pack_into('>I', buf, off, w)
                elif typ in (R_PPC_ADDR24, R_PPC_ADDR14):
                    sys.exit(f'error: {r.name}: absolute branch relocation at {sec}:{off:X}')
                else:
                    sys.exit(f'error: {r.name}: relocation type {typ} at {sec}:{off:X}')

    if text_at > DATA_BASE:
        sys.exit('error: the modules\' code does not fit below the canonical data')
    secs, bss, entry = read_dol_sections(in_dol)
    text = bytearray(text_at - TEXT_BASE)
    data = bytearray(data_at - DATA_BASE)
    for r in rels:
        for k, (off, size, exe) in enumerate(r.sections):
            if r.image[k] is None or not size:
                continue
            c = r.canon[k]
            if exe:
                text[c - TEXT_BASE:c - TEXT_BASE + size] = r.image[k]
            else:
                data[c - DATA_BASE:c - DATA_BASE + size] = r.image[k]
    if text:
        secs.append((TEXT_BASE, bytes(text), True))
    if data:
        secs.append((DATA_BASE, bytes(data), False))
    write_dol(out_dol, secs, bss, entry)

    with open(out_dol + '.rels', 'w') as f:
        f.write('# index id name prolog epilog unresolved; then per section: index canon size exec\n')
        for k, r in enumerate(rels):
            def fn(sec, off):
                return r.canon[sec] + off if sec else 0
            f.write(f'module {k} {r.id} {r.name} {fn(r.prolog_sec, r.prolog):X} '
                    f'{fn(r.epilog_sec, r.epilog):X} {fn(r.unres_sec, r.unresolved):X}\n')
            for s, (off, size, exe) in enumerate(r.sections):
                if size:
                    f.write(f'section {s} {r.canon[s]:X} {size:X} {int(exe)}\n')
    with open(out_dol + '.relocs', 'w') as f:
        for where, kind, m, s, addend in sorted(relocs):
            f.write(f'{where:X} {kind} {m} {s} {addend:X}\n')

    # The modules' symbols, from dtk's analysis of each, at the canonical addresses.
    # dtk names a module's sections .init/.text/... and numbers them as the REL does,
    # repeating a name for a second section of the same kind.
    with open(out_dol + '.syms', 'w') as f:
        for r in rels:
            base = os.path.splitext(os.path.basename(r.name))[0]
            sp = os.path.join(symdir, base + '.txt')
            if not os.path.exists(sp):
                print(f'warning: no symbols for {r.name} ({sp})', file=sys.stderr)
                continue
            secmap = section_names(r, os.path.join(symdir, base + '.splits.txt'))
            for line in open(sp):
                m = SYM_RE.match(line.strip())
                if not m:
                    continue
                name, sect, addr, rest = m.groups()
                if sect not in secmap:
                    continue
                a = r.canon[secmap[sect]] + int(addr, 16)
                # dtk's names carry the module's id, apart from the entry points every
                # module has (_prolog, _epilog, _unresolved).
                if name.startswith('_'):
                    name = f'{base}{name}'
                f.write(f'{name} = {sect}:0x{a:08X}; // {rest}\n')


def section_names(r, splits_path):
    """dtk's section names for a module: from its splits file when there is one (it lists
    `name type:... ` per section in order), otherwise the usual names by kind."""
    names = {}
    if os.path.exists(splits_path):
        order = []
        in_secs = False
        for line in open(splits_path):
            if line.startswith('Sections:'):
                in_secs = True
                continue
            if in_secs:
                if not line.startswith('\t'):
                    break
                order.append(line.split()[0])
        live = [k for k, (off, size, exe) in enumerate(r.sections) if size]
        for nm, k in zip(order, live):
            names[nm] = k
    return names


def main():
    cmd = sys.argv[1]
    if cmd == 'extract':
        image, outdir, paths = sys.argv[2], sys.argv[3], sys.argv[4:]
        img = DiscImage(image)
        files = disc_files(img)
        os.makedirs(outdir, exist_ok=True)
        for p in paths:
            data = read_disc_file(img, files, p)
            out = os.path.join(outdir, os.path.basename(p))
            if not os.path.exists(out) or open(out, 'rb').read() != data:
                open(out, 'wb').write(data)
    elif cmd == 'link':
        link(*sys.argv[2:5], sys.argv[5:])
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
