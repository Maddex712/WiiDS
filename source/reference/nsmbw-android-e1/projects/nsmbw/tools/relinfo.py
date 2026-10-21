"""Dump REL headers/sections/imports and check a proposed fixed load layout for consistency."""
import struct
import sys
from pathlib import Path

GAME = Path(r"E:\NSMBWPort\game")
ORDER = ["d_profileNP", "d_basesNP", "d_enemiesNP", "d_en_bossNP"]
# .text start addresses for SMNE01 rev 1 (PAL v1 bases from NSMBW-Decomp slices, shifted per the E1 address map)
TEXT_ADDR = {"d_profileNP": 0x807685A0, "d_basesNP": 0x8076D770, "d_enemiesNP": 0x809A2DB0, "d_en_bossNP": 0x80B1CA30}


def parse(name):
    b = (GAME / f"{name}.rel").read_bytes()
    (mid, _nxt, _prv, nsec, secoff, _nameoff, _namesz, ver, bss, reloff, impoff, impsz,
     prolog_s, epilog_s, unres_s, bss_s, prolog, epilog, unres) = struct.unpack(">IIIIIIIIIIIIBBBBIII", b[:0x40])
    align, bss_align, fix = struct.unpack(">III", b[0x40:0x4C]) if ver >= 3 else (0, 0, 0)
    secs = []
    for i in range(nsec):
        off, size = struct.unpack(">II", b[secoff + i * 8: secoff + i * 8 + 8])
        secs.append((i, off & ~1, bool(off & 1), size))
    imps = []
    for i in range(impsz // 8):
        imid, ioff = struct.unpack(">II", b[impoff + i * 8: impoff + i * 8 + 8])
        imps.append((imid, ioff))
    return dict(name=name, id=mid, ver=ver, size=len(b), nsec=nsec, bss=bss, reloff=reloff, impoff=impoff,
                impsz=impsz, prolog=(prolog_s, prolog), epilog=(epilog_s, epilog), unres=(unres_s, unres),
                align=align, bss_align=bss_align, fix=fix, secs=secs, imps=imps)


mods = [parse(n) for n in ORDER]
prev_end = None
for m in mods:
    print(f"== {m['name']} id={m['id']} ver={m['ver']} file=0x{m['size']:X} fixSize=0x{m['fix']:X} "
          f"align={m['align']} bssAlign={m['bss_align']} bss=0x{m['bss']:X} relOff=0x{m['reloff']:X} impOff=0x{m['impoff']:X}")
    print(f"   prolog={m['prolog'][0]}:0x{m['prolog'][1]:X} epilog={m['epilog'][0]}:0x{m['epilog'][1]:X} unresolved={m['unres'][0]}:0x{m['unres'][1]:X}")
    for i, off, exe, size in m["secs"]:
        if size:
            print(f"   sec{i}: off=0x{off:X} size=0x{size:X}{' exec' if exe else ''}{' (bss)' if off == 0 and i else ''}")
    print("   imports:", ", ".join(f"mod{i}@0x{o:X}" for i, o in m["imps"]))
    text = next(s for s in m["secs"] if s[2])
    file_base = TEXT_ADDR[m["name"]] - text[1]
    print(f"   => file image at 0x{file_base:08X}, text at 0x{TEXT_ADDR[m['name']]:08X}, fix end 0x{file_base + m['fix']:08X}")
    if prev_end is not None:
        print(f"   gap from previous module's fixSize end: 0x{file_base - prev_end:X}")
    prev_end = file_base + m["fix"]
