"""Rewrite Mario Kart Wii guest addresses in the WiiCompiled runtime to NSMBW (SMNE01 rev 1).

Driven by build-nsmbw/hooks.csv (hook_inventory.py):
  * resolved hook  -> only the macro's address argument is rewritten (symbol names untouched)
  * resolved literal -> the 0x... token is rewritten in place
  * hook whose function NSMBW does not link, or an MKW switch-case label -> the whole statement is
    wrapped in "#if 0 ... #endif" with a reason
gx_fatal_stubs.cpp is skipped (regenerated separately). Everything else is left for manual review.
Run once on a clean tree; git is the undo.
"""
import csv
import re
from collections import defaultdict
from pathlib import Path

ROOT = Path(r"E:\NSMBWPort")
RT = ROOT / "wiicompiled" / "runtime"
HOOK = re.compile(
    r"\b(PPC_NATIVE_OVERRIDE(?:_VOID)?|REGISTER_NATIVE_FUNCTION(?:_AS)?|REGISTER_TRANSLATED_FUNCTION|"
    r"MKW_KNOWN_TYPED_NATIVE|MKW_KNOWN_NATIVE_CPU_CALL)\s*\(\s*((?:0x)?)([0-9A-Fa-f]{8})(u?)")

rows = list(csv.DictReader((ROOT / "build-nsmbw" / "hooks.csv").open()))
by_file = defaultdict(list)
for r in rows:
    if "gx_fatal_stubs" not in r["file"]:
        by_file[r["file"]].append(r)


def same_style(old, new_value):
    """Format new_value like the old hex token (case, width)."""
    s = f"{new_value:08X}"
    return s.lower() if old == old.lower() and any(c.isalpha() for c in old) else s


def statement_end(lines, start, col):
    """Line index where the macro invocation starting at (start, col) closes its parentheses."""
    depth = 0
    started = False
    for i in range(start, len(lines)):
        text = lines[i][col:] if i == start else lines[i]
        for ch in text:
            if ch == "(":
                depth += 1
                started = True
            elif ch == ")":
                depth -= 1
                if started and depth == 0:
                    return i
    raise ValueError(f"unterminated macro at line {start + 1}")


stats = defaultdict(int)
for rel, frows in sorted(by_file.items()):
    path = RT / rel
    lines = path.read_text(encoding="utf-8", errors="surrogateescape").splitlines(keepends=True)
    disable = []  # (start_line, end_line, reason)
    for r in frows:
        i = int(r["line"]) - 1
        mkw_hex = r["mkw_addr"][2:]
        if r["kind"] == "literal":
            if r["verdict"] == "resolved":
                new = int(r["nsmbw_addr"], 16)
                lines[i], n = re.subn(rf"0x{mkw_hex}(?=\b|u)",
                                      lambda m: "0x" + same_style(m.group(0)[2:], new), lines[i], flags=re.I)
                stats["literal rewritten"] += n
            continue
        for m in HOOK.finditer(lines[i]):
            if m.group(3).upper() != mkw_hex.upper():
                continue
            if r["verdict"] == "resolved":
                new = same_style(m.group(3), int(r["nsmbw_addr"], 16))
                lines[i] = lines[i][:m.start(3)] + new + lines[i][m.end(3):]
                stats["hook rewritten"] += 1
            elif r["verdict"] in ("absent-in-nsmbw", "case-label"):
                end = statement_end(lines, i, m.start())
                why = ("MKW switch-case label; its function is native in NSMBW"
                       if r["verdict"] == "case-label" else
                       f"{r['macro_symbol'] or r['mkw_symbol']} is not linked into NSMBW")
                disable.append((i, end, why))
            break
    for start, end, why in sorted(set(disable), reverse=True):
        indent = re.match(r"\s*", lines[start]).group(0)
        lines.insert(end + 1, f"{indent}#endif\n")
        lines.insert(start, f"{indent}#if 0  // NSMBW: {why} (was MKW-only)\n")
        stats["statement disabled"] += 1
    path.write_text("".join(lines), encoding="utf-8", errors="surrogateescape")

for k, v in stats.items():
    print(f"{k}: {v}")
