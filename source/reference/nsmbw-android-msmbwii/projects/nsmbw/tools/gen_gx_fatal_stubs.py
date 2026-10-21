"""Regenerate runtime/src/hle/gx/gx_fatal_stubs.cpp for NSMBW (SMNE01 rev 1).

Mario Kart Wii's runtime splits its GX library three ways: native (Aurora) implementations, functions
it deliberately leaves translated (pure helpers), and "halt if called" stubs. This reproduces that
partition by name for NSMBW: every function in NSMBW's GX library range is stubbed unless the runtime
already registers a native at its NSMBW address, or MKW leaves the same-named function translated.
"""
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import nsmbw_versions as ver  # noqa: E402
import symhash  # noqa: E402

ROOT = Path(r"E:\NSMBWPort")
WC = ROOT / "wiicompiled"
RT = WC / "runtime"
STUBS = RT / "src" / "hle" / "gx" / "gx_fatal_stubs.cpp"
HOOK = re.compile(r"\b(?:PPC_NATIVE_OVERRIDE(?:_VOID)?|REGISTER_NATIVE_FUNCTION(?:_AS)?|REGISTER_TRANSLATED_FUNCTION|"
                  r"MKW_KNOWN_TYPED_NATIVE|MKW_KNOWN_NATIVE_CPU_CALL)\s*\(\s*(?:0x)?([0-9A-Fa-f]{8})")


def norm(n):
    """'GX::SetTevOrder' / '__GX::SaveFifo' -> SDK spelling."""
    m = re.fullmatch(r"(__)?GX::(__)?(\w+)", n)
    if m:
        return f"{'__' if (m.group(1) or m.group(2)) else ''}GX{m.group(3)}"
    return n


def is_label(n):
    return bool(re.search(r"caseD_|_switch|switchdata|LAB_", n))


# --- MKW partition -------------------------------------------------------------------------
mkw = {}
for line in (WC / "projects" / "mkwii" / "MAP.txt").read_text().splitlines():
    a, n = line.split(None, 1)
    mkw[int(a, 16)] = n
gx_mkw = {a: norm(n) for a, n in mkw.items() if re.match(r"(__)?GX(::|[A-Z_])", n) and not is_label(n)}
mkw_lo, mkw_hi = min(gx_mkw), max(gx_mkw)

git_show = __import__("subprocess").run(
    ["git", "-C", str(WC), "show", "279ce83:runtime/src/hle/gx/gx_fatal_stubs.cpp"],
    capture_output=True, text=True, check=True).stdout
mkw_stubbed = {int(a, 16) for a in re.findall(r"PPC_NATIVE_OVERRIDE_VOID\((\w{8}),", git_show)}
mkw_hooks = {int(r["mkw_addr"], 16) for r in csv.DictReader((ROOT / "build-nsmbw" / "hooks.csv").open())
             if r["kind"] != "literal"}
mkw_translated_names = {n for a, n in gx_mkw.items() if a not in mkw_stubbed and a not in mkw_hooks}

# --- NSMBW GX functions --------------------------------------------------------------------
funcs = {}
for row in (ROOT / "maps" / "nsmbw_hashes.txt").read_text().splitlines():
    c = [x.strip() for x in row.split("|")]
    if c[1] == "FUNCTION":
        e1 = ver.convert("C", "E1", int(c[0], 16))
        if e1 is not None:
            funcs[e1] = int(c[4].split()[1], 16)
names = {}
for line in (ROOT / "maps" / "nsmbw_e1_symbols.tsv").read_text(encoding="utf-8").splitlines():
    a, m, d = line.split("\t")
    names.setdefault(int(a, 16), m)
# Name uncracked GX functions by hashing every MKW GX name.
for n in set(gx_mkw.values()):
    for a, kind, _l in symhash.lookup(n):
        if kind == "FUNCTION":
            names.setdefault(a, n)
# The GX library is contiguous below 0x80200000; out-of-line inline copies (GXEnd in nw4r/EGG) live
# elsewhere and are native, so they must not stretch the range.
gx_named = [a for a, n in names.items() if a in funcs and re.match(r"(__)?GX[A-Z_]", n) and a < 0x80200000]
lo, hi = min(gx_named), max(gx_named)
lib = sorted(a for a in funcs if lo <= a <= hi)

# --- current NSMBW natives (outside the stub file, outside #if 0) ---------------------------
natives = set()
for f in list((RT / "src").rglob("*")) + list((RT / "include").rglob("*")):
    if f.suffix not in (".cpp", ".h", ".inc") or f == STUBS:
        continue
    depth = 0
    for line in f.read_text(errors="replace").splitlines():
        s = line.strip()
        if s.startswith("#if 0"):
            depth += 1
            continue
        if depth and s.startswith("#if"):
            depth += 1
        elif depth and s.startswith("#endif"):
            depth -= 1
            continue
        if depth:
            continue
        for m in HOOK.finditer(line):
            natives.add(int(m.group(1), 16))

# GX functions that touch only guest memory or do pure math, so running them translated is safe even
# though Mario Kart never exercised them: no GPU registers, no Aurora state.
SAFE_TRANSLATED = {"GXSetCurrentGXThread", "GXSetBreakPtCallback", "GXProject", "GXInitFogAdjTable"}

stubs, kept_translated, foreign = [], [], []
for a in lib:
    n = names.get(a)
    if a in natives:
        continue
    if n in SAFE_TRANSLATED:
        kept_translated.append((a, n))
        continue
    if n and not re.match(r"(__)?GX[A-Z_]", n):
        foreign.append((a, n))  # a non-GX function inside the range (e.g. libc helpers): leave it
        continue
    if n in mkw_translated_names:
        kept_translated.append((a, n))
        continue
    stubs.append((a, n or f"GX_unnamed_{a:08X}"))

# Keep everything before the first stub *invocation* (a line that starts with GX_FATAL_STUB(): the
# macro's own #define line also contains "GX_FATAL_STUB(" and must stay in the header.
header = re.split(r"(?m)^GX_FATAL_STUB\(", git_show, maxsplit=1)[0].rstrip() + "\n"
header = header.replace("// Auto-generated GX fatal stubs",
                        "// Auto-generated GX fatal stubs for NSMBW SMNE01 rev 1 (tools/gen_gx_fatal_stubs.py)")
body = "".join(
    f'GX_FATAL_STUB({a:08x}, "{n}_{a:08x}") PPC_NATIVE_OVERRIDE_VOID({a:08x}, gx_stub_{a:08x}, (CpuContext* ctx), (ctx));\n'
    for a, n in stubs)
STUBS.write_text(header + "\n" + body, encoding="utf-8")
print(f"NSMBW GX library 0x{lo:08X}-0x{hi:08X}: {len(lib)} functions")
print(f"  native (runtime): {sum(1 for a in lib if a in natives)}")
print(f"  left translated (MKW does the same): {len(kept_translated)}")
print(f"  non-GX functions inside the range: {len(foreign)}")
print(f"  fatal stubs written: {len(stubs)} ({sum(1 for _, n in stubs if n.startswith('GX_unnamed_'))} unnamed)")
print(f"MKW reference: {len(gx_mkw)} GX functions, {len(mkw_stubbed)} stubbed, {len(mkw_translated_names)} translated by name")
