"""Complete inventory of Mario Kart Wii addresses in the WiiCompiled runtime, resolved for NSMBW E1.

Covers every hook macro (PPC_NATIVE_OVERRIDE[_VOID], REGISTER_NATIVE_FUNCTION[_AS],
REGISTER_TRANSLATED_FUNCTION, MKW_KNOWN_TYPED_NATIVE, MKW_KNOWN_NATIVE_CPU_CALL) with bare or 0x
hex addresses, plus remaining 0x80xxxxxx literals (data addresses, InvokeIndirectCpu targets).

Writes build-nsmbw/hooks.csv.
"""
import csv
import re
from pathlib import Path

ROOT = Path(r"E:\NSMBWPort")
RT = ROOT / "wiicompiled" / "runtime"
HOOK = re.compile(
    r"\b(PPC_NATIVE_OVERRIDE(?:_VOID)?|REGISTER_NATIVE_FUNCTION(?:_AS)?|REGISTER_TRANSLATED_FUNCTION|"
    r"MKW_KNOWN_TYPED_NATIVE|MKW_KNOWN_NATIVE_CPU_CALL)\s*\(\s*(?:0x)?([0-9A-Fa-f]{8})u?\s*,\s*([A-Za-z_]\w*)")
LIT = re.compile(r"0x(80[0-9A-Fa-f]{6})")


def load_map(path):
    by_addr = {}
    for line in path.read_text().splitlines():
        p = line.split(None, 1)
        if len(p) == 2 and not p[1].startswith("0x"):
            by_addr[int(p[0], 16)] = p[1]
    return by_addr


mkw = load_map(ROOT / "wiicompiled" / "projects" / "mkwii" / "MAP.txt")
nsm_index = {}
for line in (ROOT / "maps" / "nsmbw_e1_symbols.tsv").read_text(encoding="utf-8").splitlines():
    a, m, d = line.split("\t")
    for k in {m, d, re.sub(r"\(.*$", "", d)}:
        nsm_index.setdefault(k, set()).add(int(a, 16))


def macro_symbol(sym):
    """'AIClockInit_801A1138' -> 'AIClockInit'; 'OS__SendMessage_HLE_801a735c' -> 'OSSendMessage'."""
    s = re.sub(r"_(?:HLE|hle|Hle|stub|Stub|native|Native)?_?[0-9A-Fa-f]{8}(?:_\w+)?$", "", sym)
    s = re.sub(r"_(HLE|hle|stub|native)$", "", s)
    s = re.sub(r"^OS____", "__OS", s)
    s = re.sub(r"^([A-Z]{2,4})__", r"\1", s)  # OS__SendMessage -> OSSendMessage
    return s


def mkw_symbol_variants(n):
    out = [n]
    m = re.fullmatch(r"(?:RVL::)?(OS|DVD|GX|VI|AI|AX|DSP|SC|NAND|PAD|SI|EXI|IPC|WPAD|KPAD|PPC|DC|IC|LC|THP)::(__)?(\w+)", n)
    if m:
        out += [f"{'__' if m.group(2) else ''}{m.group(1)}{m.group(3)}"]
    m = re.fullmatch(r"(?:RVL::)?(IOS|ISFS|ES|ESP)::(\w+)", n)
    if m:
        out.append(f"{m.group(1)}_{m.group(2)}")
    m = re.fullmatch(r"RVL::(\w+)", n)
    if m:
        out.append(m.group(1))
    m = re.fullmatch(r"__(OS|DVD|GX|VI|AI|AX|DSP|SC|NAND|SI|EXI|IPC|WPAD|KPAD)::(\w+)", n)  # __GX::SaveFifo
    if m:
        out.append(f"__{m.group(1)}{m.group(2)}")
    m = re.fullmatch(r"(NAND|WPAD|KPAD|OS|DVD)::(\w+)::(\w+)", n)  # NAND::Private::SafeOpenAsync
    if m:
        out.append(f"{m.group(1)}{m.group(2)}{m.group(3)}")
    return out


import sys  # noqa: E402
sys.path.insert(0, str(Path(__file__).parent))
import symhash  # noqa: E402


def resolve(names):
    for n in names:
        hits = nsm_index.get(n)
        if hits and len(hits) == 1:
            return next(iter(hits)), n
    # Not cracked yet: hash the guessed name against the Shield symbol table.
    for n in names:
        funcs = [a for a, kind, _l in symhash.lookup(n) if kind == "FUNCTION"]
        if len(funcs) == 1:
            return funcs[0], f"hash:{n}"
    return None, ""


def classify(names, mkw_name, macro):
    """Why an address did not resolve."""
    text = f"{mkw_name} {macro}"
    if re.search(r"caseD_|_switch\b|switchdata", text):
        return "case-label"
    if "::" in mkw_name and not re.match(r"(RVL::)?(OS|DVD|GX|__GX|VI|AI|AX|DSP|SC|NAND|PAD|SI|EXI|IPC|WPAD|KPAD|PPC|IOS|ISFS|ESP)::", mkw_name):
        return "c++-needs-mangled-name"
    if any(symhash.lookup(n) for n in names):
        return "ambiguous"
    return "absent-in-nsmbw" if names else "unnamed"


rows = []
for f in sorted(list((RT / "src").rglob("*")) + list((RT / "include").rglob("*"))):
    if f.suffix not in (".cpp", ".h", ".hpp", ".inc"):
        continue
    rel = str(f.relative_to(RT)).replace("\\", "/")
    for ln, line in enumerate(f.read_text(errors="replace").splitlines(), 1):
        hook_addrs = set()
        for m in HOOK.finditer(line):
            a = int(m.group(2), 16)
            if a < 0x80004000:
                continue
            hook_addrs.add(a)
            ms = macro_symbol(m.group(3))
            mk = mkw.get(a, "")
            names = [ms] + (mkw_symbol_variants(mk) if mk else [])
            nsm, via = resolve(names)
            rows.append(dict(file=rel, line=ln, kind=m.group(1), mkw_addr=f"0x{a:08X}", macro_symbol=ms,
                             mkw_symbol=mk, nsmbw_addr=f"0x{nsm:08X}" if nsm else "", via=via,
                             verdict="resolved" if nsm else classify(names, mk, ms)))
        for m in LIT.finditer(line):
            a = int(m.group(1), 16)
            if a < 0x80004000 or a in hook_addrs:
                continue
            mk = mkw.get(a, "")
            names = mkw_symbol_variants(mk) if mk else []
            nsm, via = resolve(names) if mk else (None, "")
            rows.append(dict(file=rel, line=ln, kind="literal", mkw_addr=f"0x{a:08X}", macro_symbol="",
                             mkw_symbol=mk, nsmbw_addr=f"0x{nsm:08X}" if nsm else "", via=via,
                             verdict="resolved" if nsm else classify(names, mk, "")))

with (ROOT / "build-nsmbw" / "hooks.csv").open("w", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)

hooks = [r for r in rows if r["kind"] != "literal"]
lits = [r for r in rows if r["kind"] == "literal"]
uh = {r["mkw_addr"] for r in hooks}
rh = {r["mkw_addr"] for r in hooks if r["nsmbw_addr"]}
print(f"hook sites: {len(hooks)} ({len(uh)} distinct MKW addresses), resolved distinct: {len(rh)}")
print(f"other literal sites: {len(lits)} ({len({r['mkw_addr'] for r in lits})} distinct), resolved distinct: "
      f"{len({r['mkw_addr'] for r in lits if r['nsmbw_addr']})}")
from collections import Counter  # noqa: E402
print("hook verdicts (distinct addresses):",
      dict(Counter({r["mkw_addr"]: r["verdict"] for r in hooks}.values())))
print("literal verdicts (distinct addresses):",
      dict(Counter({r["mkw_addr"]: r["verdict"] for r in lits}.values())))
by_file = {}
for r in hooks:
    t = by_file.setdefault(r["file"], [0, 0])
    t[0] += 1
    t[1] += 1 if r["nsmbw_addr"] else 0
print("hooks per file (resolved/total):")
for f, (tot, ok) in sorted(by_file.items(), key=lambda kv: -kv[1][0]):
    print(f"  {ok:4}/{tot:<4} {f}")
