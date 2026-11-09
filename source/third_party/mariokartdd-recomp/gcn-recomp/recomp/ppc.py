"""Gekko (PowerPC 750CL + paired singles) instruction -> C translator.

Each translate() call returns a C snippet for one instruction. Control-flow
instructions are handled by the caller (recomp.py) via decode_branch().
"""


def sx16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def sx12(v):
    v &= 0xFFF
    return v - 0x1000 if v & 0x800 else v


def mask32(mb, me):
    if mb <= me:
        return (0xFFFFFFFF >> mb) & ((0xFFFFFFFF << (31 - me)) & 0xFFFFFFFF)
    return ((0xFFFFFFFF >> mb) | ((0xFFFFFFFF << (31 - me)) & 0xFFFFFFFF)) & 0xFFFFFFFF


class Unimpl(Exception):
    pass


# ---------------------------------------------------------------------------
# Branch decoding
# ---------------------------------------------------------------------------

class Branch:
    """kind: 'b' (direct), 'bc' (conditional direct), 'blr', 'bctr'."""

    def __init__(self, kind, target=None, link=False, cond=None, ctr_dec=None):
        self.kind = kind
        self.target = target
        self.link = link
        self.cond = cond        # C expression or None (always)
        self.ctr_dec = ctr_dec  # C statement to run before evaluating cond


def bo_cond(bo, bi, allow_ctr=True):
    """Return (pre_stmt, cond_expr) for a BO/BI pair. cond_expr None = always."""
    pre = None
    parts = []
    if not (bo & 0x04):
        if not allow_ctr:
            raise Unimpl('bcctr with ctr decrement')
        pre = 'c->ctr--;'
        parts.append('c->ctr == 0' if (bo & 0x02) else 'c->ctr != 0')
    if not (bo & 0x10):
        want = (bo >> 3) & 1
        bit = f'CRB({bi})'
        parts.append(bit if want else f'!{bit}')
    cond = ' && '.join(f'({p})' for p in parts) if parts else None
    return pre, cond


def decode_branch(pc, i):
    op = i >> 26
    if op == 18:
        li = i & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        tgt = li if (i & 2) else (pc + li) & 0xFFFFFFFF
        return Branch('b', target=tgt, link=bool(i & 1))
    if op == 16:
        bo, bi = (i >> 21) & 31, (i >> 16) & 31
        bd = sx16(i & 0xFFFC)
        tgt = bd & 0xFFFFFFFF if (i & 2) else (pc + bd) & 0xFFFFFFFF
        pre, cond = bo_cond(bo, bi)
        return Branch('bc', target=tgt, link=bool(i & 1), cond=cond, ctr_dec=pre)
    if op == 19:
        xo = (i >> 1) & 0x3FF
        bo, bi = (i >> 21) & 31, (i >> 16) & 31
        if xo == 16:
            pre, cond = bo_cond(bo, bi)
            return Branch('blr', link=bool(i & 1), cond=cond, ctr_dec=pre)
        if xo == 528:
            pre, cond = bo_cond(bo, bi, allow_ctr=False)
            return Branch('bctr', link=bool(i & 1), cond=cond, ctr_dec=pre)
    return None


# ---------------------------------------------------------------------------
# Non-branch instructions
# ---------------------------------------------------------------------------

SPR_LR, SPR_CTR, SPR_XER = 8, 9, 1


def cr0(v):
    return f'SET_CR0({v});'


def ra0(a):
    """rA|0 semantics."""
    return '0' if a == 0 else f'c->r[{a}]'


def ea_d(a, d):
    if a == 0:
        return f'0x{d & 0xFFFFFFFF:X}u'
    if d == 0:
        return f'c->r[{a}]'
    return f'(c->r[{a}] + (uint32_t){d})'


def ea_x(a, b):
    if a == 0:
        return f'c->r[{b}]'
    return f'(c->r[{a}] + c->r[{b}])'


INT_LOADS = {  # op: (macro, update)
    32: ('LD32', False), 33: ('LD32', True),
    34: ('LD8', False), 35: ('LD8', True),
    40: ('LD16', False), 41: ('LD16', True),
    42: ('LDS16', False), 43: ('LDS16', True),
}
INT_STORES = {
    36: ('ST32', False), 37: ('ST32', True),
    38: ('ST8', False), 39: ('ST8', True),
    44: ('ST16', False), 45: ('ST16', True),
}
X_LOADS = {  # xo: (macro, update)
    23: ('LD32', False), 55: ('LD32', True),
    87: ('LD8', False), 119: ('LD8', True),
    279: ('LD16', False), 311: ('LD16', True),
    343: ('LDS16', False), 375: ('LDS16', True),
    534: ('LD32BR', False), 790: ('LD16BR', False),
    20: ('LD32', False),  # lwarx
}
X_STORES = {
    151: ('ST32', False), 183: ('ST32', True),
    215: ('ST8', False), 247: ('ST8', True),
    407: ('ST16', False), 439: ('ST16', True),
    662: ('ST32BR', False), 918: ('ST16BR', False),
}


def translate(pc, i):
    op = i >> 26
    d = (i >> 21) & 31
    a = (i >> 16) & 31
    b = (i >> 11) & 31
    cc = (i >> 6) & 31
    rc = i & 1
    simm = sx16(i)
    uimm = i & 0xFFFF

    if op == 14:  # addi
        if a == 0:
            return f'c->r[{d}] = 0x{simm & 0xFFFFFFFF:X}u;'
        return f'c->r[{d}] = c->r[{a}] + (uint32_t){simm};'
    if op == 15:  # addis
        v = (uimm << 16) & 0xFFFFFFFF
        if a == 0:
            return f'c->r[{d}] = 0x{v:X}u;'
        return f'c->r[{d}] = c->r[{a}] + 0x{v:X}u;'
    if op in (12, 13):  # addic, addic.
        s = f'{{ uint64_t t = (uint64_t)c->r[{a}] + (uint32_t){simm}; c->r[{d}] = (uint32_t)t; c->xer_ca = (uint8_t)(t >> 32); }}'
        if op == 13:
            s += ' ' + cr0(f'c->r[{d}]')
        return s
    if op == 8:  # subfic
        return f'{{ uint64_t t = (uint64_t)(uint32_t)~c->r[{a}] + (uint32_t){simm} + 1; c->r[{d}] = (uint32_t)t; c->xer_ca = (uint8_t)(t >> 32); }}'
    if op == 7:  # mulli
        return f'c->r[{d}] = (uint32_t)((int32_t)c->r[{a}] * {simm});'
    if op == 11:  # cmpi
        crf = d >> 2
        return f'c->cr[{crf}] = CMPS((int32_t)c->r[{a}], {simm});'
    if op == 10:  # cmpli
        crf = d >> 2
        return f'c->cr[{crf}] = CMPU(c->r[{a}], 0x{uimm:X}u);'
    if op == 24:  # ori
        if d == 0 and a == 0 and uimm == 0:
            return ';'  # nop
        return f'c->r[{a}] = c->r[{d}] | 0x{uimm:X}u;'
    if op == 25:
        return f'c->r[{a}] = c->r[{d}] | 0x{uimm << 16:X}u;'
    if op == 26:
        return f'c->r[{a}] = c->r[{d}] ^ 0x{uimm:X}u;'
    if op == 27:
        return f'c->r[{a}] = c->r[{d}] ^ 0x{uimm << 16:X}u;'
    if op == 28:
        return f'c->r[{a}] = c->r[{d}] & 0x{uimm:X}u; ' + cr0(f'c->r[{a}]')
    if op == 29:
        return f'c->r[{a}] = c->r[{d}] & 0x{uimm << 16:X}u; ' + cr0(f'c->r[{a}]')
    if op == 21:  # rlwinm
        sh, mb, me = b, cc, (i >> 1) & 31
        m = mask32(mb, me)
        src = f'c->r[{d}]' if sh == 0 else f'ROTL(c->r[{d}], {sh})'
        s = f'c->r[{a}] = {src} & 0x{m:X}u;'
        if rc:
            s += ' ' + cr0(f'c->r[{a}]')
        return s
    if op == 23:  # rlwnm
        mb, me = cc, (i >> 1) & 31
        m = mask32(mb, me)
        s = f'c->r[{a}] = ROTL(c->r[{d}], c->r[{b}] & 31) & 0x{m:X}u;'
        if rc:
            s += ' ' + cr0(f'c->r[{a}]')
        return s
    if op == 20:  # rlwimi
        sh, mb, me = b, cc, (i >> 1) & 31
        m = mask32(mb, me)
        s = f'c->r[{a}] = (ROTL(c->r[{d}], {sh}) & 0x{m:X}u) | (c->r[{a}] & 0x{(~m) & 0xFFFFFFFF:X}u);'
        if rc:
            s += ' ' + cr0(f'c->r[{a}]')
        return s

    if op in INT_LOADS:
        mac, upd = INT_LOADS[op]
        ea = ea_d(a, simm)
        if upd:
            return f'{{ uint32_t ea = {ea}; c->r[{d}] = {mac}(ea); c->r[{a}] = ea; }}'
        return f'c->r[{d}] = {mac}({ea});'
    if op in INT_STORES:
        mac, upd = INT_STORES[op]
        ea = ea_d(a, simm)
        if upd:
            return f'{{ uint32_t ea = {ea}; {mac}(ea, c->r[{d}]); c->r[{a}] = ea; }}'
        return f'{mac}({ea}, c->r[{d}]);'
    if op == 46:  # lmw
        lines = [f'{{ uint32_t ea = {ea_d(a, simm)};']
        for n in range(d, 32):
            lines.append(f'c->r[{n}] = LD32(ea + {(n - d) * 4});')
        lines.append('}')
        return ' '.join(lines)
    if op == 47:  # stmw
        lines = [f'{{ uint32_t ea = {ea_d(a, simm)};']
        for n in range(d, 32):
            lines.append(f'ST32(ea + {(n - d) * 4}, c->r[{n}]);')
        lines.append('}')
        return ' '.join(lines)

    # FP loads/stores
    if op in (48, 49):  # lfs(u)
        ea = ea_d(a, simm)
        u = f' c->r[{a}] = ea;' if op == 49 else ''
        return f'{{ uint32_t ea = {ea}; double v = LDF32(ea); c->f[{d}].d = v; c->ps1[{d}] = v;{u} }}'
    if op in (50, 51):  # lfd(u)
        ea = ea_d(a, simm)
        u = f' c->r[{a}] = ea;' if op == 51 else ''
        return f'{{ uint32_t ea = {ea}; c->f[{d}].u = LD64(ea);{u} }}'
    if op in (52, 53):  # stfs(u)
        ea = ea_d(a, simm)
        u = f' c->r[{a}] = ea;' if op == 53 else ''
        return f'{{ uint32_t ea = {ea}; STF32(ea, c->f[{d}].d);{u} }}'
    if op in (54, 55):  # stfd(u)
        ea = ea_d(a, simm)
        u = f' c->r[{a}] = ea;' if op == 55 else ''
        return f'{{ uint32_t ea = {ea}; ST64(ea, c->f[{d}].u);{u} }}'
    if op in (56, 57, 60, 61):  # psq_l, psq_lu, psq_st, psq_stu
        w = (i >> 15) & 1
        gq = (i >> 12) & 7
        off = sx12(i)
        ea = ea_d(a, off)
        u = f' c->r[{a}] = ea;' if op in (57, 61) else ''
        if op in (56, 57):
            return f'{{ uint32_t ea = {ea}; psq_load(c, ea, {d}, {w}, {gq});{u} }}'
        return f'{{ uint32_t ea = {ea}; psq_store(c, ea, {d}, {w}, {gq});{u} }}'

    if op == 31:
        return translate31(pc, i, d, a, b, rc)
    if op == 19:
        return translate19(pc, i, d, a, b)
    if op == 59:
        return translate59(pc, i, d, a, b, cc, rc)
    if op == 63:
        return translate63(pc, i, d, a, b, cc, rc)
    if op == 4:
        return translate4(pc, i, d, a, b, cc, rc)
    if op == 17:
        return f'hle_sc(c, 0x{pc:08X});'
    if op == 3:  # twi
        return f'hle_trap(c, 0x{pc:08X}, 0x{i:08X});'
    raise Unimpl(f'op {op}')


def arith_xo(xo9, d, a, b, oe):
    """XO-form integer arithmetic. Returns C or None."""
    A, B, D = f'c->r[{a}]', f'c->r[{b}]', f'c->r[{d}]'
    ov = ''
    if xo9 == 266:  # add
        s = f'{{ uint32_t x = {A}, y = {B}, r = x + y; {D} = r;'
        if oe:
            s += ' SET_OV(((x ^ r) & (y ^ r)) >> 31);'
        return s + ' }'
    if xo9 == 40:  # subf  rD = rB - rA
        s = f'{{ uint32_t x = ~{A}, y = {B}, r = y - {A}; {D} = r;'
        if oe:
            s += ' SET_OV(((x ^ r) & (y ^ r)) >> 31);'
        return s + ' }'
    carry_forms = {
        10: (A, B, '0'),                   # addc
        138: (A, B, 'c->xer_ca'),          # adde
        202: (A, '0', 'c->xer_ca'),        # addze
        234: (A, '0xFFFFFFFFu', 'c->xer_ca'),  # addme
        8: (f'(uint32_t)~{A}', B, '1'),    # subfc
        136: (f'(uint32_t)~{A}', B, 'c->xer_ca'),  # subfe
        200: (f'(uint32_t)~{A}', '0', 'c->xer_ca'),  # subfze
        232: (f'(uint32_t)~{A}', '0xFFFFFFFFu', 'c->xer_ca'),  # subfme
    }
    if xo9 in carry_forms:
        x, y, ci = carry_forms[xo9]
        s = (f'{{ uint32_t x = {x}, y = {y}; uint64_t t = (uint64_t)x + y + {ci}; '
             f'uint32_t r = (uint32_t)t; {D} = r; c->xer_ca = (uint8_t)(t >> 32);')
        if oe:
            s += ' SET_OV(((x ^ r) & (y ^ r)) >> 31);'
        return s + ' }'
    if xo9 == 104:  # neg
        s = f'{{ uint32_t x = {A}; {D} = (uint32_t)(-(int64_t)(int32_t)x);'
        if oe:
            s += ' SET_OV(x == 0x80000000u);'
        return s + ' }'
    if xo9 == 235:  # mullw
        s = f'{{ int64_t p = (int64_t)(int32_t){A} * (int32_t){B}; {D} = (uint32_t)p;'
        if oe:
            s += ' SET_OV(p != (int32_t)p);'
        return s + ' }'
    if xo9 == 75:  # mulhw
        return f'{D} = (uint32_t)(((int64_t)(int32_t){A} * (int32_t){B}) >> 32);'
    if xo9 == 11:  # mulhwu
        return f'{D} = (uint32_t)(((uint64_t){A} * {B}) >> 32);'
    if xo9 == 491:  # divw
        s = (f'{{ int32_t x = (int32_t){A}, y = (int32_t){B}; int bad = (y == 0) || (x == INT32_MIN && y == -1); '
             f'{D} = bad ? (x < 0 ? 0xFFFFFFFFu : 0) : (uint32_t)(x / y);')
        if oe:
            s += ' SET_OV(bad);'
        return s + ' }'
    if xo9 == 459:  # divwu
        s = f'{{ uint32_t x = {A}, y = {B}; {D} = y ? x / y : 0;'
        if oe:
            s += ' SET_OV(y == 0);'
        return s + ' }'
    return None


def translate31(pc, i, d, a, b, rc):
    xo = (i >> 1) & 0x3FF
    xo9 = (i >> 1) & 0x1FF
    oe = (i >> 10) & 1
    S, A, B = f'c->r[{d}]', f'c->r[{a}]', f'c->r[{b}]'

    s = arith_xo(xo9, d, a, b, oe)
    if s is not None:
        if rc:
            s += ' ' + cr0(f'c->r[{d}]')
        return s

    def logic(expr):
        r = f'{A} = {expr};'
        if rc:
            r += ' ' + cr0(A)
        return r

    if xo == 0:
        return f'c->cr[{d >> 2}] = CMPS((int32_t){A}, (int32_t){B});'
    if xo == 32:
        return f'c->cr[{d >> 2}] = CMPU({A}, {B});'
    if xo == 28:
        return logic(f'{S} & {B}')
    if xo == 60:
        return logic(f'{S} & ~{B}')
    if xo == 444:
        if d == b:
            if rc:
                return logic(S)
            return f'{A} = {S};'
        return logic(f'{S} | {B}')
    if xo == 412:
        return logic(f'{S} | ~{B}')
    if xo == 316:
        return logic(f'{S} ^ {B}')
    if xo == 124:
        return logic(f'~({S} | {B})')
    if xo == 476:
        return logic(f'~({S} & {B})')
    if xo == 284:
        return logic(f'~({S} ^ {B})')
    if xo == 24:  # slw
        return logic(f'({B} & 0x20) ? 0 : ({S} << ({B} & 31))')
    if xo == 536:  # srw
        return logic(f'({B} & 0x20) ? 0 : ({S} >> ({B} & 31))')
    if xo == 792:  # sraw
        r = (f'{{ int32_t x = (int32_t){S}; uint32_t n = {B} & 0x3F; '
             f'if (n >= 32) {{ {A} = (uint32_t)(x >> 31); c->xer_ca = x < 0; }} '
             f'else {{ {A} = (uint32_t)(x >> n); c->xer_ca = (x < 0) && n && ((uint32_t)x << (32 - n)) != 0; }} }}')
        if rc:
            r += ' ' + cr0(A)
        return r
    if xo == 824:  # srawi
        sh = b
        if sh == 0:
            r = f'{A} = {S}; c->xer_ca = 0;'
        else:
            r = (f'{{ int32_t x = (int32_t){S}; {A} = (uint32_t)(x >> {sh}); '
                 f'c->xer_ca = (x < 0) && ((uint32_t)x & 0x{(1 << sh) - 1:X}u) != 0; }}')
        if rc:
            r += ' ' + cr0(A)
        return r
    if xo == 26:
        return logic(f'{S} ? (uint32_t)GCN_CLZ32({S}) : 32')
    if xo == 954:
        return logic(f'(uint32_t)(int32_t)(int8_t){S}')
    if xo == 922:
        return logic(f'(uint32_t)(int32_t)(int16_t){S}')

    if xo in X_LOADS:
        mac, upd = X_LOADS[xo]
        if upd:
            return f'{{ uint32_t ea = {ea_x(a, b)}; c->r[{d}] = {mac}(ea); {A} = ea; }}'
        return f'c->r[{d}] = {mac}({ea_x(a, b)});'
    if xo in X_STORES:
        mac, upd = X_STORES[xo]
        if upd:
            return f'{{ uint32_t ea = {ea_x(a, b)}; {mac}(ea, {S}); {A} = ea; }}'
        return f'{mac}({ea_x(a, b)}, {S});'
    if xo == 150:  # stwcx.
        return f'ST32({ea_x(a, b)}, {S}); c->cr[0] = 2 | c->xer_so;'
    # FP indexed
    if xo in (535, 567):  # lfsx lfsux
        u = f' {A} = ea;' if xo == 567 else ''
        return f'{{ uint32_t ea = {ea_x(a, b)}; double v = LDF32(ea); c->f[{d}].d = v; c->ps1[{d}] = v;{u} }}'
    if xo in (599, 631):  # lfdx lfdux
        u = f' {A} = ea;' if xo == 631 else ''
        return f'{{ uint32_t ea = {ea_x(a, b)}; c->f[{d}].u = LD64(ea);{u} }}'
    if xo in (663, 695):
        u = f' {A} = ea;' if xo == 695 else ''
        return f'{{ uint32_t ea = {ea_x(a, b)}; STF32(ea, c->f[{d}].d);{u} }}'
    if xo in (727, 759):
        u = f' {A} = ea;' if xo == 759 else ''
        return f'{{ uint32_t ea = {ea_x(a, b)}; ST64(ea, c->f[{d}].u);{u} }}'
    if xo == 983:  # stfiwx
        return f'ST32({ea_x(a, b)}, (uint32_t)c->f[{d}].u);'
    if xo == 597:  # lswi
        return f'hle_lswi(c, {ra0(a)}, {d}, {b or 32});'
    if xo == 725:  # stswi
        return f'hle_stswi(c, {ra0(a)}, {d}, {b or 32});'

    if xo == 19:
        return f'c->r[{d}] = GET_CR();'
    if xo == 144:  # mtcrf
        crm = (i >> 12) & 0xFF
        if crm == 0xFF:
            return f'SET_CR({S});'
        parts = []
        for f in range(8):
            if crm & (0x80 >> f):
                parts.append(f'c->cr[{f}] = ({S} >> {28 - 4 * f}) & 0xF;')
        return ' '.join(parts)
    if xo == 512:  # mcrxr
        return (f'c->cr[{d >> 2}] = (c->xer_so << 3) | (c->xer_ov << 2) | (c->xer_ca << 1); '
                f'c->xer_so = c->xer_ov = c->xer_ca = 0;')
    if xo == 83:
        return f'c->r[{d}] = c->msr;'
    if xo == 146:
        return f'hle_mtmsr(c, {S});'
    if xo in (339, 371):  # mfspr, mftb
        spr = a | (b << 5)
        if spr == SPR_LR:
            return f'c->r[{d}] = c->lr;'
        if spr == SPR_CTR:
            return f'c->r[{d}] = c->ctr;'
        if spr == SPR_XER:
            return f'c->r[{d}] = GET_XER();'
        if 912 <= spr <= 919:
            return f'c->r[{d}] = c->gqr[{spr - 912}];'
        return f'c->r[{d}] = hle_mfspr(c, {spr});'
    if xo == 467:  # mtspr
        spr = a | (b << 5)
        if spr == SPR_LR:
            return f'c->lr = {S};'
        if spr == SPR_CTR:
            return f'c->ctr = {S};'
        if spr == SPR_XER:
            return f'SET_XER({S});'
        if 912 <= spr <= 919:
            return f'c->gqr[{spr - 912}] = {S};'
        return f'hle_mtspr(c, {spr}, {S});'
    # The segment registers and the TLB: a game that maps memory through page tables
    # (runtime/mmu.cpp) sets them up, so they are kept, and a tlbie drops the host's view
    # of that page so the next access reads the page table again.
    if xo == 595:  # mfsr
        return f'c->r[{d}] = hle_mfsr(c, {(i >> 16) & 15});'
    if xo == 659:  # mfsrin
        return f'c->r[{d}] = hle_mfsr(c, c->r[{b}] >> 28);'
    if xo == 210:  # mtsr
        return f'hle_mtsr(c, {(i >> 16) & 15}, c->r[{d}]);'
    if xo == 242:  # mtsrin
        return f'hle_mtsr(c, c->r[{b}] >> 28, c->r[{d}]);'
    if xo == 306:  # tlbie
        return f'hle_tlbie(c, c->r[{b}]);'
    if xo == 1014:  # dcbz
        return f'hle_dcbz(c, {ea_x(a, b)});'
    if xo == 470:  # dcbi
        return f'hle_dcbi(c, {ea_x(a, b)});'
    if xo in (54, 86, 246, 278, 982, 598, 854, 566):  # dcbst dcbf dcbtst dcbt icbi sync eieio tlbsync
        return ';'
    if xo == 4:  # tw
        return f'hle_trap(c, 0x{pc:08X}, 0x{i:08X});'
    raise Unimpl(f'op31 xo {xo}')


def translate19(pc, i, d, a, b):
    xo = (i >> 1) & 0x3FF
    if xo == 0:  # mcrf
        return f'c->cr[{d >> 2}] = c->cr[{a >> 2}];'
    if xo == 150:  # isync
        return ';'
    if xo == 50:  # rfi
        return f'hle_rfi(c, 0x{pc:08X});'
    crops = {257: '{A} & {B}', 449: '{A} | {B}', 193: '{A} ^ {B}', 225: '!({A} & {B})',
             33: '!({A} | {B})', 289: '!({A} ^ {B})', 129: '{A} & !{B}', 417: '{A} | !{B}'}
    if xo in crops:
        expr = crops[xo].format(A=f'CRB({a})', B=f'CRB({b})')
        return f'SET_CRB({d}, {expr});'
    raise Unimpl(f'op19 xo {xo}')


def F(n):
    return f'c->f[{n}].d'


def P1(n):
    return f'c->ps1[{n}]'


def cr1(rc):
    return ' c->cr[1] = (uint8_t)(c->fpscr >> 28);' if rc else ''


# An arithmetic result sets FPSCR[FPRF]; it is recorded here and classified only when the
# FPSCR is read (FPSCR_READ in runtime/recomp.h).
FPRF_S = ' c->fprf_v = v; c->fprf_pending = 2;'
FPRF_D = ' c->fprf_v = v; c->fprf_pending = 1;'


def translate59(pc, i, d, a, b, cc, rc):
    xo = (i >> 1) & 0x1F
    ops = {
        18: f'{F(a)} / {F(b)}',
        20: f'{F(a)} - {F(b)}',
        21: f'{F(a)} + {F(b)}',
        25: f'{F(a)} * {F(cc)}',
        24: f'1.0 / {F(b)}',
        28: f'fma({F(a)}, {F(cc)}, -{F(b)})',
        29: f'fma({F(a)}, {F(cc)}, {F(b)})',
        30: f'-fma({F(a)}, {F(cc)}, -{F(b)})',
        31: f'-fma({F(a)}, {F(cc)}, {F(b)})',
    }
    if xo in ops:
        return f'{{ double v = (double)(float)({ops[xo]}); {F(d)} = v; {P1(d)} = v;{FPRF_S} }}' + cr1(rc)
    raise Unimpl(f'op59 xo {xo}')


def translate63(pc, i, d, a, b, cc, rc):
    xo5 = (i >> 1) & 0x1F
    xo = (i >> 1) & 0x3FF
    ops5 = {
        18: f'{F(a)} / {F(b)}',
        20: f'{F(a)} - {F(b)}',
        21: f'{F(a)} + {F(b)}',
        25: f'{F(a)} * {F(cc)}',
        26: f'1.0 / sqrt({F(b)})',
        28: f'fma({F(a)}, {F(cc)}, -{F(b)})',
        29: f'fma({F(a)}, {F(cc)}, {F(b)})',
        30: f'-fma({F(a)}, {F(cc)}, -{F(b)})',
        31: f'-fma({F(a)}, {F(cc)}, {F(b)})',
    }
    if xo5 in ops5:
        return f'{{ double v = {ops5[xo5]}; {F(d)} = v;{FPRF_D} }}' + cr1(rc)
    if xo5 == 23:  # fsel
        return f'{F(d)} = ({F(a)} >= 0.0) ? {F(cc)} : {F(b)};' + cr1(rc)
    if xo5 == 22:  # fsqrt (not on gekko, but harmless)
        return f'{{ double v = sqrt({F(b)}); {F(d)} = v;{FPRF_D} }}' + cr1(rc)
    if xo in (0, 32):  # fcmpu, fcmpo
        return f'c->cr[{d >> 2}] = FCMP({F(a)}, {F(b)});'
    if xo == 12:  # frsp
        return f'{{ double v = (double)(float){F(b)}; {F(d)} = v; {P1(d)} = v;{FPRF_S} }}' + cr1(rc)
    if xo == 14:  # fctiw
        return f'c->f[{d}].u = 0xFFF8000000000000ull | (uint32_t)FCTIW({F(b)});' + cr1(rc)
    if xo == 15:  # fctiwz
        return f'c->f[{d}].u = 0xFFF8000000000000ull | (uint32_t)FCTIWZ({F(b)});' + cr1(rc)
    if xo == 72:  # fmr
        return f'c->f[{d}].u = c->f[{b}].u;' + cr1(rc)
    if xo == 40:
        return f'c->f[{d}].u = c->f[{b}].u ^ 0x8000000000000000ull;' + cr1(rc)
    if xo == 264:
        return f'c->f[{d}].u = c->f[{b}].u & 0x7FFFFFFFFFFFFFFFull;' + cr1(rc)
    if xo == 136:
        return f'c->f[{d}].u = c->f[{b}].u | 0x8000000000000000ull;' + cr1(rc)
    if xo == 583:  # mffs
        return f'c->f[{d}].u = 0xFFF8000000000000ull | FPSCR_READ(c);' + cr1(rc)
    if xo == 711:  # mtfsf
        fm = (i >> 17) & 0xFF
        m = 0
        for k in range(8):
            if fm & (0x80 >> k):
                m |= 0xF0000000 >> (4 * k)
        return f'hle_set_fpscr(c, (FPSCR_READ(c) & 0x{(~m) & 0xFFFFFFFF:X}u) | ((uint32_t)c->f[{b}].u & 0x{m:X}u));' + cr1(rc)
    if xo == 134:  # mtfsfi
        crf = d >> 2
        imm = (i >> 12) & 0xF
        sh = 28 - 4 * crf
        return f'hle_set_fpscr(c, (FPSCR_READ(c) & ~(0xFu << {sh})) | ({imm}u << {sh}));' + cr1(rc)
    if xo == 38:  # mtfsb1
        return f'hle_set_fpscr(c, FPSCR_READ(c) | (0x80000000u >> {d}));' + cr1(rc)
    if xo == 70:  # mtfsb0
        return f'hle_set_fpscr(c, FPSCR_READ(c) & ~(0x80000000u >> {d}));' + cr1(rc)
    if xo == 64:  # mcrfs
        return f'c->cr[{d >> 2}] = (FPSCR_READ(c) >> {28 - 4 * (a >> 2)}) & 0xF;'
    raise Unimpl(f'op63 xo {xo}')


def translate4(pc, i, d, a, b, cc, rc):
    xo5 = (i >> 1) & 0x1F
    xo6 = (i >> 1) & 0x3F
    xo = (i >> 1) & 0x3FF
    A0, A1 = F(a), P1(a)
    B0, B1 = F(b), P1(b)
    C0, C1 = F(cc), P1(cc)

    def pair(e0, e1, rnd=True):
        fprf = ''
        if rnd:
            e0, e1 = f'(double)(float)({e0})', f'(double)(float)({e1})'
            fprf = ' c->fprf_v = v0; c->fprf_pending = 2;'
        return f'{{ double v0 = {e0}, v1 = {e1}; {F(d)} = v0; {P1(d)} = v1;{fprf} }}' + cr1(rc)

    if xo6 in (6, 7, 38, 39):  # psq_lx psq_stx psq_lux psq_stux
        w = (i >> 10) & 1
        gq = (i >> 7) & 7
        ea = ea_x(a, b)
        u = f' c->r[{a}] = ea;' if xo6 in (38, 39) else ''
        if xo6 in (6, 38):
            return f'{{ uint32_t ea = {ea}; psq_load(c, ea, {d}, {w}, {gq});{u} }}'
        return f'{{ uint32_t ea = {ea}; psq_store(c, ea, {d}, {w}, {gq});{u} }}'

    if xo5 == 18:
        return pair(f'{A0} / {B0}', f'{A1} / {B1}')
    if xo5 == 20:
        return pair(f'{A0} - {B0}', f'{A1} - {B1}')
    if xo5 == 21:
        return pair(f'{A0} + {B0}', f'{A1} + {B1}')
    if xo5 == 25:
        return pair(f'{A0} * {C0}', f'{A1} * {C1}')
    if xo5 == 24:
        return pair(f'1.0 / {B0}', f'1.0 / {B1}')
    if xo5 == 26:
        return pair(f'1.0 / sqrt({B0})', f'1.0 / sqrt({B1})')
    if xo5 == 23:
        return pair(f'({A0} >= 0.0) ? {C0} : {B0}', f'({A1} >= 0.0) ? {C1} : {B1}', rnd=False)
    if xo5 == 28:
        return pair(f'fma({A0}, {C0}, -{B0})', f'fma({A1}, {C1}, -{B1})')
    if xo5 == 29:
        return pair(f'fma({A0}, {C0}, {B0})', f'fma({A1}, {C1}, {B1})')
    if xo5 == 30:
        return pair(f'-fma({A0}, {C0}, -{B0})', f'-fma({A1}, {C1}, -{B1})')
    if xo5 == 31:
        return pair(f'-fma({A0}, {C0}, {B0})', f'-fma({A1}, {C1}, {B1})')
    if xo5 == 10:  # ps_sum0
        return pair(f'{A0} + {B1}', C1)
    if xo5 == 11:  # ps_sum1
        return pair(C0, f'{A0} + {B1}')
    if xo5 == 12:  # ps_muls0
        return pair(f'{A0} * {C0}', f'{A1} * {C0}')
    if xo5 == 13:
        return pair(f'{A0} * {C1}', f'{A1} * {C1}')
    if xo5 == 14:  # ps_madds0
        return pair(f'fma({A0}, {C0}, {B0})', f'fma({A1}, {C0}, {B1})')
    if xo5 == 15:
        return pair(f'fma({A0}, {C1}, {B0})', f'fma({A1}, {C1}, {B1})')

    if xo == 0:  # ps_cmpu0
        return f'c->cr[{d >> 2}] = FCMP({A0}, {B0});'
    if xo == 32:
        return f'c->cr[{d >> 2}] = FCMP({A0}, {B0});'
    if xo == 64:
        return f'c->cr[{d >> 2}] = FCMP({A1}, {B1});'
    if xo == 96:
        return f'c->cr[{d >> 2}] = FCMP({A1}, {B1});'
    if xo == 40:
        return pair(f'-{B0}', f'-{B1}', rnd=False)
    if xo == 72:
        return pair(B0, B1, rnd=False)
    if xo == 136:
        return pair(f'-fabs({B0})', f'-fabs({B1})', rnd=False)
    if xo == 264:
        return pair(f'fabs({B0})', f'fabs({B1})', rnd=False)
    if xo == 528:
        return pair(A0, B0, rnd=False)
    if xo == 560:
        return pair(A0, B1, rnd=False)
    if xo == 592:
        return pair(A1, B0, rnd=False)
    if xo == 624:
        return pair(A1, B1, rnd=False)
    if xo == 1014:  # dcbz_l
        return f'hle_dcbz_l(c, {ea_x(a, b)});'
    raise Unimpl(f'op4 xo {xo}')


def translate_step(pc, i, x, p=1.0):
    """The instruction at pc with its per-frame step scaled (a game's steps.txt).

    x names the operand field (A, B or C) that carries a value from the previous frame;
    whatever the instruction adds to it is multiplied by gcn_step_scale(), a factor it is
    multiplied by is raised to that power, and an integer increment is multiplied by
    gcn_step_int(), which is 1 on the frames the step falls on and 0 between them. A
    multiply-add with the carried value as a factor (x*k + b, a damped approach) goes
    through gcn_step_damp, which keeps the same fixed point. p is the power of the step
    scale the step takes (1 for a rate, 2 for a force a Verlet integrator adds to a
    position, -1 for a rate derived from a position difference, to be converted back to
    the game's units); x of R scales the whole result by that power instead, and x of P
    on an add or subtract raises operand B to it, for the 1 - k beside an x *= k (a
    damping split into a kept part and a lost part, each a factor of a frame). Raises
    Unimpl for a form this does not cover."""
    op = i >> 26
    d = (i >> 21) & 31
    a = (i >> 16) & 31
    b = (i >> 11) & 31
    cc = (i >> 6) & 31
    rc = i & 1
    P = f'{p:g}'
    S = f'gcn_step_scale(0x{pc:08X}u, {P})'
    I = f'gcn_step_int(0x{pc:08X}u, {P})'
    if op == 14 and x == 'A':  # addi
        return f'c->r[{d}] = c->r[{a}] + (uint32_t){sx16(i)} * {I};'
    if op == 31:
        xo9 = (i >> 1) & 0x1FF
        A, B, D = f'c->r[{a}]', f'c->r[{b}]', f'c->r[{d}]'
        if xo9 == 266 and x in 'AB':  # add
            e = f'{A} + {B} * {I}' if x == 'A' else f'{A} * {I} + {B}'
            return f'{D} = {e};' + (' ' + cr0(D) if rc else '')
        if xo9 == 40 and x == 'B':  # subf: rD = rB - rA
            return f'{D} = {B} - {A} * {I};' + (' ' + cr0(D) if rc else '')
        raise Unimpl(f'step op31 xo {xo9} x={x}')
    if op in (59, 63):
        xo5 = (i >> 1) & 0x1F
        A, B, C = F(a), F(b), F(cc)
        e = step_expr(xo5, A, B, C, x, S, pc, P)
        if op == 59:
            return f'{{ double v = (double)(float)({e}); {F(d)} = v; {P1(d)} = v; }}' + cr1(rc)
        return f'{F(d)} = {e};' + cr1(rc)
    if op == 4:
        xo5 = (i >> 1) & 0x1F
        A0, A1, B0, B1, C0, C1 = F(a), P1(a), F(b), P1(b), F(cc), P1(cc)
        if xo5 in (12, 14):  # ps_muls0, ps_madds0: lane 1 uses C's lane 0
            e0 = step_expr(25 if xo5 == 12 else 29, A0, B0, C0, x, S, pc, P)
            e1 = step_expr(25 if xo5 == 12 else 29, A1, B1, C0, x, S, pc, P)
        elif xo5 in (13, 15):
            e0 = step_expr(25 if xo5 == 13 else 29, A0, B0, C1, x, S, pc, P)
            e1 = step_expr(25 if xo5 == 13 else 29, A1, B1, C1, x, S, pc, P)
        else:
            e0 = step_expr(xo5, A0, B0, C0, x, S, pc, P)
            e1 = step_expr(xo5, A1, B1, C1, x, S, pc, P)
        return (f'{{ double v0 = (double)(float)({e0}), v1 = (double)(float)({e1}); '
                f'{F(d)} = v0; {P1(d)} = v1; }}' + cr1(rc))
    raise Unimpl(f'step op {op} x={x}')


def step_expr(xo5, A, B, C, x, S, pc, P='1'):
    """The scaled expression for one floating-point arithmetic form (by its 5-bit extended
    opcode) whose operand x carries the state, or, for x of R, whose whole result is
    scaled; P is the power of the step scale, as a C literal."""
    if x == 'R':
        plain = {21: f'{A} + {B}', 20: f'{A} - {B}', 25: f'{A} * {C}', 18: f'{A} / {B}',
                 29: f'fma({A}, {C}, {B})', 28: f'fma({A}, {C}, -{B})',
                 30: f'-fma({A}, {C}, -{B})', 31: f'-fma({A}, {C}, {B})'}
        if xo5 not in plain:
            raise Unimpl(f'step float xo {xo5} x=R')
        return f'{S} * ({plain[xo5]})'
    D = f'gcn_step_damp(0x{pc:08X}u, {P}, '
    P = f'gcn_step_pow(0x{pc:08X}u, {P}, '
    if xo5 == 21:  # add
        if x == 'A':
            return f'{A} + {S} * {B}'
        if x == 'B':
            return f'{S} * {A} + {B}'
        if x == 'P':  # B is a per-frame factor (a k in 1 - k): raise it, do not scale it
            return f'{A} + {P}{B})'
    if xo5 == 20 and x == 'A':  # sub
        return f'{A} - {S} * {B}'
    if xo5 == 20 and x == 'P':
        return f'{A} - {P}{B})'
    if xo5 == 25:  # mul
        if x == 'A':
            return f'{A} * {P}{C})'
        if x == 'C':
            return f'{P}{A}) * {C}'
    if xo5 == 18 and x == 'A':  # div
        return f'{A} / {P}{B})'
    if xo5 == 29:  # madd: A*C + B
        if x == 'B':
            return f'fma({S} * {A}, {C}, {B})'
        if x == 'A':
            return f'{D}{A}, {C}, {B})'
        if x == 'C':
            return f'{D}{C}, {A}, {B})'
    if xo5 == 28:  # msub: A*C - B
        if x == 'A':
            return f'{D}{A}, {C}, -{B})'
        if x == 'C':
            return f'{D}{C}, {A}, -{B})'
    if xo5 == 30:  # nmsub: B - A*C
        if x == 'B':
            return f'-fma({S} * {A}, {C}, -{B})'
        if x == 'A':
            return f'{D}{A}, -{C}, {B})'
        if x == 'C':
            return f'{D}{C}, -{A}, {B})'
    if xo5 == 31:  # nmadd: -(A*C + B)
        if x == 'A':
            return f'{D}{A}, -{C}, -{B})'
        if x == 'C':
            return f'{D}{C}, -{A}, -{B})'
    raise Unimpl(f'step float xo {xo5} x={x}')
