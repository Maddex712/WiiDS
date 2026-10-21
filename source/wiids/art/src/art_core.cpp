// Core decoders for wiids_art. GX texture decoding follows gclib's texture_utils.py (MIT,
// LagoLunatic) bit for bit; BRFNT atlas packing follows the WiiDS PC extractors.
#include "wiids_art.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace wiids::art {
namespace {

[[noreturn]] void Fail(const std::string& message) { throw std::runtime_error(message); }

uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

void Check(const Bytes& data, size_t offset, size_t size, const char* what) {
    if (offset > data.size() || size > data.size() - offset) Fail(std::string("Truncated ") + what);
}

struct Rgba { uint8_t r, g, b, a; };

uint8_t X3(uint32_t v) { return uint8_t((v << 5) | (v << 2) | (v >> 1)); }
uint8_t X4(uint32_t v) { return uint8_t((v << 4) | v); }
uint8_t X5(uint32_t v) { return uint8_t((v << 3) | (v >> 2)); }
uint8_t X6(uint32_t v) { return uint8_t((v << 2) | (v >> 4)); }

Rgba Rgb565(uint16_t c) { return {X5((c >> 11) & 0x1F), X6((c >> 5) & 0x3F), X5(c & 0x1F), 255}; }
Rgba Rgb5a3(uint16_t c) {
    if (!(c & 0x8000)) return {X4((c >> 8) & 0xF), X4((c >> 4) & 0xF), X4(c & 0xF), X3((c >> 12) & 0x7)};
    return {X5((c >> 10) & 0x1F), X5((c >> 5) & 0x1F), X5(c & 0x1F), 255};
}
Rgba Ia8(uint16_t c) { const uint8_t i = uint8_t(c & 0xFF); return {i, i, i, uint8_t(c >> 8)}; }

Rgba PaletteColor(uint16_t raw, PaletteFormat format) {
    switch (format) {
        case PaletteFormat::IA8: return Ia8(raw);
        case PaletteFormat::RGB565: return Rgb565(raw);
        default: return Rgb5a3(raw);
    }
}

struct BlockShape { int w, h; size_t bytes; };
BlockShape Shape(GxFormat f) {
    switch (f) {
        case GxFormat::I4: return {8, 8, 32};
        case GxFormat::I8: return {8, 4, 32};
        case GxFormat::IA4: return {8, 4, 32};
        case GxFormat::IA8: return {4, 4, 32};
        case GxFormat::RGB565: return {4, 4, 32};
        case GxFormat::RGB5A3: return {4, 4, 32};
        case GxFormat::RGBA32: return {4, 4, 64};
        case GxFormat::C4: return {8, 8, 32};
        case GxFormat::C8: return {8, 4, 32};
        case GxFormat::C14X2: return {4, 4, 32};
        case GxFormat::CMPR: return {8, 8, 32};
    }
    Fail("Unknown texture format");
}

} // namespace

Image Image::Crop(int left, int top, int right, int bottom) const {
    Image out(std::max(0, right - left), std::max(0, bottom - top));
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const int sx = left + x, sy = top + y;
            if (sx >= 0 && sy >= 0 && sx < width && sy < height) std::memcpy(out.At(x, y), At(sx, sy), 4);
        }
    return out;
}

void Image::Paste(const Image& source, int x0, int y0) {
    for (int y = 0; y < source.height; ++y)
        for (int x = 0; x < source.width; ++x) {
            const int dx = x0 + x, dy = y0 + y;
            if (dx >= 0 && dy >= 0 && dx < width && dy < height) std::memcpy(At(dx, dy), source.At(x, y), 4);
        }
}

size_t GxImageSize(GxFormat format, int width, int height) {
    const BlockShape s = Shape(format);
    return size_t((width + s.w - 1) / s.w) * size_t((height + s.h - 1) / s.h) * s.bytes;
}

Image DecodeGx(GxFormat format, const uint8_t* data, size_t size, int width, int height,
               const uint8_t* palette, PaletteFormat paletteFormat, int paletteCount) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) Fail("Implausible texture size");
    const BlockShape s = Shape(format);
    if (GxImageSize(format, width, height) > size) Fail("Truncated texture");
    std::vector<Rgba> colors;
    if (format == GxFormat::C4 || format == GxFormat::C8 || format == GxFormat::C14X2) {
        if (!palette) Fail("Palette texture without a palette");
        for (int i = 0; i < paletteCount; ++i) colors.push_back(PaletteColor(Be16(palette + 2 * i), paletteFormat));
    }
    Image image(width, height);
    std::array<Rgba, 64> block{};
    std::array<bool, 64> valid{};
    size_t offset = 0;
    for (int by = 0; by < height; by += s.h) {
        for (int bx = 0; bx < width; bx += s.w) {
            const uint8_t* b = data + offset;
            const int count = s.w * s.h;
            valid.fill(true);
            switch (format) {
                case GxFormat::I4:
                    for (int i = 0; i < 64; ++i) { const uint8_t v = X4((b[i / 2] >> (i % 2 ? 0 : 4)) & 0xF); block[i] = {v, v, v, v}; }
                    break;
                case GxFormat::I8:
                    for (int i = 0; i < 32; ++i) block[i] = {b[i], b[i], b[i], b[i]};
                    break;
                case GxFormat::IA4:
                    for (int i = 0; i < 32; ++i) { const uint8_t l = X4(b[i] & 0xF), a = X4(b[i] >> 4); block[i] = {l, l, l, a}; }
                    break;
                case GxFormat::IA8:
                    for (int i = 0; i < 16; ++i) block[i] = Ia8(Be16(b + 2 * i));
                    break;
                case GxFormat::RGB565:
                    for (int i = 0; i < 16; ++i) block[i] = Rgb565(Be16(b + 2 * i));
                    break;
                case GxFormat::RGB5A3:
                    for (int i = 0; i < 16; ++i) block[i] = Rgb5a3(Be16(b + 2 * i));
                    break;
                case GxFormat::RGBA32:
                    for (int i = 0; i < 16; ++i) block[i] = {b[2 * i + 1], b[2 * i + 32], b[2 * i + 33], b[2 * i]};
                    break;
                case GxFormat::C4:
                    for (int i = 0; i < 64; ++i) {
                        const size_t index = (b[i / 2] >> (i % 2 ? 0 : 4)) & 0xF;
                        if (index < colors.size()) block[i] = colors[index]; else valid[i] = false;
                    }
                    break;
                case GxFormat::C8:
                    for (int i = 0; i < 32; ++i) {
                        if (b[i] < colors.size()) block[i] = colors[b[i]]; else valid[i] = false;
                    }
                    break;
                case GxFormat::C14X2:
                    for (int i = 0; i < 16; ++i) {
                        const size_t index = Be16(b + 2 * i) & 0x3FFF;
                        if (index < colors.size()) block[i] = colors[index]; else valid[i] = false;
                    }
                    break;
                case GxFormat::CMPR:
                    for (int sub = 0; sub < 4; ++sub) {
                        const uint8_t* p = b + 8 * sub;
                        const uint16_t c0 = Be16(p), c1 = Be16(p + 2);
                        const Rgba a = Rgb565(c0), z = Rgb565(c1);
                        Rgba pal[4] = {a, z, {}, {}};
                        if (c0 > c1) {
                            pal[2] = {uint8_t((2 * a.r + z.r) / 3), uint8_t((2 * a.g + z.g) / 3), uint8_t((2 * a.b + z.b) / 3), 255};
                            pal[3] = {uint8_t((a.r + 2 * z.r) / 3), uint8_t((a.g + 2 * z.g) / 3), uint8_t((a.b + 2 * z.b) / 3), 255};
                        } else {
                            pal[2] = {uint8_t(a.r / 2 + z.r / 2), uint8_t(a.g / 2 + z.g / 2), uint8_t(a.b / 2 + z.b / 2), 255};
                            pal[3] = {0, 0, 0, 0};
                        }
                        const uint32_t indexes = Be32(p + 4);
                        const int sx = (sub % 2) * 4, sy = (sub / 2) * 4;
                        for (int i = 0; i < 16; ++i)
                            block[sx + sy * 8 + (i / 4) * 8 + (i % 4)] = pal[(indexes >> ((15 - i) * 2)) & 3];
                    }
                    break;
            }
            for (int i = 0; i < count; ++i) {
                const int x = bx + i % s.w, y = by + i / s.w;
                if (x >= width || y >= height || !valid[i]) continue;
                uint8_t* px = image.At(x, y);
                px[0] = block[i].r; px[1] = block[i].g; px[2] = block[i].b; px[3] = block[i].a;
            }
            offset += s.bytes;
        }
    }
    return image;
}

Bytes ReadFile(const std::string& path) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    if (!in) Fail("Missing game file: " + path);
    const std::streamoff size = in.tellg();
    Bytes data(size_t(size > 0 ? size : 0));
    in.seekg(0);
    if (!data.empty() && !in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()))) Fail("Couldn't read " + path);
    return data;
}

void WriteFile(const std::string& path, const void* data, size_t size) {
    const auto p = std::filesystem::u8path(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(static_cast<const char*>(data), std::streamsize(size))) Fail("Couldn't write " + path);
}

Bytes Yaz0Decompress(const Bytes& data) {
    if (data.size() < 16 || std::memcmp(data.data(), "Yaz0", 4) != 0) Fail("Expected a Yaz0 archive");
    const uint32_t size = Be32(data.data() + 4);
    if (size > (256u << 20)) Fail("Implausible Yaz0 size");
    Bytes out;
    out.reserve(size);
    size_t at = 16;
    while (out.size() < size) {
        if (at >= data.size()) Fail("Truncated Yaz0 archive");
        const uint8_t header = data[at++];
        for (int bit = 7; bit >= 0 && out.size() < size; --bit) {
            if (header & (1 << bit)) {
                if (at >= data.size()) Fail("Truncated Yaz0 archive");
                out.push_back(data[at++]);
            } else {
                if (at + 2 > data.size()) Fail("Truncated Yaz0 archive");
                const uint8_t b1 = data[at], b2 = data[at + 1];
                at += 2;
                size_t length = b1 >> 4;
                const size_t distance = (size_t(b1 & 0xF) << 8 | b2) + 1;
                if (length == 0) {
                    if (at >= data.size()) Fail("Truncated Yaz0 archive");
                    length = size_t(data[at++]) + 0x12;
                } else {
                    length += 2;
                }
                if (distance > out.size()) Fail("Damaged Yaz0 archive");
                for (size_t i = 0; i < length && out.size() < size; ++i) out.push_back(out[out.size() - distance]);
            }
        }
    }
    return out;
}

std::map<std::string, Bytes> U8Files(const Bytes& data) {
    Check(data, 0, 8, "U8 archive");
    if (Be32(data.data()) != 0x55AA382D) Fail("Expected an uncompressed U8 archive");
    const uint32_t root = Be32(data.data() + 4);
    Check(data, root, 12, "U8 archive");
    const uint32_t count = Be32(data.data() + root + 8);
    Check(data, root, size_t(count) * 12, "U8 archive");
    const size_t strings = root + size_t(count) * 12;
    std::vector<std::pair<std::string, uint32_t>> stack;
    std::map<std::string, Bytes> result;
    for (uint32_t index = 1; index < count; ++index) {
        while (!stack.empty() && index >= stack.back().second) stack.pop_back();
        const uint8_t* e = data.data() + root + size_t(index) * 12;
        const uint32_t kindName = Be32(e), offset = Be32(e + 4), size = Be32(e + 8);
        const size_t at = strings + (kindName & 0xFFFFFF);
        if (at >= data.size()) Fail("Damaged U8 archive");
        const auto end = std::find(data.begin() + std::ptrdiff_t(at), data.end(), uint8_t(0));
        const std::string name(data.begin() + std::ptrdiff_t(at), end);
        if (name.empty() || name == ".." || name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
            Fail("Unsafe archive member");
        if (kindName >> 24) {
            stack.emplace_back(name, size);
        } else {
            Check(data, offset, size, "archive member");
            std::string path;
            for (const auto& entry : stack) path += entry.first + "/";
            result[path + name] = Bytes(data.begin() + offset, data.begin() + offset + size);
        }
    }
    return result;
}

Image DecodeTpl(const Bytes& data) {
    Check(data, 0, 12, "TPL");
    if (Be32(data.data()) != 0x0020AF30 || Be32(data.data() + 4) != 1) Fail("Expected a single-image TPL");
    const uint32_t table = Be32(data.data() + 8);
    Check(data, table, 8, "TPL");
    const uint32_t image = Be32(data.data() + table), palette = Be32(data.data() + table + 4);
    Check(data, image, 12, "TPL");
    const int height = Be16(data.data() + image), width = Be16(data.data() + image + 2);
    const auto format = GxFormat(Be32(data.data() + image + 4));
    const uint32_t pixels = Be32(data.data() + image + 8);
    const uint8_t* pal = nullptr;
    PaletteFormat palFormat = PaletteFormat::IA8;
    int palCount = 0;
    if (palette) {
        Check(data, palette, 12, "TPL palette");
        palCount = Be16(data.data() + palette);
        palFormat = PaletteFormat(Be32(data.data() + palette + 4));
        const uint32_t at = Be32(data.data() + palette + 8);
        Check(data, at, size_t(palCount) * 2, "TPL palette");
        pal = data.data() + at;
    }
    Check(data, pixels, 0, "TPL");
    return DecodeGx(format, data.data() + pixels, data.size() - pixels, width, height, pal, palFormat, palCount);
}

FontAtlas BuildFontAtlas(const Bytes& data, int atlasWidth, int atlasHeight) {
    Check(data, 0, 16, "BRFNT");
    if (std::memcmp(data.data(), "RFNT", 4) != 0 || data[4] != 0xFE || data[5] != 0xFF) Fail("Expected a big-endian BRFNT");
    FontAtlas font;
    std::vector<Image> sheets;
    std::map<int, std::array<int, 3>> widths;
    std::map<uint32_t, int> mapping;
    int nx = 0, perSheet = 0, cw = 0, ch = 0;
    size_t at = Be16(data.data() + 12);
    while (at < data.size()) {
        Check(data, at, 8, "BRFNT section");
        const uint32_t size = Be32(data.data() + at + 4);
        if (size < 8 || at + size > data.size()) Fail("Invalid BRFNT section");
        const std::string tag(reinterpret_cast<const char*>(data.data() + at), 4);
        const size_t s = at + 8;
        if (tag == "FINF") {
            font.lineHeight = int8_t(data[s + 1]);
            font.ascent = data[s + 22];
        } else if (tag == "TGLP") {
            cw = data[s]; ch = data[s + 1]; font.baseline = data[s + 2];
            const uint32_t sheetSize = Be32(data.data() + s + 4);
            const int count = Be16(data.data() + s + 8), fmt = Be16(data.data() + s + 10);
            nx = Be16(data.data() + s + 12);
            const int ny = Be16(data.data() + s + 14), w = Be16(data.data() + s + 16), h = Be16(data.data() + s + 18);
            const uint32_t pixels = Be32(data.data() + s + 20);
            font.cellWidth = cw; font.cellHeight = ch;
            perSheet = nx * ny;
            for (int i = 0; i < count; ++i) {
                Check(data, pixels + size_t(sheetSize) * i, sheetSize, "BRFNT sheet");
                sheets.push_back(DecodeGx(GxFormat(fmt), data.data() + pixels + size_t(sheetSize) * i, sheetSize, w, h));
            }
        } else if (tag == "CWDH") {
            const int first = Be16(data.data() + s), last = Be16(data.data() + s + 2);
            Check(data, s + 8, size_t(last - first + 1) * 3, "BRFNT widths");
            for (int i = first; i <= last; ++i) {
                const uint8_t* w = data.data() + s + 8 + size_t(i - first) * 3;
                widths[i] = {int8_t(w[0]), w[1], int8_t(w[2])};
            }
        } else if (tag == "CMAP") {
            const int first = Be16(data.data() + s), last = Be16(data.data() + s + 2), method = Be16(data.data() + s + 4);
            if (method == 0) {
                const int initial = Be16(data.data() + s + 12);
                for (int code = first; code <= last; ++code) mapping[uint32_t(code)] = initial + code - first;
            } else if (method == 1) {
                for (int code = first; code <= last; ++code) mapping[uint32_t(code)] = Be16(data.data() + s + 12 + size_t(code - first) * 2);
            } else if (method == 2) {
                const int count = Be16(data.data() + s + 12);
                for (int i = 0; i < count; ++i)
                    mapping[Be16(data.data() + s + 14 + size_t(i) * 4)] = Be16(data.data() + s + 16 + size_t(i) * 4);
            } else {
                Fail("Unsupported BRFNT character mapping");
            }
        }
        at += size;
    }
    if (!perSheet || !nx) Fail("BRFNT without glyphs");
    Image atlas(atlasWidth, atlasHeight);
    int x = 1, y = 1;
    for (const auto& [code, index] : mapping) {
        if (index == 65535 || !((code >= 32 && code <= 126) || (code >= 160 && code <= 255))) continue;
        const int sheet = index / perSheet, cell = index % perSheet, row = cell / nx, col = cell % nx;
        const auto w = widths.at(index);
        const int left = w[0], width = w[1], advance = w[2];
        if (x + std::max(width, 1) + 1 > atlas.width) { x = 1; y += ch + 2; }
        if (y + ch + 1 > atlas.height) Fail("Font atlas overflow");
        const Image glyph = sheets.at(size_t(sheet)).Crop(col * (cw + 1), row * (ch + 1),
                                                          col * (cw + 1) + std::max(width, 1), row * (ch + 1) + ch);
        atlas.Paste(glyph, x, y);
        font.glyphs[code] = {x, y, glyph.width, glyph.height, left, advance, index, Sha256Hex(glyph.rgba)};
        x += glyph.width + 2;
    }
    font.atlas = atlas.Crop(0, 0, atlasWidth, y + ch + 1);
    return font;
}

Bytes EncodePng(const Image& image) {
    auto crcAppend = [](Bytes& out, const char* type, const Bytes& body) {
        const uint32_t len = uint32_t(body.size());
        const uint8_t l[4] = {uint8_t(len >> 24), uint8_t(len >> 16), uint8_t(len >> 8), uint8_t(len)};
        out.insert(out.end(), l, l + 4);
        const size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), body.begin(), body.end());
        const uint32_t crc = uint32_t(crc32(0, out.data() + start, uInt(out.size() - start)));
        const uint8_t c[4] = {uint8_t(crc >> 24), uint8_t(crc >> 16), uint8_t(crc >> 8), uint8_t(crc)};
        out.insert(out.end(), c, c + 4);
    };
    Bytes out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    Bytes ihdr = {uint8_t(image.width >> 24), uint8_t(image.width >> 16), uint8_t(image.width >> 8), uint8_t(image.width),
                  uint8_t(image.height >> 24), uint8_t(image.height >> 16), uint8_t(image.height >> 8), uint8_t(image.height),
                  8, 6, 0, 0, 0};
    crcAppend(out, "IHDR", ihdr);
    Bytes raw;
    const size_t stride = size_t(image.width) * 4;
    raw.reserve((stride + 1) * size_t(image.height));
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(1); // Sub filter: small and fast for UI art
        const uint8_t* row = image.rgba.data() + stride * size_t(y);
        for (size_t i = 0; i < stride; ++i) raw.push_back(uint8_t(row[i] - (i >= 4 ? row[i - 4] : 0)));
    }
    uLongf packedSize = compressBound(uLong(raw.size()));
    Bytes packed(packedSize);
    if (compress2(packed.data(), &packedSize, raw.data(), uLong(raw.size()), 6) != Z_OK) Fail("PNG compression failed");
    packed.resize(packedSize);
    crcAppend(out, "IDAT", packed);
    crcAppend(out, "IEND", {});
    return out;
}

} // namespace wiids::art
