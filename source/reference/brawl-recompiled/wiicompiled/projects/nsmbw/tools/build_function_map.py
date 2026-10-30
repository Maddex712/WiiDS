"""Build WiiCompiled's function map (MAP.txt) for NSMBW SMNE01 rev 1.

Function starts = entry + direct-call targets + data pointers into code + code-materialized
(lis/addi|ori) pointers into code + REL prolog/epilog/unresolved + exec-section starts.
Names come from NSMBW-Decomp syms.txt and Newer's kamek_pal.x (both PAL v1), converted to
E1 addresses with NSMBW-Updated's address-map.txt.
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(r"E:\NSMBWPort")
GAME = ROOT / "game"
BASE = 0x80000000
OUT = ROOT / "wiicompiled" / "projects" / "nsmbw" / "MAP.txt"

flat = (GAME / "nsmbw_flat.bin").read_bytes()
ranges = [l.split() for l in (GAME / "ranges.txt").read_text().splitlines()]
exec_ranges = [(int(s, 16), int(e, 16)) for k, s, e, _ in ranges if k == "exec"]


def in_exec(a):
    return (a & 3) == 0 and any(s <= a < e for s, e in exec_ranges)


def word(a):
    return struct.unpack_from(">I", flat, a - BASE)[0]


# ---------------------------------------------------------------- P1 -> E1 address mapper
def load_e1_mapper():
    blocks, cur = {}, None
    for raw in (ROOT / "maps" / "address-map.txt").read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        m = re.fullmatch(r"\[(\w+)\]", line)
        if m:
            cur = blocks.setdefault(m.group(1), {"extend": None, "ranges": []})
            continue
        if cur is None:
            continue
        if line.startswith("extend"):
            cur["extend"] = line.split()[1]
            continue
        m = re.fullmatch(r"([0-9a-fA-F]+)-([0-9a-fA-F]+|\*)\s*:\s*([+-])0x([0-9a-fA-F]+)", line)
        if m:
            start = int(m.group(1), 16)
            end = 0xFFFFFFFF if m.group(2) == "*" else int(m.group(2), 16)
            delta = int(m.group(4), 16) * (1 if m.group(3) == "+" else -1)
            cur["ranges"].append((start, end, delta))
    e1 = blocks["E1"]
    assert e1["extend"] == "P1"

    def to_e1(p1):
        for start, end, delta in e1["ranges"]:
            if start <= p1 <= end:
                return (p1 + delta) & 0xFFFFFFFF
        return None  # removed / unmapped in E1
    return to_e1


to_e1 = load_e1_mapper()

# ---------------------------------------------------------------- names
names = {}
dropped = 0
THUNK = re.compile(r"^_(save|rest)(gpr|fpr)_(\d+)$")
THUNK_RENAMED = re.compile(r"^_(save|rest)_(gpr|fpr)_\d+$")


def add_name(p1, name):
    global dropped
    e1 = to_e1(p1)
    if e1 is None:
        dropped += 1
        return
    m = THUNK.match(name)
    if m:  # translator expects _save_gpr_N / _rest_fpr_N ...; these beat aliases like __save_gpr
        names[e1] = f"_{m.group(1)}_{m.group(2)}_{m.group(3)}"
        return
    if not THUNK_RENAMED.match(names.get(e1, "")):
        names.setdefault(e1, name)


for line in (ROOT / "nsmbw-decomp" / "syms.txt").read_text().splitlines():
    if "=" in line:
        n, a = line.strip().split("=")
        add_name(int(a, 16), n.strip())
# RootCubed's cracked symbols, already converted to E1 by resolve_sdk_symbols.py (mangled names).
for line in (ROOT / "maps" / "nsmbw_e1_symbols.tsv").read_text(encoding="utf-8").splitlines():
    e1, mangled, _dem = line.split("\t")
    a = int(e1, 16)
    m = THUNK.match(mangled)
    if m:
        names[a] = f"_{m.group(1)}_{m.group(2)}_{m.group(3)}"
    elif not THUNK_RENAMED.match(names.get(a, "")):
        names.setdefault(a, mangled)
for m in re.finditer(r"(\S+)\s*=\s*0x([0-9A-Fa-f]{8})\s*;", (ROOT / "maps" / "kamek_pal.x").read_text()):
    add_name(int(m.group(2), 16), m.group(1))

# Exact main.dol function starts and lengths from the Shield build's symbol table (hashes.txt),
# converted C -> E1. Authoritative for the DOL; the RELs are not covered by it.
sys.path.insert(0, str(Path(__file__).parent))
import nsmbw_versions as ver  # noqa: E402
dol_funcs = {}
dol_objects = {}  # every symbol-table object (functions and data) in main.dol: start -> length
table = []        # (c_addr, kind, length, e1 or None) in C order
for row in (ROOT / "maps" / "nsmbw_hashes.txt").read_text().splitlines():
    cols = [c.strip() for c in row.split("|")]
    c_addr = int(cols[0], 16)
    table.append((c_addr, cols[1], int(cols[4].split()[1], 16), ver.convert("C", "E1", c_addr)))
table.sort()
# A few C addresses have no unique preimage through the address-map chain. Recover them when the
# nearest converted neighbours on both sides shift by the same delta (the code in between did not move).
recovered = 0
for i, (c_addr, kind, length, e1) in enumerate(table):
    if e1 is not None:
        continue
    prev = next((t for t in reversed(table[:i]) if t[3] is not None), None)
    nxt = next((t for t in table[i + 1:] if t[3] is not None), None)
    if prev and nxt and prev[3] - prev[0] == nxt[3] - nxt[0]:
        table[i] = (c_addr, kind, length, (c_addr + prev[3] - prev[0]) & 0xFFFFFFFF)
        recovered += 1
for c_addr, kind, length, e1 in table:
    if e1 is None:
        continue
    dol_objects[e1] = max(length, 1)
    if kind == "FUNCTION":
        dol_funcs[e1] = length
print(f"symbol-table entries recovered by neighbour delta: {recovered}; still unconvertible: "
      f"{sum(1 for t in table if t[3] is None)}")
dol_sorted = sorted(dol_funcs)
obj_sorted = sorted(dol_objects)


def inside_known_dol_body(a):
    from bisect import bisect_right
    i = bisect_right(dol_sorted, a) - 1
    return i >= 0 and dol_sorted[i] < a < dol_sorted[i] + dol_funcs[dol_sorted[i]]


def covered_by_dol_object(a):
    """Inside any symbol-table object: a function body (other than its start) or a data object."""
    from bisect import bisect_right
    i = bisect_right(obj_sorted, a) - 1
    if i < 0:
        return False
    start = obj_sorted[i]
    if a >= start + dol_objects[start]:
        return False
    return not (a == start and start in dol_funcs)


TERMINATOR_WORDS = {0x4E800020, 0x4C000064, 0x4E800420}  # blr, rfi, bctr


def plausible_gap_start(a):
    """A heuristic start in a gap no symbol covers (E1-only code): must follow a terminator and
    must not be text (strings such as MetroTRK's messages sit right after functions in .init)."""
    cur = word(a)
    p = a - 4
    while word(p) == 0 and a - p < 0x40:  # functions are 16-byte aligned; skip the zero padding
        p -= 4
    prev = word(p)
    after_terminator = prev in TERMINATOR_WORDS or ((prev >> 26) == 18 and (prev & 1) == 0)
    looks_ascii = all(0x20 <= b <= 0x7E for b in cur.to_bytes(4, "big"))
    return after_terminator and not looks_ascii

def padded_start(a):
    """Follows zero padding that follows a terminator: nothing can fall through into it."""
    if word(a - 4) != 0:
        return False
    p = a - 4
    while word(p) == 0 and a - p < 0x40:
        p -= 4
    prev = word(p)
    return prev in TERMINATOR_WORDS or ((prev >> 26) == 18 and (prev & 1) == 0)

# ---------------------------------------------------------------- function starts
starts = {0x80004050}
sources = {"entry": 1}


HEURISTIC = {"data pointer", "lis/addi pointer", "lis/ori pointer", "named symbol"}


def add(a, why):
    if not in_exec(a) or word(a) == 0:
        return
    # In main.dol the symbol table gives exact bodies: a heuristic hit inside one is a switch label
    # or similar, not a function. Direct-call targets are kept regardless (save/restore thunk entries
    # and E1-only functions), as are the CodeWarrior thunk names.
    if why in HEURISTIC and a < 0x80700000 and a not in dol_funcs and not THUNK_RENAMED.match(names.get(a, "")):
        # Data tables (_rom_copy_info, _bss_init_info) and strings live inside .init/.text, so a
        # pointer to them is not a function start.
        # Zero padding right after a terminator is a function boundary even where a converted
        # symbol's length runs over it (dCourseSelectGuide_c::finalizeState_ScrollGuideOnStageWait
        # has no E1 symbol, and the previous body's length covers it).
        if covered_by_dol_object(a) and not padded_start(a):
            sources["dropped inside DOL object"] = sources.get("dropped inside DOL object", 0) + 1
            return
        if not plausible_gap_start(a):
            sources["dropped implausible DOL gap"] = sources.get("dropped implausible DOL gap", 0) + 1
            return
    if a not in starts:
        sources[why] = sources.get(why, 0) + 1
    starts.add(a)


for s, _e in exec_ranges:
    add(s, "section start")
for a in dol_funcs:
    add(a, "DOL symbol table")
for t in (GAME / "seeds_bl.txt").read_text().split():
    add(int(t, 16), "bl target")
for line in (GAME / "data_ptrs.txt").read_text().splitlines():
    add(int(line.split()[1], 16), "data pointer")

# lis rX,hi followed (within 8 insns) by addi/ori rY,rX,lo forming a code address
for s, e in exec_ranges:
    for a in range(s, e, 4):
        w = word(a)
        if (w >> 26) != 15 or ((w >> 16) & 31) != 0:  # lis = addis rD,0,imm
            continue
        rd, hi = (w >> 21) & 31, w & 0xFFFF
        for b in range(a + 4, min(a + 36, e), 4):
            x = word(b)
            op, rs = x >> 26, (x >> 16) & 31
            if op == 14 and rs == rd:  # addi
                lo = x & 0xFFFF
                add(((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF, "lis/addi pointer")
                break
            if op == 24 and ((x >> 21) & 31) == rd:  # ori rA,rS,imm (rS is bits 21-25)
                add((hi << 16) | (x & 0xFFFF), "lis/ori pointer")
                break

layout = json.loads((GAME / "nsmbw_rel_layout.json").read_text())
for m in layout["modules"]:
    for k in ("prolog", "epilog", "unresolved"):
        add(int(m[k], 16), "rel entry")
        names.setdefault(int(m[k], 16), f"{m['name']}_{k}")

for a in names:
    add(a, "named symbol")

lines = [f"{a:08x} {names.get(a, f'0x{a:08x}')}\n" for a in sorted(starts)]
OUT.write_text("".join(lines))
named = sum(1 for a in starts if a in names)
print(f"function starts: {len(starts)}  named: {named}  (symbols dropped as unmapped in E1: {dropped})")
for k, v in sources.items():
    print(f"  {k}: +{v}")
thunks = sorted((a, n) for a, n in names.items() if THUNK.match(n.replace('_gpr_', 'gpr_').replace('_fpr_', 'fpr_')) or n.startswith(('_save_', '_rest_')))
print(f"save/restore thunk names: {len(thunks)}  e.g. {thunks[:2]}")
print(f"wrote {OUT}")
