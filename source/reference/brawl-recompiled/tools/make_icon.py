"""The Smash Ball icon (launcher, game exe and game window) from the game's own Smash Ball textures: the rainbow
core wrapped round a sphere with the Smash emblem on its face, as the item looks in a match.
   python tools/make_icon.py [disc files dir, default Disc/DATA/files]
Writes launcher/smash.ico, launcher/smash_ball.png, launcher/smash_preview.png and wiicompiled/runtime/assets/icon/brawl.ico."""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brawl_assets  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
N = 512


def texture(files, name):
    path = os.path.join(files, "item", "ItmCommonBrres.pac")
    for tex_name, at, data in brawl_assets.textures(open(path, "rb").read()):
        if tex_name == name:
            return brawl_assets.tex0_image(data, at).convert("RGBA")
    raise SystemExit(f"{name} not found in {path}")


def smash_ball(files):
    core = texture(files, "ItmSmashBallCore")
    # The core is two strips (warm above, cool below) with a black seam between them (row 32 of 64, fading back in by
    # row 40): each strip fills the texture and the two cross-fade through the middle, so the wrap has no dark band.
    w, h = core.size
    top = core.crop((0, 0, w, h * 31 // 64)).resize((256, 256), Image.BICUBIC)
    bottom = core.crop((0, h * 40 // 64, w, h)).resize((256, 256), Image.BICUBIC)
    fade = Image.linear_gradient("L").resize((256, 256)).point(lambda v: max(0, min(255, (v - 112) * 8)))
    core = Image.composite(bottom, top, fade)
    core = ImageEnhance.Brightness(ImageEnhance.Color(core).enhance(1.35)).enhance(1.25)  # as bright as the glowing item
    mark = texture(files, "ItmSmashBallMark").convert("L")
    cw, ch = core.size
    cpx = core.load()
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    c, r = N / 2, N * 0.43

    # A golden glow behind the ball.
    glow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([c - r * 1.1, c - r * 1.1, c + r * 1.1, c + r * 1.1], fill=(255, 200, 60, 210))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(N * 0.03)))

    # The sphere: each pixel's surface direction picks a texel of the core (longitude across, latitude down, wound
    # diagonally so the colours swirl), lit from the upper left.
    ball = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    bpx = ball.load()
    for y in range(N):
        for x in range(N):
            dx, dy = (x - c) / r, (y - c) / r
            d2 = dx * dx + dy * dy
            if d2 > 1.0:
                continue
            dz = math.sqrt(1.0 - d2)
            lon = math.atan2(dx, dz) / math.pi * 0.5 + 0.5
            lat = math.asin(max(-1.0, min(1.0, dy))) / math.pi + 0.5
            # Warm colours above, cool below, meeting along a wave that winds round the ball: the item's swirl.
            u = (lon + lat * 0.6) % 1.0
            v = min(1.0, max(0.0, lat + 0.16 * math.sin((lon * 2 + lat) * math.pi * 2)))
            cr, cg, cb, _ = cpx[int(u * (cw - 1)), int(v * (ch - 1))]
            light = 0.8 + 0.55 * max(0.0, -0.5 * dx - 0.6 * dy + 0.62 * dz)
            rim = d2 ** 4 * 0.35  # brighter toward the edge, as the item glows
            bpx[x, y] = tuple(min(255, int(v * light + 255 * rim)) for v in (cr, cg, cb)) + (255,)
    img.alpha_composite(ball)

    # The emblem on the face of the ball: the game's mark texture, white where it is bright.
    size = int(r * 1.32)
    emblem = mark.resize((size, size), Image.LANCZOS)
    alpha = emblem.point(lambda v: max(0, min(255, int((v - 24) * 4))))
    corner = (int(c - size / 2), int(c - size / 2))
    shadow = Image.new("RGBA", (size, size), (40, 10, 30, 160))
    img.paste(shadow, (corner[0] + int(N * 0.008), corner[1] + int(N * 0.01)), alpha.filter(ImageFilter.GaussianBlur(N * 0.006)))
    img.paste(Image.new("RGBA", (size, size), (255, 255, 255, 255)), corner, alpha)

    # A specular highlight and a thin gold rim.
    hl = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(hl).ellipse([c - r * 0.7, c - r * 0.88, c - r * 0.1, c - r * 0.45], fill=(255, 255, 255, 110))
    img.alpha_composite(hl.filter(ImageFilter.GaussianBlur(N * 0.03)))
    ImageDraw.Draw(img).ellipse([c - r, c - r, c + r, c + r], outline=(255, 226, 130, 255), width=max(2, int(N * 0.012)))
    return img


def main():
    files = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "Disc", "DATA", "files")
    ball = smash_ball(files)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    frames = [ball.resize((s, s), Image.LANCZOS) for s in sizes]
    for path in ("launcher/smash.ico", "wiicompiled/runtime/assets/icon/brawl.ico"):
        frames[-1].save(os.path.join(ROOT, path), sizes=[(s, s) for s in sizes], append_images=frames[:-1])
    ball.resize((256, 256), Image.LANCZOS).save(os.path.join(ROOT, "launcher/smash_preview.png"))
    ball.save(os.path.join(ROOT, "launcher/smash_ball.png"))
    print("icons written")


if __name__ == "__main__":
    main()
