"""Compose private Super Mario Sunshine case art for the Thor launcher from the owner's disc art.

No retail cover scan was supplied; this builds a themed stand-in from the game's own HUD art
(build/sunshine-thor/thor-hud: Shine, area plates) so the case is never a blank placeholder.
Owner-supplied retail art can replace it with Stage-ThorLauncher.py --sunshine-cover/--sunshine-disc.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
HUD = ROOT / 'build/sunshine-thor/thor-hud'
OUT = ROOT / 'build/sunshine-thor/case-art'


def font(size):
    for name in ('ariblk.ttf', 'arialbd.ttf'):
        p = Path('C:/Windows/Fonts') / name
        if p.is_file():
            return ImageFont.truetype(str(p), size)
    return ImageFont.load_default()


def sky(w, h):
    im = Image.new('RGB', (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / h
        top, mid, low = (36, 128, 222), (124, 198, 242), (196, 236, 248)
        c = [int(top[i] + (mid[i] - top[i]) * min(1, t / 0.6)) if t < 0.6 else int(mid[i] + (low[i] - mid[i]) * (t - 0.6) / 0.4) for i in range(3)]
        d.line([(0, y), (w, y)], fill=tuple(c))
    return im


def outlined(d, xy, text, f, fill, stroke, anchor='mm'):
    d.text(xy, text, font=f, fill=fill, stroke_width=stroke, stroke_fill=(12, 36, 88), anchor=anchor)


def shine(size):
    im = Image.open(HUD / 'game_6-shine_icon.png').convert('RGBA')
    return im.resize((size, size), Image.LANCZOS).filter(ImageFilter.SMOOTH)


def cover():
    w, h = 900, 1280
    im = sky(w, h)
    d = ImageDraw.Draw(im)
    d.polygon([(0, 980), (w, 940), (w, h), (0, h)], fill=(24, 150, 214))
    d.rectangle((0, 1180, w, h), fill=(243, 226, 176))
    s = shine(560)
    im.paste(s, ((w - 560) // 2, 230), s)
    outlined(d, (w / 2, 110), 'SUPER MARIO', font(96), (255, 255, 255), 8)
    outlined(d, (w / 2, 880), 'SUNSHINE', font(150), (255, 214, 64), 10)
    d.rectangle((0, 0, w, 24), fill=(106, 90, 205))
    return im


def disc():
    n = 1024
    im = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    body = sky(n, n).convert('RGBA')
    d = ImageDraw.Draw(body)
    d.rectangle((0, n * 0.86, n, n), fill=(24, 150, 214))
    s = shine(300)
    body.paste(s, ((n - 300) // 2, 40), s)
    outlined(d, (n / 2, 715), 'SUPER MARIO', font(58), (255, 255, 255), 6)
    outlined(d, (n / 2, 800), 'SUNSHINE', font(110), (255, 214, 64), 8)
    mask = Image.new('L', (n, n), 0)
    md = ImageDraw.Draw(mask)
    md.ellipse((4, 4, n - 4, n - 4), fill=255)
    md.ellipse((n / 2 - 70, n / 2 - 70, n / 2 + 70, n / 2 + 70), fill=0)
    im.paste(body, (0, 0), mask)
    ring = ImageDraw.Draw(im)
    ring.ellipse((n / 2 - 150, n / 2 - 150, n / 2 + 150, n / 2 + 150), outline=(230, 236, 242, 255), width=26)
    ring.ellipse((4, 4, n - 4, n - 4), outline=(210, 220, 230, 255), width=8)
    return im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cover().save(OUT / 'sunshine-cover.png')
    disc().save(OUT / 'sunshine-disc.png')
    print(OUT)


if __name__ == '__main__':
    main()
