"""Bake a textured overhead picture of each course from the owner's verified GM4E01 disc.

The course's own model (<course>_course.bmd) is drawn straight down, in the retail minimap's
texture axes and bounds (duo_hud.json, staged by stage_art.py), so the duo screen's markers and
map placement apply to it unchanged. Approximate J3D shading: texture 0 of each material, times
the vertex colour (or the material colour), the material's alpha test and blend; height decides
what is on top. Output is private disc-derived art: never commit, package publicly or publish it.

Usage: bake_overview.py --iso <GM4E01.iso> [--art M:/.../lower-art] [--course 0x24 ...]
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import numpy as np
from PIL import Image

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))  # run with python -I
import stage_art as sa  # noqa: E402

LONG_SIDE = 1024
SUPERSAMPLE = 2
# Roofs, canopies and sky domes would hide the road from above: geometry more than this far
# above the nearest point of the CPU racing line (bol) is left out.
CEILING = 1200.0
# Only over the road, though: hills, castles and trees beside it stay as they are.
ROAD = 2500.0
# Sky domes sit far above the whole course: nothing this far above its highest road is drawn.
SKY = 3000.0
FMT = {0: ('B', 1), 1: ('b', 1), 2: ('H', 2), 3: ('h', 2), 4: ('f', 4)}


# ---------------------------------------------------------------- BMD sections

def sections(b):
    out, at = {}, 0x20
    while at + 8 <= len(b):
        tag = b[at:at + 4].decode('ascii', 'replace')
        size = struct.unpack_from('>I', b, at + 4)[0]
        out[tag] = at
        if size == 0:
            break
        at += size
    return out


def vtx1(b, o):
    fmt = o + struct.unpack_from('>I', b, o + 8)[0]
    offs = list(struct.unpack_from('>13I', b, o + 0x0C))
    size = struct.unpack_from('>I', b, o + 4)[0]
    # attribute id -> array slot: POS 9, NRM 10, NBT 25, CLR0 11, CLR1 12, TEX0..7 13..20
    slot = {9: 0, 10: 1, 25: 2, 11: 3, 12: 4, **{13 + i: 5 + i for i in range(8)}}
    arrays = {}
    while True:
        attr, cnt, typ, frac = struct.unpack_from('>IIIB', b, fmt)
        fmt += 16
        if attr == 0xFF:
            break
        s = slot[attr]
        start = offs[s]
        if not start:
            continue
        later = [x for x in offs if x > start] + [size]
        end = min(later)
        data = b[o + start:o + end]
        if attr in (11, 12):  # colours
            if typ == 5:   # RGBA8
                a = np.frombuffer(data[:len(data) // 4 * 4], np.uint8).reshape(-1, 4).astype(np.float32) / 255
            elif typ == 2:  # RGB8 (RGBX)
                a = np.frombuffer(data[:len(data) // 4 * 4], np.uint8).reshape(-1, 4).astype(np.float32) / 255
                a[:, 3] = 1
            elif typ == 1:  # RGB888
                a = np.frombuffer(data[:len(data) // 3 * 3], np.uint8).reshape(-1, 3).astype(np.float32) / 255
                a = np.concatenate([a, np.ones((len(a), 1), np.float32)], 1)
            else:
                raise ValueError(f'colour format {typ}')
        else:
            n = {9: 3 if cnt else 2, 10: 3}.get(attr, 2 if cnt else 1)
            ch, w = FMT[typ]
            k = len(data) // (w * n)
            a = np.array(struct.unpack_from('>' + ch * (k * n), data), np.float32).reshape(k, n)
            if typ != 4:
                a /= float(1 << frac)
        arrays[attr] = a
    return arrays


def inf1_shape_materials(b, o):
    """Shape index -> material index, from the scene hierarchy."""
    at = o + struct.unpack_from('>I', b, o + 0x14)[0]
    mat, out = 0, {}
    while True:
        kind, index = struct.unpack_from('>HH', b, at)
        at += 4
        if kind == 0:
            break
        if kind == 0x11:
            mat = index
        elif kind == 0x12:
            out[index] = mat
    return out


def jnt1_matrix(b, o):
    """The root joint's transform (courses have one joint)."""
    data = o + struct.unpack_from('>I', b, o + 0x0C)[0]
    sx, sy, sz = struct.unpack_from('>3f', b, data + 4)
    rx, ry, rz = (v * math.pi / 0x8000 for v in struct.unpack_from('>3h', b, data + 0x10))
    tx, ty, tz = struct.unpack_from('>3f', b, data + 0x18)
    cx, sx_, cy, sy_, cz, sz_ = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    R = np.array([[cy * cz, sx_ * sy_ * cz - cx * sz_, cx * sy_ * cz + sx_ * sz_],
                  [cy * sz_, sx_ * sy_ * sz_ + cx * cz, cx * sy_ * sz_ - sx_ * cz],
                  [-sy_, sx_ * cy, cx * cy]], np.float64)
    M = np.eye(4)
    M[:3, :3] = R * np.array([sx, sy, sz])
    M[:3, 3] = (tx, ty, tz)
    return M


def shp1(b, o):
    """Per shape: list of triangles, each three dicts attr -> index."""
    count = struct.unpack_from('>H', b, o + 8)[0]
    init, remap, _, descs, _, dl, _, draw = struct.unpack_from('>8I', b, o + 0x0C)
    shapes = []
    for si in range(count):
        idx = struct.unpack_from('>H', b, o + remap + si * 2)[0]
        rec = o + init + idx * 0x28
        groups, desc_off, _, first_draw = struct.unpack_from('>HHHH', b, rec + 2)
        d = o + descs + desc_off
        attrs = []
        while True:
            attr, typ = struct.unpack_from('>II', b, d)
            d += 8
            if attr == 0xFF:
                break
            attrs.append((attr, typ))
        tris = []
        for g in range(groups):
            size, off = struct.unpack_from('>II', b, o + draw + (first_draw + g) * 8)
            tris += display_list(b, o + dl + off, size, attrs)
        shapes.append(tris)
    return shapes


def display_list(b, at, size, attrs):
    end, tris = at + size, []
    while at < end:
        op = b[at]
        at += 1
        if op == 0:
            continue
        if op in (0x20, 0x28, 0x30, 0x38):
            at += 4
            continue
        if op == 0x08:
            at += 5
            continue
        if op & 0xF8 not in (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8):
            break
        prim = op & 0xF8
        n = struct.unpack_from('>H', b, at)[0]
        at += 2
        verts = []
        for _ in range(n):
            v = {}
            for attr, typ in attrs:
                if typ == 1:
                    w = 1 if attr <= 8 else 0
                    if attr <= 8:
                        v[attr] = b[at]
                    at += w
                elif typ == 2:
                    v[attr] = b[at]
                    at += 1
                elif typ == 3:
                    v[attr] = struct.unpack_from('>H', b, at)[0]
                    at += 2
            verts.append(v)
        if prim == 0x90:
            tris += [tuple(verts[i:i + 3]) for i in range(0, n - 2, 3)]
        elif prim == 0x98:
            for i in range(n - 2):
                tris.append((verts[i], verts[i + 1], verts[i + 2]) if i % 2 == 0 else (verts[i + 1], verts[i], verts[i + 2]))
        elif prim == 0xA0:
            tris += [(verts[0], verts[i], verts[i + 1]) for i in range(1, n - 1)]
        elif prim == 0x80:
            for i in range(0, n - 3, 4):
                tris += [(verts[i], verts[i + 1], verts[i + 2]), (verts[i], verts[i + 2], verts[i + 3])]
    return tris


def mat3(b, o):
    """Per material: its textures (slot k with texcoord k), material colour, alpha test, blend."""
    count = struct.unpack_from('>H', b, o + 8)[0]
    t = struct.unpack_from('>30I', b, o + 0x0C)
    init, remap = t[0], t[1]
    mat_color, color_chan, tex_coord, tex_mtx, tex_no = t[5], t[7], t[11], t[13], t[15]
    alpha_comp, blend = t[24], t[25]
    out = []
    for mi in range(count):
        idx = struct.unpack_from('>H', b, o + remap + mi * 2)[0]
        rec = o + init + idx * 0x14C
        mc = struct.unpack_from('>H', b, rec + 0x08)[0]
        color = np.frombuffer(b[o + mat_color + mc * 4:o + mat_color + mc * 4 + 4], np.uint8).astype(np.float32) / 255 if mc != 0xFFFF else np.ones(4, np.float32)
        cc = struct.unpack_from('>H', b, rec + 0x0C)[0]
        vertex_color = True
        if cc != 0xFFFF:
            # ColorChanInfo: enable, matColorSrc (0 register, 1 vertex), ...
            vertex_color = b[o + color_chan + cc * 8 + 1] == 1
        layers = []  # (texture, vertex attribute of its coordinates, SRT or None)
        for slot in range(8):
            tn = struct.unpack_from('>H', b, rec + 0x84 + slot * 2)[0]
            if tn == 0xFFFF:
                continue
            tex = struct.unpack_from('>H', b, o + tex_no + tn * 2)[0]
            tc = struct.unpack_from('>H', b, rec + 0x28 + slot * 2)[0]
            src, mtx = 4, None  # GX_TG_TEX0
            if tc != 0xFFFF:
                _, src, mid = b[o + tex_coord + tc * 4:o + tex_coord + tc * 4 + 3]
                if 0x1E <= mid < 0x3C:
                    tm = struct.unpack_from('>H', b, rec + 0x48 + ((mid - 0x1E) // 3) * 2)[0]
                    if tm != 0xFFFF:
                        e = o + tex_mtx + tm * 0x64
                        cu, cv, _ = struct.unpack_from('>3f', b, e + 4)
                        su, sv = struct.unpack_from('>2f', b, e + 0x10)
                        rot = struct.unpack_from('>h', b, e + 0x18)[0] * math.pi / 0x8000
                        tu, tv = struct.unpack_from('>2f', b, e + 0x1C)
                        mtx = (cu, cv, su, sv, rot, tu, tv)
            if 4 <= src <= 11:  # coordinates from TEX0..7; projected and environment maps are left out
                layers.append((tex, 13 + src - 4, mtx))
        ac = struct.unpack_from('>H', b, rec + 0x12E)[0]
        alpha_ref = None
        if ac != 0xFFFF:
            comp0, ref0, op, comp1, ref1 = b[o + alpha_comp + ac * 8:o + alpha_comp + ac * 8 + 5]
            if comp0 in (4, 6):  # GREATER / GEQUAL
                alpha_ref = ref0 / 255
        bl = struct.unpack_from('>H', b, rec + 0x130)[0]
        blended = bl != 0xFFFF and b[o + blend + bl * 4] == 1
        out.append(dict(color=color, vertex_color=vertex_color, layers=layers, alpha_ref=alpha_ref, blended=blended))
    return out


def tex1(b, o):
    count = struct.unpack_from('>H', b, o + 8)[0]
    hdr = o + struct.unpack_from('>I', b, o + 0x0C)[0]
    out = []
    for i in range(count):
        h = hdr + i * 32
        head = bytearray(b[h:h + 32])
        # Re-base the image (and palette) offsets so stage_art.bti reads a standalone BTI.
        data_off = struct.unpack_from('>I', head, 0x1C)[0]
        try:
            w, hgt, rgba = sa.bti(bytes(head[:0x1C]) + struct.pack('>I', 0x20) + b[h + data_off:])
            img = np.frombuffer(rgba, np.uint8).reshape(hgt, w, 4).astype(np.float32) / 255
        except Exception as e:  # unsupported format: a flat mid grey stands in for its colour
            print(f'    texture {i}: {e}')
            img = np.full((1, 1, 4), 0.5, np.float32)
        out.append((img, head[6], head[7], head[0]))
    return out


# ---------------------------------------------------------------- raster

def wrap(c, n, mode):
    if mode == 0:  # clamp
        return np.clip(c, 0, n - 1)
    if mode == 2:  # mirror
        c = np.mod(c, 2 * n)
        return np.where(c >= n, 2 * n - 1 - c, c)
    return np.mod(c, n)


def bake(bmd, bol, axes, bounds, out_png):
    sec = sections(bmd)
    arrays = vtx1(bmd, sec['VTX1'])
    M = jnt1_matrix(bmd, sec['JNT1'])
    shape_mat = inf1_shape_materials(bmd, sec['INF1'])
    shapes = shp1(bmd, sec['SHP1'])
    mats = mat3(bmd, sec['MAT3'])
    textures = tex1(bmd, sec['TEX1'])
    pos = arrays[9] @ M[:3, :3].T + M[:3, 3]
    min_u, min_v, max_u, max_v = bounds
    span_u, span_v = max_u - min_u, max_v - min_v
    scale = LONG_SIDE * SUPERSAMPLE / max(span_u, span_v)
    W, H = int(round(span_u * scale)), int(round(span_v * scale))

    def along(name):  # stage_art.axis over every vertex at once
        v = pos[:, 0] if name[-1] == 'x' else pos[:, 2]
        return -v if name[0] == '-' else v

    px_u, px_v = along(axes[0]), along(axes[1])
    sx = (px_u - min_u) * scale
    sy = (px_v - min_v) * scale
    height = pos[:, 1]
    capped = ceiling(bol, axes, min_u, min_v, scale, W, H)
    under = draw(shapes, shape_mat, mats, textures, arrays, sx, sy, height, W, H, capped)
    sky = np.full((H, W), max(sa.racing_line(bol)[0], key=lambda q: q[1])[1] + SKY, np.float32)
    top = draw(shapes, shape_mat, mats, textures, arrays, sx, sy, height, W, H, sky)
    # Under the ceiling where it drew anything; elsewhere (hills far from the road) the top surface.
    img = np.where(under[..., 3:4] > 0, under, top)
    pic = Image.fromarray((img * 255 + .5).astype(np.uint8), 'RGBA')
    pic = pic.resize((W // SUPERSAMPLE, H // SUPERSAMPLE), Image.LANCZOS)
    pic.save(out_png)
    return pic.size


def draw(shapes, shape_mat, mats, textures, arrays, sx, sy, height, W, H, cap):
    color = np.zeros((H, W, 3), np.float32)
    depth = np.full((H, W), -np.inf, np.float32)
    coverage = np.zeros((H, W), np.float32)
    clr = arrays.get(11)
    passes = [False, True]  # opaque, then blended
    for blended_pass in passes:
        for si, tris in enumerate(shapes):
            m = mats[shape_mat.get(si, 0)]
            if m['blended'] != blended_pass:
                continue
            # An intensity texture first is a detail map over the next layer's colour: both, multiplied.
            layers = [l for l in m['layers'] if l[0] < len(textures)]
            if layers and textures[layers[0][0]][3] in (0, 1, 2, 3):
                layers = layers[:2]
            else:
                layers = layers[:1]
            for t in tris:
                if any(9 not in v for v in t):
                    continue
                i = [v[9] for v in t]
                x, y, z = sx[i], sy[i], height[i]
                x0, x1 = int(max(0, math.floor(x.min()))), int(min(W - 1, math.ceil(x.max())))
                y0, y1 = int(max(0, math.floor(y.min()))), int(min(H - 1, math.ceil(y.max())))
                if x0 > x1 or y0 > y1:
                    continue
                den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
                if abs(den) < 1e-9:
                    continue
                gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
                a = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / den
                bb = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / den
                c = 1 - a - bb
                inside = (a >= -1e-6) & (bb >= -1e-6) & (c >= -1e-6)
                if not inside.any():
                    continue
                zz = a * z[0] + bb * z[1] + c * z[2]
                region = depth[y0:y1 + 1, x0:x1 + 1]
                front = inside & (zz >= region)
                if cap is not None:
                    front &= zz <= cap[y0:y1 + 1, x0:x1 + 1]
                if not front.any():
                    continue
                rgba = np.ones(front.shape + (4,), np.float32)
                for tex_i, attr, srt in layers:
                    coords = arrays.get(attr)
                    if coords is None or not all(attr in v for v in t):
                        continue
                    tex, wrap_s, wrap_t, _ = textures[tex_i]
                    st = coords[[v[attr] for v in t]]
                    s_ = a * st[0, 0] + bb * st[1, 0] + c * st[2, 0]
                    t_ = a * st[0, 1] + bb * st[1, 1] + c * st[2, 1]
                    if srt:
                        cu, cv, su, sv, rot, tu, tv = srt
                        s0, t0 = (s_ - cu) * su, (t_ - cv) * sv
                        cr, sr = math.cos(rot), math.sin(rot)
                        s_, t_ = s0 * cr - t0 * sr + cu + tu, s0 * sr + t0 * cr + cv + tv
                    th, tw = tex.shape[:2]
                    ix = wrap(np.floor(s_ * tw).astype(np.int64), tw, wrap_s)
                    iy = wrap(np.floor(t_ * th).astype(np.int64), th, wrap_t)
                    rgba = rgba * tex[iy, ix]
                if m['vertex_color'] and clr is not None and all(11 in v for v in t):
                    k = clr[[v[11] for v in t]]
                    rgba = rgba * (a[..., None] * k[0] + bb[..., None] * k[1] + c[..., None] * k[2])
                else:
                    rgba = rgba * m['color']
                alpha = rgba[..., 3]
                if m['alpha_ref'] is not None:
                    front &= alpha >= max(m['alpha_ref'], 1e-3)
                if blended_pass:
                    w = np.where(front, np.clip(alpha, 0, 1), 0)[..., None]
                    color[y0:y1 + 1, x0:x1 + 1] = color[y0:y1 + 1, x0:x1 + 1] * (1 - w) + rgba[..., :3] * w
                    coverage[y0:y1 + 1, x0:x1 + 1] = np.maximum(coverage[y0:y1 + 1, x0:x1 + 1], w[..., 0])
                else:
                    sel = front & (m['alpha_ref'] is not None or True)
                    color[y0:y1 + 1, x0:x1 + 1][sel] = rgba[..., :3][sel]
                    region[sel] = zz[sel]
                    coverage[y0:y1 + 1, x0:x1 + 1][sel] = 1
    return np.concatenate([np.clip(color, 0, 1), coverage[..., None]], 2)


def ceiling(bol, axes, min_u, min_v, scale, W, H, cell=8):
    """Per pixel near the road, the highest height drawn: the nearest racing-line point's height plus
    CEILING. Elsewhere no limit."""
    pts = np.array(sa.densify(sa.racing_line(bol), 8), np.float64)
    def along(name):
        v = pts[:, 0] if name[-1] == 'x' else pts[:, 2]
        return -v if name[0] == '-' else v
    pu, pv = (along(axes[0]) - min_u) * scale, (along(axes[1]) - min_v) * scale
    gy, gx = np.mgrid[0:H:cell, 0:W:cell].astype(np.float64) + cell / 2
    best = np.full(gx.shape, np.inf)
    cap = np.zeros(gx.shape)
    for i in range(0, len(pts), 256):
        d = (gx[..., None] - pu[i:i + 256]) ** 2 + (gy[..., None] - pv[i:i + 256]) ** 2
        k = d.argmin(-1)
        near = np.take_along_axis(d, k[..., None], -1)[..., 0]
        better = near < best
        best[better] = near[better]
        cap[better] = pts[i:i + 256, 1][k[better]]
    cap = np.where(np.sqrt(best) / scale <= ROAD, cap + CEILING, pts[:, 1].max() + SKY)
    cap = np.kron(cap, np.ones((cell, cell)))[:H, :W]
    return cap.astype(np.float32)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso', type=Path, required=True)
    p.add_argument('--art', type=Path, default=Path('M:/WiiDS-mkdd-2026-11-01/lower-art'))
    p.add_argument('--out', type=Path, default=Path('M:/WiiDS-mkdd-2026-11-01/overview'))
    p.add_argument('--course', type=lambda v: int(v, 0), nargs='*')
    a = p.parse_args()
    out = a.out.resolve()
    if out == sa.REPO or sa.REPO in out.parents:
        p.error('Refusing to write disc-derived art inside the source tree')
    manifest = json.loads((a.art / 'duo_hud.json').read_text(encoding='utf-8'))
    out.mkdir(parents=True, exist_ok=True)
    receipt = {}
    with a.iso.resolve(strict=True).open('rb') as f:
        if f.seek(0, 2) != sa.DISC_BYTES:
            p.error('Not the complete GM4E01 image')
        files = sa.disc_files(f)
        for cid, arc in sa.COURSES.items():
            if a.course and cid not in a.course:
                continue
            entry = manifest['maps'].get(str(cid))
            if not entry or entry.get('alignment') != 'verified':
                print(f'{arc:10} skipped (no verified map placement)')
                continue
            off, size = files[f'/Course/{arc}.arc']
            f.seek(off)
            course = sa.rarc(sa.yaz0(f.read(size)))
            bmd = course[f'{arc.lower()}_course.bmd']
            bol = course[f'{arc.lower()}_course.bol']
            name = f'duo_overview_{cid:02d}.png'
            w, h = bake(bmd, bol, entry['axes'], entry['bounds'], out / name)
            receipt[str(cid)] = {'png': name, 'size': [w, h], 'axes': entry['axes'], 'bounds': entry['bounds'],
                                 'sha256': hashlib.sha256((out / name).read_bytes()).hexdigest()}
            print(f'{arc:10} {w}x{h}')
    (out / 'overview.json').write_text(json.dumps({'schema': 1, 'game': 'GM4E01', 'overviews': receipt}, indent=1) + '\n')


if __name__ == '__main__':
    main()
