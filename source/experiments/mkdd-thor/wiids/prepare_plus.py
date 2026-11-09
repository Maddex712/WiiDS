"""Prepare the pinned MKDD Plus 3.3 NTSC private port from an owner's disc and patch.

Run with Python -I. All outputs, including translated C, must stay outside the repo.
No download, APK installation, save copying or public packaging is performed here.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import math
import re
import shutil
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
PATCH_SHA256 = 'e78cfc35f54b5b277ac0c7358435c9c2c82a2a0a8ecbdca6d0bdb47fbd8ba158'
PLUS_DOL_SHA1 = '3cd4cbfa6c9a7d4b95dd66028f14f26d23f6f304'
PLUS_BYTES = 1074593792
TABLE = 0x803e2824


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


sa = module('stage_art', ROOT / 'design/stage_art.py')
hp = module('hud_profile', ROOT / 'native/hud_profile.py')


def digest(path, kind):
    with path.open('rb') as f:
        return hashlib.file_digest(f, kind).hexdigest()


def prepare(args):
    out = args.output.resolve()
    if out.is_relative_to(REPO):
        raise ValueError('Private output must be outside the source tree')
    if args.disc.stat().st_size != sa.DISC_BYTES or digest(args.disc, 'sha1') != sa.DISC_SHA1:
        raise ValueError('Expected the complete supported owner GM4E01 disc')
    if digest(args.patch, 'sha256') != PATCH_SHA256:
        raise ValueError('Expected the pinned MKDD Plus 3.3 NTSC patch')
    out.mkdir(parents=True, exist_ok=True)
    print('Owner disc and Plus patch verified', flush=True)
    iso = out / 'plus.iso'
    if not iso.exists():
        subprocess.run([str(args.xdelta), '-d', '-s', str(args.disc), str(args.patch), str(iso)], check=True)
    if iso.stat().st_size != PLUS_BYTES:
        raise ValueError('Unexpected patched disc size')
    with iso.open('rb') as f:
        f.seek(0x420)
        f.seek(struct.unpack('>I', f.read(4))[0])
        start = f.tell()
        header = f.read(0x100)
        offsets, sizes = (struct.unpack_from('>18I', header, p) for p in (0, 0x90))
        f.seek(start)
        dol = f.read(max(o + n for o, n in zip(offsets, sizes)))
        if hashlib.sha1(dol).hexdigest() != PLUS_DOL_SHA1:
            raise ValueError('Unexpected patched executable')
        (out / 'plus.dol').write_bytes(dol)
        files = sa.disc_files(f)

        def read(path):
            off, n = files[path]
            f.seek(off)
            return f.read(n)

        addresses = struct.unpack_from('>18I', header, 0x48)

        def memory(va, n):
            for off, addr, size in zip(offsets, addresses, sizes):
                if size and addr <= va and va + n <= addr + size:
                    return dol[off + va - addr:off + va - addr + n]
            raise ValueError(f'Address outside executable: {va:08x}')

        float_dest = struct.unpack('>88I', memory(TABLE + 0x128, 0x160))
        orientation_dest = struct.unpack('>22I', memory(TABLE + 0x288, 0x58))
        if len(set(float_dest)) != 88 or len(set(orientation_dest)) != 22:
            raise ValueError('Unexpected Plus projection table')
        art = out / 'lower-art'
        shutil.copytree(args.base_art, art, dirs_exist_ok=True)
        manifest = json.loads((art / 'duo_hud.json').read_text(encoding='utf-8'))
        manifest['maps'] = {}
        manifest['course_names'] = True
        print('Reading Plus HUD archives', flush=True)
        # The mod's own logo, portraits, item art and lettering replace the base copies.
        race = sa.rarc(sa.yaz0(sa.rarc(sa.yaz0(read('/MRAM.arc')))['race2d.arc']))
        locale = sa.rarc(sa.yaz0(read('/MRAM_Locale/English/MRAMLoc.arc')))

        def save(name, blob):
            w, h, rgba = sa.bti(blob)
            (art / name).write_bytes(sa.png(w, h, rgba))
            return name

        for cid, (face, icon) in enumerate(zip(sa.CHARACTERS, sa.MAP_ICONS), 1):
            save(manifest['faces'][str(cid)], race[face + '.bti'])
            save(manifest['map_icons'][str(cid)], race[icon + '_mapicon32.bti'])
        for kind, name in sa.ITEMS.items():
            save(manifest['items'][str(kind)], race[name + '.bti'])
        for (kind, cid), name in sa.ITEM_VARIANTS.items():
            save(manifest['item_variants'][f'{kind}@{cid}'], race[name + '.bti'])
        for key, (arc, name) in sa.HUD.items():
            save(manifest['hud'][key], (locale if arc == 'loc' else race)[name + '.bti'])
        signature_text = args.base_header.read_text(encoding='utf-8')
        signatures = {int(h, 16): family for h, family in
                      re.findall(r'\{0x([0-9a-f]+)ull, HudFamily::(\w+)\}', signature_text)}
        blocks = {0: (8, 8, 32), 1: (8, 4, 32), 2: (8, 4, 32), 3: (4, 4, 32),
                  4: (4, 4, 32), 5: (4, 4, 32), 6: (4, 4, 64), 14: (8, 8, 32)}

        def hashes(texture):
            fmt, _, w, h = struct.unpack_from('>BBHH', texture)
            if fmt not in blocks:
                return []
            bw, bh, block = blocks[fmt]
            offset, = struct.unpack_from('>I', texture, 0x1c)
            result, size = [], 0
            for _ in range(max(1, texture[0x18])):
                size += ((w + bw - 1) // bw) * ((h + bh - 1) // bh) * block
                if offset + size > len(texture):
                    raise ValueError('Truncated Plus HUD texture')
                result.append(hp.texture_hash(texture[offset:offset + size]))
                w, h = max(1, w // 2), max(1, h // 2)
            return result

        def signature(h, family):
            previous = signatures.get(h)
            if previous and previous != family:
                if {previous, family} <= {'Item', 'SlotMask'}:
                    family = 'SlotMask'
                else:
                    raise ValueError('Plus texture collides with another HUD family')
            signatures[h] = family

        # Replacements inherit the exact role of their original disc picture. This keeps
        # Plus's changed item/portrait lettering anchored just like the standard HUD.
        original_signatures = signatures.copy()
        wanted = {entry['name'] + '.bti' for entry in
                  json.loads(args.base_header.with_suffix('.json').read_text(encoding='utf-8'))}
        with args.disc.open('rb') as original:
            original_files = sa.disc_files(original)

            def original_read(path):
                offset, size = original_files[path]
                original.seek(offset)
                return original.read(size)

            print('Reading original HUD roles', flush=True)
            original_race = sa.rarc(sa.yaz0(sa.rarc(sa.yaz0(original_read('/MRAM.arc')))['race2d.arc']))
            original_locale = sa.rarc(sa.yaz0(original_read('/MRAM_Locale/English/MRAMLoc.arc')))
            for before, after in ((original_race, race), (original_locale, locale)):
                for name in before.keys() & after.keys() & wanted:
                    old_hashes, new_hashes = hashes(before[name]), hashes(after[name])
                    for old, new in zip(old_hashes, new_hashes):
                        if old in original_signatures:
                            signature(new, original_signatures[old])
        print('Plus HUD fingerprints prepared', flush=True)
        overview = {}
        projection = []
        if args.bake:
            baker = module('plus_baker', ROOT / 'design/bake_overview.py')
            (out / 'overview').mkdir(exist_ok=True)
        for page in range(5):
            values = struct.unpack('>88f', memory(TABLE + 0x2e0 + page * 0x160, 0x160))
            floats = dict(zip(float_dest, values))
            orientations = dict(zip(orientation_dest, memory(TABLE + 0x9c0 + page * 22, 22)))
            for cid, name in sa.COURSES.items():
                key = page * 256 + cid
                course = sa.rarc(sa.yaz0(read(f'/Cours{page}/{name}.arc')))
                texture = course[name.lower() + '_map.bti']
                bol = course[name.lower() + '_course.bol']
                addr, instruction = sa.MINIMAPS[cid]
                x1, z1, x2, z2 = (floats[addr + i * 4] for i in range(4))
                orientation = orientations[instruction + 3]
                if not all(math.isfinite(x) for x in (x1, z1, x2, z2)) or x1 == x2 or z1 == z2 or orientation > 3:
                    raise ValueError(f'Invalid projection page {page}, slot {cid}')
                axes = list(sa.ORIENTATION_AXES[orientation])
                lo = {'x': x1, 'z': z1, '-x': -x2, '-z': -z2}
                hi = {'x': x2, 'z': z2, '-x': -x1, '-z': -z1}
                bounds = [lo[axes[0]], lo[axes[1]], hi[axes[0]], hi[axes[1]]]
                # Plus can reverse either native corner pair. Preserve that reflection rather
                # than sorting corners, which would put the marker on the wrong side.
                for component in range(2):
                    if bounds[component + 2] < bounds[component]:
                        axes[component] = axes[component][1:] if axes[component].startswith('-') else '-' + axes[component]
                        bounds[component], bounds[component + 2] = -bounds[component], -bounds[component + 2]
                entry = {'png': save(f'duo_map_{key}.png', texture), 'axes': list(axes), 'bounds': bounds,
                         'alignment': 'verified', 'source': 'pinned Plus runtime page-switch projection table',
                         'name_png': save(f'duo_name_{key}.png', read(f'/CourseNam{page}/English/{name}_name.bti'))}
                manifest['maps'][str(key)] = entry
                points = sa.densify(sa.racing_line(bol))
                projection.append({'key': key, 'page': page, 'slot': cid, 'orientation': orientation,
                                   'corners': [x1, z1, x2, z2], 'points': points, 'axes': axes, 'bounds': bounds})
                for fingerprint in hashes(texture):
                    signature(fingerprint, 'Right')
                if args.bake:
                    png_name = f'duo_overview_{key}.png'
                    destination = out / 'overview' / png_name
                    w, h = baker.bake(course[name.lower() + '_course.bmd'], bol, axes, bounds, destination)
                    overview[str(key)] = {'png': png_name, 'size': [w, h], 'axes': list(axes), 'bounds': bounds,
                                          'sha256': digest(destination, 'sha256')}
                print(f'Plus set {page + 1}/5, {name}: key {key}, projection {orientation}', flush=True)
        (art / 'duo_hud.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        (out / 'projection-fixtures.json').write_text(json.dumps(projection), encoding='utf-8')
        if args.bake:
            (out / 'overview/overview.json').write_text(json.dumps({'schema': 1, 'game': 'GM4E01', 'overviews': overview}, indent=1) + '\n')
    generated = out / 'generated'
    generated.mkdir(exist_ok=True)
    lines = ['// PRIVATE owner-disc/Plus-patch fingerprint metadata; no texels or translated code.',
             'static constexpr HudSignature kHudSignatures[] = {']
    lines += [f'    {{0x{h:016x}ull, HudFamily::{family}}},' for h, family in sorted(signatures.items())]
    (generated / 'wiids_hud_profile.h').write_text('\n'.join(lines + ['};', '']), encoding='utf-8')
    if args.translate:
        symbols = (args.runtime / 'analysis/symbols.txt').read_text(encoding='utf-8')
        symbols += '\nwiids_plus_code = .text2:0x803E2300; // type:function size:0x524 scope:global align:4\n'
        (out / 'symbols.txt').write_text(symbols, encoding='utf-8')
        shutil.copytree(args.runtime / 'recomp', out / 'tables', dirs_exist_ok=True)
        patches = (out / 'tables/patches.txt').read_text(encoding='utf-8')
        for addr in orientation_dest:
            pc = addr & ~3
            if addr != pc + 3 or struct.unpack('>I', memory(pc, 4))[0] >> 16 != 0x3800:
                raise ValueError('Unexpected self-modified orientation instruction')
            patches += f'{pc:08X} c->r[0] = (uint32_t)(int32_t)(int16_t)LD16(0x{pc + 2:08X}u);\n'
        (out / 'tables/patches.txt').write_text(patches, encoding='utf-8')
        with (out / 'recompile.log').open('w', encoding='utf-8') as log:
            subprocess.run([sys.executable, '-B', str(args.runtime / 'gcn-recomp/recomp/recomp.py'),
                            str(out / 'plus.dol'), str(out / 'symbols.txt'), str(generated), str(out / 'tables')],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    (out / 'preparation.json').write_text(json.dumps({'disc_sha1': sa.DISC_SHA1, 'patch_sha256': PATCH_SHA256,
        'plus_dol_sha1': PLUS_DOL_SHA1, 'plus_iso_sha256': digest(iso, 'sha256'), 'maps': 80,
        'overviews': len(overview), 'mirror': False, 'script_sha256': digest(Path(__file__), 'sha256')}, indent=2) + '\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for option in ('disc', 'patch', 'xdelta', 'output', 'runtime', 'base-art', 'base-header'):
        p.add_argument('--' + option, type=Path, required=True)
    p.add_argument('--bake', action='store_true')
    p.add_argument('--translate', action='store_true')
    prepare(p.parse_args())


if __name__ == '__main__':
    main()
