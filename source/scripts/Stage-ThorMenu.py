"""Validate private original Wii menu assets; device staging requires --stage."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FOLDER = ROOT / 'build/thor-map-data'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def regular(relative):
    if not re.fullmatch(r'(?:thor_settings/)?[A-Za-z0-9_.-]+', relative):
        raise ValueError(f'Unsafe menu path: {relative}')
    path = FOLDER / relative
    if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(FOLDER.resolve()):
        raise ValueError(f'Nonregular or escaping menu asset: {relative}')
    return path


def validate():
    manifest_path = regular('menu.json')
    data = json.loads(manifest_path.read_text(encoding='utf-8'))
    if data.get('schema') != 1 or data.get('kind') != 'private-retail-wii-menu':
        raise ValueError('Unexpected menu manifest')
    for archive, checksum in data['archives'].items():
        if archive not in ('MenuOther.szs', 'MenuSingle.szs'):
            raise ValueError('Unexpected menu archive')
        if sha(ROOT / 'reference/driftdroid/DiscData/files/Scene/UI' / archive) != checksum:
            raise ValueError('Menu source changed')
    palette = data['runtimePalette']
    if (palette['source'] != 'reference/driftdroid/generated/data_sections_init_blobs/StaticR.bin'
            or sha(ROOT / palette['source']) != palette['sha256']):
        raise ValueError('Original runtime palette source changed')
    files = [manifest_path]
    for name, record in data['files'].items():
        path = regular(name)
        if not name.startswith('menu_') or not name.endswith('.png') or sha(path) != record['sha256']:
            raise ValueError('Menu image identity mismatch')
        with Image.open(path) as image:
            if image.format != 'PNG' or image.mode != 'RGBA' or list(image.size) != record['size']:
                raise ValueError('Invalid original menu PNG')
        files.append(path)
    roles = ('background', 'panel', 'header', 'footer', 'button', 'button_focus')
    expected_native = {r + '.rgba' for r in roles} | {'font.rgba', 'settings.tsv'}
    if (data['native']['directory'] != 'thor_settings' or data['native']['manifest'] != 'settings.tsv'
            or set(data['native']['files']) != expected_native):
        raise ValueError('Unexpected native menu contract')
    for name, record in data['native']['files'].items():
        path = regular('thor_settings/' + name)
        if path.stat().st_size != record['bytes'] or sha(path) != record['sha256']:
            raise ValueError('Native raw menu identity mismatch')
        files.append(path)
    lines = ['MKW_SETTINGS 1']
    for role in roles:
        record = data['images'][role]
        with Image.open(regular(record['texture'])) as image:
            if image.tobytes() != regular('thor_settings/' + role + '.rgba').read_bytes():
                raise ValueError('PNG/raw art pixel mismatch')
            lines.append(f'texture {role} {image.width} {image.height} {role}.rgba')
    font = data['fonts']['main']
    original_hud = json.loads(regular('hud.json').read_text(encoding='utf-8'))
    if font != original_hud['fonts']['main'] or len(font['glyphs']) != 174:
        raise ValueError('Native Wii font metrics changed')
    with Image.open(regular(font['texture'])) as image:
        if image.convert('RGBA').tobytes() != regular('thor_settings/font.rgba').read_bytes():
            raise ValueError('Native Wii font pixels changed')
        lines.append(f"font {image.width} {image.height} {font['lineHeight']} {font['ascent']} font.rgba")
    font_png = regular(font['texture'])
    if sha(font_png) != original_hud['files'][font['texture']]['sha256']:
        raise ValueError('Launcher font PNG identity changed')
    files.extend([regular('hud.json'), font_png])
    for character, glyph in font['glyphs'].items():
        values = [ord(character)] + [glyph[k] for k in ('x', 'y', 'w', 'h', 'left', 'advance')]
        lines.append('glyph ' + ' '.join(map(str, values)))
    if regular('thor_settings/settings.tsv').read_text(encoding='ascii') != '\n'.join(lines) + '\n':
        raise ValueError('Native font/art TSV differs from source manifest')
    return files


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', action='store_true')
    parser.add_argument('--serial', help='Required only with --stage')
    args = parser.parse_args()
    if args.serial is not None and (not args.serial or any(c.isspace() for c in args.serial)):
        parser.error('--serial must be nonempty without whitespace')
    if args.stage and not args.serial:
        parser.error('--serial is required with --stage')
    files = validate()
    print(f'Validated {len(files)} private menu files, {sum(p.stat().st_size for p in files)} bytes')
    if args.stage:
        target = 'files/WiiCompiled/ThorMap'
        tar_path = ROOT / 'build/thor-menu.tar'
        with tarfile.open(tar_path, 'w') as archive:
            for path in files:
                archive.add(path, arcname=path.relative_to(FOLDER).as_posix(), recursive=False)
            checks = ''.join(f'{sha(p)}  {p.relative_to(FOLDER).as_posix()}\n' for p in files).encode()
            entry = tarfile.TarInfo('.thor-menu.sha256')
            entry.size = len(checks)
            archive.addfile(entry, io.BytesIO(checks))
        adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
        subprocess.run(adb + ['shell', 'run-as', 'com.driftdroid.android.thor', 'mkdir', '-p', target], check=True)
        with tar_path.open('rb') as stream:
            subprocess.run(adb + ['exec-in', 'run-as', 'com.driftdroid.android.thor', 'tar', '-xf', '-', '-C', target],
                           stdin=stream, check=True, timeout=120)
        subprocess.run(adb + ['shell', 'run-as', 'com.driftdroid.android.thor', 'sh', '-c',
                             f"'cd {target} && sha256sum -cs .thor-menu.sha256'"], check=True, timeout=120)
        receipt = {'status': 'all_device_hashes_match', 'serial': args.serial,
                   'package': 'com.driftdroid.android.thor', 'destination': target,
                   'fileCount': len(files), 'archiveBytes': tar_path.stat().st_size,
                   'files': {p.relative_to(FOLDER).as_posix(): sha(p) for p in files}}
        (ROOT / 'build/thor-menu-device-staging.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print('Staged menu assets and verified every device hash')
    else:
        print('No device contacted; --stage is explicit')
