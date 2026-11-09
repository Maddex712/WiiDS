"""Prepare or materialize the reviewed WiiDS 1.2 runtime asset inputs.

Preparing reads only the 67 named members of a verified APK. Materializing
requires an isolated empty output tree, pinned Python dependencies, and CPython
3.12 with stock zlib 1.3.1 to reproduce both ZIP containers byte-for-byte.
This never accesses Android staging, a device, an owner disc, or a release service.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import sys
import zipfile
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PRIVATE = Path('M:/WiiDS-release-wrap-2026-11-08')
INPUT_PREFIX = 'build/wiids-runtime-inputs'
METADATA = json.loads((HERE / 'runtime_assets_1_2.json').read_text(encoding='utf-8'))
APP_ASSETS = 'reference/driftdroid/android/app/src/main/assets'
PRESENTATION = 'build/wiids-merge/assets/presentation'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_path(root, relative):
    p = PurePosixPath(relative)
    if ('\\' in relative or ':' in relative or p.is_absolute() or p.as_posix() != relative or
            any(x in ('', '.', '..') for x in relative.split('/'))):
        raise ValueError('Unsafe runtime asset path: ' + relative)
    result = root.joinpath(*p.parts)
    if not result.resolve().is_relative_to(root.resolve()):
        raise ValueError('Runtime asset path escapes the isolated root')
    return result


def write_exact(root, name, data):
    target = safe_path(root, name)
    if target.exists() and target.read_bytes() != data:
        raise ValueError('Refusing to overwrite differing runtime input: ' + name)
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        target.write_bytes(data)
    if target.read_bytes() != data:
        raise ValueError('Runtime input readback mismatch: ' + name)
    return target


def prepare_inputs(apk, output):
    """Select exactly 17 distinct approved blobs and 41 plain-text cooker inputs."""
    entries = {}
    with zipfile.ZipFile(apk) as z:
        presentation = json.loads(z.read('assets/presentation/manifest.json'))
        manifested = {'assets/' + item['asset'] for item in presentation['files']}
        remaining = {n for n in z.namelist() if n.startswith('assets/') and
                     not n.startswith(('assets/licenses/', 'assets/dexopt/')) and n not in manifested}
        if len(manifested) != METADATA['manifested_presentation_count'] or remaining != set(METADATA['files']):
            raise ValueError('Runtime asset member set differs from the reviewed 1.2 inputs')
        for name, record in METADATA['files'].items():
            data = z.read(name)
            if len(data) != record['bytes'] or digest(data) != record['sha256']:
                raise ValueError('Runtime asset differs from reviewed bytes: ' + name)
            if 'input' in record:
                relative = record['input']
                path = write_exact(output, relative, data)
                entries[INPUT_PREFIX + '/' + relative] = {'path': path, 'bytes': len(data),
                    'sha256': record['sha256'], 'kind': 'reviewed-runtime-input'}
            if record['kind'] == 'cooker':
                with zipfile.ZipFile(io.BytesIO(data)) as code:
                    members = METADATA['zip_layouts']['cooker']['members']
                    if code.namelist() != [entry[0] for entry in members]:
                        raise ValueError('Cooker source member inventory changed')
                    for member, _, expected in members:
                        raw = code.read(member)
                        if PurePosixPath(member).suffix not in ('.py', '.json') or b'\0' in raw or digest(raw) != expected:
                            raise ValueError('Invalid cooker source member: ' + member)
                        raw.decode('utf-8')
                        relative = 'cooker/' + member
                        path = write_exact(output, relative, raw)
                        entries[INPUT_PREFIX + '/' + relative] = {'path': path, 'bytes': len(raw),
                            'sha256': expected, 'kind': 'actual-shipped-cooker-source'}
    return entries


def make_zip(layout, read, base=None):
    if sys.version_info[:2] != (3, 12) or zlib.ZLIB_RUNTIME_VERSION != '1.3.1':
        raise ValueError('Exact Galaxy ZIP replay requires CPython 3.12 with stock zlib 1.3.1')
    out = io.BytesIO(base or b'')
    with zipfile.ZipFile(out, 'a' if base else 'w', zipfile.ZIP_DEFLATED) as z:
        for name, index, expected in layout['members']:
            raw = read(name)
            if digest(raw) != expected:
                raise ValueError('Galaxy ZIP member source differs from its pinned identity: ' + name)
            meta = layout['attrs'][index]
            info = zipfile.ZipInfo(name, tuple(meta[0]))
            (info.create_system, info.create_version, info.extract_version, info.external_attr,
             info.internal_attr, info.compress_type) = meta[1:7]
            info.extra, info.comment = bytes.fromhex(meta[7]), bytes.fromhex(meta[8])
            z.writestr(info, raw, compresslevel=layout['compresslevel'])
    return out.getvalue()


def build_pythonhome(kit, base, wheels):
    raw = base.read_bytes()
    if digest(raw) != METADATA['python_base']['sha256']:
        raise ValueError('Pinned Galaxy base Pythonhome ZIP hash mismatch')
    manifest = json.loads((kit / 'experiments/galaxy-thor/python-packages.json').read_text(encoding='utf-8'))
    site = {}
    for item in manifest['wheels']:
        wheel = safe_path(wheels, item['name']).read_bytes()
        if digest(wheel) != item['sha256']:
            raise ValueError('Pinned Android wheel hash mismatch: ' + item['name'])
        with zipfile.ZipFile(io.BytesIO(wheel)) as z:
            for name in z.namelist():
                if not name.endswith('/') and not name.startswith('chaquopy/lib/'):
                    site['lib/python3.10/site-packages/' + name] = z.read(name)
    return make_zip(METADATA['zip_layouts']['pythonhome'], site.__getitem__, raw)


def native_asset_paths(relative):
    runtime_roots = {'': 'reference/driftdroid/runtime',
                     'runtime-nsmbw': 'reference/nsmbw-android-e1/runtime',
                     'runtime-msmbwii': 'reference/nsmbw-android-msmbwii/runtime',
                     'runtime-brawl': 'third_party/brawl/wiicompiled/runtime'}
    first, _, rest = relative.partition('/')
    prefix = first if first in runtime_roots and first else ''
    name = rest if prefix else relative
    if name == 'galaxy_shader_manifest.bin':
        return ['reference/galaxy-android/assets/galaxy_shader_manifest.bin']
    fixed = {'cacert.pem': 'certs/cacert.pem', 'dsp_coef.bin': 'dsp/dsp_coef.bin',
             'initial_pipeline_cache.db': 'pipeline/initial_pipeline_cache.db'}
    if name.startswith('wii_bootstrap/'):
        sub = 'wii/' + name.removeprefix('wii_bootstrap/')
    else:
        sub = fixed.get(name)
    return [runtime_roots[prefix] + '/assets/' + sub] if sub else []


def materialize(kit, output, base, wheels):
    if output.exists() and any(output.iterdir()):
        raise ValueError('Runtime materialization requires a new or empty isolated output directory')
    inputs = kit / INPUT_PREFIX
    presentation_raw = safe_path(kit, PRESENTATION + '/manifest.json').read_bytes()
    presentation = json.loads(presentation_raw)
    produced = {}

    def stage(name, raw, record):
        if not name.startswith('assets/') or len(raw) != record['bytes'] or digest(raw) != record['sha256']:
            raise ValueError('Reconstructed runtime asset differs from the reviewed build: ' + name)
        relative = name.removeprefix('assets/')
        write_exact(output, APP_ASSETS + '/' + relative, raw)
        for native in native_asset_paths(relative):
            write_exact(output, native, raw)
        produced[name] = record['sha256']

    for item in presentation['files']:
        stage('assets/' + item['asset'], safe_path(kit, 'build/wiids-merge/assets/' + item['asset']).read_bytes(), item)
    for name, record in METADATA['files'].items():
        kind = record['kind']
        if kind == 'presentation':
            raw = presentation_raw
        elif kind == 'catalog':
            raw = safe_path(kit, APP_ASSETS + '/' + name.removeprefix('assets/')).read_bytes()
        elif kind == 'cooker':
            raw = make_zip(METADATA['zip_layouts']['cooker'], lambda n: safe_path(inputs, 'cooker/' + n).read_bytes())
        elif kind == 'pythonhome':
            raw = build_pythonhome(kit, base, wheels)
        else:
            raw = safe_path(inputs, record['input']).read_bytes()
        stage(name, raw, record)
    if len(produced) != METADATA['content_asset_count']:
        raise ValueError('Reconstructed content asset count differs from the frozen APK')
    source_manifest = json.loads((kit / 'SOURCE-MANIFEST.json').read_text(encoding='utf-8'))
    licenses = source_manifest['candidate_apk']['license_assets']
    for name, expected in licenses.items():
        if not name.startswith('assets/licenses/'):
            raise ValueError('Unexpected license archive path')
        tail = name.removeprefix('assets/licenses/')
        raw = safe_path(kit, 'release/licenses/apk-1.2/' + tail).read_bytes()
        if digest(raw) != expected:
            raise ValueError('License source differs from the frozen APK: ' + name)
        write_exact(output, APP_ASSETS + '/licenses/' + tail, raw)
    return {'content_assets': produced, 'license_assets': licenses}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepare-inputs', action='store_true')
    p.add_argument('--apk', type=Path, default=PRIVATE / 'WiiDS-1.2-candidate3.apk')
    p.add_argument('--kit-root', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--python-base', type=Path)
    p.add_argument('--wheels', type=Path)
    a = p.parse_args()
    output = a.output.resolve()
    if a.prepare_inputs:
        if not output.is_relative_to((PRIVATE / 'package-review').resolve()):
            p.error('Prepare reviewed inputs only beneath private package-review')
        with a.apk.open('rb') as f:
            if hashlib.file_digest(f, 'sha256').hexdigest() != METADATA['candidate3_sha256']:
                raise ValueError('Preparing private inputs requires the exact frozen candidate3 APK')
        entries = prepare_inputs(a.apk, output)
        print(json.dumps({'status': 'PASS', 'exact_runtime_members_checked': len(METADATA['files']),
                          'unique_inputs_prepared': len(entries), 'output': str(output)}))
        return
    if not all((a.kit_root, a.python_base, a.wheels)):
        p.error('Materialization requires --kit-root, --python-base, and --wheels')
    result = materialize(a.kit_root.resolve(), output, a.python_base.resolve(), a.wheels.resolve())
    (output / 'runtime-assets-verification.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': 'PASS', 'content_assets': len(result['content_assets']),
                      'license_assets': len(result['license_assets']), 'output': str(output)}))


if __name__ == '__main__':
    main()
