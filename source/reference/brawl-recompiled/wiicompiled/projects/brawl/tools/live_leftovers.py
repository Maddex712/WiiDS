"""Unmapped remap_log.csv rows on lines the compiler sees (outside #if 0 blocks)."""
import csv, re, sys
from pathlib import Path
wc = Path(sys.argv[1]) / "wiicompiled"
rows = [r for r in csv.DictReader(open(wc / "projects/brawl/remap_log.csv")) if not r["brawl"]]
cache = {}
def live(path, n):
    if path not in cache:
        lines = (wc / path).read_text(encoding="utf-8", errors="replace").split("\n")
        state, depth, out = [], 0, []
        for l in lines:
            s = l.strip()
            if re.match(r"#\s*if", s):
                state.append(s.startswith("#if 0") or (state and state[-1]))
            elif re.match(r"#\s*else", s) and state:
                state[-1] = (not state[-1]) if not (len(state) > 1 and state[-2]) else True
            elif re.match(r"#\s*endif", s) and state:
                state.pop()
            out.append(bool(state and state[-1]))
        cache[path] = (out, lines)
    dead, lines = cache[path]
    return not dead[n - 1], lines[n - 1].strip()
seen = set()
for r in rows:
    path = r["file"]
    live(path, 1)
    dead, lines = cache[path]
    for n, l in enumerate(lines):
        code = l.split("//")[0]
        if (r["spm"].lower() in code.lower()) and not dead[n] and (path, n) not in seen:
            seen.add((path, n))
            print(f'{path.split("/")[-1]}:{n + 1}	{r["spm"]}	{r["how"]}	{l.strip()[:120]}')
