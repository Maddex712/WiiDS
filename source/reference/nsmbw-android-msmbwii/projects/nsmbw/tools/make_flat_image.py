"""Build a flat MEM1 image (DOL + pre-linked RELs) plus seed/range files for the Ghidra pass.

Outputs (in E:\\NSMBWPort\\game):
  nsmbw_flat.bin    0x01800000 bytes, guest address 0x80000000 at offset 0
  ranges.txt        "exec|data start end name" per line (end exclusive)
  seeds_bl.txt      direct-call (bl) targets, one hex address per line
  data_ptrs.txt     aligned words in data ranges that point into executable ranges
"""
import json
import struct
from pathlib import Path

GAME = Path(r"E:\NSMBWPort\game")
BASE = 0x80000000
SIZE = 0x01800000

flat = bytearray(SIZE)
ranges = []  # (kind, start, end, name)

dol = (GAME / "main.dol").read_bytes()
offs = struct.unpack(">18I", dol[0x00:0x48])
addrs = struct.unpack(">18I", dol[0x48:0x90])
sizes = struct.unpack(">18I", dol[0x90:0xD8])
for i in range(18):
    if not sizes[i]:
        continue
    a, o, s = addrs[i], offs[i], sizes[i]
    flat[a - BASE:a - BASE + s] = dol[o:o + s]
    ranges.append(("exec" if i < 7 else "data", a, a + s, f"dol_{'text' if i < 7 else 'data'}{i if i < 7 else i - 7}"))

layout = json.loads((GAME / "nsmbw_rel_layout.json").read_text())
rel_base = int(layout["image_start"], 16)
rel = (GAME / "nsmbw_linked.rel").read_bytes()
# The synthetic REL's first bytes are its own header; restore d_profileNP's real header bytes are
# irrelevant to analysis, so just copy the linked image as-is.
flat[rel_base - BASE:rel_base - BASE + len(rel)] = rel
for m in layout["modules"]:
    for idx, sec in m["sections"].items():
        start, size = int(sec["addr"], 16), int(sec["size"], 16)
        kind = "exec" if sec["exec"] else ("bss" if start == int(m["bss"], 16) else "data")
        ranges.append((kind, start, start + size, f"{m['name']}_s{idx}"))

ranges.sort(key=lambda r: r[1])
exec_ranges = [(s, e) for k, s, e, _ in ranges if k == "exec"]
data_ranges = [(s, e) for k, s, e, _ in ranges if k == "data"]


def in_exec(a):
    return (a & 3) == 0 and any(s <= a < e for s, e in exec_ranges)


def word(a):
    return struct.unpack_from(">I", flat, a - BASE)[0]


seeds = set()
for s, e in exec_ranges:
    for a in range(s, e, 4):
        w = word(a)
        if (w >> 26) == 18 and (w & 3) == 1:  # bl (relative, link)
            disp = w & 0x03FFFFFC
            if disp & 0x02000000:
                disp -= 0x04000000
            t = (a + disp) & 0xFFFFFFFF
            if t != a + 4 and in_exec(t):  # skip "bl next" PC-materialization idiom
                seeds.add(t)

ptrs = set()
for s, e in data_ranges:
    for a in range((s + 3) & ~3, e - 3, 4):
        v = word(a)
        if in_exec(v):
            ptrs.add((a, v))

(GAME / "nsmbw_flat.bin").write_bytes(flat)
(GAME / "ranges.txt").write_text("".join(f"{k} {s:08X} {e:08X} {n}\n" for k, s, e, n in ranges))
(GAME / "seeds_bl.txt").write_text("".join(f"{t:08X}\n" for t in sorted(seeds)))
(GAME / "data_ptrs.txt").write_text("".join(f"{a:08X} {v:08X}\n" for a, v in sorted(ptrs)))
print(f"exec ranges: {len(exec_ranges)}  data ranges: {len(data_ranges)}")
print(f"bl targets: {len(seeds)}  data pointers into code: {len(ptrs)} ({len({v for _, v in ptrs})} distinct targets)")
