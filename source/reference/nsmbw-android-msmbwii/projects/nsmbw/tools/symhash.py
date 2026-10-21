"""Look up NSMBW functions by name through the Shield build's hashed symbol table (hashes.txt).

The table stores a hash of every symbol's mangled and demangled name (RootCubed's server.js:
h = 0x1505; h = (h * 33) ^ c, 32-bit). For C functions the mangled name is the plain name, so any
correctly guessed SDK name resolves to its exact address even if nobody has cracked it yet.

usage: symhash.py NAME [NAME...]
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import nsmbw_versions as ver  # noqa: E402

ROOT = Path(r"E:\NSMBWPort")


def h(name):
    v = 0x1505
    for c in name.strip():
        v = ((v * 33) ^ ord(c)) & 0xFFFFFFFF
    return v


_table = None


def table():
    global _table
    if _table is None:
        _table = {}
        for row in (ROOT / "maps" / "nsmbw_hashes.txt").read_text().splitlines():
            c = [x.strip() for x in row.split("|")]
            entry = (int(c[0], 16), c[1], int(c[4].split()[1], 16))
            for col in (c[2], c[3]):
                _table.setdefault(int(col[1:], 16), []).append(entry)
    return _table


def lookup(name):
    """[(e1_addr, kind, length)] for every symbol whose mangled or demangled name hashes to `name`."""
    out = []
    for c_addr, kind, length in table().get(h(name), []):
        e1 = ver.convert("C", "E1", c_addr)
        if e1 is not None and (e1, kind, length) not in out:
            out.append((e1, kind, length))
    return out


if __name__ == "__main__":
    for n in sys.argv[1:]:
        hits = lookup(n)
        print(f"{n:40} " + (", ".join(f"0x{a:08X} {k} len 0x{l:X}" for a, k, l in hits) if hits else "-- not in NSMBW"))
