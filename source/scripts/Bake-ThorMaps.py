"""Bake dedicated overhead maps from the user's extracted Wii courses.

Uses the pinned local MKW-3DS offline parser/baker as a build dependency only.
Outputs stay in ignored build/, and are never downloaded game assets.
"""
import argparse
import concurrent.futures
import hashlib
import io
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import shutil
import sys
import platform

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
DEP = ROOT / 'reference/mkw-3ds'
sys.path[:0] = [str(DEP / 'tools'), str(DEP / 'vendor/gclib')]
from unpack_szs import unpack
from import_courses import kmp_database
from gx_material import material_state, retail_lighting
import race_map_bake as rb
from gclib.gx_enums import ImageFormat, PaletteFormat
from gclib.texture_utils import decode_image

WORK = ROOT / 'build/thor-map-work'
OUT = ROOT / 'build/thor-map-data'
DISC = ROOT / 'reference/driftdroid/DiscData/files'
# Override with THOR_NODE, or use the installed Node.js executable on PATH.
NODE = Path(os.environ.get('THOR_NODE') or shutil.which('node') or 'node')
BAKER_REVISION = 'bdacabbd37830086161702d57a9b9d526ff52e56'
EXPORT_OPTIONS = {'MKW_FREEZE_NONLINEAR_UV': '1', 'MKW_RETAIL_LIGHTING': '1',
                  'MKW_COURSE_MATERIALS': '1'}
FACES = ['mario', 'baby_peach', 'waluigi', 'koopa', 'baby_daisy', 'karon',
         'baby_mario', 'luigi', 'kinopio', 'donky', 'yoshi', 'wario',
         'baby_luigi', 'kinopico', 'noko', 'daisy', 'peach', 'catherine',
         'didy', 'teresa', 'koopa_jr', 'hone_koopa', 'fuky', 'roseta']
# PAL GetItemIconPaneName (0x80860AF0), plus its two-mushroom count variant.
# IDs 0..18 retain ItemKind. Slot 19 is presentation-only, not a new game item.
ITEMS = [
    ('Green Shell', 'kame_green', 'tt_item_kame_green'),
    ('Red Shell', 'kame_red', 'tt_item_kame_red'),
    ('Banana', 'banana', 'tt_item_banana'),
    ('Fake Item Box', 'dummybox', 'tt_item_dummybox'),
    ('Mushroom', 'kinoko', 'tt_item_kinoko'),
    ('Triple Mushrooms', 'kinoko_3', 'tt_item_kinoko_3'),
    ('Bob-omb', 'bomb_hei', 'tt_item_bomb_hei'),
    ('Spiny Shell', 'kame_wing', 'tt_item_kame_wing'),
    ('Lightning', 'thunder', 'tt_item_thunder'),
    ('Star', 'star', 'tt_item_star'),
    ('Golden Mushroom', 'GoldenKinoko', 'tt_item_GoldenKinoko'),
    ('Mega Mushroom', 'kinoko_big', 'fm_item_kinoko_l'),
    ('Blooper', 'gesso', 'fm_item_gesso'),
    ('POW Block', 'pow', 'fm_item_pow'),
    ('Thunder Cloud', 'thunder_c', 'fm_item_pikakumo'),
    ('Bullet Bill', 'killer', 'tt_item_killer'),
    ('Triple Green Shells', 'kame_green_3', 'tt_item_kame_green_3'),
    ('Triple Red Shells', 'kame_red_3', 'tt_item_kame_red_3'),
    ('Triple Bananas', 'banana_3', 'item_banana_3'),
    ('Two Mushrooms', 'kinoko_2', 'tt_item_kinoko_2'),
]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pipeline_fingerprint():
    """Invalidate caches for the actual local recipe, dependencies and tool bundle."""
    paths = {Path(__file__).resolve(), DEP / 'build/export_brres.mjs',
             DEP / 'include/mkw/course_lighting.hpp', DEP / 'include/mkw/course_catalog.hpp',
             DEP / 'package-lock.json'}
    for folder, patterns in ((DEP / 'tools', ('*.py', '*.ts')),
                             (DEP / 'vendor/gclib/gclib', ('*.py',))):
        for pattern in patterns:
            paths.update(folder.rglob(pattern))
    files = {p.relative_to(ROOT).as_posix(): sha256(p) for p in sorted(paths) if p.is_file()}
    versions = {'python': platform.python_version(), 'numpy': np.__version__,
                'pillow': Image.__version__}
    payload = {'files': files, 'versions': versions, 'export_options': EXPORT_OPTIONS}
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()


def image_info(path):
    with Image.open(path) as image:
        image.load()
        if image.format != 'PNG' or len(image.getcolors(image.width * image.height) or []) < 2:
            raise ValueError(f'Invalid or empty PNG: {path}')
        return list(image.size)


def decode_tpl(path):
    b = path.read_bytes()
    magic, count, table = struct.unpack_from('>III', b)
    if magic != 0x20AF30 or count != 1:
        raise ValueError(f'Unsupported TPL: {path}')
    image, palette = struct.unpack_from('>II', b, table)
    h, w, fmt, pixels = struct.unpack_from('>HHII', b, image)
    pal, pf, n = b'', 0, 0
    if palette:
        n = struct.unpack_from('>H', b, palette)[0]
        pf, at = struct.unpack_from('>II', b, palette + 4)
        pal = b[at:at + n * 2]
    return decode_image(io.BytesIO(b[pixels:]), io.BytesIO(pal), ImageFormat(fmt),
                        PaletteFormat(pf), n, w, h).convert('RGBA')


def icons():
    folder = WORK / 'ui'
    unpack(DISC / 'Scene/UI/Race.szs', folder)
    records = []
    for i, name in enumerate(FACES):
        path = OUT / f'face_{i:02}.png'
        decode_tpl(folder / f'game_image/timg/st_{name}_32x32.tpl').save(path)
        records.append({'id': i, 'source_name': name, 'file': path.name, 'sha256': sha256(path)})
    (OUT / 'faces.json').write_text(json.dumps({'source': 'Scene/UI/Race.szs',
        'source_sha256': sha256(DISC / 'Scene/UI/Race.szs'), 'faces': records}, indent=2) + '\n')


def item_layout_bindings(path):
    """Read BRLYT picture-pane -> first material texture bindings as data."""
    data = path.read_bytes()
    if data[:4] != b'RLYT':
        raise ValueError(f'Expected retail BRLYT: {path}')
    text = lambda at: data[at:].split(b'\0', 1)[0].decode('ascii')
    offset = struct.unpack_from('>H', data, 12)[0]
    textures, materials, bindings = [], [], {}
    while offset < len(data):
        tag, length = struct.unpack_from('>4sI', data, offset)
        if length < 8 or offset + length > len(data):
            raise ValueError('Invalid BRLYT section length')
        payload = offset + 8
        if tag == b'txl1':
            count = struct.unpack_from('>H', data, payload)[0]
            table = payload + 4
            textures = [text(table + struct.unpack_from('>I', data, table + i * 8)[0])
                        for i in range(count)]
        elif tag == b'mat1':
            count = struct.unpack_from('>H', data, payload)[0]
            for i in range(count):
                material = offset + struct.unpack_from('>I', data, payload + 4 + i * 4)[0]
                sampler_count = struct.unpack_from('>I', data, material + 60)[0] & 15
                materials.append(textures[struct.unpack_from('>H', data, material + 64)[0]]
                                 if sampler_count else None)
        elif tag == b'pic1':
            material_id = struct.unpack_from('>H', data, payload + 84)[0]
            bindings[text(payload + 4)] = materials[material_id]
        offset += length
    return bindings


def item_icons():
    source = DISC / 'Scene/UI/Race.szs'
    folder = WORK / 'ui'
    unpack(source, folder)
    layout = folder / 'game_image/blyt/item.brlyt'
    bindings = item_layout_bindings(layout)
    records = []
    for i, (name, pane, texture) in enumerate(ITEMS):
        if bindings.get(pane) != texture + '.tpl':
            raise ValueError(f'Retail item pane/texture mismatch for {i}: {pane}')
        relative = f'game_image/timg/{texture}.tpl'
        tpl = folder / relative
        image = decode_tpl(tpl)
        alpha = np.asarray(image)[:, :, 3]
        if not (alpha > 0).any():
            raise ValueError(f'Empty item texture: {tpl}')
        target = OUT / f'item_{i:02}.png'
        image.save(target)
        records.append({'id': i, 'name': name, 'raw_kind': 5 if i == 19 else i,
                        'count': 2 if i == 19 else None, 'pane': pane, 'source_tpl': relative,
                        'source_tpl_sha256': sha256(tpl), 'file': target.name,
                        'sha256': sha256(target), 'rgba_sha256': hashlib.sha256(image.tobytes()).hexdigest(),
                        'size': list(image.size), 'alpha_min': int(alpha.min()),
                        'alpha_max': int(alpha.max()), 'alpha_nonzero': int((alpha > 0).sum())})
    manifest = {'schema': 1, 'source': 'Scene/UI/Race.szs', 'source_sha256': sha256(source),
                'layout': 'game_image/blyt/item.brlyt', 'layout_sha256': sha256(layout),
                'mapping_function': 'PAL RMCP01 GetItemIconPaneName 0x80860AF0',
                'none_icon': -1, 'triple_mushroom_counts': {'1': 4, '2': 19, '3': 5},
                'conversion': 'Original TPL decoded to RGBA PNG; no scaling, padding or redraw',
                'items': records}
    (OUT / 'items.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Extracted {len(records)} retail item icons with verified layout bindings', flush=True)


def audit_items():
    path = OUT / 'items.json'
    pngs = {p.name for p in OUT.glob('item_*.png')}
    if not path.exists() and not pngs:
        return 0
    manifest = json.loads(path.read_text())
    records = manifest['items']
    if (manifest.get('schema') != 1 or len(records) != 20
            or {r['id'] for r in records} != set(range(20))
            or pngs != {f'item_{i:02}.png' for i in range(20)}
            or manifest.get('source') != 'Scene/UI/Race.szs'
            or manifest.get('none_icon') != -1
            or manifest.get('triple_mushroom_counts') != {'1': 4, '2': 19, '3': 5}
            or sha256(DISC / manifest['source']) != manifest['source_sha256']):
        raise ValueError('Incomplete or mismatched item manifest/source')
    for record in records:
        target = OUT / f"item_{record['id']:02}.png"
        _, pane, texture = ITEMS[record['id']]
        relative = f'game_image/timg/{texture}.tpl'
        if (record['file'] != target.name or image_info(target) != record['size']
                or sha256(target) != record['sha256'] or record.get('pane') != pane
                or record.get('source_tpl') != relative
                or record.get('raw_kind') != (5 if record['id'] == 19 else record['id'])
                or record.get('count') != (2 if record['id'] == 19 else None)):
            raise ValueError(f'Invalid item image: {target}')
        with Image.open(target) as image:
            if (image.mode != 'RGBA' or hashlib.sha256(image.tobytes()).hexdigest() != record['rgba_sha256']
                    or not image.getchannel('A').getbbox()):
                raise ValueError(f'Invalid item RGBA pixels: {target}')
        tpl = WORK / 'ui' / relative
        if tpl.exists() and (sha256(tpl) != record['source_tpl_sha256']
                or hashlib.sha256(decode_tpl(tpl).tobytes()).hexdigest() != record['rgba_sha256']):
            raise ValueError(f'Item does not match retail TPL pixels: {target}')
    return len(records)


class ExportPart:
    """Adapt offline BRRES triangle streams to the baker, without 3DS encoding."""
    def __init__(self, data, prefix):
        self.textures = [f'{prefix}_{i:03}' for i in range(len(data['textures']))]
        vertices, self.submeshes = [], []
        for model in data['models']:
            for shape in model['shapes']:
                flags, alpha, vertex_alpha, _, mix, extended = material_state(shape)
                v = np.asarray(shape['vertices'], dtype=np.float64).copy()
                if not len(v):
                    continue
                v[:, -1] = (v[:, -1] if vertex_alpha else 255) * alpha
                const = shape['constant'] if mix is None else (shape['constant'] & 0xffffff) | (mix << 24)
                self.submeshes.append((len(vertices), len(v), shape['texture'], flags,
                                       shape['secondary'], const, *shape['uvVelocity'],
                                       shape['animationFrames'] | extended, *retail_lighting(shape)))
                vertices.extend(v)
        v = np.asarray(vertices, dtype=np.float64)
        self.position, self.normal = v[:, :3], v[:, 3:6]
        self.uv, self.uv1, self.color = v[:, 6:8], v[:, 8:10], v[:, 10:14] / 255
        self.indices = np.arange(len(v))


def bake(course):
    course_id, name, archive = course
    receipt = OUT / f'course_{course_id:02}.json'
    source = DISC / archive
    digest = sha256(source)
    output = OUT / f'course_{course_id:02}.png'
    if receipt.exists() and output.exists():
        record = json.loads(receipt.read_text())
        if (record.get('source_sha256') == digest and record.get('pipeline_sha256') == PIPELINE
                and record.get('png_sha256') == sha256(output)
                and image_info(output) == record.get('size')):
            print(f'{name}: cached', flush=True)
            return
    key = Path(archive).stem
    folder = WORK / 'courses' / key
    unpack(source, folder)
    exported = folder / 'export'
    exported.mkdir(exist_ok=True)
    export_receipt = exported / 'thor-export.json'
    expected_export = {'source_sha256': digest, 'pipeline_sha256': PIPELINE}
    if (not (exported / 'model.json').exists() or not export_receipt.exists()
            or json.loads(export_receipt.read_text()) != expected_export):
        env = {**os.environ, **EXPORT_OPTIONS}
        with (exported / 'export.log').open('w') as log:
            subprocess.run([str(NODE), str(DEP / 'build/export_brres.mjs'),
                            str(folder / 'course_model.brres'), str(exported)],
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        export_receipt.write_text(json.dumps(expected_export) + '\n')
    data = json.loads((exported / 'model.json').read_text())
    prefix = f'course_{course_id:02}'
    for i, t in enumerate(data['textures']):
        pixels = (exported / (t['file'] + '.bin')).read_bytes()
        pal_path = exported / (t['file'] + '.pal')
        palette = pal_path.read_bytes() if pal_path.exists() else b''
        im = decode_image(io.BytesIO(pixels), io.BytesIO(palette), ImageFormat(t['format']),
                          PaletteFormat(t['paletteFormat'] or 0), len(palette) // 2,
                          t['width'], t['height']).convert('RGBA')
        im.save(WORK / 'textures' / f'{prefix}_{i:03}.png')
    kmp = kmp_database(folder / 'course.kmp')
    rect, size = rb.course_frame(kmp)
    frame = rb.Frame(*rect, *size)
    kcl = rb.kcl_triangles(folder / 'course.kcl')
    road, ground = rb.floor_maps(kcl, frame)
    ceiling = rb.ceiling_map(road, ground, frame)
    lights, ambient = rb.course_lighting(archive)
    background = np.array([.10, .13, .18])
    colour, depth = rb.render([ExportPart(data, prefix)], frame, ceiling, lights, ambient, background)
    image = rb.finish(colour, depth, frame, background, np.median(ground[np.isfinite(ground)]))
    Image.fromarray((np.clip(image, 0, 1) * 255 + .5).astype(np.uint8)).save(output)
    record = {'id': course_id, 'name': name, 'frame': list(map(float, rect)), 'size': size,
              'source': archive, 'source_sha256': digest, 'baker': f'MKW-3DS {BAKER_REVISION}',
              'pipeline_sha256': PIPELINE, 'png_sha256': sha256(output),
              'coverage': float(np.isfinite(depth).mean()),
              'triangles': sum(len(s['vertices']) // 3
                  for m in data['models'] for s in m['shapes']),
              'export_limitations': data.get('limitations', []),
              'geometry': 'static course model with collision-based roof removal'}
    receipt.write_text(json.dumps(record, indent=2) + '\n')
    print(f'{name}: baked {size}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ids', nargs='+', type=int)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--audit', action='store_true', help='Check all existing assets without baking or staging')
    parser.add_argument('--items-only', action='store_true', help='Extract item icons without touching maps or faces')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.items_only:
        if args.audit or args.ids is not None:
            parser.error('--items-only cannot be combined with --audit or --ids')
        OUT.mkdir(parents=True, exist_ok=True)
        item_icons()
        audit_items()
        raise SystemExit(0)
    PIPELINE = pipeline_fingerprint()
    if args.audit:
        stale = []
        for i in range(32):
            record = json.loads((OUT / f'course_{i:02}.json').read_text())
            path = OUT / f'course_{i:02}.png'
            assert record['id'] == i and image_info(path) == record['size'], f'Course {i}: id/size mismatch'
            assert sha256(DISC / record['source']) == record['source_sha256'], f'Course {i}: source mismatch'
            if 'png_sha256' in record:
                assert sha256(path) == record['png_sha256'], f'Course {i}: PNG mismatch'
            if record.get('pipeline_sha256') != PIPELINE:
                stale.append(i)
        for i in range(24):
            assert image_info(OUT / f'face_{i:02}.png') == [32, 32], f'Face {i}: size mismatch'
        print(json.dumps({'maps': 32, 'faces': 24, 'items': audit_items(), 'pipeline_sha256': PIPELINE,
                          'stale_or_legacy_course_ids': stale}, indent=2))
        raise SystemExit(0)
    for p in (WORK / 'textures', OUT):
        p.mkdir(parents=True, exist_ok=True)
    rb.SD = WORK
    rb.OVERLAY = WORK
    catalog = (DEP / 'include/mkw/course_catalog.hpp').read_text()
    rows = re.findall(r'\{"([^"]+)",Kinoko::Course::\w+,"(Race/Course/[^\"]+)","models/course_(\d+)\.list"', catalog)
    selected = [(int(i), n, a) for n, a, i in rows if int(i) < 32 and (args.ids is None or int(i) in args.ids)]
    if not selected:
        raise SystemExit('No matching courses')
    icons()
    item_icons()
    # Texture names contain the course id, so concurrent courses cannot alias the cache.
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(bake, selected))
