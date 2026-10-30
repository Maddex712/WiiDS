"""Pair Super Paper Mario USA (R8PE01 rev 0, US0) guest addresses with the PAL dol (R8PP01 rev 0, EU0) by symbol name,
and write the PAL function map for the translator.
Inputs: spm-decomp config dir (config/US0, config/EU0 symbols.txt: https://github.com/SeekyCt/spm-decomp), the USA
alignment table projects/spm/us0_names.tsv, and game_pal/spm_rel_layout.json (prelink_spm_rel.py on the PAL relF.bin).
Usage: gen_addr_map.py <repo root> <spm-decomp/config dir>
  -> projects/spm/pal/addr_map.tsv (us eu size name) and projects/spm/pal/MAP.txt (EU dol + REL function starts)."""
import bisect, json, re, sys
from collections import Counter, defaultdict
from pathlib import Path

root, cfg = Path(sys.argv[1]), Path(sys.argv[2])
out = root / "wiicompiled/projects/spm/pal"
SYM = re.compile(r"(\S+)\s*=\s*\.(\w+):0x([0-9A-Fa-f]{8});\s*//\s*(.*)")


def symbols(ver):
    """name -> list of (addr, size, type) from the dol symbol table."""
    d = defaultdict(list)
    for line in open(cfg / ver / "symbols.txt", encoding="utf-8", errors="replace"):
        m = SYM.match(line)
        if not m:
            continue
        kind = re.search(r"type:(\w+)", m.group(4))
        size = re.search(r"size:0x([0-9A-Fa-f]+)", m.group(4))
        d[m.group(1)].append((int(m.group(3), 16), int(size.group(1), 16) if size else 0, kind.group(1) if kind else ""))
    return d


US, EU = symbols("US0"), symbols("EU0")
pairs = {}  # us addr -> (eu addr, size, name)
for name, us in US.items():
    eu = EU.get(name)
    if len(us) == 1 and eu and len(eu) == 1:
        pairs[us[0][0]] = (eu[0][0], us[0][1], name)
unique = len(pairs)
# The size-sequence alignment of projects/spm/tools/align_eu0_us0.py covers the unnamed functions too.
for line in (root / "wiicompiled/projects/spm/us0_names.tsv").read_text().splitlines():
    us, name, size, eu, _ = line.split("\t")
    if eu and int(us, 16) not in pairs:
        pairs[int(us, 16)] = (int(eu, 16), int(size, 16), name)
# spm-headers' linker lists name the globals the runtime pokes (gp, seqWork, ...): https://github.com/SeekyCt/spm-headers
lists = {}
for ver in ("us0", "eu0"):
    p = root / f"wiicompiled/projects/spm/pal/spm.{ver}.lst"
    lists[ver] = {m.group(2): int(m.group(1), 16) for m in re.finditer(r"(?m)^\s*([0-9A-Fa-f]{8}):(\S+)", p.read_text(errors="replace"))} if p.exists() else {}
listed = 0
for name, us in lists["us0"].items():
    if name in lists["eu0"] and us not in pairs:
        pairs[us] = (lists["eu0"][name], 4, name)
        listed += 1
funcpairs = {us: eu for us, (eu, _, _) in pairs.items()}


def dol(path):
    import struct
    b = open(path, "rb").read()
    h = struct.unpack(">18I18I18IIII", b[:0xE4])
    secs = [(h[18 + i], h[36 + i], h[i]) for i in range(18) if h[36 + i]]

    def read(a, n):
        for addr, size, off in secs:
            if addr <= a < addr + size:
                return b[off + a - addr: off + a - addr + n]
        return b""
    return read


def refs(read, a, size, sda, sda2):
    """instruction index -> absolute address it forms (lis/addi, lis/ori, lis+load/store, r13/r2 relative)."""
    import struct
    data = read(a, size)
    w = struct.unpack(">%dI" % (len(data) // 4), data[: len(data) // 4 * 4])
    hi, out = {}, {}
    for i, x in enumerate(w):
        op, rd, ra, si = x >> 26, (x >> 21) & 31, (x >> 16) & 31, x & 0xFFFF
        ss = si - 0x10000 if si & 0x8000 else si
        addr = None
        if op == 15:
            if ra == 0:
                hi[rd] = si << 16
            else:
                hi.pop(rd, None)
        elif op == 14:
            addr = sda + ss if ra == 13 else sda2 + ss if ra == 2 else hi[ra] + ss if ra in hi else None
            if addr is None:
                hi.pop(rd, None)
        elif op == 24:
            if rd in hi:
                addr = hi[rd] | si
        elif 32 <= op <= 55:
            addr = sda + ss if ra == 13 else sda2 + ss if ra == 2 else hi[ra] + ss if ra in hi else None
            if op <= 43 and rd in hi and rd != ra:
                hi.pop(rd, None)
        if addr is not None and 0x80004000 <= addr < 0x81800000:
            out[i] = addr
    return out


# Data addresses: paired functions of equal size reference the same globals at the same instruction; majority vote.
US_READ, EU_READ = dol(root / "game/main.dol.orig"), dol(root / "game_pal/main.dol.orig")
votes = defaultdict(Counter)
for name, us in US.items():
    for ua, usize, kind in us:
        if kind != "function" or ua not in funcpairs or not usize:
            continue
        ea = funcpairs[ua]
        esize = next((s for a, s, _ in EU.get(name, []) if a == ea), usize)
        if esize != usize:
            continue
        ur, er = refs(US_READ, ua, usize, 0x80574F20, 0x80576280), refs(EU_READ, ea, esize, 0x805B5F00, 0x805B7260)
        for i, addr in ur.items():
            if i in er:
                votes[addr][er[i]] += 1
voted = 0
for us, c in votes.items():
    (eu, n), = c.most_common(1)
    if us not in pairs and n * 2 > sum(c.values()):
        pairs[us] = (eu, 4, f"ref@{us:08X}")
        voted += 1
rows = sorted((us, eu, size, name) for us, (eu, size, name) in pairs.items())
# Drop a pair whose shift disagrees with both neighbours while they agree with each other (a same-named local that
# paired across files, or a code reference that formed an unrelated address).
shifts = [eu - us for us, eu, _, _ in rows]
keep = [i for i in range(len(rows)) if not (0 < i < len(rows) - 1 and shifts[i - 1] == shifts[i + 1] != shifts[i])]
dropped = [rows[i] for i in range(len(rows)) if i not in set(keep)]
rows = [rows[i] for i in keep]
# Overlaps: a pair starting inside the previous one (bad size or mispair) goes.
clean, end = [], 0
for r in rows:
    if r[0] < end:
        dropped.append(r)
        continue
    clean.append(r)
    end = r[0] + max(r[2], 1)
rows = clean
out.mkdir(parents=True, exist_ok=True)
with open(out / "addr_map.tsv", "w", newline="\n") as fh:
    fh.write("# us0\teu0\tsize\tname  (gen_addr_map.py; shifts: %s)\n" % ", ".join(f"{k:+#x}x{v}" for k, v in Counter(eu - us for us, eu, _, _ in rows).most_common(8)))
    for us, eu, size, name in rows:
        fh.write(f"{us:08X}\t{eu:08X}\t{size:X}\t{name}\n")
print(f"{len(rows)} pairs ({unique} by unique name, {listed} from spm-headers lists, {voted} by code references), "
      f"dropped {len(dropped)}: {[(f'{u:08X}', n) for u, _, _, n in dropped[:12]]}")

# PAL function map: every EU dol function start, plus the EU REL text functions named by text offset like projects/spm/MAP_rel.txt.
funcs = {a: n for n, lst in EU.items() for a, _, kind in lst if kind == "function"}
for us, eu, _, name in rows:
    if us in {a for a, _, k in US.get(name, []) if k == "function"} or name.startswith(("fn_", "func_")):
        funcs.setdefault(eu, name)
layout = json.load(open(root / "game_pal/spm_rel_layout.json"))
text = int(layout["sections"]["1"]["addr"], 16)
for line in open(cfg / "EU0/relF/symbols.txt", encoding="utf-8", errors="replace"):
    m = SYM.match(line)
    if m and m.group(2) == "text" and "type:function" in m.group(4):
        funcs.setdefault(text + int(m.group(3), 16), f"rel_{int(m.group(3), 16):X}")
with open(out / "MAP.txt", "w", newline="\n") as fh:
    for a in sorted(funcs):
        fh.write(f"{a:08X} {funcs[a]}\n")
print(len(funcs), "function starts in MAP.txt")
