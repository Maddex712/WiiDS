"""Inventory WiiCompiled runtime addresses that are Mario Kart Wii specific and try to map them to NSMBW.

For every 0x80xxxxxx literal >= 0x80004000 in runtime/src, look up its MKW symbol in
projects/mkwii/MAP.txt and the same symbol in the NSMBW map (projects/nsmbw/MAP.txt).
Writes build-nsmbw/runtime_remap.csv and prints a summary.
"""
import csv
import re
from collections import defaultdict
from pathlib import Path

ROOT = Path(r"E:\NSMBWPort")
WC = ROOT / "wiicompiled"


def load_map(path):
    by_addr, by_name = {}, {}
    for line in path.read_text().splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2 and not parts[1].startswith("0x"):
            a = int(parts[0], 16)
            by_addr[a] = parts[1]
            by_name.setdefault(parts[1], a)
    return by_addr, by_name


mkw_addr, _ = load_map(WC / "projects" / "mkwii" / "MAP.txt")
_, nsmbw_name = load_map(WC / "projects" / "nsmbw" / "MAP.txt")
mkw_sorted = sorted(mkw_addr)

uses = defaultdict(set)
for f in (WC / "runtime" / "src").rglob("*"):
    if f.suffix not in (".cpp", ".h", ".hpp", ".inc"):
        continue
    for m in re.finditer(r"0x(80[0-9A-Fa-f]{6})", f.read_text(errors="replace")):
        a = int(m.group(1), 16)
        if a >= 0x80004000:
            uses[a].add(str(f.relative_to(WC / "runtime" / "src")).replace("\\", "/"))


def containing(a):
    """Nearest MKW symbol at or below a (for data/mid-function literals)."""
    lo, hi = 0, len(mkw_sorted) - 1
    best = None
    while lo <= hi:
        mid = (lo + hi) // 2
        if mkw_sorted[mid] <= a:
            best = mkw_sorted[mid]
            lo = mid + 1
        else:
            hi = mid - 1
    return best


rows = []
stats = defaultdict(int)
for a in sorted(uses):
    name = mkw_addr.get(a)
    exact = name is not None
    if not exact:
        base = containing(a)
        name = f"{mkw_addr[base]}+0x{a - base:X}" if base is not None and a - base < 0x400 else ""
    sym = name.split("+")[0] if name else ""
    target = nsmbw_name.get(sym) if sym else None
    if target is not None and exact:
        status = "mapped"
    elif target is not None:
        status = "mapped-offset"  # needs verification: offset inside a function/object
    elif exact:
        status = "named-unmatched"
    else:
        status = "unnamed"
    stats[status] += 1
    rows.append([f"0x{a:08X}", name, f"0x{target:08X}" if target is not None else "", status,
                 " ".join(sorted(uses[a]))])

out = ROOT / "build-nsmbw" / "runtime_remap.csv"
with out.open("w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["mkw_addr", "mkw_symbol", "nsmbw_e1_addr", "status", "files"])
    w.writerows(rows)
print(f"{len(rows)} MKW-specific addresses -> {out}")
for k in ("mapped", "mapped-offset", "named-unmatched", "unnamed"):
    print(f"  {k}: {stats[k]}")
