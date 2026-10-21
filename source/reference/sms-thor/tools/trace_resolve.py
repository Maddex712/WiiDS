# Resolve dolphin-oracle symbolic trace ranges (ranges/*.txt) against the
# native port binary, with byte-order layouts taken from its DWARF types.
# Runs inside gdb (static, the program is not started):
#
#   TRACE_RANGES=$DOLPHIN_ORACLE/ranges/play.txt TRACE_OUT=build/trace.ranges \
#       gdb -batch -x tools/trace_resolve.py build/linux-32/sms
#
# Several range files: TRACE_RANGES=a.txt:b.txt. Writes TRACE_OUT (read by
# platform/trace, SMS_TRACE_RANGES) and TRACE_OUT.json (field names per label,
# used by tools/trace_compare.py).
#
# Range syntax (as dolphin-oracle): <start> <length|size> <label>
#   start: symbol[+off] | 0xADDR | *symbol[+off] | *(symbol+n)[+off], nesting allowed:
#          *(*gpCardLoad+0x298)+0x10. Offsets are GameCube offsets.
# Output start: @0xSTATIC (host symbol) or 0xMEM1, then /0xOFF per dereference
# (read the pointer at the current address, add OFF).
# CodeWarrior-mangled static members (mPadStatus__10JUTGamePad) are looked up
# as JUTGamePad::mPadStatus. 0xADDR in MEM1 is used as is (the port maps MEM1
# at 0x80000000); its layout is unknown unless a "# type <label> <Type>" line
# gives one.
#
# Layout tokens, covering the range from offset 0: <width><kind>[x<count>]
#   kind: i integer, f float, p pointer, b raw bytes (no swap)
import json
import os
import re

import gdb

MEM1_LO, MEM1_HI = 0x80000000, 0x81800000


def cw_demangle(name):
    """mPadStatus__10JUTGamePad -> JUTGamePad::mPadStatus (static members only)."""
    m = re.match(r'^(\w+?)__(\d+)(\w+)$', name)
    if m and int(m.group(2)) == len(m.group(3)):
        return '%s::%s' % (m.group(3), m.group(1))
    return name


def lookup(sym):
    """(static address, gdb.Type) of a global, or None."""
    for cand in (sym, cw_demangle(sym)):
        try:
            v = gdb.parse_and_eval(cand)
            addr = int(gdb.parse_and_eval('&%s' % cand).cast(gdb.lookup_type('unsigned long')))
            return addr, v.type
        except gdb.error:
            continue
    return None


def layout_of(t, base=0, name='', out=None, limit=1 << 16):
    """Flatten a type into (offset, width, kind, name) scalars."""
    if out is None:
        out = []
    if base >= limit:
        return out
    t = t.strip_typedefs()
    code = t.code
    if code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        for f in t.fields():
            if not hasattr(f, 'bitpos') or f.bitpos is None:
                continue  # static member
            if f.bitsize:
                continue  # bitfield: left as raw bytes
            fname = f.name or ('<base>' if f.is_base_class else '?')
            layout_of(f.type, base + f.bitpos // 8, (name + '.' if name else '') + fname, out, limit)
            if code == gdb.TYPE_CODE_UNION:
                break
    elif code == gdb.TYPE_CODE_ARRAY:
        et = t.target()
        n = t.sizeof // max(et.sizeof, 1)
        for i in range(n):
            if base + i * et.sizeof >= limit:
                break
            layout_of(et, base + i * et.sizeof, '%s[%d]' % (name, i), out, limit)
    elif code in (gdb.TYPE_CODE_PTR, gdb.TYPE_CODE_REF, gdb.TYPE_CODE_METHODPTR, gdb.TYPE_CODE_MEMBERPTR):
        out.append((base, t.sizeof, 'p', name))
    elif code == gdb.TYPE_CODE_FLT:
        out.append((base, t.sizeof, 'f', name))
    elif code in (gdb.TYPE_CODE_INT, gdb.TYPE_CODE_ENUM, gdb.TYPE_CODE_BOOL, gdb.TYPE_CODE_CHAR):
        out.append((base, t.sizeof, 'i' if t.sizeof > 1 else 'b', name))
    else:
        out.append((base, t.sizeof, 'b', name))
    return out


def gc_layout(t, native_base=0, gc_base=0, name='', out=None):
    """Lay a type out the way MWCC (PowerPC EABI) does and pair every scalar's
    GameCube offset with its native offset.  Differences from the i386 g++ ABI
    that matter here: base-class tail padding is never reused (g++ puts a
    derived class's first member into it), and 8-byte scalars align to 8.
    Returns (size, align); appends (gc_off, native_off, width, kind, name)."""
    t = t.strip_typedefs()
    code = t.code
    if code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        off, align, size = 0, 1, 0
        for f in t.fields():
            if not hasattr(f, 'bitpos') or f.bitpos is None:
                continue
            nat = native_base + f.bitpos // 8
            fname = (name + '.' if name else '') + (f.name or ('<base>' if f.is_base_class else '?'))
            if f.bitsize:
                # bitfield: keep its storage unit where g++ put it, relative to the class
                if out is not None:
                    out.append((gc_base + f.bitpos // 8, nat, 1, 'b', fname))
                continue
            fsize, falign = gc_layout(f.type, 0, 0, '', None)
            if code == gdb.TYPE_CODE_UNION:
                gc_layout(f.type, nat, gc_base, fname, out)
                size, align = max(size, fsize), max(align, falign)
                continue
            off = (off + falign - 1) // falign * falign
            gc_layout(f.type, nat, gc_base + off, fname, out)
            off += fsize
            align = max(align, falign)
        size = max(size, off)
        size = max(1, (size + align - 1) // align * align) if (size or t.fields()) else 0
        return size, align
    if code == gdb.TYPE_CODE_ARRAY:
        et = t.target()
        esize, ealign = gc_layout(et, 0, 0, '', None)
        n = t.sizeof // max(et.sizeof, 1)
        if out is not None:
            for i in range(n):
                gc_layout(et, native_base + i * et.sizeof, gc_base + i * esize, '%s[%d]' % (name, i), out)
        return esize * n, ealign
    w = t.sizeof
    if code in (gdb.TYPE_CODE_PTR, gdb.TYPE_CODE_REF, gdb.TYPE_CODE_METHODPTR, gdb.TYPE_CODE_MEMBERPTR):
        k = 'p'
    elif code == gdb.TYPE_CODE_FLT:
        k = 'f'
    elif code in (gdb.TYPE_CODE_INT, gdb.TYPE_CODE_ENUM, gdb.TYPE_CODE_BOOL, gdb.TYPE_CODE_CHAR):
        k = 'i' if w > 1 else 'b'
    else:
        k = 'b'
    if out is not None:
        out.append((gc_base, native_base, w, k, name))
    return w, min(max(w, 1), 8)


def map_tokens(pairs):
    """Copy runs gcoff:natoff:<w><k>[x<n>] (consecutive members merged)."""
    runs = []
    for g, n, w, k in sorted(pairs):
        if w == 1:
            k = 'b'
        if runs:
            rg, rn, rw, rk, rc = runs[-1]
            if rw == w and rk == k and g == rg + rw * rc and n == rn + rw * rc:
                runs[-1][4] += 1
                continue
        runs.append([g, n, w, k, 1])
    return ','.join('%x:%x:%d%s%s' % (g, n, w, k, ('x%d' % c) if c > 1 else '') for g, n, w, k, c in runs)


def tokens(scalars, length):
    """Compact layout string covering [0, length)."""
    runs = []
    pos = 0
    for off, w, k, _ in sorted(scalars):
        if off < pos or off + w > length:
            continue
        if off > pos:
            runs.append([1, 'b', off - pos])
        if w == 1 and k != 'b':
            k = 'b'
        if runs and runs[-1][0] == w and runs[-1][1] == k:
            runs[-1][2] += 1
        else:
            runs.append([w, k, 1])
        pos = off + w
    if pos < length:
        runs.append([1, 'b', length - pos])
    # merge adjacent byte runs
    merged = []
    for r in runs:
        if merged and r[1] == 'b' and merged[-1][1] == 'b':
            merged[-1][2] += r[0] * r[2]
            merged[-1][0] = 1
        else:
            merged.append(r if r[1] != 'b' else [1, 'b', r[0] * r[2]])
    return ','.join('%d%s%s' % (w, k, ('x%d' % c) if c > 1 else '') for w, k, c in merged)


def member_type_at(t, off):
    """gdb.Type of the scalar/pointer member at byte offset `off` of type t, or None."""
    t = t.strip_typedefs()
    if t.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        for f in t.fields():
            if getattr(f, 'bitpos', None) is None or f.bitsize:
                continue
            fo = f.bitpos // 8
            if fo <= off < fo + f.type.sizeof:
                return member_type_at(f.type, off - fo)
        return None
    if t.code == gdb.TYPE_CODE_ARRAY:
        et = t.target()
        return member_type_at(et, off % et.sizeof) if et.sizeof else None
    return t if off == 0 else None


def parse_start(text):
    """Range start -> (base, ops). base: symbol name or int; ops: ('add', n) / ('deref',).
    Grammar (dolphin-oracle's, plus nesting):  E := sym[+off] | 0xADDR[+off] | *E' [+off] | *(E)[+off]
    where '*sym+off' follows the pointer stored at sym and then adds off, and
    '*(E)' follows the pointer stored at E, so '*(*gpCardLoad+0x298)' reads the
    pointer at offset 0x298 of the object gpCardLoad points to."""
    text = text.strip()
    if text.startswith('*('):
        depth, i = 0, 1
        for i in range(1, len(text)):
            depth += {'(': 1, ')': -1}.get(text[i], 0)
            if depth == 0:
                break
        base, ops = parse_start(text[2:i])
        ops = ops + [('deref',)]
        rest = text[i + 1:]
    elif text.startswith('*'):
        head, _, rest = text[1:].partition('+')
        base, ops = parse_start(head)
        ops = ops + [('deref',)]
        rest = ('+' + rest) if rest else ''
    else:
        head, _, rest = text.partition('+')
        base = int(head, 16) if re.match(r'^0x[0-9a-fA-F]+$', head) else head
        ops = []
        rest = ('+' + rest) if rest else ''
    for part in [x for x in rest.split('+') if x]:
        ops.append(('add', int(part, 0)))
    return base, ops


def gc_member(t, gc_off):
    """(native offset, gdb.Type) of the member at GameCube offset gc_off of type t."""
    if t is None:
        return gc_off, None
    full = []
    gc_layout(t, 0, 0, '', full)
    for g, nat, w, k, nm in full:
        if g == gc_off:
            return nat, member_type_at(t, nat)
    return gc_off, None


def resolve(line, types):
    words = line.split('#', 1)[0].split()
    if len(words) < 3:
        return None
    start, length, label = words[:3]
    try:
        base, ops = parse_start(start)
    except ValueError:
        return label, None, 'unsupported start %s' % start
    if isinstance(base, int):
        if not MEM1_LO <= base < MEM1_HI:
            return label, None, 'absolute address outside MEM1'
        loc, typ = '0x%08x' % base, None
    else:
        hit = lookup(base)
        if not hit:
            return label, None, 'symbol %s not in the port binary' % base
        addr, typ = hit
        loc = '@0x%x' % addr
    # walk: GameCube offsets become native offsets through each type's MWCC layout
    chain = []        # native offsets added after each dereference
    static_add = 0    # native offset added before the first dereference
    pending = 0       # GameCube offset not yet applied
    for op in ops:
        if op[0] == 'add':
            pending += op[1]
            continue
        nat, mtype = gc_member(typ, pending) if pending else (0, typ)
        if chain:
            chain[-1] += nat
        else:
            static_add += nat
        st = mtype.strip_typedefs() if mtype is not None else None
        typ = st.target() if st is not None and st.code == gdb.TYPE_CODE_PTR else None
        chain.append(0)
        pending = 0
    base_off = pending  # the range is this GameCube slice of `typ`
    if label in types:
        try:
            typ = gdb.lookup_type(types[label])
        except gdb.error:
            pass
    if length == 'size':
        if typ is None:
            return label, None, 'size of an untyped range'
        n = gc_layout(typ, 0, 0, '', None)[0] - base_off
    else:
        n = int(length, 0)
    scalars, pairs = [], []
    native_base = base_off
    if typ is not None:
        full = []
        gc_layout(typ, 0, 0, '', full)
        native_base = next((nat for g, nat, w, k, nm in full if g >= base_off), base_off)
        first_g = next((g for g, nat, w, k, nm in full if g >= base_off), base_off)
        native_base -= first_g - base_off
        for g, nat, w, k, nm in full:
            if base_off <= g and g + w <= base_off + n:
                scalars.append((g - base_off, w, k, nm))
                pairs.append((g - base_off, nat - native_base, w, k))
    else:
        scalars = [(o, 4, 'i', '+0x%x' % o) for o in range(0, n - n % 4, 4)]
        pairs = [(o, o, 4, 'i') for o in range(0, n - n % 4, 4)]
    if chain:
        chain[-1] += native_base
    else:
        static_add += native_base
    if loc.startswith('@'):
        loc = '@0x%x' % (int(loc[1:], 16) + static_add)
    else:
        loc = '0x%08x' % (int(loc, 16) + static_add)
    loc += ''.join('/0x%x' % c for c in chain)
    return label, (loc, n, tokens(scalars, n), scalars, str(typ) if typ is not None else '?', map_tokens(pairs)), None


def main():
    if not os.environ.get('TRACE_RANGES'):
        raise SystemExit('set TRACE_RANGES to the oracle range file(s), e.g. $DOLPHIN_ORACLE/ranges/play.txt')
    paths = os.environ['TRACE_RANGES'].split(':')
    out = os.environ.get('TRACE_OUT', 'trace.ranges')
    anchor = lookup('port_trace_anchor')
    lines, types = [], {}
    for p in paths:
        for raw in open(p):
            m = re.match(r'^#\s*type\s+(\S+)\s+(.+?)\s*$', raw)
            if m:
                types[m.group(1)] = m.group(2)
            elif raw.split('#', 1)[0].strip():
                lines.append(raw)
    names = {}
    with open(out, 'w') as f:
        f.write('# sms-port native trace ranges v1 (tools/trace_resolve.py from %s)\n' % ' '.join(paths))
        f.write('# anchor 0x%x\n' % (anchor[0] if anchor else 0))
        for raw in lines:
            r = resolve(raw, types)
            if r is None:
                continue
            label, info, err = r
            if err:
                f.write('# skip %s: %s\n' % (label, err))
                print('trace_resolve: skip %s: %s' % (label, err))
                continue
            loc, n, lay, scalars, tname, cmap = info
            f.write('%s 0x%x %s layout=%s map=%s\n' % (loc, n, label, lay, cmap))
            names[label] = {'type': tname, 'fields': [[o, w, k, nm] for o, w, k, nm in sorted(scalars)]}
            print('trace_resolve: %-16s %-24s 0x%-5x %s' % (label, loc, n, tname))
    json.dump(names, open(out + '.json', 'w'), indent=0)
    if not anchor:
        print('trace_resolve: WARNING: port_trace_anchor not found (platform/trace not linked?)')
    print('trace_resolve: wrote %s' % out)


main()
