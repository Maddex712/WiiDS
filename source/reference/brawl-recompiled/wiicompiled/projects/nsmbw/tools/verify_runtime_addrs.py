"""After the remap: classify every guest address still active in the runtime against NSMBW E1.

Skips code inside "#if 0" blocks. Categories:
  func   - start of an NSMBW function (MAP.txt)
  data   - an NSMBW data symbol (cracked symbols / hash table NON-FUNCTION entries)
  inside - inside an NSMBW function or data object (offset given)
  ??     - unknown: leftover MKW address or NSMBW object not identified yet
Writes build-nsmbw/verify.csv and prints the ?? list.
"""
import bisect
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import nsmbw_versions as ver  # noqa: E402

ROOT = Path(r"E:\NSMBWPort")
RT = ROOT / "wiicompiled" / "runtime"
ADDR = re.compile(r"(?:0x|PPC_NATIVE_OVERRIDE(?:_VOID)?\s*\(\s*|GX_FATAL_STUB\(\s*)(80[0-9A-Fa-f]{6})")

starts = {int(l.split()[0], 16) for l in (ROOT / "wiicompiled" / "projects" / "nsmbw" / "MAP.txt").read_text().splitlines()}
names = {}
for l in (ROOT / "maps" / "nsmbw_e1_symbols.tsv").read_text(encoding="utf-8").splitlines():
    a, m, d = l.split("\t")
    names.setdefault(int(a, 16), d)
objects = {}  # addr -> (len, kind)
for row in (ROOT / "maps" / "nsmbw_hashes.txt").read_text().splitlines():
    c = [x.strip() for x in row.split("|")]
    e1 = ver.convert("C", "E1", int(c[0], 16))
    if e1 is not None:
        objects[e1] = (int(c[4].split()[1], 16), c[1])
obj_sorted = sorted(objects)


def classify(a):
    if a in starts:
        return "func", names.get(a, "")
    if a in objects or a in names:
        return "data", names.get(a, "")
    i = bisect.bisect_right(obj_sorted, a) - 1
    if i >= 0:
        base = obj_sorted[i]
        length, kind = objects[base]
        if a < base + length:
            return "inside", f"{names.get(base, kind.lower())}+0x{a - base:X}"
    return "??", ""


rows = []
for f in sorted(list((RT / "src").rglob("*")) + list((RT / "include").rglob("*"))):
    if f.suffix not in (".cpp", ".h", ".hpp", ".inc") or "gx_fatal_stubs" in f.name:
        continue
    disabled = 0
    for ln, line in enumerate(f.read_text(errors="replace").splitlines(), 1):
        s = line.strip()
        if s.startswith("#if 0"):
            disabled += 1
            continue
        if disabled and s.startswith("#if"):
            disabled += 1
        elif disabled and s.startswith("#endif"):
            disabled -= 1
            continue
        if disabled:
            continue
        for m in ADDR.finditer(line):
            a = int(m.group(1), 16)
            if a < 0x80004000:
                continue
            cat, what = classify(a)
            rows.append(dict(file=str(f.relative_to(RT)).replace("\\", "/"), line=ln, addr=f"0x{a:08X}",
                             cat=cat, what=what, code=s[:120]))

with (ROOT / "build-nsmbw" / "verify.csv").open("w", newline="", encoding="utf-8") as fh:
    w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)
from collections import Counter  # noqa: E402
print("active address sites by category:", dict(Counter(r["cat"] for r in rows)))
unknown = [r for r in rows if r["cat"] == "??"]
print(f"distinct unknown addresses: {len({r['addr'] for r in unknown})}")
