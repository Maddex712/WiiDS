"""Make wiicompiled/pal/runtime: a copy of wiicompiled/runtime with every Super Paper Mario USA guest address
retargeted to the PAL dol (R8PP01) through projects/spm/pal/addr_map.tsv (gen_addr_map.py), the two REL layouts
(game/spm_rel_layout.json, game_pal/spm_rel_layout.json) and the heap shift between them.
Usage: make_pal_runtime.py <repo root>   -> wiicompiled/pal/runtime, wiicompiled/pal/remap_log.csv (review the 'unsure' rows)"""
import bisect, csv, json, re, shutil, sys
from collections import Counter
from pathlib import Path

root = Path(sys.argv[1])
wc = root / "wiicompiled"
src, dst = wc / "runtime", wc / "pal/runtime"
US_SDA, EU_SDA = 0x80574F20, 0x805B5F00  # r13 of each dol (__init_registers)
US_DOL_END, EU_DOL_END = 0x80576720, 0x805B7740  # bss end rounded up: heap 0 starts here (memInit)

rows = [l.split("\t") for l in (wc / "projects/spm/pal/addr_map.tsv").read_text().splitlines() if not l.startswith("#")]
starts = [int(r[0], 16) for r in rows]
eus = [int(r[1], 16) for r in rows]
sizes = [int(r[2], 16) for r in rows]
names = [r[3] for r in rows]
us_rel = json.load(open(root / "game/spm_rel_layout.json"))
eu_rel = json.load(open(root / "game_pal/spm_rel_layout.json"))
HEAP_SHIFT = EU_DOL_END - US_DOL_END  # heap 0 starts at the arena lo the runtime seeds (system_bridge.cpp)
# Runtime-seeded memory layout values: MEM1 arena lo (bss end, 0x20-aligned) and arena hi (MEM1 end minus the disc's
# 32-aligned fst.bin: PAL 69724 bytes).
DATA_OVERRIDES = {0x80576720: 0x805B7740, 0x817EF720: 0x817EEFA0,
                  0x81700000: 0x81700000}  # main.cpp's boot stack seed is a fixed MEM1 address, not a heap object
us_rel_secs = sorted((int(s["addr"], 16), int(s["size"], 16), int(eu_rel["sections"][k]["addr"], 16)) for k, s in us_rel["sections"].items())
US_REL = (int(us_rel["image"], 16), int(us_rel["end"], 16))


def map_addr(a):
    """-> (eu address, how). how: symbol (inside a paired symbol), shift (both neighbours agree), rel (REL section),
    heap (past the dol: heap shift), unsure (neighbours disagree: previous shift), none (outside the game)."""
    if a in DATA_OVERRIDES:
        return DATA_OVERRIDES[a], "override"
    if US_REL[0] <= a < US_REL[1]:
        for us_addr, size, eu_addr in us_rel_secs:
            if us_addr <= a < us_addr + size:
                return eu_addr + (a - us_addr), "rel"
        return int(eu_rel["image"], 16) + (a - US_REL[0]), "rel"  # the header before the first section (the image itself)
    if a >= US_DOL_END:
        return a + HEAP_SHIFT, "heap"
    i = bisect.bisect_right(starts, a) - 1
    if i < 0:
        return a, "none"
    if a < starts[i] + max(sizes[i], 4):
        return eus[i] + (a - starts[i]), "symbol"
    prev = eus[i] - starts[i]
    if i + 1 < len(starts) and eus[i + 1] - starts[i + 1] == prev:
        return a + prev, "shift"
    return a + prev, "unsure"


# Code addresses are checked against both dols: the first 12 instructions must match with branch and immediate
# fields masked; on a mismatch every shift the map knows is tried.
import struct
def dol(path):
    b = open(path, "rb").read()
    h = struct.unpack(">18I18I18IIII", b[:0xE4])
    secs = [(h[18 + i], h[36 + i], h[i]) for i in range(18) if h[36 + i]]
    def read(a, n):
        for addr, size, off in secs:
            if addr <= a < addr + size:
                return b[off + a - addr: off + a - addr + n]
        return b""
    return read
US_READ, EU_READ = dol(root / "game/main.dol.orig"), dol(root / "game_pal/main.dol.orig")
US_TEXT_END = 0x80319180
def skeleton(read, a):
    w = struct.unpack(">12I", read(a, 48)) if len(read(a, 48)) == 48 else ()
    return tuple(x & 0xFC000000 if (x >> 26) in (18, 16) else x & 0xFFFF0000 if (x >> 26) in (14, 15, 24, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55) else x for x in w)
# Hand-checked from the MISMATCH rows of remap_log.csv: us -> eu, or None when the PAL dol has no such function
# (its hook line is then wrapped in #if 0). DCStoreRange (dcbst loop) is dead-stripped from the PAL dol.
OVERRIDES = {0x8026D830: None,
             # SDK functions the USA symbol table leaves unnamed (hand-verified in the USA runtime), named in EU0.
             0x802CC950: 0x802CF628, 0x802CE684: 0x802D0E70, 0x802CE4C4: 0x802D0C50}  # WPADInit, WPADControlMotor, WPADGetInfoAsync
def verify_code(a, b, how):
    if a < 0x8000A800:  # .init and the mod loader's reserved window are laid out alike in both dols
        return a, "init"
    if a in OVERRIDES:
        return OVERRIDES[a] or a, "override" if OVERRIDES[a] else "drop"
    if not 0x80004000 <= a < US_TEXT_END or not skeleton(US_READ, a):
        return b, how
    if skeleton(US_READ, a) == skeleton(EU_READ, b) or skeleton(US_READ, a)[:5] == skeleton(EU_READ, b)[:5]:  # 5: tiny functions
        return b, how + "+code"
    # Inside the big unnamed fn_ blobs the PAL code gains or loses a few instructions: find the skeleton nearby.
    hits = [c for c in range(b - 0x2000, b + 0x2000, 4) if skeleton(EU_READ, c) == skeleton(US_READ, a)]
    return (hits[0], how + "+recode") if len(hits) == 1 else (b, how + "+MISMATCH" + (f"({len(hits)} candidates)" if hits else ""))

# 0x80xxxxxx literals, bare macro arguments "(80xxxxxx", and address-suffixed identifiers like GX__PokeAlphaMode_8028ce58
# (the macros paste the argument into the name, so both spellings must move together).
LIT = re.compile(r"(?<![0-9A-Za-z_])(0x)([0-9A-Fa-f]{8})(?![0-9A-Fa-f])|(\(\s*)(80[0-9A-Fa-f]{6})(?![0-9A-Za-z_])|(_)(80[0-9A-Fa-f]{6})(?![0-9A-Fa-f])")
SDA = re.compile(r"(r13 - 0x|Sda1Offset = 0x|OffsetFromR13 = 0x)([0-9A-Fa-f]{4})(u?)")
TEXT = [('kGameId = "R8PE01"', 'kGameId = "R8PP01"'), ("kRevision = 0;", "kRevision = -1;"),
        ("Super Paper Mario (USA, R8PE01, revision 0)", "Super Paper Mario (Europe, R8PP01)"),
        ("0x52385045", "0x52385050")]  # game code R8PE -> R8PP (low-memory seeds and the title-id fallbacks)
log, stats = [], Counter()
if dst.exists():
    shutil.rmtree(dst)
shutil.copytree(src, dst, ignore=shutil.ignore_patterns("build", "*.o", "*.obj"))
for path in sorted(dst.rglob("*")):
    if path.suffix not in (".cpp", ".h", ".hpp", ".inc") or "third_party" in path.parts:
        continue
    rel = str(path.relative_to(dst)).replace("\\", "/")
    text = path.read_text(encoding="utf-8", errors="surrogateescape")
    lines = text.split("\n")
    changed = False
    for i, line in enumerate(lines):
        def lit(m):
            hexpart = m.group(2) or m.group(4) or m.group(6)
            a = int(hexpart, 16)
            if not 0x80004000 <= a < 0x81800000 or a == 0x80808081:  # 0x80808081: gx_dl colour constant
                return m.group(0)
            b, how = map_addr(a)
            b, how = verify_code(a, b, how)
            if not m.group(6):  # identifier suffixes follow their macro argument; not logged twice
                stats[how] += 1
                log.append((rel, i + 1, f"{a:08X}", f"{b:08X}", how, names[bisect.bisect_right(starts, a) - 1] if how in ("symbol", "shift", "unsure") else ""))
            if how in ("none", "drop"):
                return m.group(0)
            new = f"{b:08x}" if hexpart.islower() and any(c.isalpha() for c in hexpart) else f"{b:08X}"
            return (m.group(1) or m.group(3) or m.group(5)) + new

        def sda(m):
            off = int(m.group(2), 16)
            b, how = map_addr(US_SDA - off)
            new = EU_SDA - b
            stats["sda-" + how] += 1
            log.append((rel, i + 1, f"r13-{off:04X}", f"r13-{new:04X}", "sda:" + how, ""))
            return f"{m.group(1)}{new:04X}{m.group(3)}" if m.group(2).isupper() else f"{m.group(1)}{new:04x}{m.group(3)}"

        new = LIT.sub(lit, line)
        new = SDA.sub(sda, new)
        for old, rep in TEXT:
            if old in new:
                new = new.replace(old, rep)
                stats["text"] += 1
        if any(r[0] == rel and r[1] == i + 1 and r[4] == "drop" for r in log[-8:]) and not line.lstrip().startswith(("//", "#")):
            new = "#if 0  // PAL: no such function in the PAL dol\n" + new + "\n#endif"
        if new != line:
            lines[i] = new
            changed = True
    if changed:
        path.write_text("\n".join(lines), encoding="utf-8", errors="surrogateescape", newline="\n")
with open(wc / "pal/remap_log.csv", "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["file", "line", "us", "eu", "how", "symbol"])
    w.writerows(log)
print(dict(stats), f"heap shift {HEAP_SHIFT:#x}")
for r in log:
    if r[4].startswith(("unsure", "none", "sda:unsure")) or "MISMATCH" in r[4] or "recode" in r[4]:
        print("  ", *r)
