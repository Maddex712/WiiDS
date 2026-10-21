"""Convert RootCubed's cracked NSMBW symbols (Shield/China "C" addresses) to SMNE01 rev 1 (E1) and
resolve every Mario Kart Wii symbol the WiiCompiled runtime hooks to its NSMBW address.

Outputs:
  maps/nsmbw_e1_symbols.tsv      e1_addr  mangled  demangled
  build-nsmbw/runtime_resolved.csv
"""
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import nsmbw_versions as ver  # noqa: E402

ROOT = Path(r"E:\NSMBWPort")

# ------------------------------------------------------------------ cracked symbols -> E1
syms = []  # (e1, mangled, demangled)
unconverted = 0
for row in csv.reader((ROOT / "maps" / "rc_symbols.csv").open(encoding="utf-8")):
    mangled, _dem_nv, dem_corr, c_addr = row[0], row[1], row[2], int(row[3], 16)
    e1 = ver.convert("C", "E1", c_addr)
    if e1 is None:
        unconverted += 1
        continue
    syms.append((e1, mangled, dem_corr))
syms.sort()
with (ROOT / "maps" / "nsmbw_e1_symbols.tsv").open("w", encoding="utf-8") as fh:
    for e1, m, d in syms:
        fh.write(f"{e1:08x}\t{m}\t{d}\n")
print(f"cracked symbols converted to E1: {len(syms)} (unconvertible: {unconverted})")


def base_name(demangled):
    """'EGG::TaskThread::run(void)' -> 'egg::taskthread::run' for loose matching."""
    return re.sub(r"\(.*$", "", demangled).strip().lower()


index = {}
for e1, m, d in syms:
    for key in {m, d, base_name(d), m.lower()}:
        index.setdefault(key, set()).add(e1)


def candidates(mkw):
    n = mkw.strip()
    out = [n]
    m = re.fullmatch(r"(?:RVL::)?(OS|DVD|GX|VI|AI|AX|DSP|SC|NAND|PAD|SI|EXI|IPC|WPAD|KPAD|PPC|THP|DC|IC)::(__)?(\w+)", n)
    if m:
        lib, under, rest = m.groups()
        out += [f"{'__' if under else ''}{lib}{rest}", f"{lib}{rest}"]
    m = re.fullmatch(r"(?:RVL::)?(IOS|ISFS|ES|ESP)::(\w+)", n)
    if m:
        out += [f"{m.group(1)}_{m.group(2)}", f"{m.group(1)}{m.group(2)}"]
    m = re.fullmatch(r"RVL::(\w+)", n)
    if m:
        out.append(m.group(1))
    out.append(n.lower())
    out.append(base_name(n))
    return out


rows = list(csv.DictReader((ROOT / "build-nsmbw" / "runtime_remap.csv").open()))
resolved = 0
for r in rows:
    hit, how = None, ""
    sym = r["mkw_symbol"].split("+")[0]
    if sym:
        for cand in candidates(sym):
            addrs = index.get(cand)
            if addrs and len(addrs) == 1:
                hit, how = next(iter(addrs)), cand
                break
            if addrs:
                how = f"ambiguous: {cand} -> " + ",".join(f"0x{a:08X}" for a in sorted(addrs))
    if hit is not None and "+" in r["mkw_symbol"]:
        offset = int(r["mkw_symbol"].split("+")[1], 16)
        how += f" (+0x{offset:X} offset carried from MKW; verify)"
        hit += offset
    r["nsmbw_e1_addr"] = f"0x{hit:08X}" if hit is not None else ""
    r["resolved_via"] = how
    if hit is not None:
        resolved += 1
with (ROOT / "build-nsmbw" / "runtime_resolved.csv").open("w", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)
named = sum(1 for r in rows if r["mkw_symbol"])
print(f"runtime addresses: {len(rows)}; with an MKW symbol: {named}; resolved to NSMBW: {resolved}")
