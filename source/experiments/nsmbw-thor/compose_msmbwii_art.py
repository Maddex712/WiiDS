"""Compose private MSMBWii case art from the owner's staged NSMB Wii cover and disc.

The retail art stays recognisable; a band names the mod so the two cases never look alike.
Output PNGs go to build/mods/msmbwii/art for Stage-ThorLauncher.py --msmbwii-cover/--msmbwii-disc.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'build/thor-map-data/launcher'
OUT = ROOT / 'build/mods/msmbwii/art'
FONTS = Path('C:/Windows/Fonts')


def font(size, bold=True):
    for name in (('ariblk.ttf', 'arialbd.ttf') if bold else ('arialbd.ttf', 'arial.ttf')):
        if (FONTS / name).is_file():
            return ImageFont.truetype(str(FONTS / name), size)
    return ImageFont.load_default()


def centered(draw, box, text, f, fill, stroke=0, stroke_fill=None):
    l, t, r, b = draw.textbbox((0, 0), text, font=f, stroke_width=stroke)
    x = box[0] + (box[2] - box[0] - (r - l)) / 2 - l
    y = box[1] + (box[3] - box[1] - (b - t)) / 2 - t
    draw.text((x, y), text, font=f, fill=fill, stroke_width=stroke, stroke_fill=stroke_fill)


def cover():
    src = next(p for p in (SOURCE / 'nsmbw-cover.jpg', SOURCE / 'nsmbw-cover.png') if p.is_file())
    im = Image.open(src).convert('RGB')
    w, h = im.size
    d = ImageDraw.Draw(im, 'RGBA')
    band = (0, int(h * 0.79), w, int(h * 0.915))
    d.rectangle(band, fill=(20, 36, 92, 225))
    d.rectangle((0, band[1], w, band[1] + max(3, h // 160)), fill=(255, 206, 64, 255))
    d.rectangle((0, band[3] - max(3, h // 160), w, band[3]), fill=(255, 206, 64, 255))
    mid = band[1] + (band[3] - band[1]) * 0.55
    centered(d, (0, band[1], w, mid), 'MSMBWii', font(int(h * 0.075)), (255, 255, 255), max(2, h // 220), (10, 16, 48))
    centered(d, (0, mid, w, band[3]), 'Just a Little Adventure', font(int(h * 0.036), False), (255, 222, 120))
    return im


def disc():
    im = Image.open(SOURCE / 'nsmbw-disc.png').convert('RGBA')
    w, h = im.size
    ring = Image.new('RGBA', im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(ring)
    cx, cy, r = w / 2, h / 2, min(w, h) / 2
    # A label arc along the upper rim, inside the disc edge and clear of the hub.
    d.pieslice((cx - r * 0.97, cy - r * 0.97, cx + r * 0.97, cy + r * 0.97), 215, 325, fill=(20, 36, 92, 230))
    d.ellipse((cx - r * 0.62, cy - r * 0.62, cx + r * 0.62, cy + r * 0.62), fill=(0, 0, 0, 0))
    im = Image.alpha_composite(im, ring)
    # The hole above stays the original art: restore everything inside the inner radius.
    original = Image.open(SOURCE / 'nsmbw-disc.png').convert('RGBA')
    mask = Image.new('L', im.size, 0)
    ImageDraw.Draw(mask).ellipse((cx - r * 0.62, cy - r * 0.62, cx + r * 0.62, cy + r * 0.62), fill=255)
    im.paste(original, (0, 0), mask)
    d = ImageDraw.Draw(im)
    centered(d, (cx - r * 0.6, cy - r * 0.90, cx + r * 0.6, cy - r * 0.76), 'MSMBWii', font(int(r * 0.13)), (255, 255, 255),
             max(2, int(r / 180)), (10, 16, 48))
    centered(d, (cx - r * 0.5, cy - r * 0.76, cx + r * 0.5, cy - r * 0.66), 'Just a Little Adventure', font(int(r * 0.06), False),
             (255, 222, 120))
    return im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cover().save(OUT / 'msmbwii-cover.png')
    disc().save(OUT / 'msmbwii-disc.png')
    print(OUT)


if __name__ == '__main__':
    main()
