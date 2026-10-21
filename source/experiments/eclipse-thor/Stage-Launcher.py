"""Merge only Eclipse art into the existing non-debuggable WiiDS external custom set.

Backs up every existing regular file without overwriting a backup; pushes the two
new images through hash-checked temporary files and atomically replaces manifest
last. Never installs an APK, starts an app or changes any existing game artwork.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import uuid
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ADB = ROOT / '.tools/platform-tools/adb.exe'
PACKAGE = 'io.github.maddex712.wiids'
REMOTE = f'/sdcard/Android/data/{PACKAGE}/files/custom/launcher'
OUT = ROOT / 'build/eclipse-thor'
BACKUP = OUT / 'before/launcher'
SOURCE = OUT / 'case-art'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_.:-]+', args.serial):
        raise ValueError('Unsafe serial')
    adb = [str(ADB), '-s', args.serial]

    def run(*parts):
        result = subprocess.run(adb + list(parts), capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(f'ADB {parts[0]} failed: {result.stderr[-500:]} {result.stdout[-500:]}')
        return result.stdout.strip()

    def remote_sha(name):
        value = run('shell', 'sha256sum', f'{REMOTE}/{name}').split()[0]
        if not re.fullmatch(r'[a-f0-9]{64}', value):
            raise ValueError('Invalid remote hash')
        return value

    names = [Path(x.strip()).name for x in run('shell', 'find', REMOTE,
             '-maxdepth', '1', '-type', 'f').splitlines() if x.strip()]
    if not names or len(names) > 64 or 'artwork.json' not in names:
        raise ValueError('Expected existing custom artwork manifest')
    if any(not re.fullmatch(r'[A-Za-z0-9_.-]+', name) for name in names):
        raise ValueError('Unexpected custom filename')
    if any(name.startswith('eclipse-') for name in names):
        raise ValueError('Eclipse art already exists; inspect before replacement')
    before = {name: remote_sha(name) for name in names}
    BACKUP.mkdir(parents=True, exist_ok=False)
    for name in names:
        run('pull', f'{REMOTE}/{name}', str(BACKUP / name))
        if sha(BACKUP / name) != before[name]:
            raise ValueError('Backup identity mismatch: ' + name)
    original = json.loads((BACKUP / 'artwork.json').read_text(encoding='utf-8-sig'))
    if original.get('schema') != 1 or original.get('kind') != 'private-launcher-artwork':
        raise ValueError('Unexpected existing artwork schema')
    for entry in original['images'].values():
        if entry['file'] not in before or before[entry['file']] != entry['sha256'].lower():
            raise ValueError('Existing artwork manifest/file mismatch')
    modified = json.loads(json.dumps(original))
    new_files = {}
    for stem in ('eclipse-cover', 'eclipse-disc'):
        if stem in modified['images']:
            raise ValueError('Eclipse entry already exists')
        path = SOURCE / (stem + '.png')
        with Image.open(path) as image:
            if image.format != 'PNG' or any(n < 128 or n > 4096 for n in image.size):
                raise ValueError('Unexpected new image format or dimensions')
            size = list(image.size)
            image.verify()
        new_files[path.name] = sha(path)
        modified['images'][stem] = {'file': path.name, 'sha256': sha(path), 'size': size,
            'source': 'Eclipse designer Some-Crappy-Edits; see case-art/provenance.json'}
    staged = OUT / 'staged-launcher-artwork.json'
    staged.write_text(json.dumps(modified, indent=2) + '\n', encoding='utf8')
    token = uuid.uuid4().hex[:12]
    for name, digest in new_files.items():
        temp = f'.{name}.{token}.new'
        run('push', str(SOURCE / name), f'{REMOTE}/{temp}')
        if remote_sha(temp) != digest:
            raise ValueError('Transferred Eclipse image mismatch')
        run('shell', 'mv', f'{REMOTE}/{temp}', f'{REMOTE}/{name}')
    manifest_temp = f'.artwork.json.{token}.new'
    run('push', str(staged), f'{REMOTE}/{manifest_temp}')
    if remote_sha(manifest_temp) != sha(staged):
        raise ValueError('Transferred manifest mismatch')
    run('shell', 'mv', f'{REMOTE}/{manifest_temp}', f'{REMOTE}/artwork.json')
    # Every pre-existing file other than the intentionally updated manifest must
    # be byte-identical. Pull the final manifest to prove all original entries survived.
    after = {name: remote_sha(name) for name in names + list(new_files)}
    if any(after[name] != digest for name, digest in before.items() if name != 'artwork.json'):
        raise ValueError('Existing user artwork changed')
    final = OUT / 'launcher-artwork-readback.json'
    run('pull', f'{REMOTE}/artwork.json', str(final))
    readback = json.loads(final.read_text(encoding='utf8'))
    if readback != modified or any(readback['images'][k] != v for k, v in original['images'].items()):
        raise ValueError('Final manifest differs or existing entries changed')
    receipt = {'status': 'staged_and_hash_verified', 'package': PACKAGE, 'remote': REMOTE,
        'backup': str(BACKUP), 'before': before, 'after': after,
        'old_image_files_byte_identical': True, 'old_manifest_entries_unchanged': True,
        'new_images': new_files, 'installed_apk': False, 'launched_app': False}
    receipt_path = OUT / 'launcher-art-staging.json'
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
    print(receipt_path)


if __name__ == '__main__':
    main()
