"""Convert NSMBW addresses between game versions using NSMBW-Updated's address-map.txt.

Semantics follow Kamek's address mapper: a version block lists ranges over its parent's address
space ("extend X"); an address is first mapped through the parent, then shifted by the first range
containing it, and passed through unchanged when no range contains it.
"""
import re
from functools import lru_cache
from pathlib import Path

MAP_PATH = Path(r"E:\NSMBWPort\maps\address-map.txt")


def _parse():
    blocks, cur = {}, None
    for raw in MAP_PATH.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        m = re.fullmatch(r"\[(\w+)\]", line)
        if m:
            cur = blocks.setdefault(m.group(1), {"parent": None, "ranges": []})
            continue
        if cur is None:
            continue
        if line.startswith("extend"):
            cur["parent"] = line.split()[1]
            continue
        m = re.fullmatch(r"([0-9a-fA-F]+)-([0-9a-fA-F]+|\*)\s*:\s*([+-])0x([0-9a-fA-F]+)", line)
        if m:
            start = int(m.group(1), 16)
            end = 0xFFFFFFFF if m.group(2) == "*" else int(m.group(2), 16)
            delta = int(m.group(4), 16) * (1 if m.group(3) == "+" else -1)
            cur["ranges"].append((start, end, delta))
    return blocks


BLOCKS = _parse()


def forward(version, p1):
    """PAL v1 address -> address in `version`."""
    if version == "P1":
        return p1
    block = BLOCKS[version]
    a = forward(block["parent"], p1) if block["parent"] else p1
    for start, end, delta in block["ranges"]:
        if start <= a <= end:
            return (a + delta) & 0xFFFFFFFF
    return a


@lru_cache(maxsize=None)
def inverse(version, addr):
    """Address in `version` -> PAL v1 address, or None if it has no unambiguous preimage."""
    if version == "P1":
        return addr
    block = BLOCKS[version]
    candidates = [addr - d for s, e, d in block["ranges"] if s <= addr - d <= e]
    if not any(s <= addr <= e for s, e, _ in block["ranges"]):
        candidates.append(addr)  # uncovered addresses pass through unchanged
    results = set()
    for parent_addr in candidates:
        p1 = inverse(block["parent"], parent_addr) if block["parent"] else parent_addr
        if p1 is not None and forward(version, p1) == addr:
            results.add(p1)
    if len(results) > 1:
        # e.g. [C] folds 0x9xxxxxxx down onto MEM1; code and static data live in MEM1.
        results = {r for r in results if 0x80000000 <= r < 0x81800000}
    return results.pop() if len(results) == 1 else None


def convert(src, dst, addr):
    p1 = inverse(src, addr)
    return None if p1 is None else forward(dst, p1)


if __name__ == "__main__":
    for name, c_addr in [("OSLink", 0x801B4280), ("OSCreateThread", 0x801B7490), ("GXBegin", 0x801C78D0),
                         ("ARCOpen", 0x801A1A60), ("memcpy", 0x80004364)]:
        p1 = inverse("C", c_addr)
        print(f"{name:16} C 0x{c_addr:08X} -> P1 0x{p1:08X} -> E1 0x{forward('E1', p1):08X}")
