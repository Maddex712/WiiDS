#!/usr/bin/env python3
"""Compare a retail dolphin-oracle trace with a native sms-port trace.

  tools/trace_compare.py RETAIL_TRACE NATIVE_TRACE [--names RANGES.json] [options]

Both traces use dolphin-oracle's memtrace format; the native one (from
platform/trace) carries a byte-order layout per range ("# range <label> ...
layout=<runs>"), which says where the integers, floats and pointers are.
Ranges are matched by label and fields by number (the native trace is already
numbered in retail fields, see SMS_TRACE_SYNC), or by poll count with
--align polls.

A difference is ignored when it is
  * inside a pointer member (layout kind p), or a 4-byte word whose retail
    value points into MEM1 (0x80000000..0x817fffff) and whose native value is
    non-zero (unless --strict-pointers);
  * inside a float member and within --ftol (absolute) or --frel (relative).

Reports, per label, the first field and offset that differ (with the member
name from --names, the resolver's JSON), the earliest overall, and a timing
hint (whether retail matches that native value a few fields earlier/later).
Exit status 0 when nothing differs.
"""
import argparse
import collections
import json
import math
import re
import struct
import sys


def read_trace(path, want=None):
    fields = collections.OrderedDict()
    layouts = {}
    cur = None
    with open(path) as f:
        for line in f:
            if line.startswith('F '):
                parts = line.split()
                info = dict(p.split('=', 1) for p in parts[2:] if '=' in p)
                cur = fields[int(parts[1])] = {'F': info, 'D': {}}
            elif line.startswith('D ') and cur is not None:
                _, label, addr, data = line.rstrip('\n').split(' ', 3)
                if want is None or want(label):
                    cur['D'][label] = (None if addr == '-' else int(addr, 16), data)
            elif line.startswith('# range '):
                m = re.search(r'^# range (\S+) .*layout=(\S+)', line)
                if m:
                    layouts[m.group(1)] = m.group(2)
    return fields, layouts


def parse_layout(text):
    """[(offset, width, kind)] for every member of a range."""
    out, pos = [], 0
    for tok in text.split(','):
        m = re.match(r'^(\d+)([ifpb])(?:x(\d+))?$', tok)
        if not m:
            continue
        w, k, c = int(m.group(1)), m.group(2), int(m.group(3) or 1)
        if k == 'b':
            out.append((pos, w * c, 'b'))
            pos += w * c
        else:
            for _ in range(c):
                out.append((pos, w, k))
                pos += w
    return out


class Label:
    def __init__(self, name, layout, names):
        self.name = name
        self.members = parse_layout(layout) if layout else []
        self.names = {}
        for off, w, k, nm in (names or {}).get('fields', []):
            self.names[off] = nm

    def member_at(self, off):
        for mo, w, k in self.members:
            if mo <= off < mo + w:
                return mo, w, k
        return (off - off % 4, 4, 'i') if not self.members else (off, 1, 'b')

    def describe(self, off):
        mo, w, k = self.member_at(off)
        nm = self.names.get(mo)
        return '%s+0x%x (%s%d%s)' % (self.name, off, {'i': 'int', 'f': 'float', 'p': 'ptr', 'b': 'bytes'}[k], w,
                                     ', ' + nm if nm else '')


def first_diff(lab, a, b, args):
    """Offset of the first real difference between retail entry a and native entry b, or None."""
    (addr_a, da), (addr_b, db) = a, b
    if da == db:
        return None
    if addr_a is None or addr_b is None:
        return None if (addr_a is None and addr_b is None) else 'presence'
    if da.startswith('blocks=') or db.startswith('blocks='):
        return 'hashed'
    ba, bb = bytes.fromhex(da), bytes.fromhex(db)
    n = min(len(ba), len(bb))
    off = 0
    while off < n:
        mo, w, k = lab.member_at(off)
        end = min(mo + w, n)
        start = max(mo, off)
        seg_a, seg_b = ba[mo:mo + w], bb[mo:mo + w]
        if seg_a != seg_b:
            same = False
            if k == 'p':
                same = True
            elif k == 'f' and w in (4, 8) and len(seg_a) == w and len(seg_b) == w:
                fmt = '>f' if w == 4 else '>d'
                fa, fb = struct.unpack(fmt, seg_a)[0], struct.unpack(fmt, seg_b)[0]
                same = (math.isnan(fa) and math.isnan(fb)) or abs(fa - fb) <= max(args.ftol, args.frel * abs(fa))
            elif w == 4 and len(seg_a) == 4 and not args.strict_pointers:
                va, vb = struct.unpack('>I', seg_a)[0], struct.unpack('>I', seg_b)[0]
                same = 0x80000000 <= va < 0x81800000 and vb != 0
            if not same:
                for i in range(start, end):
                    if ba[i] != bb[i]:
                        return i
                return start
        off = end if end > off else off + 1
    return None if len(ba) == len(bb) else n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('retail')
    ap.add_argument('native')
    ap.add_argument('--names', help='resolver JSON (TRACE_OUT.json) for member names')
    ap.add_argument('--align', choices=['field', 'polls'], default='field')
    ap.add_argument('--shift', type=int, default=0, help='pair native field f with retail field f+SHIFT')
    ap.add_argument('--resync', help='LABEL+HEXOFF:WIDTH=VALUE: shift native fields so that the first field where '
                    'this holds lines up in both traces (e.g. app+0xe:4=0x0f000000, the title scene)')
    ap.add_argument('--ignore', help='regex of labels to skip')
    ap.add_argument('--only', help='regex of labels to compare')
    ap.add_argument('--from', dest='first', type=int, default=0, help='first field compared')
    ap.add_argument('--to', dest='last', type=int, default=1 << 30, help='last field compared')
    ap.add_argument('--ftol', type=float, default=1e-3, help='absolute float tolerance')
    ap.add_argument('--frel', type=float, default=1e-5, help='relative float tolerance')
    ap.add_argument('--strict-pointers', action='store_true', help='compare pointer-looking words too')
    ap.add_argument('--lag', type=int, default=30, help='fields searched for a timing hint')
    ap.add_argument('--top', type=int, default=30)
    args = ap.parse_args()

    ign = re.compile(args.ignore) if args.ignore else None
    only = re.compile(args.only) if args.only else None
    want = lambda l: not ((ign and ign.search(l)) or (only and not only.search(l)))
    N, layouts = read_trace(args.native, want)
    A, _ = read_trace(args.retail, want)
    names = json.load(open(args.names)) if args.names else {}
    labels = {}

    def lab(name):
        if name not in labels:
            labels[name] = Label(name, layouts.get(name), names.get(name))
        return labels[name]

    shift = args.shift
    if args.resync:
        m = re.match(r'^([^+]+)\+(\w+):(\d+)=(\w+)$', args.resync)
        if not m:
            sys.exit('bad --resync %s' % args.resync)
        rl, ro, rw, rv = m.group(1), int(m.group(2), 0), int(m.group(3)), int(m.group(4), 0)

        def when(T):
            for f, e in T.items():
                d = e['D'].get(rl)
                if d and d[0] is not None and len(d[1]) >= 2 * (ro + rw) and int(d[1][2 * ro:2 * (ro + rw)], 16) == rv:
                    return f
            return None
        fa, fn = when(A), when(N)
        if fa is None or fn is None:
            sys.exit('--resync: condition never holds in %s' % ('retail' if fa is None else 'native'))
        shift = fa - fn
        print('resync %s: retail field %d = native field %d (native shifted by %+d)' % (args.resync, fa, fn, shift))
    # pair native fields with retail fields
    if args.align == 'polls':
        by_polls = {}
        for f, e in A.items():
            by_polls.setdefault(e['F'].get('polls'), f)
        pairs = [(by_polls[e['F'].get('polls')], f) for f, e in N.items() if e['F'].get('polls') in by_polls]
    else:
        pairs = [(f + shift, f) for f in N if f + shift in A]
    pairs = [(fa, fn) for fa, fn in pairs if args.first <= fn <= args.last]
    if not pairs:
        sys.exit('no fields in common (retail %d..%d, native %d..%d)' % (
            min(A) if A else -1, max(A) if A else -1, min(N) if N else -1, max(N) if N else -1))
    print('retail: %s (%d fields)\nnative: %s (%d fields, %d with layouts)\naligned by %s: %d field pairs, '
          'native fields %d..%d' % (args.retail, len(A), args.native, len(N), len(layouts), args.align, len(pairs),
                                    pairs[0][1], pairs[-1][1]))

    first = collections.OrderedDict()
    compared = set()
    for fa, fn in pairs:
        da, dn = A[fa]['D'], N[fn]['D']
        for label, en in dn.items():
            if label in first or label not in da:
                continue
            compared.add(label)
            off = first_diff(lab(label), da[label], en, args)
            if off is not None:
                first[label] = (fa, fn, off)
    missing = sorted(set(l for e in N.values() for l in e['D']) - set(l for e in A.values() for l in e['D']))
    print('ranges compared: %d%s' % (len(compared), ('; native-only labels: ' + ' '.join(missing)) if missing else ''))
    if not first:
        print('no differences in %d field pairs' % len(pairs))
        return 0

    order = sorted(first.items(), key=lambda kv: kv[1][1])
    label, (fa, fn, off) = order[0]
    L = lab(label)
    print('\nFIRST DIVERGENCE: native field %d (retail field %d), %s' % (
        fn, fa, L.describe(off) if isinstance(off, int) else '%s: %s' % (label, off)))
    ea, en = A[fa]['D'][label], N[fn]['D'][label]
    if isinstance(off, int):
        mo, w, k = L.member_at(off)
        s = max(0, mo - 8)
        ra, rn = bytes.fromhex(ea[1]), bytes.fromhex(en[1])
        print('  retail @%08x: %s' % (ea[0] + s, ra[s:mo + w + 8].hex(' ')))
        print('  native @%08x: %s' % (en[0] + s, rn[s:mo + w + 8].hex(' ')))
    else:
        print('  retail: %s\n  native: %s' % (ea[1][:64], en[1][:64]))
    print('  polls: retail %s, native %s; native retrace %s' % (
        A[fa]['F'].get('polls'), N[fn]['F'].get('polls'), N[fn]['F'].get('retrace')))
    for d in range(1, args.lag + 1):
        hit = None
        for g, sign in ((fa + d, 'later'), (fa - d, 'earlier')):
            if g in A and label in A[g]['D'] and first_diff(L, A[g]['D'][label], en, args) is None:
                hit = (g, sign)
                break
        if hit:
            print('  timing hint: this native value is retail\'s %d field(s) %s (retail field %d)' % (d, hit[1], hit[0]))
            break
    else:
        print('  no retail field within +-%d has this native value' % args.lag)

    print('\nfirst divergence per label (earliest %d of %d):' % (min(args.top, len(order)), len(order)))
    print('  %7s %7s  %s' % ('native', 'retail', 'where'))
    for label, (fa, fn, off) in order[:args.top]:
        print('  %7d %7d  %s' % (fn, fa, lab(label).describe(off) if isinstance(off, int) else '%s: %s' % (label, off)))
    return 1


if __name__ == '__main__':
    sys.exit(main())
