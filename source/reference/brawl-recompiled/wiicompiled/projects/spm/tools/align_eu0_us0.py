"""Carry EU0 function names over to US0 by aligning the two functions' size sequences.
Usage: align_eu0_us0.py <spm-decomp/config dir> <out tsv>   (addr_us0 <tab> name <tab> size <tab> addr_eu0)"""
import re, sys, difflib
cfg, out = sys.argv[1], sys.argv[2]

def funcs(ver):
    r = []
    for line in open(f"{cfg}/{ver}/symbols.txt", encoding="utf-8", errors="replace"):
        m = re.match(r"(\S+)\s*=\s*\.text:0x([0-9A-Fa-f]{8});\s*//\s*(.*)", line)
        if m and "type:function" in m.group(3):
            sz = re.search(r"size:0x([0-9A-Fa-f]+)", m.group(3))
            r.append((int(m.group(2), 16), int(sz.group(1), 16) if sz else 0, m.group(1)))
    return sorted(r)

E, U = funcs("EU0"), funcs("US0")
sm = difflib.SequenceMatcher(None, [f[1] for f in E], [f[1] for f in U], autojunk=False)
rows, MIN = [], 4
blocks = [(a, b, n) for a, b, n in sm.get_matching_blocks() if n >= MIN]
for a, b, n in blocks:
    for k in range(n):
        e, u = E[a + k], U[b + k]
        rows.append((u[0], e[2], u[1], e[0], u[2]))
# Gap fill: between two anchors, if both sides hold the same number of functions, pair them in order
# (the SDK object is the same build, only a few sizes differ between regions). Unequal gaps stay unmapped.
filled = 0
prev = (0, 0, 0)
for a, b, n in blocks + [(len(E), len(U), 0)]:
    ge, gu = range(prev[0] + prev[2], a), range(prev[1] + prev[2], b)
    if len(ge) == len(gu) and len(ge) > 0:
        for i, j in zip(ge, gu):
            rows.append((U[j][0], E[i][2], U[j][1], E[i][0], U[j][2])); filled += 1
    prev = (a, b, n)
print("gap-filled", filled)
# Shift pass: an unmapped EU0 function whose mapped neighbours (before and after) share one address shift
# is placed at addr - shift when US0 has a function starting exactly there.
mapped = {r[3]: r for r in rows}
ustart = {u[0]: u for u in U}
order = sorted(mapped)
shifts = {a: mapped[a][3] - mapped[a][0] for a in order}
import bisect
extra = 0
for e in E:
    if e[0] in mapped:
        continue
    i = bisect.bisect_left(order, e[0])
    used = {r[0] for r in rows}
    for j in (i, i - 1):   # shift of the next anchor first, then the previous one
        if not 0 <= j < len(order):
            continue
        cand = e[0] - shifts[order[j]]
        u = ustart.get(cand)
        same_shift = 0 < i < len(order) and shifts[order[i - 1]] == shifts[order[i]]
        if u and cand not in used and (same_shift or u[1] == e[1]):  # differing shifts need an equal size
            rows.append((u[0], e[2], u[1], e[0], u[2])); extra += 1   # shift pass
            break
print("shift pass added", extra)
# Translator pass: US0 symbols skip stretches of the DOL, but the translator found every function. Between two
# mapped anchors, if EU0 and the translator's list hold the same number of unmapped functions, pair them in order.
import json, os
tj = os.path.join(os.path.dirname(out), "..", "..", "generated", "base_translation_output.json")
if os.path.exists(tj):
    allu = sorted((f["entryPoint"], f["size"]) for f in json.load(open(tj))["functions"] if f["entryPoint"] >= 0x80004000)
    done_e = {r[3] for r in rows}; done_u = {r[0] for r in rows}
    anchors = sorted(rows, key=lambda r: r[3])
    added = 0
    for p, n in zip(anchors, anchors[1:]):
        ge = [e for e in E if p[3] < e[0] < n[3] and e[0] not in done_e]
        gu = [u for u in allu if p[0] < u[0] < n[0] and u[0] not in done_u]
        if ge and len(ge) == len(gu):
            for e, u in zip(ge, gu):
                rows.append((u[0], e[2], u[1], e[0], "fn_%08X" % u[0])); added += 1   # translator pass
    print("translator pass added", added)
open(out, "w").write("".join("%08X\t%s\t%X\t%08X\t%s\n" % r for r in rows))
named = [r for r in rows if not re.match(r"(fn|lbl|zz)_", r[1])]
print(f"EU0 funcs {len(E)}, US0 funcs {len(U)}, aligned {len(rows)}, with real EU0 names {len(named)}")
