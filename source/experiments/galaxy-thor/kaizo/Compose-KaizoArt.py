"""Compose the Kaizo case art (cover, spine, disc) from the mod's own title logo.

No public Kaizo Mario Galaxy Rebalanced box art exists, so the case is Galaxy's retail case with
the mod's new logo (TheDes7royer, credited in the creator ZIP) put where Galaxy's logo is:
- cover: Galaxy's logo is erased along its silhouette and refilled from the space around it;
  the Kaizo logo goes in at the same place and size (registered against the disc's retail logo);
- spine: Galaxy's spine with the title redone as KAIZO MARIO GALAXY / REBALANCED 2.1;
- disc: the Kaizo logo over the disc's logo area, at 3x for a crisp logo.
Private build outputs only (build/galaxy-kaizo/art); the Galaxy art is read, never written.
Stage-KaizoArt.py puts them on the device.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
import zipfile

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'experiments/galaxy-thor/lowermap'))
import gxtex  # noqa: E402
import smgarc  # noqa: E402

ZIP = ROOT / 'build/galaxy-mod-research/KaizoMarioGalaxyRebalanced-V2.1.zip'
ZIP_SHA = 'b0b43ee39e3b2c2b'  # prefix, as recorded in the Kaizo README
RETAIL_ARC = ROOT / 'build/galaxy-thor/extracted/files/LayoutData/TitleLogo.arc'
GALAXY = ROOT / 'build/galaxy-thor/art'
OUT = ROOT / 'build/galaxy-kaizo/art'
FONTS = Path('C:/Windows/Fonts')
# Where Galaxy's logo sits on galaxy-cover.png: the retail mytitlelogo.tpl (440x256) scaled by S
# with its corner at (X, Y). Found by a brute-force search; register() re-checks it each run.
S, X, Y = 0.83, 56, 82
CREDIT = 'Kaizo Mario Galaxy Rebalanced 2.1 title logo by TheDes7royer (creator ZIP credits)'


def tpl(b):
    _, table = struct.unpack_from('>II', b, 4)
    img, _ = struct.unpack_from('>II', b, table)
    h, w, fmt, data = struct.unpack_from('>HHII', b, img)
    return Image.fromarray(gxtex.decode(b, data, fmt, w, h), 'RGBA')


def logos():
    raw = ZIP.read_bytes()
    assert hashlib.sha256(raw).hexdigest().startswith(ZIP_SHA), 'unexpected creator ZIP'
    arc = zipfile.ZipFile(ZIP).read('KaizoMarioGalaxyRebalancedV2.1/KMGR/LayoutData/TitleLogo.arc')
    kaizo = tpl(smgarc.rarc(arc)['titlelogo/timg/mytitlelogo.tpl'])
    retail = tpl(smgarc.rarc(RETAIL_ARC.read_bytes())['titlelogo/timg/mytitlelogo.tpl'])
    assert kaizo.size == retail.size == (440, 256)
    return kaizo, retail


def placed(img, size):
    layer = Image.new('RGBA', size, (0, 0, 0, 0))
    layer.paste(img.resize((round(440 * S), round(256 * S)), Image.LANCZOS), (X, Y))
    return layer


def register(cover, retail):
    """Mean colour error where the placed retail logo is opaque; a misplaced logo scores far higher."""
    layer = np.asarray(placed(retail, cover.size)).astype(float)
    a = layer[..., 3] > 200
    err = np.abs(layer[..., :3][a] - np.asarray(cover).astype(float)[a]).mean()
    assert err < 35, f'retail logo no longer lines up with galaxy-cover.png (error {err:.1f})'
    return round(float(err), 1)


def compose_cover(cover, kaizo, retail):
    W, H = cover.size
    # 1. Erase the old logo: its silhouette, grown a little, refilled from the nearest space pixels.
    mask = np.asarray(placed(retail, cover.size))[..., 3] > 20
    mask = ndimage.binary_dilation(mask, iterations=5)
    px = np.asarray(cover).astype(float)
    _, (iy, ix) = ndimage.distance_transform_edt(mask, return_indices=True)
    fill = px[iy, ix]
    blurred = np.stack([ndimage.gaussian_filter(fill[..., c], 9) for c in range(3)], -1)
    rng = np.random.default_rng(7)
    stars = (rng.random((H, W)) > 0.9965) * rng.uniform(120, 255, (H, W))  # a sparse starfield, like the art around it
    blurred = np.clip(blurred * 0.85 + stars[..., None] * np.array([0.9, 0.95, 1.0]), 0, 255)
    soft = ndimage.gaussian_filter(mask.astype(float), 2)[..., None]
    clean = Image.fromarray((px * (1 - soft) + blurred * soft).astype(np.uint8))
    # 2. The Kaizo logo with a soft blue-white glow (the title's bloom).
    logo = placed(kaizo, cover.size)
    glow = Image.new('RGBA', (W, H), (150, 200, 255, 0))
    glow.putalpha(logo.split()[3].filter(ImageFilter.GaussianBlur(10)).point(lambda v: int(v * 0.55)))
    out = clean.convert('RGBA')
    out.alpha_composite(glow)
    out.alpha_composite(logo)
    return out.convert('RGB')


def font(size, style):
    f = ImageFont.truetype(str(FONTS / 'bahnschrift.ttf'), size)
    f.set_variation_by_name(style)
    return f


def compose_spine(sp):
    """Galaxy's spine (Wii mark, Lumas) with the title redone, read top to bottom like the original."""
    sp = sp.convert('RGB')
    W, _ = sp.size
    px = np.asarray(sp)
    title_px = px[300:420, 20:36].reshape(-1, 3)
    navy = tuple(int(v) for v in title_px[np.argmin(title_px.sum(1))])  # the old title's ink
    ImageDraw.Draw(sp).rectangle((4, 150, W - 4, 570), fill=(255, 255, 255))  # between the Wii mark and the Lumas
    title = Image.new('RGBA', (420, W), (0, 0, 0, 0))
    td = ImageDraw.Draw(title)
    big, small = font(30, 'Bold Condensed'), font(15, 'Bold')
    td.text((0, 2), 'KAIZO MARIO GALAXY', font=big, fill=navy)
    td.text((0, 34), 'REBALANCED 2.1', font=small, fill=(178, 24, 40))
    width = max(td.textbbox((0, 2), 'KAIZO MARIO GALAXY', font=big)[2], td.textbbox((0, 34), 'REBALANCED 2.1', font=small)[2])
    rot = title.crop((0, 0, width + 2, W)).rotate(-90, expand=True)
    out = sp.convert('RGBA')
    out.alpha_composite(rot, ((W - rot.width) // 2 + 1, 160))
    return out.convert('RGB')


def compose_disc(disc, kaizo):
    k = 3
    disc = disc.convert('RGBA')
    disc = disc.resize((disc.width * k, disc.height * k), Image.LANCZOS)
    dw = disc.width
    base = np.asarray(disc).astype(float)
    # A backing in the disc's own dark blue, faded in under the old logo (inside the disc's rim).
    patch = Image.new('L', disc.size, 0)
    ImageDraw.Draw(patch).rounded_rectangle((24 * k, 2 * k, 138 * k, 66 * k), radius=24 * k, fill=255)
    patch = patch.filter(ImageFilter.GaussianBlur(9 * k))
    rim = Image.new('L', disc.size, 0)
    ImageDraw.Draw(rim).ellipse((2 * k, 2 * k, dw - 2 * k, dw - 2 * k), fill=255)
    m = (np.asarray(patch).astype(float) / 255) * (np.asarray(rim).astype(float) / 255)
    samples = np.concatenate([base[66 * k:74 * k, 30 * k:50 * k, :3].reshape(-1, 3),
                              base[66 * k:74 * k, 112 * k:132 * k, :3].reshape(-1, 3)])  # left and right of the hub
    mix = base * (1 - m[..., None]) + np.r_[np.median(samples, 0), 255] * m[..., None]
    mix[..., 3] = base[..., 3]
    disc = Image.fromarray(mix.astype(np.uint8), 'RGBA')
    lw = 104 * k
    logo = kaizo.resize((lw, round(256 * lw / 440)), Image.LANCZOS)
    glow = Image.new('RGBA', logo.size, (150, 200, 255, 0))
    glow.putalpha(logo.split()[3].filter(ImageFilter.GaussianBlur(3 * k)).point(lambda v: int(v * 0.5)))
    pos = ((dw - lw) // 2, 6 * k)
    disc.alpha_composite(glow, pos)
    disc.alpha_composite(logo, pos)
    return disc


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    kaizo, retail = logos()
    cover = Image.open(GALAXY / 'galaxy-cover.png').convert('RGB')
    error = register(cover, retail)
    made = {
        'kaizo-cover': compose_cover(cover, kaizo, retail),
        'kaizo-spine': compose_spine(Image.open(GALAXY / 'galaxy-spine.png')),
        'kaizo-disc': compose_disc(Image.open(GALAXY / 'galaxy-disc.png'), kaizo),
    }
    images = {}
    for name, img in made.items():
        file = OUT / (name + '.png')
        img.save(file)
        images[name] = {'file': file.name, 'sha256': hashlib.sha256(file.read_bytes()).hexdigest(),
                        'size': list(img.size), 'source': 'Composed from GameTDB RMGE01 USA retail artwork and the ' + CREDIT}
    (OUT / 'images.json').write_text(json.dumps(images, indent=2))
    (OUT / 'provenance.json').write_text(json.dumps({
        'creatorZip': ZIP.name, 'creatorZipSha256Prefix': ZIP_SHA, 'logo': CREDIT,
        'galaxyArt': [f'build/galaxy-thor/art/galaxy-{p}.png' for p in ('cover', 'spine', 'disc')],
        'logoPlacement': {'scale': S, 'x': X, 'y': Y, 'retailLogoError': error},
        'note': 'No public Kaizo box art exists; this is Galaxy retail art with the mod logo in place of the Galaxy logo.'}, indent=2))
    print('composed', ', '.join(images), f'(logo error {error})')


if __name__ == '__main__':
    main()
