"""Strictly validate/package private RR maps; contact a device only with --stage."""
import argparse
import io
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import tarfile
import zlib

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FOLDER = ROOT / 'build/thor-rr-map-data'


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def validate(folder=FOLDER):
    def regular_output(name):
        path = folder / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'Expected regular map output: {name}')
        return path

    manifest_path = regular_output('maps.json')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('schema') != 1 or manifest.get('profile') != 'retro_rewind' or not manifest.get('courses'):
        raise ValueError('Expected nonempty private retro_rewind schema1 map manifest')
    pack = Path(manifest['pack_root']).resolve(strict=True)
    for relative, checksum in manifest['inputs'].items():
        path = (pack / relative).resolve(strict=True)
        if not path.is_relative_to(pack) or sha(path) != checksum:
            raise ValueError(f'Changed/unsafe pack input: {relative}')
    if manifest['code_pul_sha256'] != manifest['inputs'].get('Binaries/Code.pul'):
        raise ValueError('Code.pul identity mismatch')
    for key, failure in manifest.get('failures', {}).items():
        if (key in manifest['courses'] or not re.fullmatch('[A-Za-z0-9_-]+', key)
                or failure['key'] != key or not failure.get('error')
                or not re.fullmatch('[0-9a-f]{64}', failure['source_sha256'])):
            raise ValueError('Invalid failure/fallback identity')
    files = [manifest_path]
    for key, record in manifest['courses'].items():
        if (not re.fullmatch('[A-Za-z0-9_-]+', key) or record['key'] != key
                or record['profile'] != 'retro_rewind'
                or record['dvd_path'] != f'/Race/Course/{key}.szs'
                or record['archive_name'] != key + '.szs'
                or record['png'] != f'course_{key}.png' or record['receipt'] != f'course_{key}.json'):
            raise ValueError('Invalid profile/course identity')
        source = (pack / record['source']).resolve(strict=True)
        if (not source.is_relative_to(pack) or source.name != record['archive_name']
                or sha(source) != record['source_sha256']
                or f'{zlib.crc32(source.read_bytes()):08X}' != record['source_crc32']):
            raise ValueError(f'Changed/unsafe course source: {key}')
        for config_record in record['config_records']:
            mode, index = config_record['mode'], config_record['index']
            if mode not in ('RT', 'CT', 'BT') or not isinstance(index, int) or index < 0:
                raise ValueError('Invalid configuration identity')
            data = (pack / f'Binaries/Config{mode}.pul').read_bytes()
            cups = struct.unpack_from('>I', data, 12)[0]
            count = struct.unpack_from('>H', data, cups+12)[0] * 4
            offset = cups + 28 + index * 8
            if index >= count or offset != config_record['record_offset']:
                raise ValueError('Invalid configuration record index')
            engine, music, flags, crc = struct.unpack_from('>BBHI', data, offset)
            if ([engine, music, flags, f'{crc:08X}'] != [config_record['engine_slot'],
                    config_record['music_slot'], config_record['unknown_flags'], record['source_crc32']]
                    or config_record['cup_index'] != index // 4 or config_record['course_in_cup'] != index % 4):
                raise ValueError('Course identity does not match actual Config CUPS record')
        frame = record['frame']
        if (len(frame) != 4 or not all(math.isfinite(v) for v in frame)
                or frame[0] >= frame[2] or frame[1] >= frame[3]):
            raise ValueError(f'Invalid world bounds: {key}')
        image_path, receipt = regular_output(record['png']), regular_output(record['receipt'])
        if sha(image_path) != record['png_sha256'] or json.loads(receipt.read_text(encoding='utf-8')) != record:
            raise ValueError(f'Changed PNG/receipt: {key}')
        with Image.open(image_path) as image:
            image.load()
            if (image.format != 'PNG' or image.mode != 'RGB' or list(image.size) != record['size']
                    or len(image.getcolors(image.width * image.height) or []) < 2):
                raise ValueError(f'Invalid/empty course PNG: {key}')
        if not 0 < record['coverage'] <= 1 or record['triangles'] <= 0:
            raise ValueError(f'Empty geometry: {key}')
        files.extend((image_path, receipt))
    return files


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', action='store_true', help='Explicitly stage after validation; default is local check only')
    parser.add_argument('--serial', help='Required only with --stage')
    args = parser.parse_args()
    if args.serial is not None and (not args.serial or any(c.isspace() for c in args.serial)):
        parser.error('--serial must be nonempty without whitespace')
    if args.stage and not args.serial:
        parser.error('--serial is required with --stage')
    files = validate()
    print(f'Validated {len(files) // 2} private custom course maps')
    if args.stage:
        target = 'files/WiiCompiled/ThorMap/retro_rewind'
        package = 'com.driftdroid.android.thor'
        archive_path = ROOT / 'build/thor-rr-maps.tar'
        with tarfile.open(archive_path, 'w') as output:
            for path in files:
                output.add(path, arcname=path.name, recursive=False)
            checksums = ''.join(f'{sha(path)}  {path.name}\n' for path in files).encode()
            info = tarfile.TarInfo('.thor-map.sha256')
            info.size = len(checksums)
            output.addfile(info, io.BytesIO(checksums))
        adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
        subprocess.run(adb + ['shell', 'run-as', package, 'mkdir', '-p', target], check=True)
        with archive_path.open('rb') as stream:
            subprocess.run(adb + ['exec-in', 'run-as', package, 'tar', '-xf', '-', '-C', target],
                           stdin=stream, check=True, timeout=120)
        subprocess.run(adb + ['shell', 'run-as', package, 'sh', '-c',
                             f"'cd {target} && sha256sum -cs .thor-map.sha256'"],
                       check=True, timeout=120)
        receipt = {'schema': 1, 'serial': args.serial, 'device_root': target,
                   'map_count': (len(files) - 1) // 2,
                   'manifest_sha256': sha(FOLDER / 'maps.json'),
                   'all_device_hashes_verified': True,
                   'files': [{'name': path.name, 'bytes': path.stat().st_size,
                              'sha256': sha(path)} for path in files]}
        (ROOT / 'build/retro-rewind-map-device-verification.json').write_text(
            json.dumps(receipt, indent=2), encoding='utf-8')
        print(f'Staged into {target}')
    else:
        print('No device contacted; use --stage only when device staging is authorized')
