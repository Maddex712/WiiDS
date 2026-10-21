"""Flag retargeted runtime hooks whose NSMBW function differs in size from the Mario Kart original.

A hook that reimplements a function's body is only safe when the NSMBW function does the same thing.
Size is a cheap proxy: equal size almost always means the same code; a large difference means a
different version that needs a manual look. MKW size = gap to the next MKW map entry (switch labels
skipped); NSMBW size = exact length from the Shield symbol table.
"""
import bisect
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import nsmbw_versions as ver  # noqa: E402

ROOT = Path(r"E:\NSMBWPort")
mkw = []
for line in (ROOT / "wiicompiled" / "projects" / "mkwii" / "MAP.txt").read_text().splitlines():
    a, n = line.split(None, 1)
    if not re.search(r"caseD_|_switch|switchdata", n):
        mkw.append((int(a, 16), n))
mkw.sort()
mkw_addrs = [a for a, _ in mkw]

nsm_len = {}
for row in (ROOT / "maps" / "nsmbw_hashes.txt").read_text().splitlines():
    c = [x.strip() for x in row.split("|")]
    if c[1] == "FUNCTION":
        e1 = ver.convert("C", "E1", int(c[0], 16))
        if e1 is not None:
            nsm_len[e1] = int(c[4].split()[1], 16)


def mkw_size(a):
    i = bisect.bisect_left(mkw_addrs, a)
    if i < len(mkw_addrs) and mkw_addrs[i] == a and i + 1 < len(mkw_addrs):
        return mkw_addrs[i + 1] - a
    return None


rows = [r for r in csv.DictReader((ROOT / "build-nsmbw" / "hooks.csv").open())
        if r["kind"] != "literal" and r["verdict"] == "resolved" and "fatal_stubs" not in r["file"]]
seen, flagged = set(), []
for r in rows:
    key = (r["mkw_addr"], r["nsmbw_addr"])
    if key in seen:
        continue
    seen.add(key)
    ms, ns = mkw_size(int(r["mkw_addr"], 16)), nsm_len.get(int(r["nsmbw_addr"], 16))
    if ms is None or ns is None:
        flagged.append((r, ms, ns, "size unknown"))
        continue
    if abs(ms - ns) > max(0x20, ms // 5):
        flagged.append((r, ms, ns, "size differs"))
print(f"retargeted hooks checked: {len(seen)}; flagged: {len(flagged)}")
for r, ms, ns, why in sorted(flagged, key=lambda f: f[0]["file"]):
    name = r["mkw_symbol"] or r["macro_symbol"]
    print(f"  {why:12} mkw 0x{ms or 0:04X} nsmbw 0x{ns or 0:04X}  {name[:44]:44} {r['file'].replace('src/hle/', '')}")
