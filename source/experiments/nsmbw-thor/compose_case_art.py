"""Compose a private NSMB Wii case cover and disc label from the owner's disc banner textures.

Inputs are the PNGs written by extract_banner_art.py (build/nsmbw-thor/banner-art). The
layout (sky, white Wii header band, logo, characters, disc finish) is a generated adaptation
in the style of the existing launcher cases, not a scan of the retail box or disc. No rating
badge or other packaging text is invented. Output: build/nsmbw-thor/case-art (private).
  python compose_case_art.py [banner_dir] [out_dir]
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/nsmbw-thor/banner-art'
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / 'build/nsmbw-thor/case-art'
FONT = Path('C:/Windows/Fonts/seguisb.ttf')


def tex(name, scale=1.0):
    image = Image.open(SRC / f'banner_{name}.png').convert('RGBA')
    if scale != 1.0:
        image = image.resize((round(image.width * scale), round(image.height * scale)), Image.LANCZOS)
    return image


def tint(image, dark, light):
    """Map a greyscale texture onto a dark..light colour ramp, keeping its alpha."""
    grey = image.convert('L')
    ramp = [tuple(int(d + (l - d) * v / 255) for d, l in zip(dark, light)) for v in range(256)]
    out = Image.merge('RGB', [grey.point([r[k] for r in ramp]) for k in range(3)]).convert('RGBA')
    out.putalpha(image.getchannel('A'))
    return out


def shadowed(canvas, image, xy, blur=6, offset=(5, 8), alpha=90):
    mask = image.getchannel('A').filter(ImageFilter.GaussianBlur(blur))
    shade = Image.new('RGBA', image.size, (10, 30, 70, 0))
    shade.putalpha(mask.point(lambda v: v * alpha // 255))
    canvas.alpha_composite(shade, (xy[0] + offset[0], xy[1] + offset[1]))
    canvas.alpha_composite(image, xy)


def wii_mark(draw, x, y, size, fill):
    font = ImageFont.truetype(str(FONT), size)
    draw.text((x, y), 'Wii', font=font, fill=fill, anchor='rs')


def cover():
    w, h = 750, 1066
    canvas = Image.new('RGBA', (w, h))
    # Sky: deep blue at the top of the art, pale near the ground.
    sky = Image.new('RGBA', (1, h))
    for y in range(h):
        t = y / (h - 1)
        sky.putpixel((0, y), (int(40 + 120 * t), int(140 + 85 * t), int(235 + 18 * t), 255))
    canvas.alpha_composite(sky.resize((w, h)))
    rays = Image.new('L', (w, h), 0)
    rd = ImageDraw.Draw(rays)
    cx, cy = w // 2, 520
    for k in range(18):
        a0 = k * math.tau / 18; a1 = a0 + math.tau / 36
        rd.polygon([(cx, cy), (cx + 1600 * math.cos(a0), cy + 1600 * math.sin(a0)),
                    (cx + 1600 * math.cos(a1), cy + 1600 * math.sin(a1))], fill=26)
    canvas.alpha_composite(Image.merge('RGBA', (Image.new('L', (w, h), 255),) * 3 + (rays.filter(ImageFilter.GaussianBlur(3)),)))
    for x, y, s in ((40, 300, 1.6), (560, 250, 2.0), (90, 720, 2.4), (500, 640, 1.4), (300, 860, 2.8)):
        cloud = tex('kumo_01', s); cloud.putalpha(cloud.getchannel('A').point(lambda v: v * 220 // 255))
        canvas.alpha_composite(cloud, (x, y))
    # Mushroom-pillar scenery from the banner, framing the characters.
    # The banner stores this as a greyscale mask that its layout colours; tint it as a green hill.
    pillar = tint(tex('yama_set_00', 1.35), (88, 186, 72), (226, 246, 214))
    canvas.alpha_composite(pillar, (-30, h - pillar.height + 60))
    canvas.alpha_composite(pillar.transpose(Image.FLIP_LEFT_RIGHT), (w - pillar.width + 40, h - pillar.height + 110))
    ground = Image.new('RGBA', (w, 120), (0, 0, 0, 0))
    gd = ImageDraw.Draw(ground)
    gd.rectangle((0, 40, w, 120), fill=(96, 176, 64, 255)); gd.rectangle((0, 32, w, 46), fill=(140, 214, 90, 255))
    canvas.alpha_composite(ground, (0, h - 120))
    # Characters, painted back to front.
    shadowed(canvas, tex('Yko_jump_00', 2.0), (80, 640))
    shadowed(canvas, tex('Bko_jump_00', 2.0), (560, 610))
    shadowed(canvas, tex('lg_jump_00', 2.6), (90, 430))
    shadowed(canvas, tex('Pmr_jump_00', 2.9), (400, 380))
    shadowed(canvas, tex('mr_jump_00', 2.7), (265, 560))
    # White Wii header band with the case's swoosh edge.
    band = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    bd = ImageDraw.Draw(band)
    bd.rectangle((0, 0, w, 96), fill=(255, 255, 255, 255))
    bd.ellipse((-220, 46, w + 900, 300), fill=(0, 0, 0, 0))
    bd.pieslice((-220, -60, w + 900, 160), 0, 180, fill=(255, 255, 255, 255))
    mask = Image.new('L', (w, h), 0)
    md = ImageDraw.Draw(mask)
    md.rectangle((0, 0, w, 70), fill=255)
    md.chord((-300, -160, w + 420, 150), 0, 180, fill=255)
    white = Image.new('RGBA', (w, h), (255, 255, 255, 255)); white.putalpha(mask.filter(ImageFilter.GaussianBlur(1)))
    edge = Image.new('RGBA', (w, h), (190, 196, 204, 255)); edge.putalpha(ImageChops.subtract(mask.filter(ImageFilter.MaxFilter(7)), mask))
    canvas.alpha_composite(edge); canvas.alpha_composite(white)
    wii_mark(ImageDraw.Draw(canvas), w - 34, 92, 82, (128, 131, 135, 255))
    # Title logo from the disc banner.
    logo = tex('wiiMario_Title_logo_US_00', 1.5)
    shadowed(canvas, logo, ((w - logo.width) // 2, 150), blur=8, offset=(0, 10), alpha=110)
    nintendo = tex('nintendo_112x32', 1.15)
    canvas.alpha_composite(nintendo, (w - nintendo.width - 26, h - nintendo.height - 22))
    return canvas.convert('RGB')


def disc():
    size = 1024
    c = size / 2
    canvas = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    base = Image.new('RGBA', (size, size))
    px = base.load()
    for y in range(size):
        for x in range(size):
            dx, dy = x - c, y - c
            a = math.atan2(dy, dx)
            sheen = 0.5 + 0.5 * math.cos(2 * a + 0.6)
            v = 232 + int(18 * sheen)
            px[x, y] = (v, v + 2 if v < 253 else 255, 255, 255)
    canvas.alpha_composite(base)
    art = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    # Faded sky-blue characters across the upper half, like a printed one-colour layer.
    for name, scale, xy in (('lg_jump_00', 3.6, (250, 110)), ('Pmr_jump_00', 3.4, (520, 70))):
        sprite = tex(name, scale)
        layer = Image.new("RGBA", sprite.size, (70, 150, 230, 0))
        layer.putalpha(sprite.getchannel('A').point(lambda v: v * 120 // 255))
        lum = sprite.convert('L').point(lambda v: 255 - v)
        layer.putalpha(ImageChops.multiply(layer.getchannel('A'), lum.point(lambda v: 110 + v * 145 // 255)))
        art.alpha_composite(layer, xy)
    logo = tex('wiiMario_Title_logo_US_00', 1.3)
    art.alpha_composite(logo, ((size - logo.width) // 2, 640))
    nintendo = tint(tex('nintendo_112x32', 1.3), (96, 100, 108), (96, 100, 108))
    art.alpha_composite(nintendo, ((size - nintendo.width) // 2, 900))
    wii_mark(ImageDraw.Draw(art), 300, 560, 96, (150, 153, 158, 255))
    # Unprinted clear centre ring under the print, like the existing disc labels.
    ring = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    rd = ImageDraw.Draw(ring)
    rd.ellipse((c - size * 0.2, c - size * 0.2, c + size * 0.2, c + size * 0.2), fill=(214, 222, 230, 255))
    rd.ellipse((c - size * 0.16, c - size * 0.16, c + size * 0.16, c + size * 0.16), fill=(238, 241, 245, 255))
    canvas.alpha_composite(ring)
    canvas.alpha_composite(art)
    # Round label with clear hub: outside the rim and the centre hole are transparent.
    mask = Image.new('L', (size, size), 0)
    d = ImageDraw.Draw(mask)
    d.ellipse((2, 2, size - 2, size - 2), fill=255)
    hub = size * 0.125
    d.ellipse((c - hub, c - hub, c + hub, c + hub), fill=0)
    canvas.putalpha(ImageChops.multiply(canvas.getchannel('A'), mask))
    return canvas


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cover().save(OUT / 'nsmbw-cover.png', optimize=True)
    disc().save(OUT / 'nsmbw-disc.png', optimize=True)
    print('wrote', OUT / 'nsmbw-cover.png', OUT / 'nsmbw-disc.png')


if __name__ == '__main__':
    main()
