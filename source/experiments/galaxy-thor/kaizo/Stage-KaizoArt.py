"""Add only Kaizo's three case images (Compose-KaizoArt.py) and atomically extend the device manifest.

Works from the manifest that is on the device now: it is snapshotted first, every image it lists
must still match its hash, and the merged manifest only adds kaizo-cover/-spine/-disc.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
args = parser.parse_args()
adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
remote = '/sdcard/Android/data/io.github.maddex712.wiids/files/custom/launcher/'
art = ROOT / 'build/galaxy-kaizo/art'
out = ROOT / 'build/galaxy-kaizo'


def device_sha(file):
    return subprocess.check_output(adb + ['shell', 'sha256sum', remote + file], text=True).split()[0]


before = subprocess.check_output(adb + ['exec-out', 'cat', remote + 'artwork.json'])
(out / 'launcher-artwork-before.json').write_bytes(before)
original = json.loads(before)
assert original.get('kind') == 'private-launcher-artwork', 'unexpected device manifest'
for name, entry in original['images'].items():
    assert device_sha(entry['file']) == entry['sha256'], 'Device artwork already differs from its manifest: ' + name
added = json.loads((art / 'images.json').read_text())
for name, entry in added.items():
    if name in original['images']:
        assert original['images'][name] == entry, name + ' is already staged with other art; merge explicitly'
    assert not name.startswith('galaxy-')  # never touch the original Galaxy art

metadata = dict(original, images=dict(original['images'], **added))
staged = out / 'artwork-kaizo.json'
staged.write_text(json.dumps(metadata, indent=2))
for name, entry in added.items():
    subprocess.run(adb + ['push', str(art / entry['file']), remote + entry['file']], check=True)
    assert device_sha(entry['file']) == entry['sha256']
current = subprocess.check_output(adb + ['exec-out', 'cat', remote + 'artwork.json'])
assert current == before, 'Custom artwork manifest changed while staging; nothing was replaced'
subprocess.run(adb + ['push', str(staged), remote + '.artwork-kaizo.pending'], check=True)
subprocess.run(adb + ['shell', 'mv', remote + '.artwork-kaizo.pending', remote + 'artwork.json'], check=True)
for name, entry in metadata['images'].items():
    assert entry['file'] == Path(entry['file']).name
    assert device_sha(entry['file']) == entry['sha256'], 'Changed artwork: ' + name
(out / 'device-art-verification.json').write_text(json.dumps({
    'added': sorted(added), 'existingImagesUnchanged': len(original['images']), 'allImageHashesMatched': True,
    'manifestBeforeSha256': hashlib.sha256(before).hexdigest(),
    'manifestSha256': hashlib.sha256(staged.read_bytes()).hexdigest()}, indent=2))
print(f'PASS: Kaizo artwork added; all {len(original["images"])} existing image hashes preserved.')
