"""The launcher's art, from the game's files: fighter portraits (the character select's), the Smash Ball
(tools/make_icon.py) and the game's title font (system/font/font_hira.brfnt) as a glyph sheet with its widths.
   python tools/make_launcher_art.py [disc files dir, default Disc/DATA/files]
Writes launcher/art/: fighter_<name>.png, font.png and font.txt ("<code> <x> <glyph width> <left> <advance>" per
line after a "cell <w> <h> baseline <b>" line)."""
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brawl_assets  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "launcher", "art")

# MenSelchrFaceB<number>.brres: the character select's portraits.
FIGHTERS = {"mario": "000", "sonic": "460", "kirby": "050", "pikachu": "070", "fox": "060", "samus": "030",
            "pit": "220", "metaknight": "210", "ike": "330", "snake": "450"}


def main():
    files = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "Disc", "DATA", "files")
    os.makedirs(OUT, exist_ok=True)
    for name, number in FIGHTERS.items():
        path = os.path.join(files, "menu", "common", "char_bust_tex", f"MenSelchrFaceB{number}.brres")
        tex_name, at, data = brawl_assets.textures(open(path, "rb").read())[0]
        brawl_assets.tex0_image(data, at).save(os.path.join(OUT, f"fighter_{name}.png"))

    font = brawl_assets.Font(open(os.path.join(files, "system", "font", "font_hira.brfnt"), "rb").read())
    codes = [c for c in range(32, 127) if c in font.cmap] + [0x2019, 0x00E9]
    sheet = Image.new("RGBA", (font.cell_w * len(codes), font.cell_h), (0, 0, 0, 0))
    lines = [f"cell {font.cell_w} {font.cell_h} baseline {font.baseline}"]
    for i, code in enumerate(codes):
        img, (left, glyph_w, advance) = font.glyph(chr(code))
        alpha = img.getchannel("A")
        white = Image.new("RGBA", img.size, (255, 255, 255, 255))
        white.putalpha(alpha)
        sheet.alpha_composite(white, (i * font.cell_w, 0))
        lines.append(f"{code} {i * font.cell_w} {glyph_w} {left} {advance}")
    sheet.save(os.path.join(OUT, "font.png"))
    open(os.path.join(OUT, "font.txt"), "w", newline="\n").write("\n".join(lines) + "\n")

    ball = os.path.join(ROOT, "launcher", "smash_ball.png")
    if os.path.exists(ball):
        Image.open(ball).resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, "smash_ball.png"))

    # The Linux launcher's labels, pre-drawn at twice their size (it has no System.Drawing to draw the font at run
    # time): the same look as GameArt.Text in the Windows launcher.
    night, snow, dim, gold, ink = (0x1A, 0x1E, 0x2C), (0xF2, 0xF2, 0xF6), (0xB8, 0xBC, 0xCB), (0xFF, 0xC4, 0x3A), (0x0B, 0x0D, 0x16)
    labels = {"title": ("BRAWL RECOMPILED", 116, (0xFF, 0xF3, 0xB0), (0xFF, 0x8C, 0x28), (0x0C, 0x0E, 0x16), None),
              "play": ("PLAY", 104, (255, 255, 255), (0xFF, 0xE0, 0x8A), (0x5A, 0x0E, 0x0E), None)}
    for page, heading in (("play", "READY TO BRAWL?"), ("display", "DISPLAY"), ("controls", "CONTROLS"), ("mods", "MODS"),
                          ("online", "ONLINE PLAY")):
        labels[f"head_{page}"] = (heading, 76, (255, 255, 255), (0xFF, 0xC8, 0x6A), ink, None)
        labels[f"nav_{page}"] = (page.upper(), 52, snow, dim, ink, 332)
        labels[f"nav_{page}_on"] = (page.upper(), 52, night, (0x3A, 0x24, 0x10), gold, 332)
    for name, (text, height, top, bottom, outline, room) in labels.items():
        img = render_text(font, text, height, top, bottom, outline)
        if room and img.width > room:  # long menu entries shrink to fit the button
            img = render_text(font, text, height * room // img.width, top, bottom, outline)
        img.save(os.path.join(OUT, f"text_{name}.png"))
    print("launcher art written to", OUT)


def render_text(font, text, height, top, bottom, outline):
    """Text in the game font as its titles look: a top-to-bottom gradient, a dark outline, leaning forward."""
    from PIL import ImageChops, ImageDraw
    cell_h = font.cell_h
    widths = [font.glyph(c)[1] for c in text]
    width = sum(w[2] for w in widths) + 4
    mask = Image.new("L", (width, cell_h), 0)
    x = 2
    for ch, (left, glyph_w, advance) in zip(text, widths):
        glyph = font.glyph(ch)[0].getchannel("A").crop((0, 0, glyph_w, cell_h))
        mask.paste(ImageChops.lighter(mask.crop((x + left, 0, x + left + glyph_w, cell_h)), glyph), (x + left, 0))
        x += advance
    scale = height / cell_h
    lean, pad = int(height * 0.22), max(2, height // 10)
    mask = mask.resize((max(1, int(width * scale)), height), Image.LANCZOS)
    edge = mask.copy()
    o = max(1.2 * scale, 2.4)
    for i in range(12):
        import math
        shifted = Image.new("L", mask.size, 0)
        shifted.paste(mask, (round(math.cos(i * math.pi / 6) * o), round(math.sin(i * math.pi / 6) * o)))
        edge = ImageChops.lighter(edge, shifted)
    flat = Image.new("RGBA", mask.size, outline + (255,))
    flat.putalpha(edge)
    fill = Image.new("RGBA", mask.size)
    draw = ImageDraw.Draw(fill)
    for y in range(mask.height):
        f = y / max(1, mask.height - 1)
        draw.line([(0, y), (mask.width, y)], fill=tuple(int(a + (b - a) * f) for a, b in zip(top, bottom)) + (255,))
    fill.putalpha(mask)
    flat.alpha_composite(fill)
    out_w, out_h = mask.width + lean + pad * 2, height + pad * 2
    shear = lean / height  # the top leans right, the bottom stays put
    return flat.transform((out_w, out_h), Image.AFFINE, (1, shear, -pad - lean - shear * pad, 0, 1, -pad), Image.BICUBIC)


if __name__ == "__main__":
    main()
