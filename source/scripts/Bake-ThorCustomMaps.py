"""Bake private Retro Rewind course maps from an already extracted local pack.

No downloads, runtime changes or device operations. Outputs are restricted to
ignored build/thor-rr-map-data; the retail baker and its assets remain unchanged.
"""
import argparse
import concurrent.futures
from datetime import datetime, timezone
import gc
import hashlib
import importlib.util
import io
import inspect
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time
import traceback
import zlib

# Bound numeric helper pools as well as the explicit renderer worker count.
# Respect a caller's explicit environment choices; this affects only this process.
os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
os.environ.setdefault('OMP_NUM_THREADS', '1')
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'build/thor-rr-map-data'
WORK = ROOT / 'build/thor-rr-map-work'
DEFAULT_PACK = ROOT / 'build/retro-rewind-6.13.0/RetroRewind6'
# Exact first-cup wrapper, whose pixel recipe is unchanged by the resumable
# orchestration below. Accept it only with matching current dependency/common hashes.
FIRST_CUP_WRAPPER_SHA256 = 'db138bf89900f9c52bb36630b915068860ff4c422e5bb250fc4f9f9a77681ee6'
FIRST_RESUMABLE_PIPELINE = '86a747b9376bd7079f6b3b1a2ff2c40c591960fdba480c075b74b1711b6ad53e'
FIRST_CUP_PIPELINE = '85d962884da881b09b600c5e46f02928d15bd5e34f8c982184acbe81d31f0893'
PRE_SHADER_FIX_PIPELINE = '49b0cec793b96aa80822531291523baa4c82930cbe830850093b43379648bb6d'
SHADER_V2_PIPELINE = 'c4998ee2c3825956b69067c460443230c0339526f3003222132c45eb99d3bbd9'
SHADER_V3_PIPELINE = 'b6932bf40427f4df542565bc3db8a4053f0613ae8893d7fc30965b77fc19e164'
SHADER_V4_PIPELINE = '4ed9b904e5ed3d73ca383516158b350de8e5ece2ab9de8a2e9e7969bc446832f'
SHADER_V5_PIPELINE = '87d019cf57a155053244641b6a19ca377aaeb1539159f95c33da413ffd969543'
MUSHROOM_CITY_SHA256 = 'e89e28827cd87cbc2e682481a016110af01943699e8b5d5b1ef0d1073877011f'
FIERY_FACTORY_SHA256 = '076b864e54dcfbb7f359bc736bcd17ece9fb5a638b634ae149d11034fe046bb3'
UNSUPPORTED_SOURCE_HASHES = {
    'f5ce7508ef7073d558988e9e0ab94a72067fa6c5147d4365e8ac9aca03288d72':
        'Galaxy Colosseum-family arena: unused battle CKPT outliers distort framing; playable RM_ring1 objects require animated ring placement, unsupported by static course-only renderer',
}
spec = importlib.util.spec_from_file_location('thor_retail_baker', ROOT / 'scripts/Bake-ThorMaps.py')
retail = importlib.util.module_from_spec(spec)
spec.loader.exec_module(retail)
from cook_course_lighting import archive, course_lighting


def hashes(path):
    sha, crc = hashlib.sha256(), 0
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            sha.update(chunk)
            crc = zlib.crc32(chunk, crc)
    return sha.hexdigest(), f'{crc:08X}'


def write_json(path, value):
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                     prefix=path.name + '.', suffix='.tmp', delete=False) as stream:
        stream.write(json.dumps(value, indent=2, ensure_ascii=False) + '\n')
        temporary = Path(stream.name)
    try:
        # Windows readers/antivirus can briefly deny DELETE sharing on the old
        # index. Keep its complete version visible and retry the atomic rename.
        deadline, delay = time.monotonic() + 15, .02
        while True:
            try:
                os.replace(temporary, path)
                break
            except PermissionError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(delay)
                delay = min(delay * 2, .25)
    finally:
        temporary.unlink(missing_ok=True)


def save_png(image, path):
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + '.',
                                     suffix='.tmp', delete=False) as stream:
        temporary = Path(stream.name)
    try:
        image.save(temporary, format='PNG')
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def config_records(pack, mode):
    """Read PULS v3 CUPS primary records; retain unknown flags verbatim.

    The actual pack proves the first two bytes are engine/music slots and the
    final u32 is the compressed archive CRC32. Names independently match the
    shipped Ghosts/FolderToTrackName*.txt CRC table. Variant metadata following
    the primary records is deliberately not interpreted as additional tracks.
    """
    path = pack / f'Binaries/Config{mode}.pul'
    data = path.read_bytes()
    if data[:4] != b'PULS' or struct.unpack_from('>I', data, 4)[0] != 3:
        raise ValueError(f'Unsupported PULS config: {path}')
    cups, end = struct.unpack_from('>II', data, 12)
    if (data[cups:cups+4] != b'CUPS' or struct.unpack_from('>I', data, cups+4)[0] != 3
            or cups + struct.unpack_from('>I', data, cups+8)[0] != end or end > len(data)):
        raise ValueError(f'Invalid CUPS section: {path}')
    count = struct.unpack_from('>H', data, cups+12)[0] * 4
    if cups + 28 + count * 8 > end:
        raise ValueError('Truncated primary course records')
    names = {}
    name_file = pack / f'Ghosts/FolderToTrackName{mode}.txt'
    if name_file.exists():
        for line in name_file.read_text(encoding='utf-8-sig').splitlines():
            text, crc = line.rsplit(' = ', 1)
            if not re.fullmatch('[0-9A-Fa-f]{8}', crc):
                raise ValueError('Invalid ghost-folder CRC table')
            names[crc.upper()] = re.sub(r'\\c\{[^}]*\}', '', text)
    rows = {}
    for i in range(count):
        offset = cups + 28 + i * 8
        engine, music, flags, crc = struct.unpack_from('>BBHI', data, offset)
        crc = f'{crc:08X}'
        rows.setdefault(crc, []).append({'mode': mode, 'index': i, 'cup_index': i // 4,
            'course_in_cup': i % 4, 'record_offset': offset, 'engine_slot': engine,
            'music_slot': music, 'unknown_flags': flags, 'name': names.get(crc)})
    inputs = {path.relative_to(pack).as_posix(): hashes(path)[0]}
    if name_file.exists():
        inputs[name_file.relative_to(pack).as_posix()] = hashes(name_file)[0]
    return rows, inputs


def catalog(pack):
    rows, inputs, keys = {}, {}, set()
    for mode, folder in [('RT', 'Tracks'), ('CT', 'CT/Tracks'), ('BT', 'BT/Tracks')]:
        config, sources = config_records(pack, mode)
        inputs.update(sources)
        for path in sorted((pack / folder).glob('*.szs')):
            key = path.stem
            if (not re.fullmatch('[A-Za-z0-9_-]+', key) or key.casefold() in keys
                    or not path.resolve(strict=True).is_relative_to(pack)):
                raise ValueError(f'Unsafe or ambiguous course basename: {key}')
            keys.add(key.casefold())
            sha, crc = hashes(path)
            records = config.get(crc, [])
            names = sorted({r['name'] for r in records if r['name']})
            rows[key] = {'key': key, 'profile': 'retro_rewind',
                'source': path.relative_to(pack).as_posix(), 'archive_name': path.name,
                'dvd_path': f'/Race/Course/{path.name}', 'source_sha256': sha,
                'source_crc32': crc, 'bytes': path.stat().st_size,
                'name': names[0] if len(names) == 1 else key,
                'config_records': records, 'identity': 'Exact compressed SZS CRC32 matched to CUPS primary records where present'}
    return rows, inputs


def renderer_plan(data):
    """Recognize authored equations, never material-name or guessed-color overrides."""
    plan = {'schema': 2, 'blend_coverage': False, 'materials': {}, 'limitations': []}
    shapes = [s for model in data['models'] for s in model['shapes'] if s['vertices']]
    for index, shape in enumerate(shapes):
        plan['blend_coverage'] |= bool(retail.material_state(shape)[0] & 2)
        mat = shape['sourceMaterial']
        stages = mat['gxMaterial']['tevStages']
        color = lambda s: [s[k] for k in ('colorInA', 'colorInB', 'colorInC', 'colorInD', 'colorScale')]
        alpha = lambda s: [s[k] for k in ('alphaInA', 'alphaInB', 'alphaInC', 'alphaInD')]
        plain = all(s['colorOp'] == 0 and s['colorBias'] == 0 and s['colorClamp']
                    and s['colorRegId'] == 0 and s['alphaOp'] == 0 and s['alphaBias'] == 0
                    and s['alphaScale'] == 0 and s['alphaRegId'] == 0
                    and s['texSwapTable'] == [0, 1, 2, 3] and s['rasSwapTable'] == [0, 1, 2, 3]
                    for s in stages)
        mode = None
        if (plain and len(stages) == 2 and color(stages[0]) == [15, 12, 8, 15, 0]
                and color(stages[1]) == [15, 0, 10, 8, 1]
                and alpha(stages[0]) == [7, 4, 5, 7] and alpha(stages[1]) == [7, 7, 7, 0]
                and [s['texMap'] for s in stages] == [0, 1]
                and [s['texCoordId'] for s in stages] == [0, 1]
                and all(s['indTexMatrix'] == 0 for s in stages)
                and shape['primaryMap'] == 1 and shape['secondary'] == -1):
            mode = 'exact_two_stage_albedo_raster_shadow'
        elif (plain and len(stages) == 2 and color(stages[0]) == [15, 8, 9, 15, 0]
                and color(stages[1]) == [15, 0, 8, 15, 1]
                and alpha(stages[0]) == [4, 7, 7, 7] and alpha(stages[1]) == [7, 7, 7, 0]
                and [s['texMap'] for s in stages] == [0, 1]
                and [s['texCoordId'] for s in stages] == [0, 1]
                and all(s['indTexMatrix'] == 0 for s in stages)
                and shape['primaryMap'] == 1 and shape['secondary'] == -1):
            mode = 'exact_two_stage_albedo_alpha_shadow'
        elif (plain and len(stages) == 2 and color(stages[0]) == [15, 15, 15, 8, 0]
                and color(stages[1]) == [15, 8, 0, 15, 0]
                and alpha(stages[0]) == [7, 4, 5, 7] and alpha(stages[1]) == [7, 7, 7, 0]
                and [s['texMap'] for s in stages] == [0, 1]
                and [s['texCoordId'] for s in stages] == [0, 1]
                and all(s['indTexMatrix'] == 0 for s in stages)
                and shape['primaryMap'] == 1 and shape['secondary'] == -1):
            mode = 'exact_two_stage_albedo_shadow'
        elif (plain and len(stages) == 3
                and [color(s) for s in stages] == [[15, 8, 10, 15, 1], [15, 14, 11, 0, 0], [0, 6, 14, 15, 0]]
                and [alpha(s) for s in stages] == [[7, 4, 5, 7], [0, 7, 7, 7], [0, 7, 7, 7]]
                and [s['texMap'] for s in stages] == [0, 255, 255]
                and [s['texCoordId'] for s in stages] == [0, 7, 7]
                and [s['konstColorSel'] for s in stages] == [12, 13, 12]
                and all(s['indTexMatrix'] == 0 for s in stages)
                and all(v == 0 for c in mat['colorRegisters'] + mat['colorConstants'] for v in c.values())
                and shape['primaryMap'] == 255 and shape['secondary'] == -1):
            mode = 'exact_zero_konst_albedo_raster'
        elif (plain and len(stages) == 4 and color(stages[0]) == [15, 8, 2, 15, 0]
                and alpha(stages[0]) == [7, 4, 5, 7]
                and all(alpha(s) == [7, 7, 7, 0] for s in stages[1:])
                and [s['texMap'] for s in stages] == [0, 1, 2, 3]
                and shape['primaryMap'] == 2 and shape['secondary'] == -1
                and all(s['indTexMatrix'] != 0 for s in stages[2:])):
            mode = 'source_stage_zero_albedo_preview'
            plan['limitations'].append(f"{shape['material']}: original sampler0/C0 stage-zero albedo; "
                                       'projected/indirect additive TEV stages1..3 omitted')
        if mode:
            equations = {'exact_two_stage_albedo_raster_shadow': 'clamp(2*(albedo*raster+shadow))',
                         'exact_two_stage_albedo_alpha_shadow': 'clamp(2*albedo.rgb*albedo.a*shadow.rgb)',
                         'exact_two_stage_albedo_shadow': 'clamp(albedo.rgb*shadow.rgb)',
                         'exact_zero_konst_albedo_raster': 'clamp(2*albedo*raster)',
                         'source_stage_zero_albedo_preview': 'clamp(albedo*C0)'}
            plan['materials'][index] = {'mode': mode, 'material': shape['material'],
                'albedo': mat['samplers'][0]['name'], 'albedo_uv': 'export secondaryCoord0',
                'equation': equations[mode]}
    return plan


def render_custom(data, part, frame, ceiling, lights, ambient, background, plan):
    """Adapt local renderer calls without copying the dependency's renderer source.

    Blended fragments retain their true colour/alpha. Reusing its opaque alpha-test
    path into scratch colour provides visible heights and honors authored depthWrite.
    """
    rb = retail.rb
    shapes = [s for model in data['models'] for s in model['shapes'] if s['vertices']]
    texture_ids = {texture['name']: i for i, texture in enumerate(data['textures'])}
    contexts = {}
    for index, fix in plan['materials'].items():
        mat = shapes[index]['sourceMaterial']
        sub = list(part.submeshes[index])
        sampler = mat['samplers'][0]
        sub[4] = texture_ids[sampler['name']]
        sub[3] = (sub[3] & ~((3 << 16) | (3 << 18))) | (sampler['wrapS'] << 16) | (sampler['wrapT'] << 18)
        sub = tuple(sub)
        part.submeshes[index] = sub
        contexts[id(sub)] = (fix['mode'], mat)
    original_shade, original_draw = rb.shade, rb.draw_triangle
    visible = np.full((frame.height, frame.width), -np.inf) if plan['blend_coverage'] else None
    scratch = np.empty((frame.height, frame.width, 3)) if visible is not None else None
    def shade(sub, textures, vc, uv0, uv1, lod0, lod1):
        context = contexts.get(id(sub))
        if context is None:
            return original_shade(sub, textures, vc, uv0, uv1, lod0, lod1)
        mode, mat = context
        sampler = mat['samplers'][0]
        albedo = textures[sub[4]].sample(uv1[:, 0], uv1[:, 1], sampler['wrapS'], sampler['wrapT'], lod1)
        if mode in ('exact_two_stage_albedo_raster_shadow', 'exact_two_stage_albedo_alpha_shadow', 'exact_two_stage_albedo_shadow'):
            sampler = mat['samplers'][1]
            shadow = textures[sub[2]].sample(uv0[:, 0], uv0[:, 1], sampler['wrapS'], sampler['wrapT'], lod0)
            rgb = (2 * (albedo[:, :3] * vc[:, :3] + shadow[:, :3])
                   if mode == 'exact_two_stage_albedo_raster_shadow'
                   else 2 * albedo[:, :3] * albedo[:, 3:4] * shadow[:, :3]
                   if mode == 'exact_two_stage_albedo_alpha_shadow' else albedo[:, :3] * shadow[:, :3])
        elif mode == 'exact_zero_konst_albedo_raster':
            rgb = 2 * albedo[:, :3] * vc[:, :3]
        else:
            c0 = mat['colorRegisters'][1]
            rgb = albedo[:, :3] * np.array([c0[c] for c in ('r', 'g', 'b')])
        alpha = albedo[:, 3] if mode == 'exact_two_stage_albedo_alpha_shadow' else albedo[:, 3] * vc[:, 3]
        return np.concatenate((np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)[:, None]), axis=1)
    def draw(*args):
        original_draw(*args)
        part, sub, triangle, textures, pixels, frame, ceiling, lights, ambient, colour, depth, blend = args
        if not blend:
            return
        coverage = rb.raster(pixels[triangle], frame.width, frame.height)
        if coverage is None:
            return
        iy, ix, *_ = coverage
        saved = depth[iy, ix].copy()
        original_draw(part, sub, triangle, textures, pixels, frame, ceiling, lights, ambient, scratch, depth, False)
        visible[iy, ix] = np.maximum(visible[iy, ix], depth[iy, ix])
        if not sub[3] & (1 << 24):
            depth[iy, ix] = saved
    if contexts:
        rb.shade = shade
    if visible is not None:
        rb.draw_triangle = draw
    try:
        colour, depth = rb.render([part], frame, ceiling, lights, ambient, background)
    finally:
        rb.shade, rb.draw_triangle = original_shade, original_draw
    return colour, np.maximum(depth, visible) if visible is not None else depth


def source_placement(record, folder):
    """Recover one verified source-authored static course object, never guessed meshes."""
    if record['source_sha256'] == FIERY_FACTORY_SHA256:
        checksum = '239634d63665f525aa01329d0063cd31c1d89b1de6476c1045d91d9574c06dfc'
        if hashes(folder / 'vrcorn_model.brres')[0] != checksum:
            raise ValueError('Verified alternate static course model no longer matches source')
        return dict(resource='vrcorn_model.brres', sha256=checksum, position=[0, 0, 0],
                    rotation=[0, 0, 0], scale=[1, 1, 1],
                    limitation='Source vrcorn static geometry at authored identity bind pose/frame zero; sky runtime behavior and animated/placed obstacles not reproduced')
    if record['source_sha256'] != MUSHROOM_CITY_SHA256:
        return None
    rows = [r for r in retail.kmp_database(folder / 'course.kmp')['GOBJ']['records'] if r['id'] == 0x6f]
    if (len(rows) != 1 or any(rows[0]['position']) or any(rows[0]['rotation'])
            or rows[0]['route'] != -1 or rows[0]['scale'] != (1.2000000476837158,) * 3):
        raise ValueError('Verified static sun placement no longer matches source KMP')
    path = folder / 'sun.brres'
    if hashes(path)[0] != 'bfea50e81892188973ed86b9ddcac8ddfe18d8f8759b948608e40ff86610823d':
        raise ValueError('Verified static sun model no longer matches source')
    return dict(rows[0], resource='sun.brres', sha256=hashes(path)[0],
                limitation='Authored static KMP placement; runtime object behavior not reproduced; traffic omitted')


def validate_lighting(part, lighting):
    # The renderer only reads the referenced directional light. Unused point
    # lights, including enabled ones in unlit models, do not affect any pixels.
    used = {(s[11] >> 4) & 7 for s in part.submeshes if s[11] & 1 and (s[11] >> 2) & 3}
    for index in used:
        light = lighting['lights'][index]
        if light['flags'] & 0x41 == 0x41 and light['type'] != 1:
            raise ValueError(f'Referenced non-directional light {index} is unsupported')


def export_model(source, exported):
    command = [str(retail.NODE), str(retail.DEP / 'build/export_brres.mjs'), str(source), str(exported)]
    log_path = exported / 'export.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, env={**os.environ, **retail.EXPORT_OPTIONS}, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        text = log_path.read_text(encoding='utf-8')
        if "reading 'rotation'" not in text or 'PoseEvaluator.calculate' not in text:
            result.check_returncode()
        # Match noclip's byte-preserving BRRES name decoder. The original local
        # tool remains untouched; this private generated bundle is never shipped.
        original = (retail.DEP / 'build/export_brres.mjs').read_text(encoding='utf-8')
        needle = 'data2.toString("utf8", p, data2.indexOf(0, p))'
        if original.count(needle) != 1:
            raise ValueError('Pose-name adapter does not match pinned local exporter')
        adapted = exported / 'export-byte-names.mjs'
        adapted.write_text(original.replace(needle, 'data2.toString("latin1", p, data2.indexOf(0, p))'), encoding='utf-8')
        command[1] = str(adapted)
        with log_path.open('a') as log:
            subprocess.run(command, env={**os.environ, **retail.EXPORT_OPTIONS}, stdout=log, stderr=subprocess.STDOUT, check=True)


def bake(pack, record, common, pipeline, legacy_pipeline):
    key = record['key']
    if record['source_sha256'] in UNSUPPORTED_SOURCE_HASHES:
        raise ValueError(UNSUPPORTED_SOURCE_HASHES[record['source_sha256']])
    model_path = WORK / key / 'export/model.json'
    known_old = {FIRST_CUP_PIPELINE, FIRST_RESUMABLE_PIPELINE, PRE_SHADER_FIX_PIPELINE}
    old_dependencies_match = legacy_pipeline == FIRST_CUP_PIPELINE
    png_path, receipt = OUT / f'course_{key}.png', OUT / f'course_{key}.json'
    if receipt.exists() and png_path.exists():
        old = json.loads(receipt.read_text(encoding='utf-8'))
        compatible = old.get('pipeline_sha256') == pipeline
        if (not compatible and old_dependencies_match and old.get('pipeline_sha256') in
                (SHADER_V2_PIPELINE, SHADER_V3_PIPELINE, SHADER_V4_PIPELINE, SHADER_V5_PIPELINE)
                and record['source_sha256'] not in (MUSHROOM_CITY_SHA256, FIERY_FACTORY_SHA256) and model_path.exists()):
            compatible = old.get('renderer') == renderer_plan(json.loads(model_path.read_text()))
        if (not compatible and old_dependencies_match and old.get('pipeline_sha256') in known_old
                and model_path.exists()):
            old_plan = renderer_plan(json.loads(model_path.read_text()))
            compatible = not old_plan['blend_coverage'] and not old_plan['materials']
        if (old.get('source_sha256') == record['source_sha256'] and compatible
                and old.get('png_sha256') == hashes(png_path)[0]):
            # Configuration/name records may change without changing geometry.
            # Refresh identity metadata even when the expensive image stays cached.
            old.update(record)
            if old['pipeline_sha256'] != pipeline:
                old['previous_pipeline_sha256'] = old['pipeline_sha256']
                old['pipeline_sha256'] = pipeline
            write_json(receipt, old)
            print(f"{record['name']}: cached", flush=True)
            return old
    folder = WORK / key
    retail.unpack(pack / record['source'], folder)
    placement = source_placement(record, folder)
    exported = folder / ('export-' + Path(placement['resource']).stem if placement else 'export')
    exported.mkdir(exist_ok=True)
    expected = {'source_sha256': record['source_sha256'], 'export_pipeline_sha256': retail.pipeline_fingerprint()}
    if placement:
        expected['placed_model_sha256'] = placement['sha256']
    export_receipt = exported / 'thor-export.json'
    previous_export = json.loads(export_receipt.read_text()) if export_receipt.exists() else {}
    compatible_export = (previous_export == expected or (old_dependencies_match
        and previous_export.get('source_sha256') == record['source_sha256']
        and previous_export.get('pipeline_sha256') in known_old))
    if not (exported / 'model.json').exists() or not compatible_export:
        export_model(folder / (placement['resource'] if placement else 'course_model.brres'), exported)
    write_json(export_receipt, expected)
    data = json.loads((exported / 'model.json').read_text())
    prefix = f'custom_{key}'
    for i, texture in enumerate(data['textures']):
        pixels = (exported / (texture['file'] + '.bin')).read_bytes()
        pal = exported / (texture['file'] + '.pal')
        palette = pal.read_bytes() if pal.exists() else b''
        image = retail.decode_image(io.BytesIO(pixels), io.BytesIO(palette),
            retail.ImageFormat(texture['format']), retail.PaletteFormat(texture['paletteFormat'] or 0),
            len(palette) // 2, texture['width'], texture['height']).convert('RGBA')
        image.save(WORK / 'textures' / f'{prefix}_{i:03}.png')
    source = pack / record['source']
    lighting = course_lighting(source, common['lightset/default.blight'], common['lightset/default.blmap'])
    part = retail.ExportPart(data, prefix)
    if placement:
        part.position *= np.array(placement['scale'])
        part.position += np.array(placement['position'])
    validate_lighting(part, lighting)
    lights = [(l['flags'], l['space'], tuple(l['direction']), int.from_bytes(bytes(l['color']), 'big'))
              for l in lighting['lights']]
    ambient = [int.from_bytes(bytes(a), 'big') for a in lighting['ambients']]
    kmp = retail.kmp_database(folder / 'course.kmp')
    rect, size = retail.rb.course_frame(kmp)
    frame = retail.rb.Frame(*rect, *size)
    kcl = retail.rb.kcl_triangles(folder / 'course.kcl')
    road, ground = retail.rb.floor_maps(kcl, frame)
    ceiling = retail.rb.ceiling_map(road, ground, frame)
    background = np.array([.10, .13, .18])
    plan = renderer_plan(data)
    colour, depth = render_custom(data, part, frame, ceiling,
                                  lights, ambient, background, plan)
    result = retail.rb.finish(colour, depth, frame, background, np.median(ground[np.isfinite(ground)]))
    if not np.isfinite(depth).any() or not np.isfinite(result).all():
        raise ValueError('Renderer produced empty geometry or nonfinite pixels')
    image = Image.fromarray((np.clip(result, 0, 1) * 255 + .5).astype(np.uint8))
    if not any(low != high for low, high in image.getextrema()):
        raise ValueError('Renderer produced a uniform image')
    save_png(image, png_path)
    record = dict(record, schema=1, frame=list(map(float, rect)), size=list(size),
        png=png_path.name, receipt=receipt.name, png_sha256=hashes(png_path)[0],
        pipeline_sha256=pipeline, coverage=float(np.isfinite(depth).mean()),
        renderer=plan,
        triangles=sum(len(s['vertices']) // 3 for m in data['models'] for s in m['shapes']),
        lighting=lighting, export_limitations=data.get('limitations', []) + plan['limitations'],
        source_placement=placement,
        geometry='Static course_model.brres with collision roof removal; no placed objects/sky/animated obstacles; simplified GX/fog/indirect effects')
    write_json(receipt, record)
    print(f"{record['name']}: baked {size}, coverage {record['coverage']:.4f}", flush=True)
    return record


def run_job(pack, record, common, pipeline, legacy_pipeline):
    """Workers write only their own key's image/receipt, never the shared index."""
    started = time.monotonic()
    retail.rb.SD = retail.rb.OVERLAY = WORK
    image = OUT / f"course_{record['key']}.png"
    previous_mtime = image.stat().st_mtime_ns if image.exists() else None
    try:
        result = bake(pack, record, common, pipeline, legacy_pipeline)
        return {'ok': True, 'record': result, 'cached': previous_mtime == image.stat().st_mtime_ns,
                'seconds': time.monotonic() - started}
    except Exception as error:
        return {'ok': False, 'record': record, 'seconds': time.monotonic() - started,
                'error': f'{type(error).__name__}: {error}', 'traceback': traceback.format_exc()}
    finally:
        # The local renderer caches floating-point mip levels globally. Do not
        # retain every course's textures across a 402-course batch.
        retail.rb.Texture.cache.clear()
        gc.collect()


def job_results(pack, records, common, pipeline, legacy_pipeline, jobs, keep_going):
    if jobs == 1:
        for record in records:
            result = run_job(pack, record, common, pipeline, legacy_pipeline)
            yield result
            if not result['ok'] and not keep_going:
                break
        return
    # Separate processes isolate the renderer's global texture cache. At most
    # jobs tasks are queued, and only this parent ever updates maps.json.
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
        iterator = iter(records)
        pending = {}
        def submit_one():
            record = next(iterator, None)
            if record is not None:
                future = pool.submit(run_job, pack, record, common, pipeline, legacy_pipeline)
                pending[future] = record
        for _ in range(jobs):
            submit_one()
        stopping = False
        while pending:
            done, _ = concurrent.futures.wait(pending, return_when=concurrent.futures.FIRST_COMPLETED)
            results = []
            for future in done:
                record = pending.pop(future)
                try:
                    result = future.result()
                except Exception as error:
                    result = {'ok': False, 'record': record, 'seconds': None,
                              'error': f'Worker failure: {error}', 'traceback': traceback.format_exc()}
                results.append(result)
            if not keep_going and any(not result['ok'] for result in results):
                stopping = True
            for result in results:
                yield result
            if not stopping:
                for _ in results:
                    submit_one()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack-root', type=Path, default=DEFAULT_PACK)
    parser.add_argument('--track', action='append', help='Exact pack-relative SZS path; repeatable; default Tracks/0.szs')
    parser.add_argument('--all', action='store_true', help='Attempt every actual SZS in the local RT/CT/BT catalogue')
    parser.add_argument('--jobs', type=int, default=1, help='Bounded isolated renderer workers, 1..2; default 1')
    parser.add_argument('--keep-going', action='store_true', help='Record per-course failures and continue; exit nonzero if any fail')
    parser.add_argument('--catalog-only', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 2:
        parser.error('--jobs must be 1 or 2')
    if args.all and args.track:
        parser.error('--all cannot be combined with --track')
    if args.catalog_only and (args.all or args.track or args.keep_going):
        parser.error('--catalog-only cannot be combined with course selection or --keep-going')
    pack = args.pack_root.resolve(strict=True)
    OUT.mkdir(parents=True, exist_ok=True)
    (WORK / 'textures').mkdir(parents=True, exist_ok=True)
    retail.rb.SD = retail.rb.OVERLAY = WORK
    rows, inputs = catalog(pack)
    common_path = pack / 'UI/Common.szs'
    common = {name.removeprefix('./'): data for name, data in archive(common_path).items()}
    for relative in ('Binaries/Code.pul', 'UI/Common.szs', 'xml/RetroRewind6.xml', 'version.txt'):
        inputs[relative] = hashes(pack / relative)[0]
    dependencies = retail.pipeline_fingerprint()
    # Orchestration-only edits must not invalidate hundreds of rendered images.
    recipe = ''.join(inspect.getsource(function) for function in
                     (bake, save_png, renderer_plan, render_custom, source_placement, validate_lighting, export_model))
    recipe += json.dumps({'placed_source_hashes': [MUSHROOM_CITY_SHA256, FIERY_FACTORY_SHA256],
                          'unsupported_source_hashes': UNSUPPORTED_SOURCE_HASHES}, sort_keys=True)
    pipeline = hashlib.sha256((dependencies + recipe + inputs['UI/Common.szs']).encode()).hexdigest()
    legacy_pipeline = hashlib.sha256((dependencies + FIRST_CUP_WRAPPER_SHA256
                                     + inputs['UI/Common.szs']).encode()).hexdigest()
    info = {'schema': 1, 'profile': 'retro_rewind', 'pack_version': (pack / 'version.txt').read_text().strip(),
        'pack_root': str(pack), 'inputs': inputs, 'code_pul_sha256': inputs['Binaries/Code.pul'],
        'identity_rule': 'Profile plus exact successfully loaded /Race/Course/<basename>.szs; never retail engine slot',
        'courses': rows}
    write_json(OUT / 'catalog.json', info)
    print(f'Catalogued {len(rows)} real course archives', flush=True)
    if args.catalog_only:
        return
    selected = [record['source'] for record in rows.values()] if args.all else args.track or ['Tracks/0.szs']
    if len(set(selected)) != len(selected):
        parser.error('Duplicate --track selections are not allowed')
    requested = {r['source']: r for r in rows.values()}
    manifest_path = OUT / 'maps.json'
    current = json.loads(manifest_path.read_text(encoding='utf-8')) if manifest_path.exists() else {}
    # Discard index entries for another pack/code build; files stay private on disk.
    courses = current.get('courses', {}) if current.get('inputs') == inputs else {}
    failures = current.get('failures', {}) if current.get('inputs') == inputs else {}
    records = []
    for relative in selected:
        if relative not in requested:
            raise ValueError(f'Unknown exact course source: {relative}')
        records.append(requested[relative])
    started = time.monotonic()
    report = {'schema': 1, 'profile': 'retro_rewind', 'started_utc': datetime.now(timezone.utc).isoformat(),
              'requested': len(records), 'jobs': args.jobs, 'keep_going': args.keep_going, 'results': {}}
    failures_this_run = 0
    for result in job_results(pack, records, common, pipeline, legacy_pipeline, args.jobs, args.keep_going):
        record = result['record']
        key = record['key']
        if result['ok']:
            courses[key] = record
            failures.pop(key, None)
        else:
            failures_this_run += 1
            # Never publish a stale map as the substitute for an unsupported one.
            courses.pop(key, None)
            failure = dict(record, pipeline_sha256=pipeline, code_pul_sha256=info['code_pul_sha256'],
                failed_utc=datetime.now(timezone.utc).isoformat(), error=result['error'],
                traceback=result['traceback'], upper_hud_fallback=True)
            write_json(OUT / f'failure_{key}.json', failure)
            failures[key] = {name: failure[name] for name in ('key', 'source', 'source_sha256', 'error', 'failed_utc')}
            print(f"FAILED {key} ({record['name']}): {result['error']}", flush=True)
        report['results'][key] = {name: value for name, value in result.items() if name not in ('record', 'traceback')}
        report['elapsed_seconds'] = time.monotonic() - started
        # Atomic checkpoint after each completed course, including partial runs.
        write_json(manifest_path, dict(info, courses=courses, failures=failures))
        write_json(OUT / 'bake-status.json', report)
    print(f"Completed {len(report['results'])}/{len(records)} requests: {len(courses)} published maps, "
          f"{failures_this_run} failures this run, {time.monotonic()-started:.1f}s", flush=True)
    if failures_this_run:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
