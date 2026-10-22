"""Prepare the Galaxy retail cover/spine/disc without changing existing art."""
from pathlib import Path
import hashlib
import json
import urllib.request
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/galaxy-thor/art'
OUT.mkdir(parents=True, exist_ok=True)
sources = []
for name, url in (
    ('cover-full', 'https://art.gametdb.com/wii/coverfullHQ/US/RMGE01.png'),
    ('disc', 'https://art.gametdb.com/wii/disc/US/RMGE01.png')):
    path = OUT / f'RMGE01-{name}.png'
    if not path.exists():
        with urllib.request.urlopen(url, timeout=45) as response:
            data = response.read(8 * 1024 * 1024 + 1)
        if len(data) > 8 * 1024 * 1024 or not data.startswith(b'\x89PNG\r\n\x1a\n'):
            raise ValueError('Unexpected artwork download')
        path.write_bytes(data)
    with Image.open(path) as image:
        image.verify()
    sources.append({'file': path.name, 'url': url, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
with Image.open(OUT / 'RMGE01-cover-full.png') as image:
    if image.size != (1024, 680):
        raise ValueError(f'Inspect cover geometry before cropping: {image.size}')
    image.crop((540, 0, 1024, 680)).save(OUT / 'galaxy-cover.png')
    image.crop((484, 0, 540, 680)).save(OUT / 'galaxy-spine.png')
with Image.open(OUT / 'RMGE01-disc.png') as image:
    image.save(OUT / 'galaxy-disc.png')
manifest = json.loads((ROOT / 'build/galaxy-thor/launcher-artwork-before.json').read_text(encoding='utf-8'))
for name in ('galaxy-cover', 'galaxy-spine', 'galaxy-disc'):
    path = OUT / (name + '.png')
    with Image.open(path) as image:
        size = list(image.size)
    manifest['images'][name] = {'file': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                                'size': size, 'source': 'GameTDB RMGE01 USA retail artwork'}
(OUT / 'artwork.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
(OUT / 'provenance.json').write_text(json.dumps({'sources': sources,
    'coverCrop': [540, 0, 1024, 680], 'spineCrop': [484, 0, 540, 680],
    'existingManifestEntriesPreserved': True}, indent=2), encoding='utf-8')
print(OUT)
