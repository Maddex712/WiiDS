"""Add only Galaxy's three images and atomically extend the existing manifest."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
args = parser.parse_args()
adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
remote = '/sdcard/Android/data/io.github.maddex712.wiids/files/custom/launcher/'
art = ROOT / 'build/galaxy-thor/art'
before = (ROOT / 'build/galaxy-thor/launcher-artwork-before.json').read_bytes()
current = subprocess.check_output(adb + ['exec-out', 'cat', remote + 'artwork.json'])
assert current == before, 'Custom artwork changed since snapshot; preserve it and merge explicitly'
metadata = json.loads((art / 'artwork.json').read_text())
original = json.loads(before)
for key, value in original.items():
    if key != 'images':
        assert metadata[key] == value
for name, entry in original['images'].items():
    assert metadata['images'][name] == entry
for name in ('galaxy-cover', 'galaxy-disc', 'galaxy-spine'):
    file = art / (name + '.png')
    subprocess.run(adb + ['push', str(file), remote + file.name], check=True)
    value = subprocess.check_output(adb + ['shell', 'sha256sum', remote + file.name], text=True).split()[0]
    assert value == metadata['images'][name]['sha256']
subprocess.run(adb + ['push', str(art / 'artwork.json'), remote + '.artwork-galaxy.pending'], check=True)
subprocess.run(adb + ['shell', 'mv', remote + '.artwork-galaxy.pending', remote + 'artwork.json'], check=True)
for name, entry in metadata['images'].items():
    assert entry['file'] == Path(entry['file']).name
    value = subprocess.check_output(adb + ['shell', 'sha256sum', remote + entry['file']], text=True).split()[0]
    assert value == entry['sha256'], 'Changed artwork: ' + name
(ROOT / 'build/galaxy-thor/device-art-verification.json').write_text(json.dumps({
    'added': ['galaxy-cover', 'galaxy-disc', 'galaxy-spine'],
    'existingImagesUnchanged': len(original['images']), 'allImageHashesMatched': True,
    'manifestSha256': hashlib.sha256((art / 'artwork.json').read_bytes()).hexdigest()}, indent=2))
print('PASS: Galaxy artwork added; all existing image hashes preserved.')
