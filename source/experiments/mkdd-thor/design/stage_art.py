"""Stage lower-screen art from the owner's verified GM4E01 disc into a private folder.

Reads the image in place (no extraction to disk besides the PNGs it writes), checks
the exact disc SHA-1 first, and refuses to write inside this source tree. Output is
private disc-derived art: never commit, package publicly or publish it.

Course maps are staged as "unverified": their placement is not decompiled yet, and
DuoArt ignores a map until Codex's alignment check marks it verified with bounds.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zlib

DISC_BYTES = 1459978240
DISC_SHA1 = '34bdef709a1ae9799fdabe8e54cff1c7d3665f2b'
REPO = Path(__file__).resolve().parents[3]

# ECharID 1..20 -> race2d portrait / map icon base names (Race2D.cpp map-icon table order).
CHARACTERS = ['baby_mario', 'baby_luigi', 'pata', 'noko', 'peach', 'daisy', 'mario', 'luigi', 'wario',
              'waluigi', 'yoshi', 'catherine', 'donky', 'diddy', 'koopa', 'koopa_jr', 'kinopio',
              'kinopico', 'teresa', 'pakkun']
MAP_ICONS = ['babymario', 'babyluigi', 'patapata', 'nokonoko', 'peach', 'daisy', 'mario', 'luigi', 'wario',
             'waluigi', 'yoshi', 'catherine', 'donkeykong', 'diddykong', 'koopa', 'koopajr', 'kinopio',
             'kinopico', 'teresa', 'pakkun']
# Raw item kinds with direct decomp evidence (see DuoNames). Triple kinds use the full-stack icon.
ITEMS = {0: 'item_kame_green', 2: 'item_kame_red', 3: 'item_banana', 4: 'item_big_banana',
         5: 'item_kinoko', 7: 'item_wanwan', 8: 'item_bomb_hei', 11: 'item_egg', 12: 'item_goldenkinoko',
         13: 'item_kame_wing', 15: 'item_dummybox', 17: 'item_kame_green_3', 18: 'item_kinoko_3',
         19: 'item_kame_red_3', 21: 'item_fire_ball'}
# Holder-specific icons: Birdo's egg, Luigi's green fireballs.
ITEM_VARIANTS = {(11, 12): 'item_egg_catherine', (21, 8): 'item_fire_ball_luigi'}
# Probable but unconfirmed kinds; staged for the live check, ignored by DuoArt.
UNVERIFIED_ITEMS = {1: 'item_koopa', 6: 'item_star', 10: 'item_thunder', 14: 'item_heart'}
# The game's own HUD lettering and frames (MkddArt.HUD): manifest key -> (archive, texture).
HUD = {'lap': ('loc', 'im_lap1_1'), 'time': ('loc', 'im_time1_3'), 'best_lap': ('loc', 'm_best_lap'),
       'p1': ('loc', 'playernumbersimple_1p'), 'p1_map': ('loc', 'map_icon_player1'), 'mirror': ('loc', 'power_mozi.4'),
       'window': ('race2d', 'chara_window2'), 'logo': ('race2d', 'e3_mariokart_copy3'),
       'glyph_colon': ('race2d', 'mariofontcolon'), 'glyph_period': ('race2d', 'mariofontperiod'),
       'glyph_slash': ('race2d', 'mariofontslash'), 'glyph_exclamation': ('race2d', 'mariofontexclamation'),
       'glyph_question': ('race2d', 'mariofontquestion'), 'glyph_dash': ('race2d', 'mariofont_number_xline')}
HUD.update({f'glyph_{c}': ('race2d', f'mariofont_{c}') for c in 'abcdefghijklmnopqrstuvwxyz'})
HUD.update({f'glyph_{d}': ('race2d', f'mariofont_number_{d}') for d in range(10)})
# ECourseID -> course archive (ResMgr.cpp getCrsArcName).
COURSES = {0x21: 'BabyLuigi', 0x22: 'Peach', 0x23: 'Daisy', 0x24: 'Luigi', 0x25: 'Mario', 0x26: 'Yoshi',
           0x28: 'Nokonoko', 0x29: 'Patapata', 0x2a: 'Waluigi', 0x2b: 'Wario', 0x2c: 'Diddy',
           0x2d: 'Donkey', 0x2f: 'Koopa', 0x31: 'Rainbow', 0x32: 'Desert', 0x33: 'Snow'}


def disc_files(f):
    f.seek(0x424)
    off, size = struct.unpack('>II', f.read(8))
    f.seek(off)
    t = f.read(size)
    count = struct.unpack_from('>I', t, 8)[0]
    names = t[count*12:]
    out, stack = {}, [(count, '')]
    for i in range(1, count):
        while stack and i >= stack[-1][0]:
            stack.pop()
        word, a, b = struct.unpack_from('>III', t, i*12)
        o = word & 0xffffff
        n = stack[-1][1] + '/' + names[o:names.index(b'\0', o)].decode('shift-jis')
        if word >> 24:
            stack.append((b, n))
        else:
            out[n] = (a, b)
    return out


def yaz0(d):
    if d[:4] != b'Yaz0':
        return d
    size = struct.unpack_from('>I', d, 4)[0]
    out, i = bytearray(), 16
    while len(out) < size:
        code = d[i]; i += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if code & (0x80 >> bit):
                out.append(d[i]); i += 1
                continue
            b1, b2 = d[i], d[i+1]; i += 2
            dist, n = ((b1 & 0xf) << 8 | b2) + 1, b1 >> 4
            if n == 0:
                n = d[i] + 0x12; i += 1
            else:
                n += 2
            for _ in range(n):
                out.append(out[-dist])
    return bytes(out)


def rarc(d):
    """{lowercase file name: bytes} for every file in a RARC archive."""
    if d[:4] != b'RARC':
        raise ValueError('Not a RARC archive')
    data = struct.unpack_from('>I', d, 0xc)[0] + 0x20
    _, _, count, entries, _, strings = struct.unpack_from('>IIIIII', d, 0x20)
    entries += 0x20; strings += 0x20
    out = {}
    for e in range(count):
        _, _, kind, name, a, b = struct.unpack_from('>HHHHII', d, entries + e*20)
        if not kind & 0x0200:
            n = d[strings+name:d.index(b'\0', strings+name)].decode('shift-jis').lower()
            out[n] = d[data+a:data+a+b]
    return out


def rgb565(v):
    return ((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31


def rgb5a3(v):
    if v & 0x8000:
        return (*(((v >> s) & 31) * 255 // 31 for s in (10, 5, 0)), 255)
    return (*(((v >> s) & 15) * 17 for s in (8, 4, 0)), ((v >> 12) & 7) * 255 // 7)


def bti(blob):
    """Decode a BTI texture to (width, height, RGBA bytes)."""
    fmt, _, w, h = struct.unpack_from('>BBHH', blob, 0)
    data = blob[struct.unpack_from('>I', blob, 0x1c)[0]:]
    px = bytearray(w*h*4)

    def put(x, y, rgba):
        if x < w and y < h:
            px[(y*w+x)*4:(y*w+x)*4+4] = bytes(rgba)

    blocks = {0: (8, 8), 1: (8, 4), 2: (8, 4), 3: (4, 4), 4: (4, 4), 5: (4, 4), 6: (4, 4), 14: (8, 8)}
    if fmt not in blocks:
        raise ValueError(f'Unsupported texture format {fmt}')
    bw, bh = blocks[fmt]
    pos = 0
    for by in range(0, h, bh):
        for bx in range(0, w, bw):
            if fmt == 14:
                for sub in range(4):
                    c0, c1, bits = struct.unpack_from('>HHI', data, pos); pos += 8
                    a, b = rgb565(c0), rgb565(c1)
                    if c0 > c1:
                        pal = [(*a, 255), (*b, 255), (*((2*p+q)//3 for p, q in zip(a, b)), 255),
                               (*((p+2*q)//3 for p, q in zip(a, b)), 255)]
                    else:
                        pal = [(*a, 255), (*b, 255), (*((p+q)//2 for p, q in zip(a, b)), 255), (0, 0, 0, 0)]
                    sx, sy = bx + (sub & 1)*4, by + (sub >> 1)*4
                    for i in range(16):
                        put(sx + i % 4, sy + i // 4, pal[(bits >> (30 - 2*i)) & 3])
            elif fmt == 6:
                for i in range(16):
                    ar, gb = data[pos+i*2:pos+i*2+2], data[pos+32+i*2:pos+32+i*2+2]
                    put(bx + i % 4, by + i // 4, (ar[1], gb[0], gb[1], ar[0]))
                pos += 64
            else:
                for i in range(bw*bh):
                    x, y = bx + i % bw, by + i // bw
                    if fmt == 0:
                        v = (data[pos + i//2] >> (4 if i % 2 == 0 else 0) & 15) * 17
                        put(x, y, (v, v, v, v))
                    elif fmt == 1:
                        v = data[pos+i]; put(x, y, (v, v, v, v))
                    elif fmt == 2:
                        v = data[pos+i]; g = (v & 15)*17; put(x, y, (g, g, g, (v >> 4)*17))
                    elif fmt == 3:
                        a, g = data[pos+i*2], data[pos+i*2+1]; put(x, y, (g, g, g, a))
                    else:
                        v = struct.unpack_from('>H', data, pos+i*2)[0]
                        put(x, y, (*rgb565(v), 255) if fmt == 4 else rgb5a3(v))
                pos += 32  # every non-CMPR, non-RGBA8 block is 32 bytes
    return w, h, bytes(px)


def png(w, h, rgba):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    rows = b''.join(b'\0' + rgba[y*w*4:(y+1)*w*4] for y in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


# Retail US minimap table, from RenolY2/mkdd-track-patcher resources/minimap_locations.json:
# corner 1 X/Z and corner 2 X/Z floats, then the code address of a `li r0, orientation`.
MINIMAPS = {0x21: (0x803CDDEC, 0x80141E14), 0x22: (0x803CDE0C, 0x80141EE0), 0x23: (0x803CDE34, 0x8014200C),
            0x24: (0x803CDE5C, 0x801420D8), 0x25: (0x803CDE74, 0x801421A4), 0x26: (0x803CDE90, 0x80142270),
            0x28: (0x803CDEB0, 0x8014233C), 0x29: (0x803CDEC8, 0x80142408), 0x2a: (0x803CDEE8, 0x801424D4),
            0x2b: (0x803CDF0C, 0x801425A0), 0x2c: (0x803CDF28, 0x8014266C), 0x2d: (0x803CDF40, 0x80142738),
            0x2f: (0x803CDF5C, 0x80142804), 0x31: (0x803CDF70, 0x801428D0), 0x32: (0x803CDF84, 0x8014299C),
            0x33: (0x803CDFA4, 0x80142A68)}
# Orientation -> which signed world axes run along the texture's u (right) and v (down). Derived by
# fitting each course's checkpoint line to its map with all 8 layouts: every decisive fit agreed.
ORIENTATION_AXES = {0: ('x', 'z'), 1: ('-z', 'x'), 2: ('-x', '-z'), 3: ('z', '-x')}
# Courses below the automatic road check that were accepted after looking at checks/fit_*.png.
# Peach's white line follows the shoreline, not the CPU route across the wide sand. The retail
# projection agrees with the course model; fitting the route to the white line would move markers.
REVIEWED_MAPS = {
    0x22: 'checked 2026-11-03: retail projection and course-model overview agree; all 241 densified '
          'CPU-route points fit the bounds, but the shoreline art is not the racing centre line',
    0x28: 'checked 2026-11-01: only the bridge section, which the map art simplifies, is off road'}


def minimap(dol, course):
    """(axes, bounds in those axes) for a course, read from the retail executable."""
    offsets, addresses, sizes = (struct.unpack_from('>18I', dol, at) for at in (0, 0x48, 0x90))

    def at(va):
        for o, a, n in zip(offsets, addresses, sizes):
            if n and a <= va < a + n:
                return o + va - a
        raise ValueError(f'{va:#x} is outside the executable')
    table, code = MINIMAPS[course]
    x1, z1, x2, z2 = struct.unpack_from('>4f', dol, at(table))
    op = struct.unpack_from('>I', dol, at(code))[0]
    if op >> 16 != 0x3800 or op & 0xffff > 3 or not (x1 < x2 and z1 < z2):
        raise ValueError(f'Course {course:#x}: unexpected minimap data {op:#010x}')
    axes = ORIENTATION_AXES[op & 0xffff]
    lo = {'x': x1, 'z': z1, '-x': -x2, '-z': -z2}; hi = {'x': x2, 'z': z2, '-x': -x1, '-z': -z1}
    return axes, [lo[axes[0]], lo[axes[1]], hi[axes[0]], hi[axes[1]]]


def axis(p, name):
    v = p[0] if name[-1] == 'x' else p[2]
    return -v if name[0] == '-' else v


def racing_line(bol):
    """CPU racing-line points (SOblHeader clPointNum u16 @1A, clPointOffset u32 @44; 0x20 each)."""
    count, offset = struct.unpack_from('>H', bol, 0x1a)[0], struct.unpack_from('>I', bol, 0x44)[0]
    return [[struct.unpack_from('>3f', bol, offset + i*0x20) for i in range(count)]]


def densify(runs, steps=4):
    """Points every 1/steps along each run, so the reverse (road-covered) check sees a line."""
    out = []
    for run in runs:
        for a, b in zip(run, run[1:]):
            out += [tuple(a[k] + (b[k]-a[k])*t/steps for k in range(3)) for t in range(steps)]
        out += run[-1:]
    return out


def road_centre(bol):
    """Main-path checkpoint midpoints: the road's centre line, lap-complete (CrsData::getCheckPointTable).

    SOblHeader: checkPathNum u16 @1C, checkPathOffset u32 @48. CheckPath is 0x14 bytes (pointCount,
    link with bit 15 marking a side path); the 0x1C-byte SCheckPoints (cpStart, cpEnd) follow them.
    """
    paths, offset = struct.unpack_from('>H', bol, 0x1c)[0], struct.unpack_from('>I', bol, 0x48)[0]
    runs, at = [], offset + paths*0x14
    for i in range(paths):
        count, link = struct.unpack_from('>HH', bol, offset + i*0x14)
        run = []
        for j in range(count):
            sx, sy, sz, ex, ey, ez = struct.unpack_from('>6f', bol, at + j*0x1c)
            run.append(((sx+ex)/2, (sy+ey)/2, (sz+ez)/2))
        if not link & 0x8000:
            runs.append(run)
        at += count*0x1c
    return runs


def chamfer(inside):
    """Texel distance from every texel to the nearest texel where inside is False (3-4 chamfer)."""
    import numpy as np
    h, w = inside.shape
    d = np.where(inside, 1e9, 0.0)
    for ys, xs, steps in ((range(h), range(w), ((-1, -1, 4), (-1, 0, 3), (-1, 1, 4), (0, -1, 3))),
                          (range(h-1, -1, -1), range(w-1, -1, -1), ((1, 1, 4), (1, 0, 3), (1, -1, 4), (0, 1, 3)))):
        for y in ys:
            for x in xs:
                for dy, dx, cost in steps:
                    yy, xx = y+dy, x+dx
                    if 0 <= yy < h and 0 <= xx < w and d[yy, xx] + cost < d[y, x]:
                        d[y, x] = d[yy, xx] + cost
    return d / 3


def road_field(rgba, w, h):
    """Signed texel field: depth inside the drawn road (centre line highest), minus distance outside."""
    import numpy as np
    road = np.frombuffer(rgba, np.uint8)[3::4].reshape(h, w) > 0
    return chamfer(road) - chamfer(~road)


def check_map(points, axes, bounds, w, h, rgba, cap=8.0):
    """How well a transform puts a course's line on the drawn road: (mean texels off road, share within 3)."""
    import numpy as np
    field = road_field(rgba, w, h)
    A = np.array([axis(p, axes[0]) for p in points]); B = np.array([axis(p, axes[1]) for p in points])
    U = ((A - bounds[0]) / (bounds[2]-bounds[0]) * w).astype(int)
    V = ((B - bounds[1]) / (bounds[3]-bounds[1]) * h).astype(int)
    inside = (U >= 0) & (U < w) & (V >= 0) & (V < h)
    off = np.full(A.shape, cap)
    off[inside] = np.clip(-field[V[inside], U[inside]], 0, cap)
    return float(off.mean()), float((off <= 3).mean())


def overlay(w, h, rgba, points, axes, bounds, zoom=3):
    """Private check image: the map with centre-line points (green on road, red off road)."""
    s, bu, bv = w/(bounds[2]-bounds[0]), -bounds[0]*w/(bounds[2]-bounds[0]), -bounds[1]*h/(bounds[3]-bounds[1])
    W, H = w*zoom, h*zoom
    img = bytearray(W*H*4)
    for y in range(H):
        for x in range(W):
            i, j = ((y//zoom)*w + x//zoom)*4, (y*W+x)*4
            a = rgba[i+3]
            img[j:j+4] = bytes((min(255, rgba[i]*a//255 + 20), min(255, rgba[i+1]*a//255 + 24), min(255, rgba[i+2]*a//255 + 40), 255))
    for p in points:
        u, v = s*axis(p, axes[0]) + bu, s*axis(p, axes[1]) + bv
        on = 0 <= int(u) < w and 0 <= int(v) < h and rgba[(int(v)*w+int(u))*4+3]
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                x, y = int(u*zoom)+dx, int(v*zoom)+dy
                if 0 <= x < W and 0 <= y < H:
                    img[(y*W+x)*4:(y*W+x)*4+4] = bytes((40, 230, 90, 255) if on else (240, 50, 50, 255))
    return png(W, H, bytes(img))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso', type=Path, required=True)
    p.add_argument('--output', type=Path, default=Path('M:/WiiDS-mkdd-2026-11-01/lower-art'))
    a = p.parse_args()
    iso, out = a.iso.resolve(strict=True), a.output.resolve()
    if out == REPO or REPO in out.parents:
        p.error(f'Refusing to write disc-derived art inside the source tree: {out}')
    if iso.stat().st_size != DISC_BYTES:
        p.error('Not the complete GM4E01 image')
    digest = hashlib.sha1()
    with iso.open('rb') as f:
        for block in iter(lambda: f.read(8 << 20), b''):
            digest.update(block)
    if digest.hexdigest() != DISC_SHA1:
        p.error(f'Unsupported disc {digest.hexdigest()}')

    out.mkdir(parents=True, exist_ok=True)
    written, manifest = {}, {'schema': 1, 'game': 'GM4E01', 'faces': {}, 'map_icons': {}, 'items': {},
                             'item_variants': {}, 'unverified_items': {}, 'maps': {}}

    def save(name, blob):
        w, h, rgba = bti(blob)
        data = png(w, h, rgba)
        (out/name).write_bytes(data)
        written[name] = {'size': [w, h], 'sha256': hashlib.sha256(data).hexdigest()}
        return name

    with iso.open('rb') as f:
        files = disc_files(f)

        def read(path):
            off, size = files[path]; f.seek(off)
            return yaz0(f.read(size))

        f.seek(0x420)
        dol_offset = struct.unpack('>I', f.read(4))[0]
        f.seek(dol_offset)
        dol = f.read(16 << 20)
        race2d = rarc(yaz0(rarc(read('/MRAM.arc'))['race2d.arc']))
        for cid, (face, icon) in enumerate(zip(CHARACTERS, MAP_ICONS), 1):
            manifest['faces'][str(cid)] = save(f'duo_face_{cid:02d}.png', race2d[face + '.bti'])
            manifest['map_icons'][str(cid)] = save(f'duo_mapicon_{cid:02d}.png', race2d[icon + '_mapicon32.bti'])
        for kind, name in ITEMS.items():
            manifest['items'][str(kind)] = save(f'duo_item_{kind:02d}.png', race2d[name + '.bti'])
        for (kind, cid), name in ITEM_VARIANTS.items():
            manifest['item_variants'][f'{kind}@{cid}'] = save(f'duo_item_{kind:02d}_c{cid:02d}.png', race2d[name + '.bti'])
        for kind, name in UNVERIFIED_ITEMS.items():
            manifest['unverified_items'][str(kind)] = save(f'duo_unverified_item_{kind:02d}.png', race2d[name + '.bti'])
        loc = rarc(read('/MRAM_Locale/English/MRAMLoc.arc'))
        manifest['hud'] = {key: save(f'duo_hud_{key}.png', (loc if arc == 'loc' else race2d)[name + '.bti'])
                           for key, (arc, name) in HUD.items()}
        (out/'checks').mkdir(exist_ok=True)
        for cid, arc in COURSES.items():
            course = rarc(read(f'/Course/{arc}.arc'))
            texture, bol = course[f'{arc.lower()}_map.bti'], course[f'{arc.lower()}_course.bol']
            if bol[0x19] != cid:
                raise ValueError(f'{arc}: course file says {bol[0x19]:#x}, expected {cid:#x}')
            entry = {'png': save(f'duo_map_{cid:02d}.png', texture), 'alignment': 'unverified'}
            w, h, rgba = bti(texture)
            axes, bounds = minimap(dol, cid)
            checks = {}
            for source, runs in (('checkpoints', road_centre(bol)), ('racing line', racing_line(bol))):
                mean, within3 = check_map(densify(runs), axes, bounds, w, h, rgba)
                checks[source] = {'mean_texels_off_road': round(mean, 3), 'within_3_texels': round(within3, 3)}
            entry.update(axes=list(axes), bounds=bounds, source='retail minimap table', check=checks)
            # The table is the game's own; the road check guards against a wrong address or convention.
            if max(c['within_3_texels'] for c in checks.values()) >= 0.9:
                entry['alignment'] = 'verified'
            elif cid in REVIEWED_MAPS:
                entry.update(alignment='verified', reviewed=REVIEWED_MAPS[cid])
            (out/'checks'/f'fit_{cid:02d}_{arc}.png').write_bytes(
                overlay(w, h, rgba, densify(road_centre(bol)), axes, bounds))
            print(f'{arc:10s} axes {axes} ' + ' '.join(f'{k} {v["within_3_texels"]:.2f}' for k, v in checks.items())
                  + f' -> {entry["alignment"]}')
            manifest['maps'][str(cid)] = entry

    (out/'duo_hud.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    receipt = {'disc_sha1': DISC_SHA1, 'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               'files': written, 'maps_verified': sorted(int(k) for k, m in manifest['maps'].items()
                                                         if m['alignment'] == 'verified'),
               'mirror_maps': False, 'device_staged': False}
    (out/'staging-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(f'Staged {len(written)} images to {out}')


if __name__ == '__main__':
    main()
