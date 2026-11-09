"""Minimal J3D2 (BDL/BMD) reader: sections, vertex arrays, shape triangles, materials, textures."""
import struct
import numpy as np

TEXFMT = {0: 'I4', 1: 'I8', 2: 'IA4', 3: 'IA8', 4: 'RGB565', 5: 'RGB5A3', 6: 'RGBA8', 8: 'C4', 9: 'C8', 10: 'C14X2', 14: 'CMPR'}
ATTR = {0: 'PNMTXIDX', 9: 'POS', 10: 'NRM', 11: 'CLR0', 12: 'CLR1', 13: 'TEX0', 14: 'TEX1', 15: 'TEX2', 16: 'TEX3',
        17: 'TEX4', 18: 'TEX5', 19: 'TEX6', 20: 'TEX7', 25: 'NBT'}


def _c565(v):
    v = v.astype(np.int64)
    return np.stack([(v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31], -1).astype(float)


def tex_average(d, h):
    """Approximate average RGB of a BTI texture's top mip (header at h)."""
    fmt, alpha, w, hgt = struct.unpack_from('>BBHH', d, h)
    off = h + struct.unpack_from('>I', d, h + 0x1C)[0]
    try:
        if fmt == 14:  # CMPR: average of DXT1 endpoint colours
            nblk = max(1, (w * hgt) // 16)
            blk = np.frombuffer(d, '>u2', count=nblk * 4, offset=off).reshape(-1, 4)
            return (_c565(blk[:, 0]) + _c565(blk[:, 1])).mean(0) / 2
        if fmt == 4:
            return _c565(np.frombuffer(d, '>u2', count=w * hgt, offset=off)).mean(0)
        if fmt == 5:
            v = np.frombuffer(d, '>u2', count=w * hgt, offset=off).astype(np.int64)
            opaque = (v & 0x8000) != 0
            rgb = np.where(opaque[:, None], np.stack([(v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31], -1),
                           np.stack([(v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17], -1))
            return rgb.mean(0).astype(float)
        if fmt == 1:
            i = np.frombuffer(d, np.uint8, count=w * hgt, offset=off).mean()
            return np.array([i, i, i], float)
        if fmt == 0:
            b = np.frombuffer(d, np.uint8, count=w * hgt // 2, offset=off)
            i = ((b >> 4).mean() + (b & 15).mean()) / 2 * 17
            return np.array([i, i, i], float)
        if fmt == 3:
            b = np.frombuffer(d, np.uint8, count=w * hgt * 2, offset=off).reshape(-1, 2)
            i = b[:, 1].mean()
            return np.array([i, i, i], float)
        if fmt == 6:
            b = np.frombuffer(d, np.uint8, count=w * hgt * 4, offset=off).reshape(-1, 64)
            r = b[:, 1:32:2].mean()
            g = b[:, 32::2].mean()
            bb = b[:, 33::2].mean()
            return np.array([r, g, bb], float)
    except ValueError:
        pass
    return np.array([200.0, 200.0, 200.0])


def nametable(d, off):
    n = struct.unpack_from('>H', d, off)[0]
    out = []
    for i in range(n):
        h, so = struct.unpack_from('>HH', d, off + 4 + i * 4)
        e = d.index(b'\0', off + so)
        out.append(d[off + so:e].decode('shift_jis', 'replace'))
    return out


def parse(d):
    assert d[:4] == b'J3D2', d[:8]
    nsec = struct.unpack_from('>I', d, 0x0C)[0]
    secs = {}
    off = 0x20
    for _ in range(nsec):
        mg = d[off:off + 4].decode()
        sz = struct.unpack_from('>I', d, off + 4)[0]
        secs[mg] = (off, sz)
        off += sz
    res = {'kind': d[4:8].decode(), 'sections': {k: v[1] for k, v in secs.items()}}

    # INF1
    o = secs['INF1'][0]
    flags, _, npk, nvtx, hoff = struct.unpack_from('>HHIII', d, o + 8)
    res['inf1_vertex_count'] = nvtx
    res['inf1_packets'] = npk

    # VTX1
    o, sz = secs['VTX1']
    fmt_off = struct.unpack_from('>I', d, o + 8)[0]
    data_offs = list(struct.unpack_from('>13I', d, o + 0x0C))
    fmts = {}
    p = o + fmt_off
    while True:
        a, cnt, typ, frac = struct.unpack_from('>IIIB', d, p)
        if a == 0xFF:
            break
        fmts[a] = (cnt, typ, frac)
        p += 16
    slot_of = {9: 0, 10: 1, 25: 2, 11: 3, 12: 4}
    for t in range(8):
        slot_of[13 + t] = 5 + t

    def arr_len(slot):
        st = data_offs[slot]
        if st == 0:
            return 0, 0
        nxt = [x for x in data_offs if x > st]
        end = min(nxt) if nxt else sz
        return st, end - st

    st, ln = arr_len(0)
    cnt, typ, frac = fmts[9]
    ncomp = 3 if cnt == 1 else 2
    if typ == 4:
        pos = np.frombuffer(d, '>f4', count=ln // 4, offset=o + st)
    elif typ == 3:
        pos = np.frombuffer(d, '>i2', count=ln // 2, offset=o + st).astype(np.float64) / (1 << frac)
    elif typ == 2:
        pos = np.frombuffer(d, '>u2', count=ln // 2, offset=o + st).astype(np.float64) / (1 << frac)
    else:
        raise ValueError('pos type %d' % typ)
    pos = pos[:len(pos) // ncomp * ncomp].reshape(-1, ncomp).astype(np.float64)
    res['vtx_attrs'] = {ATTR.get(a, a): dict(comp=c, type=t, frac=f) for a, (c, t, f) in fmts.items()}
    res['positions'] = len(pos)
    colors = None
    if 11 in fmts:
        st, ln = arr_len(3)
        if fmts[11][1] == 5:
            colors = np.frombuffer(d, np.uint8, count=ln // 4 * 4, offset=o + st).reshape(-1, 4)
        elif fmts[11][1] == 1:
            colors = np.frombuffer(d, np.uint8, count=ln // 3 * 3, offset=o + st).reshape(-1, 3)
    res['colors'] = 0 if colors is None else len(colors)

    # SHP1
    o, sz = secs['SHP1']
    nshp = struct.unpack_from('>H', d, o + 8)[0]
    init_off, remap_off, _, attr_off, mtxtab_off, dl_off, mtxdata_off, pkt_off = struct.unpack_from('>8I', d, o + 0x0C)
    tris = []
    tri_col = []
    tri_shape = []
    prim_counts = {}
    for s in range(nshp):
        sp = o + init_off + s * 0x28
        mtype, _, npkt, aoff, fmtx, fpkt = struct.unpack_from('>BBHHHH', d, sp)
        descs = []
        q = o + attr_off + aoff
        while True:
            a, t = struct.unpack_from('>II', d, q)
            if a == 0xFF:
                break
            descs.append((a, t))
            q += 8
        for pk in range(npkt):
            psz, poff = struct.unpack_from('>II', d, o + pkt_off + (fpkt + pk) * 8)
            q = o + dl_off + poff
            end = q + psz
            while q < end:
                op = d[q]
                q += 1
                if op == 0:
                    continue
                prim = op & 0xF8
                if prim not in (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8):
                    break
                n = struct.unpack_from('>H', d, q)[0]
                q += 2
                verts = []
                for _ in range(n):
                    pi = ci = 0
                    for a, t in descs:
                        # GX attribute types: 1 DIRECT (matrix indices: one byte), 2 INDEX8, 3 INDEX16
                        if t == 2 or t == 1:
                            v = d[q]
                            q += 1
                        elif t == 3:
                            v = struct.unpack_from('>H', d, q)[0]
                            q += 2
                        else:
                            v = 0
                        if a == 9:
                            pi = v
                        elif a == 11:
                            ci = v
                    verts.append((pi, ci))
                prim_counts[prim] = prim_counts.get(prim, 0) + 1
                before = len(tris)
                if prim == 0x90:
                    for i in range(0, n - 2, 3):
                        tris.append((verts[i][0], verts[i + 1][0], verts[i + 2][0]))
                        tri_col.append(verts[i][1])
                elif prim == 0x98:
                    for i in range(n - 2):
                        a, b, c = verts[i], verts[i + 1], verts[i + 2]
                        if i & 1:
                            a, b = b, a
                        tris.append((a[0], b[0], c[0]))
                        tri_col.append(a[1])
                elif prim == 0xA0:
                    for i in range(1, n - 1):
                        tris.append((verts[0][0], verts[i][0], verts[i + 1][0]))
                        tri_col.append(verts[0][1])
                elif prim == 0x80:
                    for i in range(0, n - 3, 4):
                        tris.append((verts[i][0], verts[i + 1][0], verts[i + 2][0]))
                        tris.append((verts[i][0], verts[i + 2][0], verts[i + 3][0]))
                        tri_col += [verts[i][1]] * 2
                tri_shape += [s] * (len(tris) - before)
    res['shapes'] = nshp
    res['triangles'] = len(tris)
    res['prims'] = {hex(k): v for k, v in prim_counts.items()}

    # JNT1
    if 'JNT1' in secs:
        o = secs['JNT1'][0]
        res['joints'] = struct.unpack_from('>H', d, o + 8)[0]
        jnames_off = struct.unpack_from('>I', d, o + 0x14)[0]
        res['joint_names'] = nametable(d, o + jnames_off)[:8]
    # MAT3
    if 'MAT3' in secs:
        o = secs['MAT3'][0]
        res['materials'] = struct.unpack_from('>H', d, o + 8)[0]
        mn_off = struct.unpack_from('>I', d, o + 0x14)[0]
        res['material_names'] = nametable(d, o + mn_off)
    # TEX1
    if 'TEX1' in secs:
        o = secs['TEX1'][0]
        ntex = struct.unpack_from('>H', d, o + 8)[0]
        hdr_off, nm_off = struct.unpack_from('>II', d, o + 0x0C)
        names = nametable(d, o + nm_off)
        texs = []
        for i in range(ntex):
            h = o + hdr_off + i * 0x20
            fmt, alpha, w, hgt = struct.unpack_from('>BBHH', d, h)
            texs.append('%s %dx%d %s' % (names[i] if i < len(names) else '?', w, hgt, TEXFMT.get(fmt, fmt)))
        res['textures'] = texs
    # Shape -> material (INF1 scene graph: 0x11 material node encloses 0x12 shape nodes)
    o = secs['INF1'][0]
    q = o + hoff
    cur_mat = 0
    shape_mat = {}
    while True:
        typ, idx = struct.unpack_from('>HH', d, q)
        q += 4
        if typ == 0:
            break
        if typ == 0x11:
            cur_mat = idx
        elif typ == 0x12:
            shape_mat[idx] = cur_mat
    res['_shape_mat'] = shape_mat
    # Material -> first texture's average colour, and material colour
    mat_rgb = {}
    if 'MAT3' in secs and 'TEX1' in secs:
        o = secs['MAT3'][0]
        offs = struct.unpack_from('>30I', d, o + 0x0C)
        init_off, remap_off = offs[0], offs[1]
        matcol_off = offs[5]
        texidx_off = offs[15]
        t1 = secs['TEX1'][0]
        ntex = struct.unpack_from('>H', d, t1 + 8)[0]
        thdr = struct.unpack_from('>I', d, t1 + 0x0C)[0]
        avg = [tex_average(d, t1 + thdr + i * 0x20) for i in range(ntex)]
        nmat = struct.unpack_from('>H', d, o + 8)[0]
        for m in range(nmat):
            entry = struct.unpack_from('>H', d, o + remap_off + m * 2)[0]
            e = o + init_off + entry * 0x14C
            texno = struct.unpack_from('>H', d, e + 0x84)[0]
            mc_idx = struct.unpack_from('>H', d, e + 0x08)[0]
            rgb = None
            if texno != 0xFFFF:
                ti = struct.unpack_from('>H', d, o + texidx_off + texno * 2)[0]
                if ti < len(avg):
                    rgb = avg[ti]
            if mc_idx != 0xFFFF and matcol_off:
                mcol = np.frombuffer(d, np.uint8, 4, o + matcol_off + mc_idx * 4)[:3].astype(float)
                rgb = mcol if rgb is None else rgb * (mcol / 255.0) if mcol.mean() > 20 else rgb
            mat_rgb[m] = rgb
    res['_mat_rgb'] = mat_rgb
    res['_tri_shape'] = np.array(tri_shape, dtype=np.int64)
    res['_pos'] = pos
    res['_tris'] = np.array(tris, dtype=np.int64) if tris else np.zeros((0, 3), np.int64)
    res['_colors'] = colors
    res['_tri_col'] = np.array(tri_col, dtype=np.int64)
    return res
