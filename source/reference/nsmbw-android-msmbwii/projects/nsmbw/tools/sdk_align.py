"""Align Mario Kart Wii's named SDK functions onto NSMBW (SMNE01 rev 1) using shared anchors.

Both games link the same Revolution SDK libraries in the same order, so between two anchors that
are known in both games, functions keep their relative layout whenever the code in between is the
same size. For each anchor we record delta = nsmbw - mkw; MKW functions bracketed by anchors with
equal deltas are placed at mkw + delta and then verified against NSMBW's own code (the target must
be a direct-call target or a known function start, and be preceded by a terminator instruction).

Writes build-nsmbw/sdk_aligned.csv.
"""
import csv
import re
import struct
from bisect import bisect_right
from pathlib import Path

ROOT = Path(r"E:\NSMBWPort")
WC = ROOT / "wiicompiled"
BASE = 0x80000000
flat = (ROOT / "game" / "nsmbw_flat.bin").read_bytes()
bl_targets = {int(x, 16) for x in (ROOT / "game" / "seeds_bl.txt").read_text().split()}


def word(a):
    return struct.unpack_from(">I", flat, a - BASE)[0]


def normalize(name):
    """MKW map names mix SDK spellings ("DVDReadPrio") and namespaced ones ("DVD::Init")."""
    n = name.strip()
    m = re.fullmatch(r"(DVD|OS|GX|VI|AI|AX|DSP|SC|NAND|PAD|SI|EXI|IPC|IOS|WPAD|KPAD|PPC|DC|IC|ISFS)::(\w+)", n)
    if m:
        return m.group(1) + m.group(2)
    m = re.fullmatch(r"RVL::(\w+)", n)
    if m:
        return m.group(1)
    return n


def load(path, norm=False):
    by_name, by_addr = {}, {}
    for line in path.read_text().splitlines():
        parts = line.split(None, 1)
        if len(parts) != 2 or parts[1].startswith("0x"):
            continue
        a = int(parts[0], 16)
        n = normalize(parts[1]) if norm else parts[1]
        by_addr[a] = n
        by_name.setdefault(n, []).append(a)
    return by_name, by_addr


mkw_name, mkw_addr = load(WC / "projects" / "mkwii" / "MAP.txt", norm=True)
nsm_name, nsm_addr = load(WC / "projects" / "nsmbw" / "MAP.txt")
nsm_starts = {int(l.split()[0], 16) for l in (WC / "projects" / "nsmbw" / "MAP.txt").read_text().splitlines()}

# Anchors: unique names present in both maps, restricted to the main DOL (SDK lives there).
anchors = []
for n, addrs in nsm_name.items():
    if len(addrs) == 1 and n in mkw_name and len(mkw_name[n]) == 1:
        na, ma = addrs[0], mkw_name[n][0]
        if na < 0x80400000 and ma < 0x80400000:
            anchors.append((ma, na, n))
anchors.sort()
anchor_mkw = [a[0] for a in anchors]

TERMINATORS = {0x4E800020, 0x4C000064, 0x4E800420}  # blr, rfi, bctr


def plausible_start(a):
    if a in bl_targets or a in nsm_starts:
        prev = word(a - 4)
        return prev in TERMINATORS or (prev >> 26) == 18 and (prev & 1) == 0 or prev == 0
    return False


def place(mkw_a):
    """Return (nsmbw_addr, how) for an MKW address, or (None, reason)."""
    i = bisect_right(anchor_mkw, mkw_a) - 1
    if i < 0 or i + 1 >= len(anchors):
        return None, "outside anchored range"
    lo, hi = anchors[i], anchors[i + 1]
    d_lo, d_hi = lo[1] - lo[0], hi[1] - hi[0]
    candidates = []
    if d_lo == d_hi:
        candidates.append((mkw_a + d_lo, f"bracketed {lo[2]}..{hi[2]} delta {d_lo:+#x}"))
    else:
        # Layout changed somewhere between the anchors: try both deltas, keep what verifies.
        candidates.append((mkw_a + d_lo, f"near {lo[2]} delta {d_lo:+#x} (bracket deltas differ)"))
        candidates.append((mkw_a + d_hi, f"near {hi[2]} delta {d_hi:+#x} (bracket deltas differ)"))
    for c, how in candidates:
        if plausible_start(c):
            return c, how
    return None, f"no verified start ({lo[2]} {d_lo:+#x} / {hi[2]} {d_hi:+#x})"


if __name__ == "__main__":
    print(f"anchors shared with MKW: {len(anchors)}")
    runs, prev = [], None
    for ma, na, n in anchors:
        d = na - ma
        if prev is None or d != prev[2]:
            runs.append([n, n, d, 1])
        else:
            runs[-1][1] = n
            runs[-1][3] += 1
        prev = runs[-1]
    print("delta runs (first..last anchor, delta, count):")
    for first, last, d, c in runs:
        print(f"  {first:28} .. {last:28} {d:+#9x}  x{c}")

    wanted = list(csv.DictReader((ROOT / "build-nsmbw" / "runtime_remap.csv").open()))
    out_rows, ok = [], 0
    for row in wanted:
        mkw_a = int(row["mkw_addr"], 16)
        nsm, how = place(mkw_a)
        if nsm is not None and row["status"] in ("named-unmatched", "mapped") and mkw_a in mkw_addr:
            ok += 1
        out_rows.append({**row, "aligned_nsmbw": f"0x{nsm:08X}" if nsm else "", "how": how})
    with (ROOT / "build-nsmbw" / "sdk_aligned.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(out_rows[0].keys()))
        w.writeheader()
        w.writerows(out_rows)
    print(f"runtime addresses: {len(wanted)}; named functions placed+verified: {ok}")
