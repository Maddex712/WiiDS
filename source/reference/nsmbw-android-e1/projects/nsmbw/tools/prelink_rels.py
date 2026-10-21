"""Offline OSLinkFixed for New Super Mario Bros. Wii (SMNE01 rev 1).

The game loads its four RELs at boot into deterministic heap addresses and links them against
main.dol and against each other. This script reproduces the fully-linked memory state of all four
modules and wraps it in one synthetic, pre-linked REL (no imports) that WiiCompiled's translator
can consume as its single REL input. It also writes a JSON layout file for the runtime.
"""
import json
import struct
import sys
from pathlib import Path

# The folder unpack_game.py filled (default: "game" next to this repository), or the first argument.
GAME = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[3].parent / "game"
OUT_REL = GAME / "nsmbw_linked.rel"
OUT_JSON = GAME / "nsmbw_rel_layout.json"

# File-image (REL header) addresses on SMNE01 rev 1. Derived from the PAL v1 .text bases in
# NSMBW-Decomp's slices plus the E1 offsets in NSMBW-Updated's address-map.txt, and cross-checked
# below against the heap-chaining formula.
IMAGE_BASE = {1: 0x807684C0, 2: 0x8076D680, 3: 0x809A2CC0, 4: 0x80B1C940}
NAMES = {1: "d_profileNP", 2: "d_basesNP", 3: "d_enemiesNP", 4: "d_en_bossNP"}
BSS_PLACEMENT_ALIGN = 0x20   # bss = image + alignUp(fixSize, 0x20)
HEAP_BLOCK_HEADER = 0x10
HEAP_ALIGN = 0x20

R = dict(NONE=0, ADDR32=1, ADDR24=2, ADDR16=3, ADDR16_LO=4, ADDR16_HI=5, ADDR16_HA=6, ADDR14=7,
         ADDR14_BRTAKEN=8, ADDR14_BRNTAKEN=9, REL24=10, REL14=11, REL14_BRTAKEN=12, REL14_BRNTAKEN=13,
         RVL_NONE=201, RVL_SECT=202, RVL_STOP=203)


def align_up(v, a):
    return (v + a - 1) & ~(a - 1)


class Rel:
    def __init__(self, mid):
        self.id = mid
        self.name = NAMES[mid]
        self.raw = (GAME / f"{self.name}.rel").read_bytes()
        b = self.raw
        (file_id, _n, _p, nsec, secoff, _no, _ns, ver, self.bss_size, self.rel_off, self.imp_off, self.imp_size,
         self.prolog_sec, self.epilog_sec, self.unres_sec, self.bss_sec, self.prolog, self.epilog,
         self.unres) = struct.unpack(">IIIIIIIIIIIIBBBBIII", b[:0x40])
        assert file_id == mid and ver == 3, (self.name, file_id, ver)
        self.align, self.bss_align, self.fix_size = struct.unpack(">III", b[0x40:0x4C])
        self.sections = []  # (offset, exec, size)
        for i in range(nsec):
            off, size = struct.unpack(">II", b[secoff + i * 8:secoff + i * 8 + 8])
            self.sections.append((off & ~1, bool(off & 1), size))
        self.imports = [struct.unpack(">II", b[self.imp_off + i * 8:self.imp_off + i * 8 + 8])
                        for i in range(self.imp_size // 8)]
        self.image = IMAGE_BASE[mid]
        self.bss_addr = self.image + align_up(self.fix_size, BSS_PLACEMENT_ALIGN)
        self.end = self.bss_addr + self.bss_size

    def section_addr(self, idx):
        off, _exe, _size = self.sections[idx]
        # A section with no file offset is the BSS section (the header's bssSection byte is
        # only filled in by OSLink at runtime, so it is 0 in the file).
        if off == 0 and idx != 0:
            return self.bss_addr
        return self.image + off


mods = {mid: Rel(mid) for mid in sorted(IMAGE_BASE)}

# Cross-check the chained layout: each module's file buffer comes from the next heap block
# after the previous module's (fixSize + bss) allocation.
ids = sorted(mods)
for prev, cur in zip(ids, ids[1:]):
    expect = align_up(mods[prev].end + HEAP_BLOCK_HEADER, HEAP_ALIGN)
    status = "ok" if expect == mods[cur].image else "MISMATCH"
    print(f"layout {mods[prev].name} -> {mods[cur].name}: predicted 0x{expect:08X}, using 0x{mods[cur].image:08X} [{status}]")
    if status != "ok":
        sys.exit("REL layout does not chain; refusing to guess")

base = mods[ids[0]].image
end = align_up(mods[ids[-1]].end, 0x20)
mem = bytearray(end - base)


def off_of(addr):
    return addr - base


# Load: copy [0, fixSize) of each file (the rest is reused for BSS), then clear BSS.
for m in mods.values():
    o = off_of(m.image)
    mem[o:o + m.fix_size] = m.raw[:m.fix_size]
    bo = off_of(m.bss_addr)
    mem[bo:bo + m.bss_size] = bytes(m.bss_size)


def rd32(addr):
    return struct.unpack(">I", mem[off_of(addr):off_of(addr) + 4])[0]


def wr32(addr, v):
    mem[off_of(addr):off_of(addr) + 4] = struct.pack(">I", v & 0xFFFFFFFF)


def wr16(addr, v):
    mem[off_of(addr):off_of(addr) + 2] = struct.pack(">H", v & 0xFFFF)


counts = {}
for m in mods.values():
    for imp_mod, rel_off in m.imports:
        cursor = rel_off
        cur_sec_addr = 0
        while True:
            delta, typ, sym_sec, addend = struct.unpack(">HBBI", m.raw[cursor:cursor + 8])
            cursor += 8
            if typ == R["RVL_STOP"]:
                break
            if typ == R["RVL_SECT"]:
                cur_sec_addr = m.section_addr(sym_sec)
                continue
            cur_sec_addr += delta  # running destination pointer
            dst = cur_sec_addr
            if imp_mod == 0:
                target = addend
            else:
                target = mods[imp_mod].section_addr(sym_sec) + addend
            counts[typ] = counts.get(typ, 0) + 1
            if typ in (R["NONE"], R["RVL_NONE"]):
                continue
            if typ == R["ADDR32"]:
                wr32(dst, target)
            elif typ == R["ADDR24"]:
                wr32(dst, (rd32(dst) & ~0x03FFFFFC) | (target & 0x03FFFFFC))
            elif typ in (R["ADDR16"], R["ADDR16_LO"]):
                wr16(dst, target)
            elif typ == R["ADDR16_HI"]:
                wr16(dst, target >> 16)
            elif typ == R["ADDR16_HA"]:
                wr16(dst, (target >> 16) + (1 if target & 0x8000 else 0))
            elif typ in (R["ADDR14"], R["ADDR14_BRTAKEN"], R["ADDR14_BRNTAKEN"]):
                wr32(dst, (rd32(dst) & ~0xFFFC) | (target & 0xFFFC))
            elif typ == R["REL24"]:
                wr32(dst, (rd32(dst) & ~0x03FFFFFC) | ((target - dst) & 0x03FFFFFC))
            elif typ in (R["REL14"], R["REL14_BRTAKEN"], R["REL14_BRNTAKEN"]):
                wr32(dst, (rd32(dst) & ~0xFFFC) | ((target - dst) & 0xFFFC))
            else:
                sys.exit(f"{m.name}: unhandled relocation type {typ} at 0x{dst:08X}")

names = {v: k for k, v in R.items()}
print("relocations applied:", ", ".join(f"{names[t]}={n}" for t, n in sorted(counts.items())))

# Synthetic REL: header + section table at the start of the image, sections as absolute ranges.
syn_sections = [(0, False, 0)]
layout = {"image_start": f"0x{base:08X}", "image_end": f"0x{end:08X}", "modules": []}
for m in mods.values():
    text_off, _exe, text_size = m.sections[1]
    syn_sections.append((m.image + text_off - base, True, text_size))
    data_start = m.image + m.sections[2][0]
    data_end = m.image + m.sections[5][0] + m.sections[5][2]
    syn_sections.append((data_start - base, False, data_end - data_start))
    syn_sections.append((m.bss_addr - base, False, m.bss_size))
    sec_abs = {}
    for i, (o, exe, size) in enumerate(m.sections):
        if size:
            sec_abs[str(i)] = {"addr": f"0x{m.section_addr(i):08X}", "size": f"0x{size:X}", "exec": exe}
    layout["modules"].append({
        "id": m.id, "name": m.name,
        "image": f"0x{m.image:08X}", "fix_size": f"0x{m.fix_size:X}",
        "bss": f"0x{m.bss_addr:08X}", "bss_size": f"0x{m.bss_size:X}", "end": f"0x{m.end:08X}",
        "prolog": f"0x{m.section_addr(m.prolog_sec) + m.prolog:08X}",
        "epilog": f"0x{m.section_addr(m.epilog_sec) + m.epilog:08X}",
        "unresolved": f"0x{m.section_addr(m.unres_sec) + m.unres:08X}",
        "ctors": f"0x{m.section_addr(2):08X}-0x{m.section_addr(2) + m.sections[2][2]:08X}",
        "dtors": f"0x{m.section_addr(3):08X}-0x{m.section_addr(3) + m.sections[3][2]:08X}",
        "sections": sec_abs,
    })

hdr_size = 0x4C + 8 * len(syn_sections)
first_payload = min(o for o, _e, s in syn_sections if s)
assert hdr_size <= first_payload, f"synthetic header (0x{hdr_size:X}) would overwrite payload at 0x{first_payload:X}"
size = len(mem)
hdr = struct.pack(">IIIIIIIIIIIIBBBBIII", 0x7E, 0, 0, len(syn_sections), 0x4C, 0, 0, 3, 0, size, size, 0,
                  0, 0, 0, 0, 0, 0, 0) + struct.pack(">III", 0x20, 0x20, size)
table = b"".join(struct.pack(">II", o | (1 if exe else 0), s) for o, exe, s in syn_sections)
out = bytearray(mem)
out[0:hdr_size] = hdr + table
OUT_REL.write_bytes(out)
OUT_JSON.write_text(json.dumps(layout, indent=2))
print(f"wrote {OUT_REL} (0x{size:X} bytes, base 0x{base:08X}, {len(syn_sections)} sections)")
for m in layout["modules"]:
    print(f"  {m['name']:12} image {m['image']} bss {m['bss']}+{m['bss_size']} prolog {m['prolog']} epilog {m['epilog']} ctors {m['ctors']}")
