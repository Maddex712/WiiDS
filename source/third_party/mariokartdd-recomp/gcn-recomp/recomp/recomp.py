#!/usr/bin/env python3
"""Static recompiler: GameCube main.dol -> C.

Usage: recomp.py <main.dol> <dtk symbols.txt> <outdir> <tables dir>

The tables directory holds the per-game steering files: names.txt, hle.txt,
patches.txt, special_calls.txt, idle.txt and steps.txt (any may be absent).
"""
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from dol import Dol, load_symbols  # noqa: E402
import ppc  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
FUNCS_PER_FILE = 150


def load_kv(path):
    """addr name lines -> {addr: name}"""
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        out[int(parts[0], 16)] = parts[1]
    return out


def load_list(path):
    s = set()
    if os.path.exists(path):
        for line in open(path):
            line = line.split('#', 1)[0].strip()
            if line:
                s.add(line)
    return s


STORE_OPS = {36, 37, 38, 39, 44, 45, 47, 52, 53, 54, 55, 60, 61}
STORE_XO31 = {151, 183, 215, 247, 407, 439, 662, 918, 150, 663, 695, 727, 759, 983, 725, 1014}


def store_ea(i):
    """The C expression for the address a store instruction wrote, valid after the store
    (an update form has already moved its base register onto the address)."""
    op = i >> 26
    a = (i >> 16) & 31
    b = (i >> 11) & 31
    if op in STORE_OPS:
        if op in (37, 39, 45, 53, 55, 61):  # the update forms
            return f'c->r[{a}]'
        if op in (60,):
            return ppc.ea_d(a, ppc.sx12(i))
        return ppc.ea_d(a, ppc.sx16(i))
    if op == 31 and ((i >> 1) & 0x3FF) in STORE_XO31:
        if ((i >> 1) & 0x3FF) in (183, 247, 439, 695, 759):
            return f'c->r[{a}]'
        if ((i >> 1) & 0x3FF) == 725:  # stswi: rB is the byte count
            return ppc.ea_d(a, 0)
        return ppc.ea_x(a, b)
    if op == 4 and ((i >> 1) & 0x3F) in (7, 39):
        return f'c->r[{a}]' if ((i >> 1) & 0x3F) == 39 else ppc.ea_x(a, b)
    return '0u'


LOAD_OPS = {32, 33, 34, 35, 40, 41, 42, 43, 48, 49, 50, 51, 56, 57}
LOAD_XO31 = {23, 55, 87, 119, 279, 311, 343, 375, 535, 567, 599, 631, 534, 790, 20}


def load_ea(i):
    """The C expression for the address a load instruction reads, valid before the load
    (it may overwrite its own base register, and an update form moves it)."""
    op = i >> 26
    a = (i >> 16) & 31
    b = (i >> 11) & 31
    if op in LOAD_OPS:
        if op in (56, 57):
            return ppc.ea_d(a, ppc.sx12(i))
        return ppc.ea_d(a, ppc.sx16(i))
    if op == 31 and ((i >> 1) & 0x3FF) in LOAD_XO31:
        return ppc.ea_x(a, b)
    if op == 4 and ((i >> 1) & 0x3F) in (6, 38):
        return ppc.ea_x(a, b)
    return '0u'


def is_load(i):
    op = i >> 26
    if op in LOAD_OPS:
        return True
    if op == 31 and ((i >> 1) & 0x3FF) in LOAD_XO31:
        return True
    if op == 4 and ((i >> 1) & 0x3F) in (6, 38):
        return True
    return False


def is_store(i):
    op = i >> 26
    if op in STORE_OPS:
        return True
    if op == 31 and ((i >> 1) & 0x3FF) in STORE_XO31:
        return True
    if op == 4 and ((i >> 1) & 0x3F) in (7, 39):
        return True
    return False


class Func:
    def __init__(self, addr, size, name):
        self.addr = addr
        self.size = size
        self.end = addr + size
        self.name = name
        self.entries = [addr]  # extra entry points (mid-function)
        self.labels = set()
        self.jt_targets = set()
        self.hle = False


def load_modules(path):
    """<dol>.modules: `canon size entry name` per line, hex. Code the game loads at run time
    from its data files, laid out by the game project's DOL fixup at canonical addresses;
    the runtime binds each to where the game put it (runtime/modules.cpp)."""
    out = []
    if not os.path.exists(path):
        return out
    for line in open(path):
        parts = line.split()
        if len(parts) >= 4:
            out.append((int(parts[0], 16), int(parts[1], 16), int(parts[2], 16), parts[3]))
    return out


def load_rels(dol_path):
    """<dol>.rels and <dol>.relocs, from recomp/rel.py: the relocatable modules linked at
    canonical addresses, and the instructions whose immediate holds a relocated address.
    Returns ([(id, name, [(section, canon, size, exec)])], {addr: (kind, module, section, addend)})."""
    rels, relocs = [], {}
    if os.path.exists(dol_path + '.rels'):
        for line in open(dol_path + '.rels'):
            parts = line.split()
            if parts and parts[0] == 'module':
                rels.append((int(parts[2]), parts[3], []))
            elif parts and parts[0] == 'section':
                rels[-1][2].append((int(parts[1]), int(parts[2], 16), int(parts[3], 16), parts[4] == '1'))
        for line in open(dol_path + '.relocs'):
            a, kind, m, sec, addend = line.split()
            relocs[int(a, 16)] = (kind, int(m), int(sec), int(addend, 16))
    return rels, relocs


# A relocated instruction is translated with this in its immediate field, and the
# translation's spelling of it replaced with the expression for the run-time value.
RELOC_MAGIC = 0x1234


def translate_reloc(pc, i, reloc):
    kind, m, sec, addend = reloc
    i = (i & 0xFFFF0000) | RELOC_MAGIC
    code = ppc.translate(pc, i)
    if is_store(i):
        code += f' WATCH_STORE(0x{pc:08X}u, {store_ea(i)});'
    if is_load(i):
        code = f'WATCH_LOAD(0x{pc:08X}u, {load_ea(i)}); ' + code
    args = f'{m}, {sec}, 0x{addend:X}u'
    signed = f'(uint32_t)(int32_t)(int16_t)REL_LO({args})'
    op = i >> 26
    if kind in ('HA', 'HI'):
        forms = {f'0x{RELOC_MAGIC << 16:X}u': f'((uint32_t)REL_{kind}({args}) << 16)'}
    elif op in (24, 25, 26, 27, 28, 29, 10):  # ori oris xori xoris andi. andis. cmpli: unsigned
        forms = {f'0x{RELOC_MAGIC:X}u': f'(uint32_t)REL_LO({args})'}
    else:
        forms = {f'(uint32_t){RELOC_MAGIC}': signed, f'0x{RELOC_MAGIC:X}u': signed,
                 f'{RELOC_MAGIC} ': f'(int16_t)REL_LO({args}) '}
    n = 0
    for k, v in forms.items():
        n += code.count(k)
        code = code.replace(k, v)
    if not n:
        sys.exit(f'error: {pc:08X}: no immediate to relocate in {code}')
    return code


def main():
    dol_path, sym_path, outdir, tables = sys.argv[1:5]
    dol = Dol(dol_path)
    modules = load_modules(dol_path + '.modules')
    rels, relocs = load_rels(dol_path)
    rel_text = [(c, sz) for _, _, secs in rels for _, c, sz, exe in secs if exe]

    def in_module(addr):
        return any(c <= addr < c + sz for c, sz, _, _ in modules)

    def in_rel(addr):
        return any(c <= addr < c + sz for c, sz in rel_text)
    syms = load_symbols(sym_path)
    if os.path.exists(dol_path + '.syms'):
        syms += load_symbols(dol_path + '.syms')
    names = load_kv(os.path.join(tables, 'names.txt'))
    hle = load_list(os.path.join(tables, 'hle.txt'))

    funcs = []
    for s in syms:
        if s['type'] == 'function' and s['size']:
            name = names.get(s['addr'], s['name'])
            funcs.append(Func(s['addr'], s['size'], name))
    funcs.sort(key=lambda f: f.addr)
    by_addr = {f.addr: f for f in funcs}
    starts = [f.addr for f in funcs]

    import bisect

    def func_containing(addr):
        k = bisect.bisect_right(starts, addr) - 1
        if k >= 0 and funcs[k].addr <= addr < funcs[k].end:
            return funcs[k]
        return None

    # Jump tables: dtk-identified objects in data sections.
    jt_count = 0
    for s in syms:
        if s['name'].startswith('jumptable_') and s['size']:
            for off in range(0, s['size'], 4):
                tgt = dol.read32(s['addr'] + off)
                f = func_containing(tgt)
                if f:
                    f.labels.add(tgt)
                    f.jt_targets.add(tgt)
                    jt_count += 1

    # Extra indirect-branch targets that dtk's jump table analysis missed:
    #  (a) words in data sections that point into a function body,
    #  (b) addresses materialised in code with lis + addi/ori.
    extra_targets = 0

    def add_indirect_target(t):
        nonlocal extra_targets
        if t & 3:
            return
        f = func_containing(t)
        if f and t != f.addr and t not in f.jt_targets:
            f.jt_targets.add(t)
            f.labels.add(t)
            extra_targets += 1

    for a, size, off, is_text in dol.sections:
        if is_text:
            continue
        for k in range(0, size - 3, 4):
            add_indirect_target(struct.unpack_from('>I', dol.data, off + k)[0])
    for f in funcs:
        hi = {}
        for pc in range(f.addr, f.end, 4):
            i = dol.read32(pc)
            op = i >> 26
            d, a_ = (i >> 21) & 31, (i >> 16) & 31
            if op == 15 and a_ == 0:  # lis
                hi[d] = (i & 0xFFFF) << 16
                continue
            if op in (14, 24) and a_ in hi:  # addi / ori
                lo = i & 0xFFFF
                v = (hi[a_] + (lo - 0x10000 if (op == 14 and lo & 0x8000) else lo)) & 0xFFFFFFFF if op == 14 else hi[a_] | lo
                add_indirect_target(v)
            if op not in (36, 37, 38, 39, 44, 45, 47, 52, 53, 54, 55) and d in hi:
                del hi[d]

    # Pass 1: find labels, mid-function entries, stats.
    stats = collections.Counter()
    unimpl = collections.Counter()
    for f in funcs:
        for pc in range(f.addr, f.end, 4):
            i = dol.read32(pc)
            br = ppc.decode_branch(pc, i)
            if br is None or br.target is None:
                continue
            t = br.target
            if f.addr <= t < f.end and not br.link:
                f.labels.add(t)
            elif t not in by_addr:
                g = func_containing(t)
                if g is None:
                    stats['branch_outside_code'] += 1
                    print(f'warning: {pc:08X} branches to non-code {t:08X}', file=sys.stderr)
                    continue
                if g is f and br.link:
                    stats['local_bl'] += 1
                    print(f'warning: {pc:08X} bl to own body {t:08X}', file=sys.stderr)
                if t not in g.entries:
                    g.entries.append(t)
                    g.labels.add(t)
                    stats['mid_entries'] += 1

    for f in funcs:
        if f.name in hle:
            f.hle = True
        # The SDK's module linker, when there are modules: the runtime is told where each
        # section went (runtime/rels.cpp) after a link and before an unlink.
        f.rel_hook = None
        if rels and not f.hle and len(f.entries) == 1:
            if f.name in ('OSLink', 'OSLinkFixed'):
                f.rel_hook = f'uint32_t m = c->r[3]; rels_linking(m); orig_{f.name}(c); if (c->r[3]) rels_linked(m);'
            elif f.name == 'OSUnlink':
                f.rel_hook = f'rels_unlinked(c->r[3]); orig_{f.name}(c);'

    os.makedirs(outdir, exist_ok=True)

    # Every configure runs this, and a file rewritten with what it already held is still
    # newer than its object: after any change to a project's CMake, the whole game would
    # compile again. Only a file whose text differs is written.
    def write(fn, text):
        path = os.path.join(outdir, fn)
        try:
            if open(path).read() == text:
                return
        except OSError:
            pass
        open(path, 'w').write(text)

    def cname(addr):
        return f'fn_{addr:08X}'

    def emit_call(pc, t, link):
        s = ''
        if link:
            s += f'c->lr = 0x{pc + 4:08X}u; '
        if t in by_addr or func_containing(t):
            s += f'{cname(t)}(c);'
        else:
            s += f'call_indirect(c, 0x{t:08X}u);'
        return s

    special_calls = load_kv(os.path.join(tables, 'special_calls.txt'))
    # idle.txt: addresses of backward branches that are the OS's "nothing to run" spin
    # (the SDK scheduler's wait for RunQueueBitmap). They poll through IDLE_CHECK, which
    # lets the runtime skip guest time forward to the next event instead of spinning.
    idle_loops = set(load_kv(os.path.join(tables, 'idle.txt')).keys())
    # patches.txt: "ADDR  <C statement>" replaces the instruction at ADDR.
    patches = {}
    pp = os.path.join(tables, 'patches.txt')
    if os.path.exists(pp):
        for line in open(pp):
            line = line.split('#', 1)[0].rstrip()
            if line.strip():
                a, code = line.split(None, 1)
                patches[int(a, 16)] = code

    # steps.txt: "ADDR  A|B|C [power]" marks an instruction as a per-frame step whose named
    # operand carries the value from the previous frame; it is emitted scaled by the
    # runtime's step scale raised to the power (1 when absent: a rate; 2 a force, which a
    # Verlet integrator adds to a position; -1 a derived rate to convert back to the game's
    # units), see ppc.translate_step and runtime/step.cpp. "ADDR R power" scales the whole
    # result instead. A patch at the same address wins.
    steps = {}
    path = os.path.join(tables, 'steps.txt')
    if os.path.exists(path):
        for line in open(path):
            line = line.split('#', 1)[0].strip()
            if line:
                parts = line.split()
                steps[int(parts[0], 16)] = (parts[1], float(parts[2]) if len(parts) > 2 else 1.0)
    for a in steps:
        if a in patches:
            print(f'warning: steps.txt {a:08X} is also patched; the patch wins', file=sys.stderr)

    def emit_body(f, out):
        in_range = lambda t: f.addr <= t < f.end
        jt_sorted = sorted(f.jt_targets)
        last_uncond = False
        for pc in range(f.addr, f.end, 4):
            i = dol.read32(pc)
            if pc in f.labels:
                out.append(f'L_{pc:08X}:')
            if pc in patches:
                out.append(f'\t/* {pc:08X} {i:08X} PATCHED */ {patches[pc]}')
                last_uncond = False
                continue
            if pc in steps:
                out.append(f'\t/* {pc:08X} {i:08X} STEP */ {ppc.translate_step(pc, i, *steps[pc])}')
                last_uncond = False
                continue
            br = ppc.decode_branch(pc, i)
            last_uncond = False
            if br is None and pc in relocs:
                out.append(f'\t/* {pc:08X} {i:08X} RELOC */ {translate_reloc(pc, i, relocs[pc])}')
                continue
            if br is None:
                try:
                    code = ppc.translate(pc, i)
                except ppc.Unimpl as e:
                    unimpl[str(e)] += 1
                    code = f'unimpl(c, 0x{pc:08X}u, 0x{i:08X}u); /* {e} */'
                if (i >> 26) == 19 and ((i >> 1) & 0x3FF) == 50:
                    # rfi: an unconditional transfer to SRR0. See hle_rfi for the two
                    # idioms the SDK uses it for; either way this function is done.
                    code += ' RET();'
                    last_uncond = True
                if (i >> 26) == 31 and ((i >> 1) & 0x3FF) == 146:
                    code += ' IRQ_CHECK();'
                if is_store(i):
                    code += f' WATCH_STORE(0x{pc:08X}u, {store_ea(i)});'
                if is_load(i):
                    code = f'WATCH_LOAD(0x{pc:08X}u, {load_ea(i)}); ' + code
                out.append(f'\t/* {pc:08X} {i:08X} */ {code}')
                continue

            pre = (br.ctr_dec + ' ') if br.ctr_dec else ''
            cond = br.cond
            s = pre
            if br.kind in ('b', 'bc'):
                t = br.target
                if br.link:
                    if t in special_calls:
                        # setjmp-style call site (OSSaveContext)
                        call = (f'c->lr = 0x{pc + 4:08X}u; if (GCN_SETJMP(*hle_context_jmpbuf(c)) == 0) {cname(t)}(c);')
                    else:
                        call = emit_call(pc, t, True)
                    if cond:
                        s += f'if ({cond}) {{ {call} }} else c->lr = 0x{pc + 4:08X}u;'
                    else:
                        s += call
                elif in_range(t):
                    irq = ('IDLE_CHECK(); ' if pc in idle_loops else 'IRQ_CHECK(); ') if t <= pc else ''
                    if cond:
                        s += f'if ({cond}) {{ {irq}goto L_{t:08X}; }}'
                    else:
                        s += f'{irq}goto L_{t:08X};'
                        last_uncond = True
                else:
                    call = emit_call(pc, t, False) + ' RET();'
                    if cond:
                        s += f'if ({cond}) {{ {call} }}'
                    else:
                        s += call
                        last_uncond = True
            elif br.kind == 'blr':
                if br.link:
                    s += f'{{ uint32_t t = c->lr; c->lr = 0x{pc + 4:08X}u; '
                    s += f'if ({cond}) call_indirect(c, t); }}' if cond else 'call_indirect(c, t); }'
                elif cond:
                    s += f'if ({cond}) RET();'
                else:
                    s += 'RET();'
                    last_uncond = True
            elif br.kind == 'bctr':
                if br.link:
                    body = f'{{ uint32_t t = c->ctr; c->lr = 0x{pc + 4:08X}u; call_indirect(c, t); }}'
                else:
                    # A module's own labels are at canonical addresses; the game's ctr holds
                    # the address where it loaded the module.
                    target = ('modules_canon(c->ctr)' if in_module(f.addr) else
                              'rels_canon(c->ctr)' if in_rel(f.addr) else 'c->ctr')
                    if jt_sorted:
                        cases = ' '.join(f'case 0x{t:08X}u: goto L_{t:08X};' for t in jt_sorted)
                        body = (f'switch ({target}) {{ {cases} default: call_indirect(c, c->ctr); RET(); }}')
                    else:
                        body = 'call_indirect(c, c->ctr); RET();'
                if cond:
                    s += f'if ({cond}) {{ {body} }}'
                else:
                    s += body
                    last_uncond = not br.link
            out.append(f'\t/* {pc:08X} {i:08X} */ {s}')
        if not last_uncond:
            if f.end in by_addr:
                out.append(f'\t{cname(f.end)}(c); RET(); /* fallthrough */')
            else:
                out.append(f'\tunimpl(c, 0x{f.end:08X}u, 0); /* fell off end */')
            stats['fallthrough'] += 1

    # Emit files
    header = ['#pragma once', '#include "recomp.h"', '']
    table = ['#include "recomp_funcs.h"', '', 'const RecompFunc g_recomp_funcs[] = {']
    files = []
    for k in range(0, len(funcs), FUNCS_PER_FILE):
        chunk = funcs[k:k + FUNCS_PER_FILE]
        out = ['#include "recomp_funcs.h"', '#include <setjmp.h>', '']
        for f in chunk:
            multi = len(f.entries) > 1
            body_name = cname(f.addr)
            if f.rel_hook:
                body_name = f'orig_{f.name}'
                header.append(f'void {body_name}(CPU* c);')
            if f.hle:
                body_name = f'orig_{f.name}'
                header.append(f'void {body_name}(CPU* c); /* original body of {f.name} */')
                header.append(f'void hle_{f.name}(CPU* c);')
            out.append(f'/* {f.name}  size 0x{f.size:X} */')
            if multi:
                out.append(f'static void body_{f.addr:08X}(CPU* c, uint32_t entry) {{')
                out.append('\tENTER(entry);')
                out.append('\tswitch (entry) {')
                for e in f.entries:
                    out.append(f'\tcase 0x{e:08X}u: goto L_{e:08X};')
                out.append('\t}')
                f.labels.add(f.addr)
            else:
                out.append(f'void {body_name}(CPU* c) {{')
                out.append(f'\tENTER(0x{f.addr:08X}u);')
            emit_body(f, out)
            out.append('}')
            if multi:
                for e in f.entries:
                    n = body_name if e == f.addr else cname(e)
                    out.append(f'void {n}(CPU* c) {{ body_{f.addr:08X}(c, 0x{e:08X}u); }}')
            if f.hle:
                out.append(f'void {cname(f.addr)}(CPU* c) {{ hle_{f.name}(c); }}')
            if f.rel_hook:
                out.append(f'void {cname(f.addr)}(CPU* c) {{ {f.rel_hook} }}')
            out.append('')
            for e in f.entries:
                header.append(f'void {cname(e)}(CPU* c);' + (f' /* {f.name} */' if e == f.addr else ''))
                nm = f.name if e == f.addr else f'{f.name}+0x{e - f.addr:X}'
                table.append(f'\t{{0x{e:08X}u, {cname(e)}, "{nm}"}},')
        fn = f'recomp_{k // FUNCS_PER_FILE:03d}.c'
        files.append(fn)
        write(fn, '\n'.join(out) + '\n')

    # tail calls to f.end for the very last function may reference an unknown symbol
    table.append('};')
    table.append(f'const uint32_t g_recomp_func_count = {sum(len(f.entries) for f in funcs)};')
    code_base = min(a for a, _, _ in dol.text_sections())
    code_end = max(a + sz for a, sz, _ in dol.text_sections())
    table.append(f'const uint32_t g_recomp_code_base = 0x{code_base:08X}u;')
    table.append(f'const uint32_t g_recomp_code_end = 0x{code_end:08X}u;')
    # The modules' bytes, for the runtime to recognise a loaded one by.
    for k, (canon, size, entry, name) in enumerate(modules):
        raw = dol.read_bytes(canon, size)
        table.append(f'static const uint8_t module_{k}[] = {{' + ','.join(str(b) for b in raw) + '};')
    table.append('const RecompModule g_recomp_modules[] = {')
    for k, (canon, size, entry, name) in enumerate(modules):
        # Where the module's code ends: the end of its last function. What follows is data
        # the loader relocates, which the runtime must not match against.
        ends = [f.end for f in funcs if canon <= f.addr < canon + size]
        code_end = (max(ends) - canon) if ends else entry
        table.append(f'\t{{0x{canon:08X}u, 0x{size:X}u, 0x{entry:X}u, 0x{code_end:X}u, module_{k}, "{name}"}},')
    table.append('\t{0, 0, 0, 0, 0, 0}};')
    table.append(f'const uint32_t g_recomp_module_count = {len(modules)};')
    # The relocatable modules: their canonical sections, for runtime/rels.cpp.
    if len(rels) > 256:
        sys.exit(f'error: {len(rels)} modules, more than REL_MAX_MODULES (runtime/recomp.h)')
    table.append('const RecompRel g_recomp_rels[] = {')
    for mid, name, secs in rels:
        table.append(f'\t{{{mid}u, "{name}", {len(secs)}u, {{' +
                     ', '.join(f'{{{s}u, 0x{c:08X}u, 0x{sz:X}u, {int(e)}u}}' for s, c, sz, e in secs) + '}},')
    table.append('\t{0, 0, 0, {{0, 0, 0, 0}}}};')
    table.append(f'const uint32_t g_recomp_rel_count = {len(rels)};')
    write('recomp_funcs.h', '\n'.join(header) + '\n')
    write('recomp_table.c', '\n'.join(table) + '\n')
    write('sources.cmake',
        'set(RECOMP_SOURCES\n' + '\n'.join(f'  ${{RECOMP_GEN_DIR}}/{f}' for f in files + ['recomp_table.c']) + '\n)\n')

    print(f'functions: {len(funcs)}  jumptable targets: {jt_count}  extra indirect targets: {extra_targets}', file=sys.stderr)
    for k, v in stats.items():
        print(f'  {k}: {v}', file=sys.stderr)
    if unimpl:
        print('unimplemented:', file=sys.stderr)
        for k, v in unimpl.most_common():
            print(f'  {k}: {v}', file=sys.stderr)


if __name__ == '__main__':
    main()
