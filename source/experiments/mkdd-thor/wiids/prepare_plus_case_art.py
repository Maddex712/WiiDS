"""Prepare private Plus case art from the owner's existing case set and actual Plus logo.

Only the mod's own Plus lettering is added; base art stays at its original dimensions.
No downloads, generated artwork or APK assets. Output must stay outside the repository.
"""
from pathlib import Path
import argparse
import hashlib
import json
from PIL import Image

REPO = Path(__file__).resolve().parents[3]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--logo', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    out = a.out.resolve()
    if out.is_relative_to(REPO):
        p.error('Private game artwork must stay outside the repo')
    original = json.loads((a.base / 'provenance.json').read_text())
    for stem in ('mkdd-cover', 'mkdd-spine', 'mkdd-disc'):
        if sha(a.base / original[stem]['file']) != original[stem]['sha256']:
            p.error('Existing owner case art did not match its approved receipt')
    logo = Image.open(a.logo).convert('RGBA')
    if logo.size != (256, 102):
        p.error('Expected the pinned Plus 3.3 race2d logo')
    word = logo.crop((80, 72, 176, 100))
    word = word.crop(word.getchannel('A').getbbox())
    out.mkdir(parents=True, exist_ok=True)
    records = {}
    for stem in ('cover', 'spine', 'disc'):
        image = Image.open(a.base / ('mkdd-' + stem + '.png')).convert('RGBA')
        badge = word
        if stem == 'cover':
            width = 138
            badge = badge.resize((width, round(width * word.height / word.width)), Image.Resampling.LANCZOS)
            at = ((image.width - badge.width) // 2, 202)
        elif stem == 'spine':
            badge = badge.transpose(Image.Transpose.ROTATE_270)
            scale = min(38 / badge.width, 84 / badge.height)
            badge = badge.resize((round(badge.width * scale), round(badge.height * scale)), Image.Resampling.LANCZOS)
            at = ((image.width - badge.width) // 2, 418)
        else:
            badge = badge.resize((48, round(48 * word.height / word.width)), Image.Resampling.LANCZOS)
            at = ((image.width - badge.width) // 2, 122)
        image.alpha_composite(badge, at)
        dest = out / ('mkdd-plus-' + stem + '.png')
        image.save(dest)
        records[dest.stem] = {'file': dest.name, 'sha256': sha(dest), 'size': list(image.size),
            'source': 'Owner-approved Double Dash case scan plus actual Plus 3.3 race2d Plus lettering',
            'base_sha256': sha(a.base / ('mkdd-' + stem + '.png')), 'logo_sha256': sha(a.logo),
            'logo_crop': [80, 72, 176, 100], 'placement': list(at), 'badge_size': list(badge.size)}
    (out / 'provenance.json').write_text(json.dumps(records, indent=2) + '\n')
    print(json.dumps(records, indent=2))


if __name__ == '__main__':
    main()
