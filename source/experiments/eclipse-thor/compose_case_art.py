"""Normalize the Eclipse graphic designer's published art for the private Thor library.

No AI substitute, new logo or lettering: an owner cover override keeps its pixels;
the circular disc is extracted from the creator's flat mockup and sized to 1024.
Downloaded inputs and outputs stay in ignored build/eclipse-thor/case-art.
"""
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageOps

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/eclipse-thor/case-art'
SOURCE = OUT / 'source'
INPUTS = {
    'cover-art.png': ('e859856a96fc01a919689cba58a6f39026fb4b716bf204245e59dab44a60fa56',
        'https://i.redd.it/lpzhio8v3gld1.png',
        'https://www.reddit.com/r/supermariosunshine/comments/1f3h3o0/cover_art_made_for_super_mario_eclipse/'),
    'disc-art.png': ('d62dc9d095b6302c9fc7e20dc2257ed7fffc071f7a3a665ffb61b9104e9bfca6',
        'https://i.redd.it/kz8odtuouvld1.png',
        'https://www.reddit.com/r/supermariosunshine/comments/1f58wu3/disc_art_for_super_mario_eclipse/'),
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, (expected, _, _) in INPUTS.items():
        if sha(SOURCE / name) != expected:
            raise ValueError('Source identity mismatch: ' + name)
    owner_cover = SOURCE / 'owner-cover.jpg'
    cover = (Image.open(owner_cover).convert('RGB') if owner_cover.exists() else
             ImageOps.fit(Image.open(SOURCE / 'cover-art.png').convert('RGB'),
                          (900, 1280), Image.Resampling.LANCZOS))
    cover.save(OUT / 'eclipse-cover.png')

    # Mockup is orthographic, not a perspective case render. Crop the disc rim;
    # the wooden tabletop and centre-hole background are deliberately removed.
    disc = Image.open(SOURCE / 'disc-art.png').convert('RGBA').crop((122, 96, 1078, 1052))
    disc = disc.resize((1024, 1024), Image.Resampling.LANCZOS)
    mask = Image.new('L', (4096, 4096), 0)
    draw = ImageDraw.Draw(mask)
    draw.ellipse((20, 20, 4076, 4076), fill=255)
    # Thor's 8 cm mini-disc keeps a 15 mm hole (25/400 / 2/3 = 9.375% radius).
    # This clears the actual mockup hole as well as the library's own mask.
    draw.ellipse((2048 - 390, 2048 - 390, 2048 + 390, 2048 + 390), fill=0)
    disc.putalpha(mask.resize((1024, 1024), Image.Resampling.LANCZOS))
    disc.save(OUT / 'eclipse-disc.png')

    # Inspection sheet only, not a fake device screenshot.
    preview = Image.new('RGB', (1450, 1340), (24, 26, 37))
    preview.paste(ImageOps.contain(cover, (900, 1280), Image.Resampling.LANCZOS), (30, 30))
    small = disc.resize((470, 470), Image.Resampling.LANCZOS)
    preview.paste(small, (950, 435), small)
    preview.save(OUT / 'case-art-preview.png')
    receipt = {
        'creator': 'Some-Crappy-Edits (Eclipse logo, save-banner and cover designer; author-stated)',
        'private_only': True,
        'presentation_adaptations': [('Owner-supplied cover preserved at native dimensions' if owner_cover.exists()
            else 'Cover fitted to 900x1280'),
            'Disc cropped from creator mockup; tabletop and centre-hole background masked',
            'Disc normalized to transparent 1024x1024'],
        'inputs': {name: {'sha256': values[0], 'url': values[1], 'creator_post': values[2]}
                   for name, values in INPUTS.items()},
        'outputs': {name: {'sha256': sha(OUT / name), 'size': list(Image.open(OUT / name).size)}
                    for name in ('eclipse-cover.png', 'eclipse-disc.png', 'case-art-preview.png')},
    }
    if owner_cover.exists():
        receipt['inputs']['owner-cover.jpg'] = {'sha256': sha(owner_cover),
            'source': 'Owner attachment; selected replacement cover, creator unverified'}
    (OUT / 'provenance.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(OUT)


if __name__ == '__main__':
    main()
