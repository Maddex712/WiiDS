"""Extract private NSMB Wii lower-screen HUD assets from the owner's game files.

Pixels are decoded losslessly from the original TPL/BRFNT data. No game data is
included with this tool. Requires Pillow and the existing private gclib decoder
checkout under reference/mkw-3ds/vendor/gclib; that dependency is never copied.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tarfile
import types

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "reference/mkw-3ds/vendor/gclib"))
# gclib imports its optional encoding dependency even for decode-only use.
sys.modules.setdefault("imagequant", types.ModuleType("imagequant"))
from gclib.gx_enums import ImageFormat, PaletteFormat
from gclib.texture_utils import decode_image
from PIL import Image


def sha(data):
    return hashlib.sha256(data).hexdigest()


def u8_files(data):
    magic, root = struct.unpack_from(">II", data)
    if magic != 0x55AA382D:
        raise ValueError("Expected uncompressed retail U8 archive")
    count = struct.unpack_from(">I", data, root + 8)[0]
    strings = root + count * 12
    stack = []
    result = {}
    for index in range(1, count):
        while stack and index >= stack[-1][1]:
            stack.pop()
        kind_name, offset, size = struct.unpack_from(">III", data, root + index * 12)
        at = strings + (kind_name & 0xFFFFFF)
        name = data[at:data.index(0, at)].decode("ascii")
        if name in ("", "..") or "/" in name or "\\" in name:
            raise ValueError("Unsafe archive member")
        if kind_name >> 24:
            stack.append((name, size))
        else:
            if offset + size > len(data):
                raise ValueError("Truncated archive member")
            result["/".join([entry[0] for entry in stack] + [name])] = data[offset:offset + size]
    return result


def tpl(data):
    magic, count, table = struct.unpack_from(">III", data)
    if magic != 0x20AF30 or count != 1:
        raise ValueError("Expected single-image retail TPL")
    image, palette = struct.unpack_from(">II", data, table)
    height, width, fmt, pixels = struct.unpack_from(">HHII", data, image)
    pal, pal_format, pal_count = b"", 0, 0
    if palette:
        pal_count = struct.unpack_from(">H", data, palette)[0]
        pal_format, at = struct.unpack_from(">II", data, palette + 4)
        pal = data[at:at + pal_count * 2]
    return decode_image(io.BytesIO(data[pixels:]), io.BytesIO(pal),
                        ImageFormat(fmt), PaletteFormat(pal_format), pal_count,
                        width, height).convert("RGBA")


def sections(data):
    if data[:4] != b"RFNT" or data[4:6] != b"\xfe\xff":
        raise ValueError("Expected big-endian BRFNT")
    at = struct.unpack_from(">H", data, 12)[0]
    while at < len(data):
        size = struct.unpack_from(">I", data, at + 4)[0]
        if size < 8 or at + size > len(data):
            raise ValueError("Invalid BRFNT section")
        yield data[at:at + 4], at + 8
        at += size


def png(out, name, image, **extra):
    image = image.convert("RGBA")
    path = out / name
    image.save(path)
    return dict(file=name, sha256=sha(path.read_bytes()), width=image.width,
                height=image.height, rgbaSha256=sha(image.tobytes()), **extra)


def font(out, key, path, source):
    data = path.read_bytes()
    info, widths, mapping, sheets = {}, {}, {}, []
    for tag, at in sections(data):
        if tag == b"FINF":
            info.update(lineHeight=struct.unpack_from(">b", data, at + 1)[0],
                        ascent=data[at + 22])
        elif tag == b"TGLP":
            cw, ch, baseline, _, size, count, fmt, nx, ny, w, h, pixels = struct.unpack_from(">4BI6HI", data, at)
            info.update(cellWidth=cw, cellHeight=ch, baseline=baseline)
            per_sheet = nx * ny
            for index in range(count):
                raw = data[pixels + size * index:pixels + size * (index + 1)]
                sheets.append(decode_image(io.BytesIO(raw), io.BytesIO(),
                    ImageFormat(fmt), PaletteFormat(0), 0, w, h).convert("RGBA"))
        elif tag == b"CWDH":
            first, last = struct.unpack_from(">HH", data, at)
            for index in range(first, last + 1):
                widths[index] = struct.unpack_from(">bBb", data, at + 8 + (index - first) * 3)
        elif tag == b"CMAP":
            first, last, method = struct.unpack_from(">HHH", data, at)
            if method == 0:
                initial = struct.unpack_from(">H", data, at + 12)[0]
                mapping.update((code, initial + code - first) for code in range(first, last + 1))
            elif method == 1:
                mapping.update((code, struct.unpack_from(">H", data, at + 12 + (code - first) * 2)[0])
                               for code in range(first, last + 1))
            elif method == 2:
                count = struct.unpack_from(">H", data, at + 12)[0]
                mapping.update(struct.unpack_from(">HH", data, at + 14 + index * 4) for index in range(count))
            else:
                raise ValueError("Unsupported BRFNT character mapping")
    atlas = Image.new("RGBA", (1024, 2048))
    x = y = 1
    glyphs = {}
    for code, index in sorted(mapping.items()):
        if index == 65535 or not (32 <= code <= 126 or 160 <= code <= 255):
            continue
        sheet, cell = divmod(index, per_sheet)
        row, col = divmod(cell, nx)
        left, width, advance = widths[index]
        if x + max(width, 1) + 1 > atlas.width:
            x, y = 1, y + ch + 2
        if y + ch + 1 > atlas.height:
            raise ValueError("Font atlas overflow")
        image = sheets[sheet].crop((col * (cw + 1), row * (ch + 1),
            col * (cw + 1) + max(width, 1), row * (ch + 1) + ch))
        atlas.paste(image, (x, y))
        glyphs[chr(code)] = dict(x=x, y=y, w=image.width, h=image.height,
            left=left, advance=advance, sourceGlyphIndex=index, rgbaSha256=sha(image.tobytes()))
        x += image.width + 2
    atlas = atlas.crop((0, 0, 1024, y + ch + 1))
    return dict(png(out, f"font_{key}.png", atlas), **info, glyphs=glyphs,
        source=source, sourceSha256=sha(data),
        pixelRule="Native straight RGBA; no scaling, recoloring, extra outlines or font substitution.",
        drawRule="Draw at penX+left,topY; advance by advance. Scale all metrics together. Baseline uses ascent.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, default=ROOT / "build/nsmbw-thor/e1/extracted")
    parser.add_argument("--out", type=Path, default=ROOT / "build/nsmbw-thor/e1/lower-assets")
    parser.add_argument("--archive", type=Path, help="Optional private tar with ThorHud/ root for external-files staging")
    args = parser.parse_args()
    files = args.disc / "files"
    boot = (args.disc / "sys/boot.bin").read_bytes()
    if boot[:6] != b"SMNE01" or boot[7] != 1:
        raise ValueError("This asset profile requires the owner's USA Rev 1 extracted disc")
    if sha((args.disc / "sys/main.dol").read_bytes()) != "166eb6b7cca21418e316a6cfbf1bfbf0a213eb8f6396d42126a1482bb764a955":
        raise ValueError("Clean E1 source fingerprint does not match the pinned asset profile")
    args.out.mkdir(parents=True, exist_ok=True)
    archive_path = files / "Layout/gameScene/gameScene.arc"
    archive = archive_path.read_bytes()
    members = u8_files(archive)
    selection = {"coin": "im_coin_01", "clock": "im_timer_00",
                 "character0": "im_marioIcon_01", "character1": "im_luijiIcon_01",
                 "character2": "im_kinopioIcon_Blue_01", "character3": "im_kinopioIcon_Yellow_01",
                 "starCoin": "im_collectionCoin_01", "starCoinOff": "im_collectionOff_00"}
    manifest = dict(schema=1, kind="private-nsmbw-retail-hud", gameId="SMNE01", revision=1,
        images={}, fonts={}, sourceArchives={"files/Layout/gameScene/gameScene.arc": sha(archive)},
        characterOrder=["Mario", "Luigi", "Blue Toad", "Yellow Toad"],
        presentationNote="Independent passive lower HUD using original game pixels; placement is an adaptation. Upper HUD remains intact.")
    for key, stem in selection.items():
        name = f"arc/timg/{stem}.tpl"
        payload = members[name]
        manifest["images"][key] = png(args.out, key + ".png", tpl(payload),
            source="files/Layout/gameScene/gameScene.arc!" + name, sourceSha256=sha(payload))
    # Authored Wii UI pieces for the handheld-style progress/reserve presentation.
    extra = {"panel": ("gameScene", "im_miniWindowBar_32_400_00"),
             "reserve0": ("stockItem", "im_kinoko_02"),
             "reserve1": ("stockItem", "im_fireFlower_02"),
             "reserve4": ("stockItem", "im_iceFlower_02"),
             "reserve11": ("stockItem", "im_propeller_02"),
             "reserve13": ("stockItem", "im_mameKinoko_02"),
             "reserve14": ("stockItem", "im_penguin_02"),
             "mapTile": ("worldSelectMap", "im_block128_00"),
             # Handheld companion framing: the course-intro world strips, the stock-item window
             # corner and rounded bar, and the results-screen portrait for title/loading states.
             "frameCorner": ("stockItem", "im_kadomaruFlame_00"),
             "pill": ("stockItem", "im_miniWindow_32_220_R_00"),
             "heroMario": ("pointResult", "im_mario_00"),
             # NSMB DS-style course bar: the course-select guide's skull flag marks the goal.
             "goalFlag": ("corseSelectUIGuide", "im_middleFlagSkull_05")}
    for number, stem in enumerate(("im_worldBase_1_00", "im_worldBase_2_01", "im_worldBase_3_02",
                                   "im_worldBase_4_01", "im_worldBase_5_00", "im_worldBase_6_00",
                                   "im_worldBase_7_00", "im_worldBase_8_01", "im_worldBase_9_00"), 1):
        extra["world" + str(number)] = ("preGame", stem)
    for key, (group, stem) in extra.items():
        relative = "files/Layout/" + group + "/" + group + ".arc"
        data = (args.disc / relative).read_bytes()
        manifest["sourceArchives"][relative] = sha(data)
        member = "arc/timg/" + stem + ".tpl"
        payload = u8_files(data)[member]
        manifest["images"][key] = png(args.out, key + ".png", tpl(payload),
            source=relative + "!" + member, sourceSha256=sha(payload))
    # NSMB2-style world bubbles: each world's first course picture from the free-mode course select.
    relative = "files/Layout/textures/MultiCorseSelectTexture.arc"
    data = (args.disc / relative).read_bytes()
    manifest["sourceArchives"][relative] = sha(data)
    textures = u8_files(data)
    for number in range(1, 10):
        member = f"MultiCorseSelectTexture/im_W{number}_1.tpl"
        payload = textures[member]
        manifest["images"][f"course{number}"] = png(args.out, f"course{number}.png", tpl(payload),
            source=relative + "!" + member, sourceSha256=sha(payload))
    for key, name in (("main", "mj2d01_marioFont_64_IA4.brfnt"),
                      ("number", "mj2d00_numberFont_I4A.brfnt"),
                      ("message", "mj2d00_MessageFont_32_I4.brfnt")):
        relative = "US/EngUS/Font/" + name
        manifest["fonts"][key] = font(args.out, key, files / relative, "files/" + relative)
    (args.out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.archive:
        args.archive.parent.mkdir(parents=True, exist_ok=True)
        names = {"manifest.json"}
        names.update(record["file"] for record in manifest["images"].values())
        names.update(record["file"] for record in manifest["fonts"].values())
        with tarfile.open(args.archive, "w") as archive:
            for name in sorted(names):
                data = (args.out / name).read_bytes()
                entry = tarfile.TarInfo("ThorHud/" + name)
                entry.size, entry.mode, entry.mtime = len(data), 0o644, 0
                archive.addfile(entry, io.BytesIO(data))
    print(json.dumps(dict(out=str(args.out), images=len(manifest["images"]),
        fonts={key: len(record["glyphs"]) for key, record in manifest["fonts"].items()})))


if __name__ == "__main__":
    main()
