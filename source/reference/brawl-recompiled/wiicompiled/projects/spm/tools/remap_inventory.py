"""Inventory the runtime's NSMBW guest addresses and map each to Super Paper Mario (US0) by symbol name.
Usage: remap_inventory.py <repo root>   -> projects/spm/runtime_remap.csv"""
import csv, re, sys
from collections import defaultdict
from pathlib import Path
root = Path(sys.argv[1])

def load_map(p):
    by_addr = {}
    for l in p.read_text().splitlines():
        a = l.split(None, 1)
        if len(a) == 2 and not a[1].startswith("0x"):
            by_addr[int(a[0], 16)] = a[1].strip()
    return by_addr
nsmbw = load_map(root / "projects/nsmbw/MAP.txt")
spm_by_name = defaultdict(list)
for l in (root / "projects/spm/us0_names.tsv").read_text().splitlines():
    us, eu_name, _sz, _eu, us_name = l.split("\t")
    spm_by_name[eu_name].append(int(us, 16))
    if us_name != eu_name and not re.match(r"(fn|lbl|zz)_", us_name):
        spm_by_name[us_name].append(int(us, 16))

for l in open(root.parent / "spm-decomp/config/US0/symbols.txt", encoding="utf-8", errors="replace"):  # own_names
    m = re.match(r"(\S+)\s*=\s*\.(text|init):0x([0-9A-Fa-f]{8});\s*//\s*(.*)", l)
    if m and "type:function" in m.group(4) and not re.match(r"(fn|lbl|zz)_", m.group(1)):
        spm_by_name[m.group(1)].append(int(m.group(3), 16))

uses = defaultdict(set)
for f in (root / "runtime").rglob("*"):
    if f.suffix in (".cpp", ".h", ".hpp", ".inc") and "gx_fatal_stubs" not in f.name:
        for m in re.finditer(r"0x(80[0-9A-Fa-f]{6})|\(\s*(80[0-9A-Fa-f]{6})\b", f.read_text(errors="replace")):
            a = int(m.group(1) or m.group(2), 16)
            if a >= 0x80004000:
                uses[a].add(str(f.relative_to(root / "runtime")).replace("\\", "/"))
stat, rows = defaultdict(int), []
for a in sorted(uses):
    name = nsmbw.get(a)
    if name is None: st, tgt = "not-a-function-start", ""
    else:
        c = sorted(set(spm_by_name.get(name, [])))
        st, tgt = ("mapped", c[0]) if len(c) == 1 else ("ambiguous", c) if c else ("name-not-in-spm", "")
    stat[st] += 1
    rows.append([f"0x{a:08X}", name or "", tgt if isinstance(tgt, str) else (f"0x{tgt:08X}" if isinstance(tgt, int) else " ".join(f"0x{x:08X}" for x in tgt)), st, " ".join(sorted(uses[a]))])
with open(root / "projects/spm/runtime_remap.csv", "w", newline="") as fh:
    w = csv.writer(fh); w.writerow(["nsmbw_addr", "name", "spm_us0_addr", "status", "files"]); w.writerows(rows)
print(len(rows), "distinct guest addresses;", dict(stat))
