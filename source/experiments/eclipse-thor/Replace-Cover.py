"""Replace only Eclipse's library cover; retain and verify every other image."""
import argparse
import copy
import json
import re
import shutil
import subprocess
import uuid
from pathlib import Path
from PIL import Image
from compose_case_art import ROOT, OUT, SOURCE, sha, main as compose


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--cover', required=True, type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_.:-]+', args.serial):
        raise ValueError('Unsafe serial')
    token = uuid.uuid4().hex[:12]
    backup = ROOT / 'build/eclipse-thor/cover-replacement' / token
    backup.mkdir(parents=True)
    remote = '/sdcard/Android/data/io.github.maddex712.wiids/files/custom/launcher'
    adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]

    def run(*parts):
        result = subprocess.run(adb + list(parts), capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stderr + result.stdout)
        return result.stdout.strip()

    def remote_sha(name):
        return run('shell', 'sha256sum', f'{remote}/{name}').split()[0]

    run('pull', f'{remote}/artwork.json', str(backup / 'artwork.json'))
    original = json.loads((backup / 'artwork.json').read_text(encoding='utf-8-sig'))
    assert original['schema'] == 1 and original['kind'] == 'private-launcher-artwork'
    before = {}
    for entry in original['images'].values():
        name = entry['file']
        assert re.fullmatch(r'[A-Za-z0-9_.-]+', name)
        before[name] = remote_sha(name)
        assert before[name] == entry['sha256']
    old_cover = original['images']['eclipse-cover']['file']
    run('pull', f'{remote}/{old_cover}', str(backup / old_cover))
    assert sha(backup / old_cover) == before[old_cover]
    for path in [OUT / 'eclipse-cover.png', OUT / 'provenance.json',
                 SOURCE / 'owner-cover.jpg']:
        if path.exists():
            shutil.copy2(path, backup / path.name)
    with Image.open(args.cover) as image:
        assert image.format == 'JPEG' and all(128 <= n <= 4096 for n in image.size)
        image.verify()
    disc_before = sha(OUT / 'eclipse-disc.png')
    shutil.copy2(args.cover, SOURCE / 'owner-cover.jpg')
    compose()
    assert sha(OUT / 'eclipse-disc.png') == disc_before
    cover = OUT / 'eclipse-cover.png'
    # The runtime accepts only the exact stem plus .png/.jpg filenames.
    name = 'eclipse-cover.png'
    entry = {'file': name, 'sha256': sha(cover), 'size': list(Image.open(cover).size),
             'source': 'Owner-supplied replacement cover; see case-art/provenance.json'}
    modified = copy.deepcopy(original)
    modified['images']['eclipse-cover'] = entry
    staged = backup / 'artwork-new.json'
    staged.write_text(json.dumps(modified, indent=2) + '\n', encoding='utf8')
    run('push', str(cover), f'{remote}/.{name}.{token}.new')
    assert remote_sha(f'.{name}.{token}.new') == sha(cover)
    run('shell', 'mv', f'{remote}/.{name}.{token}.new', f'{remote}/{name}')
    run('push', str(staged), f'{remote}/.artwork.{token}.new')
    assert remote_sha(f'.artwork.{token}.new') == sha(staged)
    run('shell', 'mv', f'{remote}/.artwork.{token}.new', f'{remote}/artwork.json')
    run('pull', f'{remote}/artwork.json', str(backup / 'artwork-readback.json'))
    assert json.loads((backup / 'artwork-readback.json').read_text()) == modified
    assert all(remote_sha(n) == h for n, h in before.items() if n != name)
    assert remote_sha(name) == sha(cover)
    host = ROOT / 'build/thor-map-data/launcher'
    shutil.copy2(host / 'artwork.json', backup / 'host-artwork.json')
    shutil.copy2(host / 'eclipse-cover.png', backup / 'host-eclipse-cover.png')
    host_manifest = json.loads((host / 'artwork.json').read_text(encoding='utf-8-sig'))
    host_manifest['images']['eclipse-cover'] = dict(entry, file='eclipse-cover.png')
    shutil.copy2(cover, host / 'eclipse-cover.png')
    (host / 'artwork.json').write_text(json.dumps(host_manifest, indent=2) + '\n')
    receipt = {'status': 'cover_replaced_and_readback_verified', 'backup': str(backup),
        'cover': entry, 'owner_source_sha256': sha(args.cover), 'disc_sha256': disc_before,
        'all_other_device_images_byte_identical': True, 'only_manifest_cover_entry_changed': True}
    (backup / 'verification.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
