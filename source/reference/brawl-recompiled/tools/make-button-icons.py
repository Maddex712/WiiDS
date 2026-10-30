"""Builds the per-controller button prompt textures (UserData/button_icons/<xbox|playstation|nintendo>).

Each Wii Remote button picture in the game (1, 2, A, +, -, 1+2, the "Use Tippi" remote) is redrawn with the matching
controller button from Kenney's Input Prompts (CC0, tools/button-art), set in the game's own black button ring, on
top of the HD texture pack's artwork, and saved at the pack's 4x size under the game texture's own name. Also writes
controller.png (the Controls screen picture) and remote/ (the Controls screen's 3D Wii Remote, made transparent).
The game picks the set for the controller in use (aurora texture_replacement.cpp, virtual_remote.cpp).

    python tools/make-button-icons.py [--src <8x PNG pack Interface folder>] [--out Play/UserData/button_icons]
Needs ImageMagick (magick) to render the SVGs.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "tools", "button-art")

# Wii button -> Kenney SVG per style (tools/button-art/<style>/<name>.svg), following the default controls.
# Nintendo pads keep the game's own + and - (the same symbols).
STYLES = {
    "xbox": {"2": "xbox_button_color_a", "1": "xbox_button_color_b", "A": "xbox_button_color_y",
             "+": "xbox_button_menu", "-": "xbox_button_view", "stick": "xbox_stick_r", "stick_l": "xbox_stick_l", "shake": "xbox_rb",
             "controller": "controller_xboxseries"},
    "playstation": {"2": "playstation_button_color_cross", "1": "playstation_button_color_circle",
                    "A": "playstation_button_color_triangle", "+": "playstation5_button_options",
                    "-": "playstation5_button_create", "stick": "playstation_stick_r", "stick_l": "playstation_stick_l", "shake": "playstation_trigger_r1",
                    "controller": "controller_playstation5"},
    "nintendo": {"2": "switch_button_b", "1": "switch_button_y", "A": "switch_button_x", "stick": "switch_stick_r", "stick_l": "switch_stick_l", "shake": "switch_button_r",
                 "controller": "controller_switch_pro"},
}

# Textures (Interface/ of the 8x pack) and where their buttons are: (button, cx, cy, search radius) in fractions of
# the width/height. "disc" = a round button with a dark ring; "glyph" = a grey symbol on white (the icons inside text
# boxes); "face" = a symbol on a 3D button's top face; "tippi" = the "Use Tippi" picture.
TEXTURES = [
    ("tex1_152x144_eba7359cd27cf8b6_5.png", "disc", [("A", 0.23, 0.207, 0.12)]),
    ("tex1_152x48_26571db98021534d_5.png", "disc", [("-", 0.848, 0.52, 0.08)]),
    ("tex1_152x48_36da3dfecc7fc2ef_5.png", "disc", [("+", 0.152, 0.50, 0.08)]),
    ("tex1_176x168_6208c1ec2d692b4f_5.png", "disc", [("2", 0.409, 0.159, 0.10)]),
    ("tex1_200x152_8394740b60e9c228_5.png", "disc", [("1", 0.24, 0.547, 0.08)]),
    ("tex1_208x48_3566a7d381dc2f35_5.png", "disc", [("1", 0.115, 0.50, 0.045), ("2", 0.321, 0.50, 0.045)]),
    ("tex1_40x40_1d6fdad80b7dcd8a_14.png", "disc", [("-", 0.5, 0.5, 0.5)]),
    ("tex1_40x40_341c9fd2189acc61_5.png", "disc", [("2", 0.5, 0.5, 0.5)]),
    ("tex1_48x48_42bca1c8ab4b3dfb_5.png", "disc", [("A", 0.5, 0.5, 0.5)]),
    ("tex1_40x40_21ecf92a70736554_14.png", "glyph", [("1", 0.5, 0.5, 0.5)]),
    ("tex1_40x40_db14bc3d6c7b16a5_14.png", "glyph", [("2", 0.5, 0.5, 0.5)]),
    ("tex1_40x40_e73bd2aadadde9d9_14.png", "glyph", [("A", 0.5, 0.5, 0.5)]),
    ("tex1_56x56_4986da0fff2e544e_5.png", "face", [("A", 0.507, 0.453, 0.15)]),
    ("tex1_56x56_ff3270a64a8b87b2_5.png", "face", [("A", 0.507, 0.385, 0.15)]),
    ("tex1_56x72_38dcf921a993679a_5.png", "face", [("2", 0.504, 0.368, 0.15)]),
    ("tex1_168x136_42b4c0a037f4bbee_5.png", "tippi", [("stick", 0, 0, 0)]),
    ("tex1_248x184_fd844c6227d275cf_5.png", "none", []),  # the Controls screen's "Move" bubble (arrow cut only)
    ("tex1_248x200_55ef64a9d7e806f7_5.png", "none", []),
]

# Marker numbers (button_icons/<style>/marker<N>): the game reports whether these are drawn. 1 = the Controls screen's
# 3D Wii Remote (hidden): with a controller in use the game shows its own controls panel there (settings_overlay.cpp);
# 2-6 = that screen's Move, Flip, Jump, Use a Pixl, Use Tippi bubbles: the panel lists the actions the game shows.
MARKERS = {"tex1_248x184_fd844c6227d275cf_5.png": 2, "tex1_248x200_55ef64a9d7e806f7_5.png": 2,
           "tex1_152x144_eba7359cd27cf8b6_5.png": 3, "tex1_176x168_6208c1ec2d692b4f_5.png": 4,
           "tex1_200x152_8394740b60e9c228_5.png": 5, "tex1_168x136_42b4c0a037f4bbee_5.png": 6}

# Where each action's button is on the controller picture (fractions of controller.png): Move = left stick, Flip = top
# face button, Jump = bottom, Use a Pixl = the 1 button's, Use Tippi = right stick. The panel's callouts point there.
BUTTON_SPOTS = {
    "xbox": {"move": (0.267, 0.266), "flip": (0.722, 0.193), "jump": (0.722, 0.426), "pixl": (0.803, 0.31),
             "tippi": (0.58, 0.514)},
    "playstation": {"move": (0.358, 0.499), "flip": (0.792, 0.242), "jump": (0.792, 0.422), "pixl": (0.853, 0.333),
                    "tippi": (0.641, 0.499)},
    "nintendo": {"move": (0.236, 0.28), "flip": (0.763, 0.222), "jump": (0.763, 0.397), "pixl": (0.682, 0.31),
                 "tippi": (0.621, 0.513)},
}

# The Controls screen's 3D Wii Remote (its textures), hidden in every controller set; <style>/remote/ also tells the
# game to draw controller.png in its place (settings_overlay.cpp).
REMOTE_TEXTURES = [(256, 64, "ddc0097f76e8016b", 14), (256, 64, "a2fd9566973811d6", 14),   # only the remote's body:
                   (64, 32, "3b9919c664469aa9", 14), (80, 44, "c113b191224defc0", 14)]   # its button icons are used
                                                                                         # elsewhere too (issue #7)

_render_cache = {}


def render_svg(style, name, size):
    """Kenney SVG rendered to fit size x size, RGBA."""
    key = (style, name, size)
    if key not in _render_cache:
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "r.png")
            subprocess.run(["magick", "-background", "none", "-density", "1200", os.path.join(ART, style, name + ".svg"),
                            "-resize", f"{size}x{size}", out], check=True)
            _render_cache[key] = Image.open(out).convert("RGBA").copy()
    return _render_cache[key]


def luma(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


def dark_bbox(img, cx, cy, r, threshold):
    """Bounding box of the dark (ring/letter) pixels in the window around (cx, cy)."""
    w, h = img.size
    px = img.load()
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(max(0, int(cy - r)), min(h, int(cy + r))):
        for x in range(max(0, int(cx - r)), min(w, int(cx + r))):
            p = px[x, y]
            if p[3] > 160 and luma(p) < threshold:
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    if x1 < 0:
        raise ValueError("no button found")
    return x0, y0, x1, y1


def holes(icon):
    """An icon's see-through details (a button's symbol, a controller's buttons): transparent pixels that the
    outside can't reach."""
    a = icon.getchannel("A").point(lambda v: 255 if v > 128 else 0)
    filled = a.copy()
    ImageDraw.floodfill(filled, (0, 0), 128)
    return filled.point(lambda v: 255 if v == 0 else 0)


def solid(color, mask):
    layer = Image.new("RGBA", mask.size, color)
    layer.putalpha(mask)
    return layer


def disc(img, box, style, name):
    """The game's look: a black ring, then Kenney's button, its symbol showing the ring's black through."""
    x0, y0, x1, y1 = box
    ImageDraw.Draw(img).ellipse(box, fill="#141414")
    ring = (x1 - x0) * 0.07
    img.alpha_composite(render_svg(style, name, int(round(x1 - x0 - 2 * ring))),
                        (int(round(x0 + ring)), int(round(y0 + ring))))


def tippi(img, style):
    """Use Tippi: cover the TV and the remote with tape from elsewhere in the picture, then put the stick there."""
    w, h = img.size
    src = img.copy()

    def patch(dst, frm):
        (dx0, dy0, dx1, dy1), (fx0, fy0, fx1, fy1) = dst, frm
        piece = src.crop((int(fx0 * w), int(fy0 * h), int(fx1 * w), int(fy1 * h)))
        img.paste(piece.resize((int((dx1 - dx0) * w), int((dy1 - dy0) * h)), Image.LANCZOS), (int(dx0 * w), int(dy0 * h)))

    patch((0.045, 0.29, 0.29, 0.535), (0.55, 0.705, 0.795, 0.93))   # the TV, from the long tape strip
    patch((0.11, 0.535, 0.225, 0.88), (0.62, 0.70, 0.735, 0.93))    # the remote and its red arrow
    size = int(0.24 * w)
    stick = render_svg(style, STYLES[style]["stick"], size)
    at = (int(0.168 * w - stick.width / 2), int(0.47 * h - stick.height / 2))
    a = stick.getchannel("A")
    img.alpha_composite(solid((20, 20, 20, 255), a), at)                     # dark stick
    img.alpha_composite(solid((255, 255, 255, 255), holes(stick)), at)       # its R and ring in white


def sideways_picture(picture):
    """The boot screen "Hold the Wii Remote sideways." (its line reads "Enjoy New Super Paper Bros!", dvd.cpp): its
    hands-and-remote texture (464x288) becomes the controller in use, at the 4x size of the HD pack."""
    canvas = Image.new("RGBA", (464 * 4, 288 * 4), (0, 0, 0, 0))
    pic = picture.copy()
    pic.thumbnail((int(canvas.width * 0.8), int(canvas.height * 0.85)), Image.LANCZOS)
    canvas.alpha_composite(pic, ((canvas.width - pic.width) // 2, (canvas.height - pic.height) // 2))
    return canvas


SIDEWAYS_TEXTURE = "tex1_464x288_36a6f13147d63352_14.png"


def controller_picture(style, name=None):
    """Kenney's flat controller: white body, dark buttons, black outline, like the game's paper cut-outs."""
    pic = render_svg(style, name or STYLES[style]["controller"], 1100)
    a = pic.getchannel("A").point(lambda v: 255 if v > 128 else 0)
    detail = holes(pic)
    pad = 24
    out = Image.new("RGBA", (pic.width + 2 * pad, pic.height + 2 * pad), (0, 0, 0, 0))

    def layer(mask, color, grow=0):
        m = Image.new("L", out.size, 0)
        m.paste(mask, (pad, pad))
        if grow:
            m = m.filter(ImageFilter.MaxFilter(grow))
        out.alpha_composite(solid(color, m))

    layer(ImageChops.lighter(a, detail), (20, 20, 20, 255), 25)  # outline
    layer(detail, (64, 64, 70, 255))                              # buttons and sticks
    layer(a, (250, 250, 250, 255))                                # body
    return out.crop(out.getchannel("A").getbbox())


def render(src, kind, buttons, style):
    img = Image.open(src).convert("RGBA")
    w, h = img.size
    changed = kind == "none"  # unchanged art, only there to be reported (a marker)
    for button, fx, fy, fr in buttons:
        name = STYLES[style].get(button) if button else None
        if name is None:
            continue
        if kind == "tippi":
            tippi(img, style)
        elif kind == "disc":
            x0, y0, x1, y1 = dark_bbox(img, fx * w, fy * h, fr * max(w, h), 150 if button in "+-" else 90)
            pad = (x1 - x0) * 0.05
            disc(img, (x0 - pad, y0 - pad, x1 + pad, y1 + pad), style, name)
        elif kind == "glyph":  # grey symbol on white, like the game's own
            ImageDraw.Draw(img).rectangle([0, 0, w, h], fill="#FFFFFF")
            size = int(w * 0.84)
            img.alpha_composite(solid((156, 156, 156, 255), holes(render_svg(style, name, size))),
                                ((w - size) // 2, (h - size) // 2))
        else:  # face: paint the old symbol out with the face colour, then the new symbol squashed onto the face
            x0, y0, x1, y1 = dark_bbox(img, fx * w, fy * h, fr * max(w, h), 110)
            px = img.load()
            light = [px[x, y] for y in range(int(y0), int(y1)) for x in range(int(x0), int(x1)) if luma(px[x, y]) > 170]
            fill = tuple(sorted(light, key=luma)[len(light) // 2]) if light else (230, 232, 236, 255)
            m = (x1 - x0) * 0.12
            ImageDraw.Draw(img).ellipse([x0 - m, y0 - m, x1 + m, y1 + m], fill=fill)
            size = int(max(x1 - x0, y1 - y0) * 1.5)
            sym = holes(render_svg(style, name, size))
            squash = max(0.45, min(1.0, (y1 - y0) / max(1, x1 - x0)))
            sym = sym.resize((sym.width, max(1, int(sym.height * squash))), Image.LANCZOS)
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            img.alpha_composite(solid((46, 46, 46, 255), sym), (int(cx - sym.width / 2), int(cy - sym.height / 2)))
        changed = True
    return img if changed else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(ROOT, "Play", "UserData", "texture_pack_png_8x", "R8PE01", "Interface"))
    ap.add_argument("--out", default=os.path.join(ROOT, "Play", "UserData", "button_icons"))
    args = ap.parse_args()
    for style in STYLES:
        out = os.path.join(args.out, style)
        shutil.rmtree(out, ignore_errors=True)
        for m in range(1, 7):
            os.makedirs(os.path.join(out, f"marker{m}"), exist_ok=True)
        n = 0
        for name, kind, buttons in TEXTURES:
            img = render(os.path.join(args.src, name), kind, buttons, style)
            if img is None:
                continue
            img = img.resize((img.width // 2, img.height // 2), Image.LANCZOS)  # 4x, like the HD pack in use
            img.save(os.path.join(out, f"marker{MARKERS[name]}" if name in MARKERS else "", name))
            n += 1
        for w, h, hsh, fmt in REMOTE_TEXTURES:
            Image.new("RGBA", (w, h), (0, 0, 0, 0)).save(os.path.join(out, "marker1", f"tex1_{w}x{h}_{hsh}_{fmt}.png"))
        controller_picture(style).save(os.path.join(out, "controller.png"))
        sideways_picture(Image.open(os.path.join(out, "controller.png"))).save(os.path.join(out, SIDEWAYS_TEXTURE))
        with open(os.path.join(out, "panel.txt"), "w", encoding="ascii") as f:
            # <action> <x> <y>: where the action's button is, in fractions of controller.png
            for name, (fx, fy) in BUTTON_SPOTS[style].items():
                print(name, fx, fy, file=f)
            # Stylish move (shake) = the right shoulder: the top of the picture's outline above x = 0.78.
            pad = Image.open(os.path.join(out, "controller.png"))
            x = int(pad.width * 0.78)
            top = next(y for y in range(pad.height) if pad.getpixel((x, y))[3] > 128)
            print("stylish", 0.78, round(top / pad.height + 0.02, 3), file=f)
        # The panel's button icons, one per action.
        os.makedirs(os.path.join(out, "ui"), exist_ok=True)
        icons = {"move": "stick_l", "flip": "A", "jump": "2", "pixl": "1", "tippi": "stick", "stylish": "shake"}
        for action, button in icons.items():
            render_svg(style, STYLES[style][button], 128).save(os.path.join(out, "ui", action + ".png"))
        print(f"{style}: {n} textures -> {out}")
    # For players without a controller (button_icons/common, looked up after a style's own set): the boot screen
    # shows a keyboard instead of the hands holding a Wii Remote.
    common = os.path.join(args.out, "common")
    shutil.rmtree(common, ignore_errors=True)
    os.makedirs(common)
    sideways_picture(controller_picture("common", "keyboard")).save(os.path.join(common, SIDEWAYS_TEXTURE))
    print(f"common: -> {common}")


if __name__ == "__main__":
    sys.exit(main())
