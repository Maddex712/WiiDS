"""Repair NSMBW_NAND_TRACE calls whose "\\n" escape was turned into a real line break."""
from pathlib import Path

path = Path(r"E:\NSMBWPort\wiicompiled\runtime\src\hle\storage\nand_api.cpp")
lines = path.read_text(encoding="utf-8").split("\n")
out, fixed, i = [], 0, 0
while i < len(lines):
    line = lines[i]
    if "NSMBW_NAND_TRACE(\"" in line and line.count('"') % 2 == 1 and i + 1 < len(lines) \
            and lines[i + 1].startswith('", '):
        out.append(line + "\\n" + lines[i + 1])  # "\\n" here is a backslash followed by n
        fixed += 1
        i += 2
        continue
    out.append(line)
    i += 1
path.write_text("\n".join(out), encoding="utf-8")
print("repaired trace calls:", fixed)
