"""Reads textures and fonts out of Brawl's files (ARC .pac, BRRES TEX0, BRFNT): enough for the launcher's art.
   python tools/brawl_assets.py find <disc files dir> <word>...     lists TEX0 / font names containing a word
   python tools/brawl_assets.py tex <file> <texture name> <out.png> decodes one texture (first mip level)"""
import os
import struct
import sys

from PIL import Image


def lz77(data):
    """Brawl's compressed .pac (type 0x10 or 0x11 LZ77, Nintendo's), else the data as it is."""
    if not data or data[0] not in (0x10, 0x11):
        return data
    size = data[1] | data[2] << 8 | data[3] << 16
    pos = 4
    if size == 0:
        size = struct.unpack_from("<I", data, 4)[0]
        pos = 8
    out = bytearray()
    ext = data[0] == 0x11
    while len(out) < size and pos < len(data):
        flags = data[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                if pos + 1 >= len(data):
                    return bytes(out)
                b0 = data[pos]
                if not ext:
                    length = (b0 >> 4) + 3
                    disp = ((b0 & 0xF) << 8 | data[pos + 1]) + 1
                    pos += 2
                else:
                    hi = b0 >> 4
                    if hi == 0:
                        length = ((b0 & 0xF) << 4 | data[pos + 1] >> 4) + 0x11
                        disp = ((data[pos + 1] & 0xF) << 8 | data[pos + 2]) + 1
                        pos += 3
                    elif hi == 1:
                        length = ((b0 & 0xF) << 12 | data[pos + 1] << 4 | data[pos + 2] >> 4) + 0x111
                        disp = ((data[pos + 2] & 0xF) << 8 | data[pos + 3]) + 1
                        pos += 4
                    else:
                        length = hi + 1
                        disp = ((b0 & 0xF) << 8 | data[pos + 1]) + 1
                        pos += 2
                for _ in range(length):
                    out.append(out[-disp])
            else:
                out.append(data[pos])
                pos += 1
    return bytes(out)


def name_at(data, base, offset):
    """BRRES strings: the offset points at the text, its length is the u32 before it."""
    at = base + offset
    if offset <= 0 or at >= len(data):
        return ""
    end = data.find(b"\0", at)
    return data[at:end].decode("latin-1", "replace")


def textures(data):
    """Every TEX0 block in a file (searched, so it works through ARC/BRRES nesting): (name, offset)."""
    data = lz77(data)
    found = []
    at = data.find(b"TEX0")
    while at != -1:
        if at + 0x40 <= len(data):
            version = struct.unpack_from(">I", data, at + 8)[0]
            if version in (1, 2, 3):
                name_off = struct.unpack_from(">i", data, at + 0x14)[0]
                found.append((name_at(data, at, name_off), at, data))
        at = data.find(b"TEX0", at + 4)
    return found


# ---- GX texture decoders (block layouts from the GX docs, as Dolphin decodes them) ----

def rgb565(v):
    return ((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, 255)


def rgb5a3(v):
    if v & 0x8000:
        return ((v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
    return ((v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17, (v >> 12 & 7) * 255 // 7)


def decode(fmt, w, h, src):
    img = Image.new("RGBA", (w, h))
    px = img.load()
    bw, bh = {0: (8, 8), 1: (8, 4), 2: (8, 4), 3: (4, 4), 4: (4, 4), 5: (4, 4), 6: (4, 4), 14: (8, 8)}[fmt]
    pos = 0
    for by in range(0, h, bh):
        for bx in range(0, w, bw):
            if fmt == 14:  # CMPR: 2x2 DXT1 sub-blocks
                for sy in (0, 4):
                    for sx in (0, 4):
                        c0, c1 = struct.unpack_from(">HH", src, pos)
                        bits = struct.unpack_from(">I", src, pos + 4)[0]
                        pos += 8
                        p0, p1 = rgb565(c0), rgb565(c1)
                        if c0 > c1:
                            pal = [p0, p1, tuple((2 * a + b) // 3 for a, b in zip(p0, p1)),
                                   tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))]
                        else:
                            pal = [p0, p1, tuple((a + b) // 2 for a, b in zip(p0, p1)), (0, 0, 0, 0)]
                        for i in range(16):
                            x, y = bx + sx + i % 4, by + sy + i // 4
                            if x < w and y < h:
                                px[x, y] = pal[bits >> (30 - 2 * i) & 3]
                continue
            if fmt == 6:  # RGBA8: AR then GB planes
                for i in range(16):
                    a, r = src[pos + 2 * i], src[pos + 2 * i + 1]
                    g, b = src[pos + 32 + 2 * i], src[pos + 33 + 2 * i]
                    x, y = bx + i % 4, by + i // 4
                    if x < w and y < h:
                        px[x, y] = (r, g, b, a)
                pos += 64
                continue
            for y in range(by, by + bh):
                for x in range(bx, bx + bw):
                    if fmt == 0:  # I4
                        v = src[pos // 2] >> (4 if pos % 2 == 0 else 0) & 15
                        c = (v * 17,) * 3 + (255,)
                        pos += 1
                    elif fmt == 1:  # I8
                        c = (src[pos],) * 3 + (255,)
                        pos += 1
                    elif fmt == 2:  # IA4
                        v = src[pos]
                        c = ((v & 15) * 17,) * 3 + ((v >> 4) * 17,)
                        pos += 1
                    elif fmt == 3:  # IA8
                        c = (src[pos + 1],) * 3 + (src[pos],)
                        pos += 2
                    elif fmt == 4:
                        c = rgb565(struct.unpack_from(">H", src, pos)[0])
                        pos += 2
                    else:  # 5: RGB5A3
                        c = rgb5a3(struct.unpack_from(">H", src, pos)[0])
                        pos += 2
                    if x < w and y < h:
                        px[x, y] = c
            if fmt == 0:
                pass
    if fmt == 0:
        pass
    return img


def palette(data, name):
    """The PLT0 block named like a paletted texture: its colours."""
    at = data.find(b"PLT0")
    while at != -1:
        if name_at(data, at, struct.unpack_from(">i", data, at + 0x14)[0]) == name:
            data_off = struct.unpack_from(">I", data, at + 0x10)[0]
            fmt = struct.unpack_from(">I", data, at + 0x18)[0]
            count = struct.unpack_from(">H", data, at + 0x1C)[0]
            out = []
            for i in range(count):
                v = struct.unpack_from(">H", data, at + data_off + 2 * i)[0]
                out.append(((v & 0xFF,) * 3 + (v >> 8,)) if fmt == 0 else rgb565(v) if fmt == 1 else rgb5a3(v))
            return out
        at = data.find(b"PLT0", at + 4)
    return [(i, i, i, 255) for i in range(256)]


def decode_indexed(fmt, w, h, src, pal):
    """C4 (8x8 blocks), C8 (8x4) and C14X2 (4x4) through the palette."""
    img = Image.new("RGBA", (w, h))
    px = img.load()
    bw, bh = {8: (8, 8), 9: (8, 4), 10: (4, 4)}[fmt]
    pos = 0
    for by in range(0, h, bh):
        for bx in range(0, w, bw):
            for y in range(by, by + bh):
                for x in range(bx, bx + bw):
                    if fmt == 8:
                        i = src[pos // 2] >> (4 if pos % 2 == 0 else 0) & 15
                        pos += 1
                    elif fmt == 9:
                        i = src[pos]
                        pos += 1
                    else:
                        i = struct.unpack_from(">H", src, pos)[0] & 0x3FFF
                        pos += 2
                    if x < w and y < h and i < len(pal):
                        px[x, y] = pal[i]
            if fmt == 8 and pos % 2:
                pos += 1
    return img


def tex0_image(data, at):
    data_off = struct.unpack_from(">I", data, at + 0x10)[0]
    w, h = struct.unpack_from(">HH", data, at + 0x1C)
    fmt = struct.unpack_from(">I", data, at + 0x20)[0]
    if fmt in (8, 9, 10):
        name = name_at(data, at, struct.unpack_from(">i", data, at + 0x14)[0])
        return decode_indexed(fmt, w, h, data[at + data_off:], palette(data, name))
    if fmt == 0:  # I4 packs two texels a byte: give the decoder a nibble index
        return decode_i4(w, h, data[at + data_off:])
    return decode(fmt, w, h, data[at + data_off:])


def decode_i4(w, h, src):
    img = Image.new("RGBA", (w, h))
    px = img.load()
    pos = 0
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            for y in range(by, by + 8):
                for x in range(bx, bx + 8, 2):
                    v = src[pos]
                    pos += 1
                    for dx, n in ((0, v >> 4), (1, v & 15)):
                        if x + dx < w and y < h:
                            px[x + dx, y] = (255, 255, 255, n * 17)
    return img


def main():
    if sys.argv[1] == "find":
        root, words = sys.argv[2], [w.lower() for w in sys.argv[3:]]
        for dirpath, _, files in os.walk(root):
            for f in files:
                path = os.path.join(dirpath, f)
                if not f.lower().endswith((".pac", ".brres", ".pcs", ".brfnt", ".arc")):
                    continue
                data = open(path, "rb").read()
                if f.lower().endswith(".brfnt") and any(w in f.lower() for w in words + ["font"]):
                    print("FONT", os.path.relpath(path, root))
                for name, _, d in textures(data):
                    if any(w in name.lower() for w in words):
                        print(os.path.relpath(path, root), "::", name)
    elif sys.argv[1] == "tex":
        path, want, out = sys.argv[2:5]
        for name, at, d in textures(open(path, "rb").read()):
            if name == want:
                tex0_image(d, at).save(out)
                print("saved", out)
                return
        print("not found")


if __name__ == "__main__":
    main()


# ---- BRFNT (NW4R bitmap fonts) ----

class Font:
    """A BRFNT's glyph sheets, widths and character map."""

    def __init__(self, data):
        self.data = data = lz77(data)
        finf = data.find(b"FINF")
        (self.font_type, self.leading, self.default_index, dl, dg, dc, self.encoding, tglp, cwdh, cmap,
         self.height, self.width, self.ascent) = struct.unpack_from(">BbHbBbBIIIBBB", data, finf + 8)
        self.default_width = (dl, dg, dc)
        t = tglp - 8
        (self.cell_w, self.cell_h, self.baseline, _, sheet_size, sheet_count, fmt, self.cols, self.rows, sw, sh,
         sheet_off) = struct.unpack_from(">BBbBIHHHHHHI", data, t + 8)
        self.sheets = []
        for i in range(sheet_count):
            src = data[sheet_off + i * sheet_size:]
            self.sheets.append(decode_i4(sw, sh, src) if fmt == 0 else decode(fmt, sw, sh, src))
        self.widths = {}
        at = cwdh - 8
        while at > 0:
            start, end, nxt = struct.unpack_from(">HHI", data, at + 8)
            for i in range(start, end + 1):
                self.widths[i] = struct.unpack_from(">bBb", data, at + 16 + 3 * (i - start))
            at = nxt - 8 if nxt else 0
        self.cmap = {}
        at = cmap - 8
        while at > 0:
            begin, end, method, nxt = struct.unpack_from(">HHH2xI", data, at + 8)
            body = at + 20
            if method == 0:
                offset = struct.unpack_from(">H", data, body)[0]
                for code in range(begin, end + 1):
                    self.cmap[code] = code - begin + offset
            elif method == 1:
                for code in range(begin, end + 1):
                    idx = struct.unpack_from(">H", data, body + 2 * (code - begin))[0]
                    if idx != 0xFFFF:
                        self.cmap[code] = idx
            else:
                count = struct.unpack_from(">H", data, body)[0]
                for k in range(count):
                    code, idx = struct.unpack_from(">HH", data, body + 2 + 4 * k)
                    self.cmap[code] = idx
            at = nxt - 8 if nxt else 0

    def glyph(self, ch):
        """(cell image, (left, glyph width, advance)) for a character, the default glyph when it has none."""
        idx = self.cmap.get(ord(ch), self.default_index)
        per = self.cols * self.rows
        sheet, cell = divmod(idx, per)
        row, col = divmod(cell, self.cols)
        x, y = col * (self.cell_w + 1), row * (self.cell_h + 1)
        img = self.sheets[sheet].crop((x, y, x + self.cell_w, y + self.cell_h))
        return img, self.widths.get(idx, self.default_width)

    def render(self, text, color=(255, 255, 255)):
        width = sum(self.glyph(ch)[1][2] for ch in text) + 4
        out = Image.new("RGBA", (max(1, width), self.cell_h), (0, 0, 0, 0))
        x = 0
        for ch in text:
            img, (left, glyph_w, advance) = self.glyph(ch)
            tinted = Image.new("RGBA", img.size, color + (255,))
            tinted.putalpha(img.getchannel("A"))
            out.alpha_composite(tinted.crop((0, 0, glyph_w, img.height)), (x + left, 0))
            x += advance
        return out
