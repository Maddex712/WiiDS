#!/usr/bin/env python3
"""Names analysis/symbols.txt's unnamed functions from the linker map on the disc.

usage: name_from_map.py <debugInfoS.MAP> <symbols.txt>   (rewrites symbols.txt in place)

The disc carries the map of an earlier build (build/debugInfoS.MAP, written by the build's
extraction step or by hand; see docs/dev/symbols.md). Its addresses do not match the retail
DOL, but the link order does, and most functions are the same size. The two function lists
are aligned on size in order. Runs of four or more agreeing sizes are taken; shorter ones
only where they keep a neighbouring long run's index offset (chance agreements in a stretch
the alignment has wrong do not); and between two long runs at the same offset, the functions
in between whose sizes are within a quarter of each other. Only `fn_` names are replaced: what dtk recognised stays.
"""
import difflib, re, sys

def map_functions(path):
    lines = open(path, encoding='latin-1').read().split('\n')
    start = lines.index('.text section layout') + 4
    out = []
    for l in lines[start:]:
        if l.endswith('section layout'): break
        t = l.split()
        if len(t) >= 5 and t[3] != '1' and re.fullmatch(r'[0-9a-f]{8}', t[0]):
            out.append((t[4], int(t[1], 16)))
    return out

def main():
    mp, sp = sys.argv[1:3]
    m = map_functions(mp)
    text = open(sp).read().split('\n')
    r = []
    for i, l in enumerate(text):
        mm = re.match(r'(\S+) = \.text:0x([0-9A-F]+); // type:function size:0x([0-9A-F]+)', l)
        if mm: r.append((mm[1], int(mm[2], 16), int(mm[3], 16), i))
    r.sort(key=lambda x: x[1])
    sm = difflib.SequenceMatcher(None, [x[1] for x in m], [x[2] for x in r], autojunk=False)
    blocks = [b for b in sm.get_matching_blocks() if b.size]
    # A true match keeps the index offset between the lists (a - b) of the long runs
    # around it; two or three sizes agreeing by chance in a stretch the alignment has
    # wrong do not. Short runs are taken only at a long neighbour's offset, and a gap only
    # between two long runs at one offset (a stretch where sizes differ but nothing was
    # added or removed).
    LONG = 4
    longs = [b for b in blocks if b.size >= LONG]
    pairs = []
    for i, (a, b, n) in enumerate(blocks):
        if n < LONG:
            prev = next((x for x in reversed(blocks[:i]) if x.size >= LONG), None)
            nxt = next((x for x in blocks[i + 1:] if x.size >= LONG), None)
            if not any(x and x.a - x.b == a - b for x in (prev, nxt)): continue
        for k in range(n): pairs.append((m[a + k], r[b + k]))
    for x, y in zip(longs, longs[1:]):
        if x.a - x.b != y.a - y.b: continue
        for k in range(x.size, y.a - x.a):
            ma, rb = m[x.a + k], r[x.b + k]
            if abs(ma[1] - rb[2]) <= max(ma[1], rb[2]) // 4: pairs.append((ma, rb))
    taken = set()
    named = 0
    used = {x[0] for x in r}
    for (mn, _), (rn, ra, rs, line) in pairs:
        if not rn.startswith('fn_') or mn in taken or mn in used or not re.fullmatch(r'[\w@$:<>,.~*&]+', mn): continue
        taken.add(mn)
        text[line] = text[line].replace(rn + ' = ', mn + ' = ', 1)
        named += 1
    open(sp, 'w').write('\n'.join(text))
    print(f'{named} functions named ({len(pairs)} paired of {len(r)})', file=sys.stderr)

if __name__ == '__main__':
    main()
