"""J3D2 (BDL/BMD) models as renderable meshes: per material, welded vertices with the model's own
normals, first texture coordinates and vertex colours, the material's texture (decoded RGBA) and
how it is drawn (lit or not, alpha test or blend, culling).

Approximations, all deliberate: one texture per material (the first TEV stage that samples one),
its texture matrix applied as a static SRT (no animation), environment-mapped textures folded
into a flat colour, and the TEV combiners read as texture x colour channel. Joint transforms are
applied (rigid parts in joint space, weighted envelopes in model space).
"""
import math
import struct
import numpy as np
from . import gxtex

CULL_NONE, CULL_FRONT, CULL_BACK = 0, 1, 2
# flags; bits 8-9 hold log2 of the TEV output scale (the shader multiplies colour by 1, 2 or 4)
F_LIT, F_ALPHA_TEST, F_BLEND, F_ADDITIVE, F_TWO_SIDED, F_TEXTURED = 1, 2, 4, 8, 16, 32


def _sections(d):
    assert d[:4] == b'J3D2', d[:8]
    n = struct.unpack_from('>I', d, 0x0C)[0]
    secs, off = {}, 0x20
    for _ in range(n):
        secs[d[off:off + 4].decode()] = (off, struct.unpack_from('>I', d, off + 4)[0])
        off += struct.unpack_from('>I', d, off + 4)[0]
    return secs


_DT = {0: (np.uint8, 1), 1: (np.int8, 1), 2: ('>u2', 2), 3: ('>i2', 2), 4: ('>f4', 4)}


def _vertex_arrays(d, secs):
    o, sz = secs['VTX1']
    fmt_off = struct.unpack_from('>I', d, o + 8)[0]
    offs = list(struct.unpack_from('>13I', d, o + 0x0C))
    fmts, p = {}, o + fmt_off
    while True:
        a, cnt, typ, frac = struct.unpack_from('>IIIB', d, p)
        if a == 0xFF:
            break
        fmts[a] = (cnt, typ, frac)
        p += 16

    def span(slot):
        st = offs[slot]
        if st == 0:
            return None
        later = [x for x in offs if x > st]
        return o + st, (min(later) if later else sz) - st

    def numeric(slot, attr, ncomp):
        s = span(slot)
        if s is None or attr not in fmts:
            return None
        cnt, typ, frac = fmts[attr]
        dt, size = _DT[typ]
        a = np.frombuffer(d, dt, count=s[1] // size, offset=s[0]).astype(np.float64)
        if typ != 4:
            a = a / (1 << frac)
        return a[:len(a) // ncomp * ncomp].reshape(-1, ncomp)

    def colour(slot, attr):
        s = span(slot)
        if s is None or attr not in fmts:
            return None
        typ = fmts[attr][1]
        b = np.frombuffer(d, np.uint8, count=s[1], offset=s[0])
        if typ in (2, 5):  # RGBX8 / RGBA8
            c = b[:len(b) // 4 * 4].reshape(-1, 4).copy()
            if typ == 2:
                c[:, 3] = 255
            return c
        if typ == 1:  # RGB8
            c = b[:len(b) // 3 * 3].reshape(-1, 3)
            return np.concatenate([c, np.full((len(c), 1), 255, np.uint8)], 1)
        if typ == 0:  # RGB565
            return gxtex._565(np.frombuffer(d, '>u2', count=s[1] // 2, offset=s[0]))
        if typ == 3:  # RGBA4
            v = np.frombuffer(d, '>u2', count=s[1] // 2, offset=s[0]).astype(np.uint32)
            return np.stack([(v >> 12 & 15) * 17, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17], -1).astype(np.uint8)
        if typ == 4:  # RGBA6
            b = b[:len(b) // 3 * 3].reshape(-1, 3).astype(np.uint32)
            v = b[:, 0] << 16 | b[:, 1] << 8 | b[:, 2]
            return np.stack([(v >> 18 & 63) * 255 // 63, (v >> 12 & 63) * 255 // 63, (v >> 6 & 63) * 255 // 63,
                             (v & 63) * 255 // 63], -1).astype(np.uint8)
        return None

    pos = numeric(0, 9, 3 if fmts.get(9, (1,))[0] == 1 else 2)
    if pos is not None and pos.shape[1] == 2:
        pos = np.concatenate([pos, np.zeros((len(pos), 1))], 1)
    nrm = numeric(1, 10, 3) if 10 in fmts and fmts[10][0] == 0 else None
    tex = [numeric(5 + t, 13 + t, 2 if fmts.get(13 + t, (1,))[0] == 1 else 1) for t in range(8)]
    tex = [None if t is None or t.shape[1] != 2 else t for t in tex]
    return pos, nrm, [colour(3, 11), colour(4, 12)], tex


def _joints(d, secs):
    """World matrix per joint (bind pose), from JNT1 and the INF1 hierarchy."""
    if 'JNT1' not in secs:
        return [np.eye(4)]
    o = secs['JNT1'][0]
    n = struct.unpack_from('>H', d, o + 8)[0]
    ent, remap = struct.unpack_from('>II', d, o + 0x0C)
    local = []
    for i in range(n):
        e = o + ent + struct.unpack_from('>H', d, o + remap + 2 * i)[0] * 0x40
        sx, sy, sz = struct.unpack_from('>3f', d, e + 4)
        rx, ry, rz = (v * math.pi / 0x8000 for v in struct.unpack_from('>3h', d, e + 0x10))
        tx, ty, tz = struct.unpack_from('>3f', d, e + 0x18)
        cx, sx_, cy, sy_, cz, sz_ = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
        R = np.array([[cz * cy, cz * sy_ * sx_ - sz_ * cx, cz * sy_ * cx + sz_ * sx_],
                      [sz_ * cy, sz_ * sy_ * sx_ + cz * cx, sz_ * sy_ * cx - cz * sx_],
                      [-sy_, cy * sx_, cy * cx]])
        M = np.eye(4)
        M[:3, :3] = R @ np.diag([sx, sy, sz])
        M[:3, 3] = (tx, ty, tz)
        local.append(M)
    world = [None] * n
    o = secs['INF1'][0]
    q = o + struct.unpack_from('>I', d, o + 0x14)[0]
    stack, last = [], None
    while True:
        typ, idx = struct.unpack_from('>HH', d, q)
        q += 4
        if typ == 0:
            break
        if typ == 1:
            stack.append(last)
        elif typ == 2:
            last = stack.pop() if stack else None
        elif typ == 0x10 and idx < n:
            parent = stack[-1] if stack else None
            world[idx] = (world[parent] if parent is not None and world[parent] is not None else np.eye(4)) @ local[idx]
            last = idx
    return [w if w is not None else np.eye(4) for w in world]


def _draw_matrices(d, secs, joints):
    """DRW1 entry -> matrix: a rigid joint's world matrix, or identity for a weighted envelope."""
    if 'DRW1' not in secs:
        return [np.eye(4)]
    o = secs['DRW1'][0]
    n = struct.unpack_from('>H', d, o + 8)[0]
    wo, do = struct.unpack_from('>II', d, o + 0x0C)
    out = []
    for i in range(n):
        weighted = d[o + wo + i]
        j = struct.unpack_from('>H', d, o + do + 2 * i)[0]
        out.append(np.eye(4) if weighted or j >= len(joints) else joints[j])
    return out


def _materials(d, secs):
    """Per material: texture (TEX1 index or None), its texcoord source and SRT, colour source,
    material colour, flags."""
    out = {}
    if 'MAT3' not in secs:
        return out
    o = secs['MAT3'][0]
    nmat = struct.unpack_from('>H', d, o + 8)[0]
    T = struct.unpack_from('>30I', d, o + 0x0C)

    def tab(k):
        return o + T[k] if T[k] else None

    def u16(at, i):
        return struct.unpack_from('>H', d, at + 2 * i)[0]

    for m in range(nmat):
        e = o + T[0] + u16(o + T[1], m) * 0x14C
        mode = d[e]
        cull = CULL_BACK
        if tab(4) is not None and d[e + 1] != 0xFF:
            cull = struct.unpack_from('>I', d, tab(4) + 4 * d[e + 1])[0]
        stages = 1
        if tab(19) is not None and d[e + 4] != 0xFF:
            stages = d[tab(19) + d[e + 4]]
        # the first stage that samples a texture
        tex = coord = None
        for s in range(max(1, stages)):
            oi = u16(e + 0xBC, s)
            if oi == 0xFFFF or tab(16) is None:
                continue
            tc, tm, _ch = d[tab(16) + 4 * oi], d[tab(16) + 4 * oi + 1], d[tab(16) + 4 * oi + 2]
            if tm != 0xFF and u16(e + 0x84, tm) != 0xFFFF:
                tex = u16(tab(15), u16(e + 0x84, tm))
                coord = tc
                break
        uv_src, srt, env = None, None, False
        if tex is not None and coord != 0xFF and coord is not None and coord < 8 and tab(11) is not None:
            gi = u16(e + 0x28, coord)
            if gi != 0xFFFF:
                _typ, src, mtx = d[tab(11) + 4 * gi], d[tab(11) + 4 * gi + 1], d[tab(11) + 4 * gi + 2]
                if 4 <= src <= 11:
                    uv_src = src - 4
                else:
                    env = True
                if 30 <= mtx < 60 and tab(13) is not None:
                    ti = u16(e + 0x48, (mtx - 30) // 3)
                    if ti != 0xFFFF:
                        t = tab(13) + ti * 0x64
                        info = d[t + 1]
                        if (info & 0x3F) not in (0,):
                            env = True
                        else:
                            cx, cy = struct.unpack_from('>2f', d, t + 4)
                            sx, sy = struct.unpack_from('>2f', d, t + 0x10)
                            rot = struct.unpack_from('>h', d, t + 0x18)[0] * math.pi / 0x8000
                            tx, ty = struct.unpack_from('>2f', d, t + 0x1C)
                            srt = (cx, cy, sx, sy, rot, tx, ty, info >> 7)
        # The last stage's colour scale: SMG often bakes vertex colours at half and doubles them in TEV.
        gain = 0
        si = u16(e + 0xE4, max(1, stages) - 1)
        if si != 0xFFFF and tab(20) is not None:
            gain = {1: 1, 2: 2}.get(d[tab(20) + 20 * si + 7], 0)
        lit, vtx_colour = False, True
        ci = u16(e + 0x0C, 0)
        if ci != 0xFFFF and tab(7) is not None:
            lit = d[tab(7) + 8 * ci] != 0
            vtx_colour = d[tab(7) + 8 * ci + 1] == 1
        mc = u16(e + 0x08, 0)
        matcol = np.frombuffer(d, np.uint8, 4, tab(5) + 4 * mc).copy() if mc != 0xFFFF and tab(5) else np.full(4, 255, np.uint8)
        blend = additive = False
        bi = u16(e + 0x148, 0)
        if bi != 0xFFFF and tab(25) is not None:
            btype, bsrc, bdst = d[tab(25) + 4 * bi], d[tab(25) + 4 * bi + 1], d[tab(25) + 4 * bi + 2]
            blend = btype == 1
            additive = blend and bdst == 1  # GX_BL_ONE
        alpha_test = mode == 2 or (not blend and _alpha_compare(d, tab(24), u16(e + 0x146, 0)))
        flags = (F_LIT if lit else 0) | (F_ALPHA_TEST if alpha_test else 0) | (F_BLEND if blend else 0) \
            | (F_ADDITIVE if additive else 0) | (F_TWO_SIDED if cull == CULL_NONE else 0) | (gain << 8)
        out[m] = dict(tex=tex, uv=uv_src, srt=srt, env=env, vtx_colour=vtx_colour, matcol=matcol, flags=flags,
                      cull=cull, mode=mode)
    return out


def _alpha_compare(d, at, i):
    """Whether the alpha compare actually rejects pixels (not 'always')."""
    if at is None or i == 0xFFFF:
        return False
    c0, r0, op, c1, r1 = d[at + 8 * i:at + 8 * i + 5]
    def always(c, r):  # GX_ALWAYS, or GX_GEQUAL 0
        return c == 7 or (c == 6 and r == 0)
    if op == 0:  # AND
        return not (always(c0, r0) and always(c1, r1))
    return not (always(c0, r0) or always(c1, r1))


def _apply_srt(uv, srt):
    if srt is None:
        return uv
    cx, cy, sx, sy, rot, tx, ty, maya = srt
    c, s = math.cos(rot), math.sin(rot)
    if maya:
        u, v = uv[:, 0] * sx, uv[:, 1] * sy
        u, v = u - 0.5 * sx, v + 0.5 * sy - 1
        u, v = c * u + s * v, -s * u + c * v
        return np.stack([u + 0.5 - tx, v + 0.5 + ty], 1)
    u, v = (uv[:, 0] - cx) * sx, (uv[:, 1] - cy) * sy
    return np.stack([c * u - s * v + cx + tx, s * u + c * v + cy + ty], 1)


def load(d):
    """{'batches': [...], 'textures': {tex1 index: (rgba, wrapS, wrapT)}} for model bytes [d]."""
    secs = _sections(d)
    pos, nrm, clr, tex = _vertex_arrays(d, secs)
    joints = _joints(d, secs)
    drw = _draw_matrices(d, secs, joints)
    mats = _materials(d, secs)

    # shape -> material, from the scene graph
    o = secs['INF1'][0]
    q = o + struct.unpack_from('>I', d, o + 0x14)[0]
    cur, shape_mat = 0, {}
    while True:
        typ, idx = struct.unpack_from('>HH', d, q)
        q += 4
        if typ == 0:
            break
        if typ == 0x11:
            cur = idx
        elif typ == 0x12:
            shape_mat[idx] = cur

    # Every triangle corner: (material, matrix, pos, nrm, clr0, uv-per-texcoord...)
    o, _ = secs['SHP1']
    nshp = struct.unpack_from('>H', d, o + 8)[0]
    init_off, _remap, _, attr_off, mtxtab_off, dl_off, mtxdata_off, pkt_off = struct.unpack_from('>8I', d, o + 0x0C)
    corners = []   # rows: shape, matrix(drw), pos, nrm, clr0, tex0..tex7
    for s in range(nshp):
        sp = o + init_off + s * 0x28
        _mtype, _, npkt, aoff, fmtx, fpkt = struct.unpack_from('>BBHHHH', d, sp)
        descs, qq = [], o + attr_off + aoff
        while True:
            a, t = struct.unpack_from('>II', d, qq)
            if a == 0xFF:
                break
            descs.append((a, t))
            qq += 8
        table = [0]
        for pk in range(npkt):
            _cur, cnt, first = struct.unpack_from('>HHI', d, o + mtxdata_off + (fmtx + pk) * 8)
            nt = [struct.unpack_from('>H', d, o + mtxtab_off + 2 * (first + i))[0] for i in range(cnt)]
            table = [nt[i] if i < len(nt) and nt[i] != 0xFFFF else (table[i] if i < len(table) else 0)
                     for i in range(max(len(nt), len(table)))] or [0]
            psz, poff = struct.unpack_from('>II', d, o + pkt_off + (fpkt + pk) * 8)
            qq = o + dl_off + poff
            end = qq + psz
            while qq < end:
                op = d[qq]
                qq += 1
                if op == 0:
                    continue
                prim = op & 0xF8
                if prim not in (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8):
                    break
                n = struct.unpack_from('>H', d, qq)[0]
                qq += 2
                verts = []
                for _ in range(n):
                    row = [s, table[0], -1, -1, -1] + [-1] * 8
                    for a, t in descs:
                        if t == 1 or t == 2:
                            v = d[qq]
                            qq += 1
                        elif t == 3:
                            v = struct.unpack_from('>H', d, qq)[0]
                            qq += 2
                        else:
                            v = -1
                        if a == 0:
                            k = v // 3
                            row[1] = table[k] if k < len(table) else table[0]
                        elif a == 9:
                            row[2] = v
                        elif a == 10:
                            row[3] = v
                        elif a == 11:
                            row[4] = v
                        elif 13 <= a <= 20:
                            row[5 + a - 13] = v
                    verts.append(row)
                if prim == 0x90:
                    tris = [(verts[i], verts[i + 1], verts[i + 2]) for i in range(0, n - 2, 3)]
                elif prim == 0x98:
                    tris = [((verts[i + 1], verts[i], verts[i + 2]) if i & 1 else (verts[i], verts[i + 1], verts[i + 2]))
                            for i in range(n - 2)]
                elif prim == 0xA0:
                    tris = [(verts[0], verts[i], verts[i + 1]) for i in range(1, n - 1)]
                elif prim == 0x80:
                    tris = []
                    for i in range(0, n - 3, 4):
                        tris += [(verts[i], verts[i + 1], verts[i + 2]), (verts[i], verts[i + 2], verts[i + 3])]
                else:
                    tris = []
                for a, b, c in tris:
                    corners += [c, b, a]  # GX fronts wind clockwise: reversed, outward faces are counter-clockwise
    if not corners or pos is None:
        return {'batches': [], 'textures': {}}
    C = np.array(corners, dtype=np.int64)
    C = C.reshape(-1, 3, C.shape[1])
    ok = (C[:, :, 2] >= 0).all(1) & (C[:, :, 2] < len(pos)).all(1)
    C = C[ok]

    # texture headers
    textures, t1 = {}, secs.get('TEX1')
    ntex = struct.unpack_from('>H', d, t1[0] + 8)[0] if t1 else 0
    thdr = struct.unpack_from('>I', d, t1[0] + 0x0C)[0] if t1 else 0

    def texture(i):
        if i not in textures and i < ntex:
            try:
                rgba, ws, wt, _fmt = gxtex.bti(d, t1[0] + thdr + i * 0x20)
                textures[i] = (rgba, ws, wt)
            except (ValueError, KeyError):
                textures[i] = None
        return textures.get(i)

    batches = []
    shape_of = C[:, 0, 0]
    mat_of = np.array([shape_mat.get(int(s), 0) for s in range(nshp)])[shape_of] if nshp else np.zeros(len(C), int)
    for m in sorted(set(mat_of.tolist())):
        info = mats.get(m, dict(tex=None, uv=None, srt=None, env=False, vtx_colour=True, matcol=np.full(4, 255, np.uint8),
                                flags=0, cull=CULL_BACK, mode=1))
        sel = C[mat_of == m].reshape(-1, C.shape[2])
        uvcol = 5 + info['uv'] if info['uv'] is not None else None
        key = sel[:, [1, 2, 3, 4] + ([uvcol] if uvcol is not None else [])]
        uniq, inv = np.unique(key, axis=0, return_inverse=True)
        inv = inv.reshape(-1)
        mtx = np.stack([drw[k] if 0 <= k < len(drw) else np.eye(4) for k in uniq[:, 0]]) if len(drw) > 1 else \
            np.repeat(drw[0][None], len(uniq), 0)
        p = np.einsum('nij,nj->ni', mtx[:, :3, :3], pos[uniq[:, 1]]) + mtx[:, :3, 3]
        if nrm is not None and (uniq[:, 2] >= 0).all() and (uniq[:, 2] < len(nrm)).all():
            nn = np.einsum('nij,nj->ni', mtx[:, :3, :3], nrm[uniq[:, 2]])
        else:
            nn = None
        idx = inv.reshape(-1, 3)
        if nn is None or not np.isfinite(nn).all():
            nn = smooth_normals(p, idx, uniq[:, 1])
        nn /= np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-9)
        cc = clr[0]
        if info['vtx_colour'] and cc is not None and (uniq[:, 3] >= 0).all() and (uniq[:, 3] < len(cc)).all():
            col = cc[uniq[:, 3]].copy()
        else:
            col = np.repeat(info['matcol'][None], len(uniq), 0)
        flags = info['flags']
        tex_id, uv = None, np.zeros((len(uniq), 2))
        if info['tex'] is not None:
            t = texture(info['tex'])
            if t is not None:
                arr = tex[info['uv']] if info['uv'] is not None else None
                if info['env'] or arr is None or not ((uniq[:, -1] >= 0) & (uniq[:, -1] < len(arr))).all():
                    # environment-mapped (or no coordinates): the texture's average colour
                    avg = t[0].reshape(-1, 4).astype(float).mean(0)
                    col = np.clip(col.astype(float) * np.r_[avg[:3], 255] / 255, 0, 255).astype(np.uint8)
                else:
                    uv = _apply_srt(arr[uniq[:, -1]], info['srt'])
                    tex_id = info['tex']
                    flags |= F_TEXTURED
                    # Vertex colours here are relative shade (the game's TEV combines them with
                    # registers we do not model): the brightest tenth reads as full light.
                    if info['vtx_colour'] and cc is not None:
                        top = np.percentile(col[:, :3].max(1), 90)
                        if 8 < top < 140:
                            col[:, :3] = np.clip(col[:, :3].astype(float) * min(2.0, 200.0 / top), 0, 255).astype(np.uint8)
        batches.append(dict(material=m, tex=tex_id, flags=flags, pos=p, nrm=nn, uv=uv, col=col, idx=idx))
    return {'batches': batches, 'textures': {k: v for k, v in textures.items() if v is not None}}


def smooth_normals(p, idx, weld):
    """Area-weighted vertex normals, shared by every vertex at the same position index."""
    a, b, c = p[idx[:, 0]], p[idx[:, 1]], p[idx[:, 2]]
    fn = np.cross(b - a, c - a)
    acc = np.zeros((weld.max() + 1, 3))
    for k in range(3):
        np.add.at(acc, weld[idx[:, k]], fn)
    return acc[weld]
