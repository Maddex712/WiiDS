"""Map Super Paper Mario (R8PE01 rev 0) main.dol addresses to Super Smash Bros. Brawl (RSBE01 rev 1) main.dol.

Functions: identical masked instruction bodies (unique match), else the same name in the Brawl decomp's symbols.txt.
Data: the matched functions' references (lis/addi pairs, r13/r2 small-data offsets) at the same instruction index.
Imported by remap_runtime.py; run alone for a summary:  addr_map.py <spm main.dol> <brawl main.dol> <spm MAP.txt> <brawl symbols.txt>"""
import bisect, collections, difflib, re, struct, sys, hashlib
from pathlib import Path


class Dol:
    def __init__(self, path):
        b = open(path, "rb").read()
        h = struct.unpack(">18I18I18IIII", b[:0xE4])
        self.b = b
        self.secs = [(h[18 + i], h[36 + i], h[i], i < 7) for i in range(18) if h[36 + i]]
        self.text = [(a, a + s) for a, s, o, x in self.secs if x]

    def read(self, a, n):
        for addr, size, off, _ in self.secs:
            if addr <= a and a + n <= addr + size:
                return self.b[off + a - addr: off + a - addr + n]
        return None

    def words(self, a, size):
        raw = self.read(a, size)
        return list(struct.unpack(">%dI" % (size // 4), raw)) if raw else []

    def is_text(self, a):
        return any(s <= a < e for s, e in self.text)


def r13_r2(dol):
    """__init_registers: lis/ori r2 then r13 (whichever order); scans .init."""
    regs = {}
    s, e = dol.text[0]
    w = dol.words(s, e - s)
    for i in range(len(w) - 1):
        op, rd, ra = w[i] >> 26, (w[i] >> 21) & 31, (w[i] >> 16) & 31
        if op == 15 and ra == 0 and rd in (1, 2, 13):
            n = w[i + 1]
            if n >> 26 == 24 and (n >> 21) & 31 == rd and (n >> 16) & 31 == rd:  # ori rd,rd,lo
                regs.setdefault(rd, ((w[i] & 0xFFFF) << 16) | (n & 0xFFFF))
    return regs.get(13), regs.get(2)


def dsy_checksum(w):
    """Dolphin HashSignatureDB::ComputeCodeChecksum."""
    s = 0
    for x in w:
        op = x & 0xFC000000
        aux = op >> 26
        op2 = op3 = 0
        if aux == 4:
            op2 = x & 0x3F
            if op2 in (0, 8, 16, 21, 22):
                op3 = x & 0x7C0
        elif aux in (7, 8, 10, 11, 12, 13, 14, 15):
            op2 = x & 0x03FF0000
        elif aux in (19, 31, 63):
            op2 = x & 0x7FF
        elif aux == 59:
            op2 = x & 0x3F
            if op2 < 16:
                op3 = x & 0x7C0
        elif 32 <= aux < 56:
            op2 = x & 0x03FF0000
        s = ((s << 17) & 0xFFFE0000) | ((s >> 15) & 0x1FFFF)
        s ^= op | op2 | op3
    return s


def load_dsy(path):
    b = open(path, "rb").read()
    n = struct.unpack("<I", b[:4])[0]
    out = {}
    for i in range(n):
        o = 4 + i * 136
        ck, size = struct.unpack("<II", b[o:o + 8])
        name = b[o + 8:o + 136].split(bytes(1))[0].decode("latin-1").split(" ")[0].split("	")[0]
        out.setdefault((ck, size), name)
    return out


def sext26(v):
    return v - 0x4000000 if v & 0x2000000 else v


def sext(v):
    return v - 0x10000 if v & 0x8000 else v


def skeleton(w):
    """Mask relocated fields: branch targets, immediates of address-forming and memory ops."""
    out = []
    for x in w:
        op = x >> 26
        if op == 18:
            out.append(x & 0xFC000003)
        elif op in (14, 15, 24, 25) or 32 <= op <= 55 or op in (56, 57, 60, 61):  # addi/lis/ori/oris, loads/stores, psq
            out.append(x & 0xFFFF0000)
        else:
            out.append(x)
    return tuple(out)


def refs(dol, a, size, r13, r2):
    """[(instruction index, absolute address)] for lis+lo pairs and r13/r2-relative accesses."""
    w = dol.words(a, size)
    val = {}  # GPR -> known absolute address (lis, then addi/ori/addi chains)
    if r13:
        val[13] = r13
    if r2:
        val[2] = r2
    out = []
    for i, x in enumerate(w):
        op, rd, ra, imm = x >> 26, (x >> 21) & 31, (x >> 16) & 31, x & 0xFFFF
        if op == 15 and ra == 0:  # lis
            val[rd] = imm << 16
            continue
        if op == 15 or op == 14:  # addis/addi rD, rA, si
            if ra != 0 and ra in val:
                t = (val[ra] + (sext(imm) << (16 if op == 15 else 0))) & 0xFFFFFFFF
                if op == 14:
                    out.append((i, t))
                val[rd] = t
            else:
                val.pop(rd, None)
            continue
        if op == 24:  # ori rA, rS, ui
            if rd in val:
                val[ra] = val[rd] | imm
                out.append((i, val[ra]))
            else:
                val.pop(ra, None)
            continue
        if 32 <= op <= 55 or op in (56, 57, 60, 61):  # D-form loads/stores (psq: 12-bit offset)
            d = sext(imm) if op < 56 else ((imm & 0xFFF) - 0x1000 if imm & 0x800 else imm & 0xFFF)
            if ra in val and ra != 0:
                out.append((i, (val[ra] + d) & 0xFFFFFFFF))
            if op in (32, 33, 34, 35, 40, 41, 42, 43, 46):  # GPR loads overwrite rD
                val.pop(rd, None)
            if op in (33, 35, 37, 39, 41, 43, 45, 49, 51, 53, 55, 57, 61):  # update forms move rA
                val.pop(ra, None)
            continue
        if op in (7, 8, 12, 13, 31):  # rD-writing arithmetic and X-forms (approximate)
            val.pop(rd, None)
            if op == 31:
                val.pop(ra, None)
        elif op in (20, 21, 23, 25, 26, 27, 28, 29):  # rotates and logical immediates write rA
            val.pop(ra, None)
        elif op == 18 and x & 1:  # bl clobbers volatiles
            for r in list(val):
                if r <= 12 and r != 2:
                    val.pop(r)
    return out


class Mapper:
    def __init__(self, spm_dol, brawl_dol, spm_map, brawl_syms, spm_lst=None):
        self.S, self.B = Dol(spm_dol), Dol(brawl_dol)
        self.s13, self.s2 = r13_r2(self.S)
        self.b13, self.b2 = r13_r2(self.B)
        rows = [l.split(None, 1) for l in open(spm_map).read().splitlines() if l.strip()]
        sf = sorted((int(a, 16), n.strip()) for a, n in rows)
        self.sstart = [a for a, _ in sf]
        self.sname = [n for _, n in sf]
        if spm_lst:
            for l in open(spm_lst).read().splitlines():
                a, _, n = l.partition(":")
                if re.fullmatch(r"[0-9A-Fa-f]{8}", a):
                    a = int(a, 16)
                    k = bisect.bisect_left(self.sstart, a)
                    if k < len(self.sstart) and self.sstart[k] == a and self.sname[k].startswith(("fn_", "func_")):
                        self.sname[k] = n.strip()
        self.bfunc, self.bobj, self.bname = [], [], {}
        for l in open(brawl_syms).read().splitlines():
            m = re.match(r"(\S+) = (\S+):0x([0-9A-Fa-f]+); // type:(\w+)(?: size:0x([0-9A-Fa-f]+))?", l)
            if not m or m[2] not in (".init", ".text", ".data", ".bss", ".sdata", ".sbss", ".sdata2", ".sbss2", ".rodata", ".ctors"):
                continue
            a, size = int(m[3], 16), int(m[5] or "0", 16)
            if m[4] == "function":
                self.bfunc.append((a, size, m[1]))
            self.bname.setdefault(m[1], a)
        self.bfunc.sort()
        dsy = Path(brawl_syms).parents[3] / "totaldb.dsy"
        if dsy.exists():  # Dolphin's signature database names SDK functions the decomp leaves as fn_
            db = load_dsy(dsy)
            seen = collections.defaultdict(list)
            for a, size, n in self.bfunc:
                hit = db.get((dsy_checksum(self.B.words(a, size)), size)) if size >= 0x20 else None
                if hit:
                    seen[hit].append(a)
            for hit, addrs in seen.items():
                if len(addrs) == 1:  # identical bodies (getter/setter twins) say nothing about which is which
                    self.bname.setdefault(hit, addrs[0])
        self.bstart = [a for a, _, _ in self.bfunc]
        idx = collections.defaultdict(list)
        for a, size, n in self.bfunc:
            if size >= 8:
                idx[(size, hashlib.sha1(repr(skeleton(self.B.words(a, size))).encode()).digest())].append(a)
        self.bidx = idx
        self._fcache = {}
        self._busy = set()
        self._failed = {}

    def spm_func(self, a):
        k = bisect.bisect_right(self.sstart, a) - 1
        if k < 0:
            return None
        end = self.sstart[k + 1] if k + 1 < len(self.sstart) else a + 4
        return self.sstart[k], end - self.sstart[k], self.sname[k]

    def brawl_func_size(self, a):
        k = bisect.bisect_left(self.bstart, a)
        return self.bfunc[k][1] if k < len(self.bstart) and self.bstart[k] == a else None

    def map_func(self, start, depth=0):
        """SPM function start -> (brawl start, how) or (None, why). depth > 0: a lookup made while matching another
        function (cheaper methods only, results kept only when they succeed)."""
        if start in self._fcache:
            return self._fcache[start]
        if self._failed.get(start, 99) <= depth:  # already failed with at least this much effort
            return (None, "unmatched")
        if start in self._busy:
            return (None, "cycle")
        self._busy.add(start)
        f = self.spm_func(start)
        res = (None, "nofunc")
        if f and f[0] == start:
            _, size, name = f
            w = self.S.words(start, size)
            # Trailing alignment padding belongs to no function in symbols.txt.
            while w and w[-1] == 0:
                w.pop()
            key = (len(w) * 4, hashlib.sha1(repr(skeleton(w)).encode()).digest())
            hits = self.bidx.get(key, [])
            byname = self.bname.get(name) if not name.startswith(("fn_", "func_")) else None
            if len(hits) == 1:
                res = (hits[0], "body")
            else:
                res = self._by_neighbours(start, w, hits, depth)
                if res[0] is None and byname is not None and (not hits or byname in hits):
                    res = (byname, "name" + ("+body" if hits else ""))
        self._busy.discard(start)
        if depth == 0 or res[0] is not None:
            self._fcache[start] = res
        elif res[1] != "cycle":
            self._failed[start] = min(self._failed.get(start, 99), depth)
        return res

    def _anchors(self):
        """Unique body matches that keep both dols in the same order (longest increasing run)."""
        if hasattr(self, "_anch"):
            return self._anch
        pairs = []
        for s in self.sstart:
            if not self.S.is_text(s):
                continue
            k = bisect.bisect_right(self.sstart, s)
            size = (self.sstart[k] if k < len(self.sstart) else s + 4) - s
            w = self.S.words(s, size)
            while w and w[-1] == 0:
                w.pop()
            if len(w) < 6:  # tiny bodies match by accident
                continue
            hits = self.bidx.get((len(w) * 4, hashlib.sha1(repr(skeleton(w)).encode()).digest()), [])
            if len(hits) == 1:
                pairs.append((s, hits[0]))
        # Longest increasing subsequence on the Brawl side.
        tails, prev, idx = [], [None] * len(pairs), []
        for i, (_, b) in enumerate(pairs):
            j = bisect.bisect_left(tails, b)
            if j == len(tails):
                tails.append(b); idx.append(i)
            else:
                tails[j] = b; idx[j] = i
            prev[i] = idx[j - 1] if j else None
        out, i = [], idx[-1] if idx else None
        while i is not None:
            out.append(pairs[i]); i = prev[i]
        self._anch = sorted(out)
        self._anch_s = [s for s, _ in self._anch]
        return self._anch

    def _by_neighbours(self, start, w, hits, depth=0):
        anch = self._anchors()
        k = bisect.bisect_right(self._anch_s, start) - 1
        if k < 0 or k + 1 >= len(anch):
            return (None, "unmatched")
        (s0, b0), (s1, b1) = anch[k], anch[k + 1]
        between_s = [a for a in self.sstart if s0 < a < s1]
        between_b = [a for a in self.bstart if b0 < a < b1]
        if hits:
            near = [h for h in hits if b0 < h < b1]
            if len(near) == 1:
                return (near[0], "body+order")
        if len(between_s) == len(between_b) and start in between_s:
            c = between_b[between_s.index(start)]
            return (c, "order")
        if depth >= 3:
            return (None, "unmatched")
        # Called from a matched function: the call with the same ordinal in its Brawl counterpart.
        votes = collections.Counter()
        for caller, n in self._callers().get(start, []):
            b, how = self.map_func(caller, depth + 1)
            if b is None or not how.startswith(("body", "name", "order")):
                continue
            sl = self._bls(self.S, caller, self.spm_func(caller)[1])
            bl = self._bls(self.B, b, self.brawl_func_size(b) or 4)
            if len(sl) == len(bl):
                votes[bl[n]] += 1
        if votes:
            (c, k), = votes.most_common(1)
            if k * 2 > sum(votes.values()):
                return (c, "caller(%d/%d)" % (k, sum(votes.values())))
        # Address taken by a matched function (callbacks): the same instruction in its Brawl counterpart.
        votes = collections.Counter()
        for user, i in self._code_refs().get(start, []):
            b, how = self.map_func(user, depth + 1)
            if b is None:
                continue
            for j, t in refs(self.B, b, self.brawl_func_size(b) or 4, self.b13, self.b2):
                if j == i and self.B.is_text(t):
                    votes[t] += 1
        if votes:
            (c, k), = votes.most_common(1)
            if k * 2 > sum(votes.values()):
                return (c, "pointer(%d/%d)" % (k, sum(votes.values())))
        # Rewritten in Brawl's SDK revision but calling the same functions: the Brawl function sharing most callees.
        if depth <= 1:
            mine = set()
            for t in self._bls(self.S, start, len(w) * 4):
                b, how = self.map_func(t, depth + 1) if t != start else (None, "")
                if b is not None:
                    mine.add(b)
            if len(mine) >= 2:
                score = collections.Counter()
                for b in mine:
                    for c in self._brawl_callers().get(b, ()):
                        score[c] += 1
                top = score.most_common(2)
                if top and top[0][1] >= max(2, (len(mine) * 3 + 4) // 5) and (len(top) == 1 or top[1][1] < top[0][1]):
                    return (top[0][0], "callees(%d/%d)" % (top[0][1], len(mine)))
        if depth:
            return (None, "unmatched")
        # Different SDK revision: closest whole-body match among the functions between the anchors.
        sk = skeleton(w)
        best, score = None, 0.0
        for c in (between_b if len(between_b) <= 200 else []):
            cs = skeleton(self.B.words(c, self.brawl_func_size(c) or 4))
            if not 0.6 < len(cs) / max(len(sk), 1) < 1.6:
                continue
            r = difflib.SequenceMatcher(None, sk, cs, autojunk=False).ratio()
            if r > score:
                best, score = c, r
        if best is not None and score >= 0.8 and len(sk) >= 6:
            return (best, "similar(%.2f)" % score)
        return (None, "unmatched")

    @staticmethod
    def _bls(dol, a, size):
        return [(a + 4 * i + (sext26(x & 0x03FFFFFC))) & 0xFFFFFFFF for i, x in enumerate(dol.words(a, size)) if x >> 26 == 18 and x & 3 == 1]

    def _code_refs(self):
        if hasattr(self, "_crefs"):
            return self._crefs
        cr = collections.defaultdict(list)
        for k, s in enumerate(self.sstart):
            if not self.S.is_text(s):
                continue
            size = (self.sstart[k + 1] if k + 1 < len(self.sstart) else s + 4) - s
            if size > 0x10000:
                continue
            for i, t in refs(self.S, s, size, self.s13, self.s2):
                if self.S.is_text(t):
                    cr[t].append((s, i))
        self._crefs = cr
        return cr

    def _brawl_callers(self):
        if hasattr(self, "_bcallers"):
            return self._bcallers
        bc = collections.defaultdict(set)
        for a, size, _ in self.bfunc:
            for t in self._bls(self.B, a, size):
                bc[t].add(a)
        self._bcallers = bc
        return bc

    def _callers(self):
        if hasattr(self, "_callmap"):
            return self._callmap
        cm = collections.defaultdict(list)
        for k, s in enumerate(self.sstart):
            if not self.S.is_text(s):
                continue
            size = (self.sstart[k + 1] if k + 1 < len(self.sstart) else s + 4) - s
            if size > 0x10000:
                continue
            for n, t in enumerate(self._bls(self.S, s, size)):
                cm[t].append((s, n))
        self._callmap = cm
        return cm

    def map_code(self, a):
        f = self.spm_func(a)
        if not f:
            return None, "nofunc"
        b, how = self.map_func(f[0])
        if b is None:
            return None, how
        off = a - f[0]
        bs = self.brawl_func_size(b)
        if bs is not None and off >= max(bs, 4):
            return None, "offset-out"
        return b + off, how

    def _ref_index(self):
        if hasattr(self, "_refs"):
            return self._refs
        by_target = collections.defaultdict(list)
        for k, s in enumerate(self.sstart):
            if not self.S.is_text(s):
                continue
            size = (self.sstart[k + 1] if k + 1 < len(self.sstart) else s + 4) - s
            if size > 0x8000:
                continue
            for i, t in refs(self.S, s, size, self.s13, self.s2):
                if 0x80000000 <= t < 0x81800000 and not self.S.is_text(t):
                    by_target[t].append((s, size, i))
        self._refs = (sorted(by_target), by_target)
        return self._refs

    def map_data(self, d):
        """Vote over SPM references to d (or the nearest symbol below it within 0x400)."""
        keys, by_target = self._ref_index()
        k = bisect.bisect_right(keys, d) - 1
        votes = collections.Counter()
        tried = 0
        while k >= 0 and d - keys[k] < 0x400 and tried < 6:
            t = keys[k]
            for s, size, i in by_target[t]:
                b, how = self.map_func(s)
                if b is None:
                    continue
                bsize = self.brawl_func_size(b) or size
                for j, bt in refs(self.B, b, bsize, self.b13, self.b2):
                    if j == i:
                        votes[bt + (d - t)] += 1 if how.startswith("body") else 0.5
            if votes:
                break
            k -= 1
            tried += 1
        if not votes:
            return None, "noref"
        (best, n), = votes.most_common(1)
        total = sum(votes.values())
        return best, "data(%g/%g)" % (n, total)

    def map(self, a):
        if a < 0x80003100:  # low memory globals are the same on every Wii game
            return a, "lowmem"
        if self.S.is_text(a):
            return self.map_code(a)
        return self.map_data(a)


if __name__ == "__main__":
    m = Mapper(*sys.argv[1:5], *(sys.argv[5:6]))
    print("spm r13/r2", hex(m.s13), hex(m.s2), "brawl r13/r2", hex(m.b13), hex(m.b2))
    c = collections.Counter()
    for s in m.sstart:
        if m.S.is_text(s) and s < 0x80300000:
            c[m.map_func(s)[1].split(":")[0]] += 1
    print(c)
