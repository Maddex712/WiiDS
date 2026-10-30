"""Retarget the runtime's HLE hooks from NSMBW (SMNE01 rev 1) to Super Paper Mario (R8PE01 rev 0), by symbol name.
Run once on a clean copy of the NSMBW-retargeted runtime. Hooks whose function is not in SPM are wrapped in #if 0.
Usage: apply_spm_remap.py <repo root>   -> rewrites runtime/src and runtime/include, writes projects/spm/remap_actions.csv"""
import csv, re, sys
from collections import defaultdict
from pathlib import Path
root = Path(sys.argv[1]); rt = root / "runtime"

def load_map(p):
    d = {}
    for l in p.read_text().splitlines():
        a = l.split(None, 1)
        if len(a) == 2 and not a[1].startswith("0x"): d[int(a[0], 16)] = a[1].strip()
    return d
nsmbw = load_map(root / "projects/nsmbw/MAP.txt")
spm = defaultdict(set)
for l in (root / "projects/spm/us0_names.tsv").read_text().splitlines():
    us, eu_name, _sz, _eu, us_name = l.split("\t")
    spm[eu_name].add(int(us, 16))
    if not re.match(r"(fn|lbl|zz)_", us_name): spm[us_name].add(int(us, 16))
for l in open(root.parent / "spm-decomp/config/US0/symbols.txt", encoding="utf-8", errors="replace"):
    m = re.match(r"(\S+)\s*=\s*\.(text|init):0x([0-9A-Fa-f]{8});\s*//\s*(.*)", l)
    if m and "type:function" in m.group(4) and not re.match(r"(fn|lbl|zz)_", m.group(1)): spm[m.group(1)].add(int(m.group(3), 16))

# Hand-verified SPM US0 addresses for SDK functions with no US0 symbol (see PLAN.md, "Input").
for n, a in {"WPADInit": 0x802CC950, "WPADGetInfoAsync": 0x802CE4C4, "WPADControlMotor": 0x802CE684,
             "DCStoreRange": 0x8026D830}.items(): spm[n].add(a)
import json
for k, v in json.loads((root / "projects/spm/bindiff.json").read_text()).items():
    nm = nsmbw.get(int(k, 16))
    if nm and not spm.get(nm): spm[nm].add(int(v, 16))

HOOK = re.compile(r"\b(PPC_NATIVE_OVERRIDE_VOID|PPC_NATIVE_OVERRIDE|REGISTER_NATIVE_FUNCTION_AS|REGISTER_NATIVE_FUNCTION|"
                  r"REGISTER_TRANSLATED_FUNCTION|MKW_KNOWN_TYPED_NATIVE|MKW_KNOWN_NATIVE_CPU_CALL)\s*\(\s*((?:0x)?)([0-9A-Fa-f]{8})(u?)")

def statement_end(lines, start, col):
    depth, started = 0, False
    for i in range(start, len(lines)):
        for ch in (lines[i][col:] if i == start else lines[i]):
            if ch == "(": depth += 1; started = True
            elif ch == ")":
                depth -= 1
                if started and depth == 0: return i
    raise ValueError(f"unterminated macro at line {start + 1}")

actions, stats = [], defaultdict(int)
for path in sorted(list((rt / "src").rglob("*")) + list((rt / "include").rglob("*"))):
    if path.suffix not in (".cpp", ".h", ".hpp", ".inc") or "gx_fatal_stubs" in path.name or "third_party" in path.parts: continue
    lines = path.read_text(encoding="utf-8", errors="surrogateescape").splitlines(keepends=True)
    disable, dirty = [], False
    for i, line in enumerate(lines):
        for m in HOOK.finditer(line):
            old = int(m.group(3), 16); name = nsmbw.get(old)
            cands = sorted(spm.get(name, ())) if name else []
            rel = str(path.relative_to(rt)).replace("\\", "/")
            if len(cands) == 1:
                new = f"{cands[0]:08X}"
                if m.group(3).islower() or (m.group(3).lower() == m.group(3)): new = new.lower()
                lines[i] = lines[i][:m.start(3)] + new + lines[i][m.end(3):]; dirty = True
                stats["rewritten"] += 1; actions.append((rel, i + 1, f"{old:08X}", name, new.upper(), "rewritten"))
            else:
                why = "name not in SPM" if name and not cands else "ambiguous in SPM" if cands else "no NSMBW symbol at this address"
                disable.append((i, statement_end(lines, i, m.start()), f"{name or hex(old)}: {why}"))
                stats["disabled (" + why + ")"] += 1; actions.append((rel, i + 1, f"{old:08X}", name or "", "", "disabled: " + why))
            break
    for start, end, why in sorted(set(disable), reverse=True):
        indent = re.match(r"\s*", lines[start]).group(0)
        lines.insert(end + 1, f"{indent}#endif\n"); lines.insert(start, f"{indent}#if 0  // SPM: {why}\n"); dirty = True
    if dirty: path.write_text("".join(lines), encoding="utf-8", errors="surrogateescape")
with open(root / "projects/spm/remap_actions.csv", "w", newline="") as fh:
    w = csv.writer(fh); w.writerow(["file", "line", "nsmbw_addr", "symbol", "spm_addr", "action"]); w.writerows(actions)
print(dict(stats))
