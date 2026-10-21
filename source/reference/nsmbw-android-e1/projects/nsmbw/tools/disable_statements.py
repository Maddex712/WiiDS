"""Wrap whole C/C++ statements in "#if 0 // NSMBW: <reason>" ... "#endif".

usage: disable_statements.py <file> <line>:<reason> [<line>:<reason> ...]
A statement starts at the given 1-based line and ends at the line where its parentheses close
(macro invocations) or, for a function definition, where its braces close.
"""
import re
import sys
from pathlib import Path


def statement_end(lines, start):
    depth_paren = depth_brace = 0
    seen_brace = seen_paren = False
    for i in range(start, len(lines)):
        for ch in lines[i]:
            if ch == "(":
                depth_paren += 1
                seen_paren = True
            elif ch == ")":
                depth_paren -= 1
            elif ch == "{":
                depth_brace += 1
                seen_brace = True
            elif ch == "}":
                depth_brace -= 1
        if seen_brace and depth_brace == 0 and depth_paren == 0:
            return i
        if not seen_brace and seen_paren and depth_paren == 0 and lines[i].rstrip().endswith((";", ")")):
            # a macro/declaration statement; include a following "{" body if one starts here
            if i + 1 < len(lines) and lines[i + 1].lstrip().startswith("{"):
                continue
            return i
    raise ValueError(f"statement at line {start + 1} never closes")


def main():
    path = Path(sys.argv[1])
    lines = path.read_text(encoding="utf-8", errors="surrogateescape").splitlines(keepends=True)
    targets = []
    for arg in sys.argv[2:]:
        ln, reason = arg.split(":", 1)
        targets.append((int(ln) - 1, reason))
    for start, reason in sorted(targets, reverse=True):
        if lines[start - 1].lstrip().startswith("#if 0") if start else False:
            continue  # already disabled
        end = statement_end(lines, start)
        indent = re.match(r"\s*", lines[start]).group(0)
        lines.insert(end + 1, f"{indent}#endif\n")
        lines.insert(start, f"{indent}#if 0  // NSMBW: {reason}\n")
        print(f"{path.name}: disabled lines {start + 1}-{end + 1}: {reason}")
    path.write_text("".join(lines), encoding="utf-8", errors="surrogateescape")


if __name__ == "__main__":
    main()
