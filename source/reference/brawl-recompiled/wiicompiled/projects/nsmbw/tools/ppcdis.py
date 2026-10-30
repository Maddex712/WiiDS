"""Disassemble NSMBW (E1) code with symbol and small-data annotations.

usage: ppcdis.py <addr|symbol> [count]     e.g. ppcdis.py OSCreateThread 120
"""
import bisect
import re
import struct
import sys
from pathlib import Path

import capstone

ROOT = Path(r"E:\NSMBWPort")
BASE = 0x80000000
R13, R2 = 0x8042F680, 0x80433080
flat = (ROOT / "game" / "nsmbw_flat.bin").read_bytes()

syms = {}
for line in (ROOT / "maps" / "nsmbw_e1_symbols.tsv").read_text(encoding="utf-8").splitlines():
    a, m, d = line.split("\t")
    syms.setdefault(int(a, 16), d)
for line in (ROOT / "wiicompiled" / "projects" / "nsmbw" / "MAP.txt").read_text().splitlines():
    a, n = line.split(None, 1)
    if not n.startswith("0x"):
        syms.setdefault(int(a, 16), n)
by_name = {}
for a, n in syms.items():
    by_name.setdefault(n, a)
    by_name.setdefault(re.sub(r"\(.*$", "", n), a)
sorted_addrs = sorted(syms)


def label(a):
    i = bisect.bisect_right(sorted_addrs, a) - 1
    if i < 0:
        return ""
    base = sorted_addrs[i]
    off = a - base
    return syms[base] if off == 0 else (f"{syms[base]}+0x{off:X}" if off < 0x4000 else "")


def main():
    target = sys.argv[1]
    start = int(target, 16) if re.fullmatch(r"(0x)?[0-9A-Fa-f]{8}", target) else by_name[target]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    md.skipdata = True
    code = flat[start - BASE:start - BASE + count * 4]
    print(f"; {label(start)} @ 0x{start:08X}")
    for ins in md.disasm(code, start):
        note = ""
        ops = ins.op_str
        m = re.search(r"(-?0x[0-9a-f]+|-?\d+)\((r13|r2)\)", ops)
        if m:
            off = int(m.group(1), 0)
            ea = ((R13 if m.group(2) == "r13" else R2) + off) & 0xFFFFFFFF
            note = f"  ; 0x{ea:08X} {label(ea)}"
        if ins.mnemonic in ("bl", "b") or ins.mnemonic.startswith("b") and ops.startswith("0x"):
            try:
                t = int(ops.split(",")[-1].strip(), 16)
                note = f"  ; {label(t)}"
            except ValueError:
                pass
        if ins.address != start and ins.address in syms:
            print(f"; ---- {syms[ins.address]}")
        print(f"{ins.address:08X}: {ins.mnemonic:8} {ops}{note}")
        if ins.mnemonic == "blr" and len(sys.argv) <= 3:
            pass


if __name__ == "__main__":
    main()
