"""Cut the Double Dash launcher case art from GameTDB's GM4E01 retail scans (owner-approved download).

Same method as experiments/launcher-spines/prepare_spines.py: exact original-resolution crops of the
pinned full cover, no resizing or recolouring. Personal launcher art only (custom/launcher), never
shipped in the APK.
"""
from pathlib import Path
import hashlib
import json
from PIL import Image

SRC = Path('M:/WiiDS-mkdd-2026-11-01/case-art/download')
OUT = Path('M:/WiiDS-mkdd-2026-11-01/case-art/out')
PINS = {
    'coverfullHQ-US-GM4E01.png': ('https://art.gametdb.com/wii/coverfullHQ/US/GM4E01.png',
                                  '5344b10d00ce952996fb4b1edc050c9de9f8faafd3b575afe9c1198b16a1b8ff', (1024, 680)),
    'disc-US-GM4E01.png': ('https://art.gametdb.com/wii/disc/US/GM4E01.png',
                           'b66967820bba530dd36ac1889ab64b9291279af9ea1611920bea4c31eed8bb74', (160, 160)),
}
# The back/spine fold is the sharp column edge at x487; the front's black band starts at x539.
SPINE = (487, 0, 539, 680)
FRONT = (539, 0, 1024, 680)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    for name, (_, digest, size) in PINS.items():
        path = SRC / name
        if sha(path) != digest or Image.open(path).size != size:
            raise SystemExit(f'{name} is not the pinned download')
    OUT.mkdir(parents=True, exist_ok=True)
    full = Image.open(SRC / 'coverfullHQ-US-GM4E01.png').convert('RGB')
    records = {}
    for stem, rect in (('mkdd-spine', SPINE), ('mkdd-cover', FRONT)):
        crop = full.crop(rect)
        dest = OUT / f'{stem}.png'
        crop.save(dest)
        assert Image.open(dest).convert('RGB').tobytes() == full.crop(rect).tobytes()
        records[stem] = {'file': dest.name, 'sha256': sha(dest), 'size': list(crop.size),
                         'source': 'GameTDB GM4E01 USA retail artwork',
                         'source_url': PINS['coverfullHQ-US-GM4E01.png'][0], 'crop_ltrb_exclusive': list(rect)}
    disc = OUT / 'mkdd-disc.png'
    disc.write_bytes((SRC / 'disc-US-GM4E01.png').read_bytes())
    records['mkdd-disc'] = {'file': disc.name, 'sha256': sha(disc), 'size': [160, 160],
                            'source': 'GameTDB GM4E01 USA retail artwork', 'source_url': PINS['disc-US-GM4E01.png'][0]}
    (OUT / 'provenance.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(records, indent=1))


if __name__ == '__main__':
    main()
