#include "netplay/game_layout.h"

#include "display_settings.h"
#include "runtime_config.h"
#include "runtime_log.h"

#include <aurora/imgui.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace GameLayout {

class Font {
public:
    struct Glyph {
        int8_t left = 0;
        uint8_t glyphWidth = 0;
        int8_t charWidth = 0;
        uint16_t sheet = 0;
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    };
    bool ok = false;
    int8_t linefeed = 0;
    uint8_t height = 0;
    uint8_t width = 0;
    uint8_t ascent = 0;
    uint8_t cellWidth = 0;
    uint8_t cellHeight = 0;
    uint16_t defaultIndex = 0;
    std::vector<uint64_t> sheets;  // ImTextureID per sheet
    std::vector<Glyph> glyphs;     // by glyph index
    std::unordered_map<uint16_t, uint16_t> map;  // code -> glyph index

    const Glyph* Find(char16_t code) const {
        const auto it = map.find(static_cast<uint16_t>(code));
        const uint16_t index = it != map.end() ? it->second : defaultIndex;
        return index < glyphs.size() ? &glyphs[index] : nullptr;
    }
};

namespace {

// ---------------------------------------------------------------- files
uint16_t BE16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t BE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}
float BEF(const uint8_t* p) {
    const uint32_t v = BE32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
int16_t BES16(const uint8_t* p) { return static_cast<int16_t>(BE16(p)); }

// Straight from the extracted game's files/ folder (the disc root): this also works before the
// game has set up its own disc access, which the boot screen needs.
bool ReadDiscFile(const std::string& dvdPath, std::vector<uint8_t>& out) {
    const std::filesystem::path root = RuntimeConfigFile::ResolvedDvdRoot();
    if (root.empty()) {
        return false;
    }
    std::string relative = dvdPath;
    while (!relative.empty() && relative.front() == '/') {
        relative.erase(relative.begin());
    }
    std::ifstream in(root / "files" / std::filesystem::u8path(relative), std::ios::binary);
    if (!in) {
        RT_LOGF(RT_TAG_RUNTIME, "netplay ui: %s is not in the game folder\n", dvdPath.c_str());
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

// U8 archive: name -> (offset, size) into the archive bytes.
std::map<std::string, std::pair<uint32_t, uint32_t>> ListU8(const std::vector<uint8_t>& data) {
    std::map<std::string, std::pair<uint32_t, uint32_t>> files;
    if (data.size() < 0x20 || BE32(data.data()) != 0x55AA382Du) {
        return files;
    }
    const uint32_t rootOffset = BE32(&data[4]);
    if (rootOffset + 12 > data.size()) {
        return files;
    }
    const uint32_t total = BE32(&data[rootOffset + 8]);
    const uint32_t stringTable = rootOffset + total * 12;
    if (stringTable > data.size()) {
        return files;
    }
    const auto name = [&](uint32_t offset) {
        std::string s;
        for (uint32_t i = stringTable + offset; i < data.size() && data[i] != 0; ++i) {
            s.push_back(static_cast<char>(data[i]));
        }
        return s;
    };
    std::vector<std::pair<std::string, uint32_t>> dirs{{"", total}};
    for (uint32_t i = 1; i < total; ++i) {
        while (dirs.size() > 1 && i >= dirs.back().second) {
            dirs.pop_back();
        }
        const uint8_t* node = &data[rootOffset + i * 12];
        const uint32_t typeName = BE32(node);
        const std::string nodeName = name(typeName & 0xFFFFFFu);
        std::string path;
        for (const auto& d : dirs) {
            if (!d.first.empty()) {
                path += d.first + "/";
            }
        }
        path += nodeName;
        if ((typeName >> 24) == 1) {
            dirs.emplace_back(nodeName, BE32(node + 8));
        } else {
            files[path] = {BE32(node + 4), BE32(node + 8)};
        }
    }
    return files;
}

// ---------------------------------------------------------------- GX texture decoding
uint8_t Exp3(uint32_t v) { return static_cast<uint8_t>((v << 5) | (v << 2) | (v >> 1)); }
uint8_t Exp4(uint32_t v) { return static_cast<uint8_t>((v << 4) | v); }
uint8_t Exp5(uint32_t v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }
uint8_t Exp6(uint32_t v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }

void Rgb565(uint16_t c, uint8_t* o) {
    o[0] = Exp5(c >> 11);
    o[1] = Exp6((c >> 5) & 63);
    o[2] = Exp5(c & 31);
    o[3] = 255;
}
void Rgb5a3(uint16_t c, uint8_t* o) {
    if (c & 0x8000) {
        o[0] = Exp5((c >> 10) & 31);
        o[1] = Exp5((c >> 5) & 31);
        o[2] = Exp5(c & 31);
        o[3] = 255;
    } else {
        o[3] = Exp3((c >> 12) & 7);
        o[0] = Exp4((c >> 8) & 15);
        o[1] = Exp4((c >> 4) & 15);
        o[2] = Exp4(c & 15);
    }
}
void Ia8(uint16_t c, uint8_t* o) {
    o[0] = o[1] = o[2] = static_cast<uint8_t>(c & 0xFF);
    o[3] = static_cast<uint8_t>(c >> 8);
}

struct FormatInfo {
    uint32_t bw, bh, bits;
};
bool Info(uint32_t format, FormatInfo& info) {
    switch (format) {
    case 0: info = {8, 8, 4}; return true;    // I4
    case 1: info = {8, 4, 8}; return true;    // I8
    case 2: info = {8, 4, 8}; return true;    // IA4
    case 3: info = {4, 4, 16}; return true;   // IA8
    case 4: info = {4, 4, 16}; return true;   // RGB565
    case 5: info = {4, 4, 16}; return true;   // RGB5A3
    case 6: info = {4, 4, 32}; return true;   // RGBA8
    case 8: info = {8, 8, 4}; return true;    // C4
    case 9: info = {8, 4, 8}; return true;    // C8
    case 10: info = {4, 4, 16}; return true;  // C14X2
    case 14: info = {8, 8, 4}; return true;   // CMPR
    default: return false;
    }
}

size_t EncodedSize(uint32_t format, uint32_t width, uint32_t height) {
    FormatInfo info{};
    if (!Info(format, info)) {
        return 0;
    }
    const size_t w = (width + info.bw - 1) / info.bw * info.bw;
    const size_t h = (height + info.bh - 1) / info.bh * info.bh;
    return w * h * info.bits / 8;
}

void PaletteColor(const uint8_t* palette, uint32_t paletteFormat, uint32_t index, uint8_t* o) {
    const uint16_t c = BE16(palette + index * 2);
    if (paletteFormat == 0) Ia8(c, o);
    else if (paletteFormat == 1) Rgb565(c, o);
    else Rgb5a3(c, o);
}

bool DecodeTexture(const uint8_t* src, size_t size, uint32_t format, uint32_t width, uint32_t height,
                   std::vector<uint8_t>& rgba, const uint8_t* palette = nullptr, uint32_t paletteFormat = 0) {
    FormatInfo info{};
    if (!Info(format, info) || EncodedSize(format, width, height) > size || width == 0 || height == 0) {
        return false;
    }
    if ((format == 8 || format == 9 || format == 10) && palette == nullptr) {
        return false;
    }
    rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    const uint32_t blocksX = (width + info.bw - 1) / info.bw;
    const uint32_t blocksY = (height + info.bh - 1) / info.bh;
    const size_t blockBytes = info.bw * info.bh * info.bits / 8;
    const uint8_t* block = src;
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx, block += blockBytes) {
            if (format == 14) {
                // CMPR: 2x2 DXT1 sub-blocks with big-endian colours.
                for (uint32_t sub = 0; sub < 4; ++sub) {
                    const uint8_t* s = block + sub * 8;
                    const uint16_t c0 = BE16(s), c1 = BE16(s + 2);
                    uint8_t pal[4][4];
                    Rgb565(c0, pal[0]);
                    Rgb565(c1, pal[1]);
                    if (c0 > c1) {
                        for (int k = 0; k < 3; ++k) {
                            pal[2][k] = static_cast<uint8_t>((2 * pal[0][k] + pal[1][k]) / 3);
                            pal[3][k] = static_cast<uint8_t>((pal[0][k] + 2 * pal[1][k]) / 3);
                        }
                        pal[2][3] = pal[3][3] = 255;
                    } else {
                        for (int k = 0; k < 3; ++k) {
                            pal[2][k] = static_cast<uint8_t>((pal[0][k] + pal[1][k]) / 2);
                            pal[3][k] = 0;
                        }
                        pal[2][3] = 255;
                        pal[3][3] = 0;
                    }
                    for (uint32_t y = 0; y < 4; ++y) {
                        const uint8_t row = s[4 + y];
                        for (uint32_t x = 0; x < 4; ++x) {
                            const uint32_t px = bx * 8 + (sub & 1) * 4 + x;
                            const uint32_t py = by * 8 + (sub >> 1) * 4 + y;
                            if (px < width && py < height) {
                                std::memcpy(&rgba[(static_cast<size_t>(py) * width + px) * 4],
                                            pal[(row >> (6 - x * 2)) & 3], 4);
                            }
                        }
                    }
                }
                continue;
            }
            for (uint32_t y = 0; y < info.bh; ++y) {
                for (uint32_t x = 0; x < info.bw; ++x) {
                    const uint32_t px = bx * info.bw + x;
                    const uint32_t py = by * info.bh + y;
                    uint8_t o[4] = {0, 0, 0, 0};
                    const uint32_t i = y * info.bw + x;
                    switch (format) {
                    case 0: {
                        const uint8_t b = block[i / 2];
                        o[0] = o[1] = o[2] = o[3] = Exp4((i & 1) ? (b & 15) : (b >> 4));
                        break;
                    }
                    case 1: o[0] = o[1] = o[2] = o[3] = block[i]; break;
                    case 2:
                        o[0] = o[1] = o[2] = Exp4(block[i] & 15);
                        o[3] = Exp4(block[i] >> 4);
                        break;
                    case 3: Ia8(BE16(block + i * 2), o); break;
                    case 4: Rgb565(BE16(block + i * 2), o); break;
                    case 5: Rgb5a3(BE16(block + i * 2), o); break;
                    case 6:
                        o[3] = block[i * 2];
                        o[0] = block[i * 2 + 1];
                        o[1] = block[32 + i * 2];
                        o[2] = block[32 + i * 2 + 1];
                        break;
                    case 8: {
                        const uint8_t b = block[i / 2];
                        PaletteColor(palette, paletteFormat, (i & 1) ? (b & 15) : (b >> 4), o);
                        break;
                    }
                    case 9: PaletteColor(palette, paletteFormat, block[i], o); break;
                    case 10: PaletteColor(palette, paletteFormat, BE16(block + i * 2) & 0x3FFF, o); break;
                    default: break;
                    }
                    if (px < width && py < height) {
                        std::memcpy(&rgba[(static_cast<size_t>(py) * width + px) * 4], o, 4);
                    }
                }
            }
        }
    }
    return true;
}

struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t wrapS = 0;
    uint8_t wrapT = 0;
    std::vector<uint8_t> rgba;
};

bool ParseTpl(const uint8_t* data, size_t size, Image& image) {
    if (size < 12 || BE32(data) != 0x0020AF30u || BE32(data + 4) == 0) {
        return false;
    }
    const uint32_t table = BE32(data + 8);
    if (table + 8 > size) {
        return false;
    }
    const uint32_t header = BE32(data + table);
    const uint32_t paletteHeader = BE32(data + table + 4);
    if (header + 0x24 > size) {
        return false;
    }
    image.height = BE16(data + header);
    image.width = BE16(data + header + 2);
    const uint32_t format = BE32(data + header + 4);
    const uint32_t offset = BE32(data + header + 8);
    image.wrapS = static_cast<uint8_t>(BE32(data + header + 12));
    image.wrapT = static_cast<uint8_t>(BE32(data + header + 16));
    const uint8_t* palette = nullptr;
    uint32_t paletteFormat = 0;
    if (paletteHeader != 0 && paletteHeader + 12 <= size) {
        paletteFormat = BE32(data + paletteHeader + 4);
        const uint32_t paletteOffset = BE32(data + paletteHeader + 8);
        if (paletteOffset < size) {
            palette = data + paletteOffset;
        }
    }
    if (offset >= size) {
        return false;
    }
    return DecodeTexture(data + offset, size - offset, format, image.width, image.height, image.rgba, palette,
                         paletteFormat);
}

uint64_t Upload(const Image& image) {
    return static_cast<uint64_t>(aurora_imgui_add_texture(image.width, image.height, image.rgba.data()));
}

uint64_t WhiteTexture() {
    static const uint64_t id = [] {
        Image white;
        white.width = white.height = 1;
        white.rgba = {255, 255, 255, 255};
        return Upload(white);
    }();
    return id;
}

// ---------------------------------------------------------------- colours
struct Rgba {
    float r = 1, g = 1, b = 1, a = 1;
};
Rgba FromRgba8(uint32_t c) {
    return {((c >> 24) & 0xFF) / 255.0f, ((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f};
}
ImU32 ToImU32(const Rgba& c) {
    const auto q = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return IM_COL32(q(c.r), q(c.g), q(c.b), q(c.a));
}
Rgba Mul(const Rgba& a, const Rgba& b) { return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a}; }

// ---------------------------------------------------------------- caches
std::mutex g_cacheMutex;
std::unordered_map<std::string, std::shared_ptr<Font>> g_fonts;

} // namespace

// ---------------------------------------------------------------- fonts
std::shared_ptr<Font> LoadFont(const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        if (const auto it = g_fonts.find(name); it != g_fonts.end()) {
            return it->second;
        }
    }
    auto font = std::make_shared<Font>();
    std::vector<uint8_t> data;
    if (ReadDiscFile("/US/EngUS/Font/" + name, data) && data.size() > 0x30 && BE32(data.data()) == 0x52464E54u) {
        const uint16_t headerSize = BE16(&data[12]);
        const uint8_t* finf = &data[headerSize];
        if (BE32(finf) == 0x46494E46u) {  // FINF
            const uint8_t* f = finf + 8;
            font->linefeed = static_cast<int8_t>(f[1]);
            font->defaultIndex = BE16(f + 2);
            const uint32_t tglp = BE32(f + 8);
            uint32_t cwdh = BE32(f + 12);
            uint32_t cmap = BE32(f + 16);
            font->height = f[20];
            font->width = f[21];
            font->ascent = f[22];
            if (tglp + 0x18 <= data.size()) {
                const uint8_t* t = &data[tglp];
                font->cellWidth = t[0];
                font->cellHeight = t[1];
                const uint32_t sheetSize = BE32(t + 4);
                const uint16_t sheetCount = BE16(t + 8);
                const uint16_t sheetFormat = BE16(t + 10);
                const uint16_t perRow = BE16(t + 12);
                const uint16_t perColumn = BE16(t + 14);
                const uint16_t sheetWidth = BE16(t + 16);
                const uint16_t sheetHeight = BE16(t + 18);
                const uint32_t sheetData = BE32(t + 20);
                for (uint16_t i = 0; i < sheetCount; ++i) {
                    const size_t at = sheetData + static_cast<size_t>(i) * sheetSize;
                    Image sheet;
                    sheet.width = sheetWidth;
                    sheet.height = sheetHeight;
                    if (at + sheetSize > data.size() ||
                        !DecodeTexture(&data[at], sheetSize, sheetFormat, sheetWidth, sheetHeight, sheet.rgba)) {
                        break;
                    }
                    font->sheets.push_back(Upload(sheet));
                }
                const uint32_t perSheet = static_cast<uint32_t>(perRow) * perColumn;
                // Widths, then the glyph cells.
                while (cwdh != 0 && cwdh + 8 <= data.size()) {
                    const uint8_t* w = &data[cwdh];
                    const uint16_t first = BE16(w);
                    const uint16_t last = BE16(w + 2);
                    for (uint32_t index = first; index <= last && cwdh + 8 + (index - first) * 3 + 3 <= data.size(); ++index) {
                        if (index >= font->glyphs.size()) {
                            font->glyphs.resize(index + 1);
                        }
                        Font::Glyph& g = font->glyphs[index];
                        const uint8_t* e = w + 8 + (index - first) * 3;
                        g.left = static_cast<int8_t>(e[0]);
                        g.glyphWidth = e[1];
                        g.charWidth = static_cast<int8_t>(e[2]);
                    }
                    const uint32_t next = BE32(w + 4);
                    cwdh = next;
                }
                for (uint32_t index = 0; index < font->glyphs.size() && perSheet != 0; ++index) {
                    Font::Glyph& g = font->glyphs[index];
                    g.sheet = static_cast<uint16_t>(index / perSheet);
                    const uint32_t cell = index % perSheet;
                    const float x = static_cast<float>((cell % perRow) * (font->cellWidth + 1));
                    const float y = static_cast<float>((cell / perRow) * (font->cellHeight + 1));
                    g.u0 = x / sheetWidth;
                    g.v0 = y / sheetHeight;
                    g.u1 = (x + g.glyphWidth) / sheetWidth;
                    g.v1 = (y + font->cellHeight) / sheetHeight;
                }
            }
            while (cmap != 0 && cmap + 12 <= data.size()) {
                const uint8_t* m = &data[cmap];
                const uint16_t begin = BE16(m);
                const uint16_t end = BE16(m + 2);
                const uint16_t method = BE16(m + 4);
                const uint8_t* body = m + 12;
                if (method == 0) {
                    const uint16_t offset = BE16(body);
                    for (uint32_t code = begin; code <= end; ++code) {
                        font->map[static_cast<uint16_t>(code)] = static_cast<uint16_t>(code - begin + offset);
                    }
                } else if (method == 1) {
                    for (uint32_t code = begin; code <= end; ++code) {
                        const uint16_t index = BE16(body + (code - begin) * 2);
                        if (index != 0xFFFF) {
                            font->map[static_cast<uint16_t>(code)] = index;
                        }
                    }
                } else if (method == 2) {
                    const uint16_t count = BE16(body);
                    for (uint16_t i = 0; i < count; ++i) {
                        font->map[BE16(body + 2 + i * 4)] = BE16(body + 4 + i * 4);
                    }
                }
                cmap = BE32(m + 8);
            }
            font->ok = !font->sheets.empty() && !font->glyphs.empty();
        }
    }
    if (!font->ok) {
        RT_LOGF(RT_TAG_RUNTIME, "netplay ui: could not load the game font %s\n", name.c_str());
    }
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_fonts[name] = font;
    return font;
}

namespace {

struct TextLine {
    std::u16string text;
    float width = 0;
};

std::vector<TextLine> SplitLines(const Font& font, const std::u16string& text, float scaleX, float charSpace) {
    std::vector<TextLine> lines(1);
    for (char16_t c : text) {
        if (c == u'\n') {
            lines.emplace_back();
            continue;
        }
        const Font::Glyph* g = font.Find(c);
        if (g == nullptr) {
            continue;
        }
        if (!lines.back().text.empty()) {
            lines.back().width += charSpace;
        }
        lines.back().text.push_back(c);
        lines.back().width += g->charWidth * scaleX;
    }
    return lines;
}

void PushQuad(ImDrawList* list, uint64_t texture, const ImVec2 p[4], const ImVec2 uv[4], const ImU32 col[4]) {
    list->PushTextureID(static_cast<ImTextureID>(texture));
    list->PrimReserve(6, 4);
    const ImDrawIdx base = static_cast<ImDrawIdx>(list->_VtxCurrentIdx);
    list->PrimWriteIdx(base);
    list->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
    list->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
    list->PrimWriteIdx(base);
    list->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
    list->PrimWriteIdx(static_cast<ImDrawIdx>(base + 3));
    for (int i = 0; i < 4; ++i) {
        list->PrimWriteVtx(p[i], uv[i], col[i]);
    }
    list->PopTextureID();
}

// Layout-space affine transform: x' = a*x + b*y + tx, y' = c*x + d*y + ty.
struct Mtx {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
};
Mtx Compose(const Mtx& p, float tx, float ty, float rotDeg, float sx, float sy) {
    const float r = rotDeg * 3.14159265f / 180.0f;
    const float cs = std::cos(r), sn = std::sin(r);
    // local = T * R * S
    Mtx l;
    l.a = cs * sx;
    l.b = -sn * sy;
    l.c = sn * sx;
    l.d = cs * sy;
    l.tx = tx;
    l.ty = ty;
    Mtx o;
    o.a = p.a * l.a + p.b * l.c;
    o.b = p.a * l.b + p.b * l.d;
    o.c = p.c * l.a + p.d * l.c;
    o.d = p.c * l.b + p.d * l.d;
    o.tx = p.a * l.tx + p.b * l.ty + p.tx;
    o.ty = p.c * l.tx + p.d * l.ty + p.ty;
    return o;
}
ImVec2 ToScreen(const View& view, const Mtx& m, float x, float y) {
    const float lx = m.a * x + m.b * y + m.tx;
    const float ly = m.c * x + m.d * y + m.ty;
    return ImVec2(view.centerX + lx * view.scale, view.centerY - ly * view.scale);
}

void DrawTextLines(ImDrawList* list, const View& view, const Mtx& m, const Font& font, const std::u16string& text,
                   float rectX0, float rectY0, float rectW, float rectH, int textPosition, int alignment, float fontW,
                   float fontH, float charSpace, float lineSpace, const Rgba& top, const Rgba& bottom) {
    if (!font.ok || font.width == 0 || font.height == 0) {
        return;
    }
    const float scaleX = fontW / font.width;
    const float scaleY = fontH / font.height;
    const float lineHeight = font.linefeed * scaleY + lineSpace;
    const auto lines = SplitLines(font, text, scaleX, charSpace);
    float blockWidth = 0;
    for (const auto& line : lines) {
        blockWidth = std::max(blockWidth, line.width);
    }
    const float blockHeight = lineHeight * (lines.size() - 1) + font.cellHeight * scaleY;
    const int posH = textPosition % 3;
    const int posV = textPosition / 3;
    const float blockX = rectX0 + (posH == 0 ? 0.0f : posH == 1 ? (rectW - blockWidth) / 2 : rectW - blockWidth);
    float y = rectY0 - (posV == 0 ? 0.0f : posV == 1 ? (rectH - blockHeight) / 2 : rectH - blockHeight);
    const ImU32 topCol = ToImU32(top), bottomCol = ToImU32(bottom);
    for (const auto& line : lines) {
        const int align = alignment == 0 ? posH : alignment - 1;
        float x = blockX + (align == 0 ? 0.0f : align == 1 ? (blockWidth - line.width) / 2 : blockWidth - line.width);
        for (char16_t c : line.text) {
            const Font::Glyph* g = font.Find(c);
            if (g == nullptr || g->sheet >= font.sheets.size()) {
                continue;
            }
            const float gx = x + g->left * scaleX;
            const float gw = g->glyphWidth * scaleX;
            const float gh = font.cellHeight * scaleY;
            const ImVec2 p[4] = {ToScreen(view, m, gx, y), ToScreen(view, m, gx + gw, y), ToScreen(view, m, gx + gw, y - gh),
                                 ToScreen(view, m, gx, y - gh)};
            const ImVec2 uv[4] = {ImVec2(g->u0, g->v0), ImVec2(g->u1, g->v0), ImVec2(g->u1, g->v1), ImVec2(g->u0, g->v1)};
            const ImU32 col[4] = {topCol, topCol, bottomCol, bottomCol};
            PushQuad(list, font.sheets[g->sheet], p, uv, col);
            x += g->charWidth * scaleX + charSpace;
        }
        y -= lineHeight;
    }
}

} // namespace

std::vector<std::u16string> WrapText(const Font& font, const std::u16string& text, float fontSize, float maxWidth) {
    std::vector<std::u16string> lines;
    std::u16string line;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(u' ', pos);
        if (end == std::u16string::npos) {
            end = text.size();
        }
        const std::u16string word = text.substr(pos, end - pos);
        const std::u16string candidate = line.empty() ? word : line + u' ' + word;
        if (!line.empty() && MeasureText(font, candidate, fontSize) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        pos = end + 1;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

float MeasureText(const Font& font, const std::u16string& text, float fontSize) {
    if (!font.ok || font.width == 0) {
        return 0;
    }
    float widest = 0;
    for (const auto& line : SplitLines(font, text, fontSize / font.width, 0)) {
        widest = std::max(widest, line.width);
    }
    return widest;
}

float LineHeight(const Font& font, float fontSize) {
    return font.height == 0 ? 0 : font.linefeed * fontSize / font.height;
}

void DrawText(ImDrawList* list, const View& view, const Font& font, const std::u16string& text, float x, float y,
              float fontSize, uint32_t topColor, uint32_t bottomColor, float alpha, int originH, int originV) {
    Rgba top = FromRgba8(topColor), bottom = FromRgba8(bottomColor);
    top.a *= alpha;
    bottom.a *= alpha;
    const float w = MeasureText(font, text, fontSize);
    const float h = font.height == 0 ? 0 : font.cellHeight * fontSize / font.height;
    const float x0 = x - (originH == 0 ? 0 : originH == 1 ? w / 2 : w);
    const float y0 = y + (originV == 0 ? 0 : originV == 1 ? h / 2 : h);
    DrawTextLines(list, view, Mtx{}, font, text, x0, y0, w, h, 0, 0, fontSize, fontSize, 0, 0, top, bottom);
}

// ---------------------------------------------------------------- layouts
struct Layout::Material {
    std::string name;
    Rgba black{0, 0, 0, 0};
    Rgba white{1, 1, 1, 1};
    bool hasTexture = false;
    uint64_t texture = 0;  // the first texture map, interpolated between black and white
    uint32_t textureWidth = 0;
    uint32_t textureHeight = 0;
    uint8_t wrapS = 0, wrapT = 0;
    float srtTx = 0, srtTy = 0, srtSx = 1, srtSy = 1;
    bool hasMatColor = false;
    Rgba matColor;
    bool colorFromMaterial = false;
};

struct Layout::Pane {
    enum Kind { kNull, kPicture, kText, kWindow, kBounding } kind = kNull;
    std::string name;
    uint8_t flags = 1;
    uint8_t origin = 4;
    uint8_t alpha = 255;
    float tx = 0, ty = 0, rot = 0, sx = 1, sy = 1, w = 0, h = 0;
    std::vector<const Pane*> children;
    // picture / window content
    uint32_t vtx[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    int material = -1;
    float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    // text
    std::u16string text;
    int font = -1;
    uint8_t textPosition = 4;
    uint8_t textAlignment = 0;
    uint32_t topColor = 0xFFFFFFFFu, bottomColor = 0xFFFFFFFFu;
    float fontW = 32, fontH = 32, charSpace = 0, lineSpace = 0;
    // window
    float overflow[4] = {0, 0, 0, 0};  // left, right, top, bottom
    std::vector<std::pair<int, uint8_t>> frames;  // material, flip
};

Layout::Layout() = default;
Layout::~Layout() = default;

bool FontReady(const Font* font) {
    return font != nullptr && font->ok;
}

bool Layout::Load(const std::string& arcPath, const std::string& brlytName) {
    std::vector<uint8_t> arc;
    if (!ReadDiscFile(arcPath, arc)) {
        return false;
    }
    const auto files = ListU8(arc);
    const uint8_t* lyt = nullptr;
    size_t lytSize = 0;
    for (const auto& [path, where] : files) {
        if (path.size() > 6 && path.compare(path.size() - 6, 6, ".brlyt") == 0 &&
            (brlytName.empty() || path.find(brlytName) != std::string::npos) && where.first + where.second <= arc.size()) {
            lyt = &arc[where.first];
            lytSize = where.second;
            break;
        }
    }
    if (lyt == nullptr || lytSize < 16 || BE32(lyt) != 0x524C5954u) {  // RLYT
        return false;
    }
    const auto findFile = [&](const std::string& name) -> std::pair<const uint8_t*, size_t> {
        for (const auto& [path, where] : files) {
            if (path.size() >= name.size() && path.compare(path.size() - name.size(), name.size(), name) == 0 &&
                where.first + where.second <= arc.size()) {
                return {&arc[where.first], where.second};
            }
        }
        return {nullptr, 0};
    };

    std::vector<std::string> textureNames;
    std::vector<std::string> fontNames;
    std::unordered_map<std::string, Image> images;
    const auto image = [&](const std::string& name) -> const Image* {
        if (const auto it = images.find(name); it != images.end()) {
            return it->second.rgba.empty() ? nullptr : &it->second;
        }
        Image& decoded = images[name];
        const auto [data, size] = findFile(name);
        if (data == nullptr || !ParseTpl(data, size, decoded)) {
            decoded.rgba.clear();
            return nullptr;
        }
        return &decoded;
    };

    std::vector<Pane*> stack;
    Pane* last = nullptr;
    const uint16_t headerSize = BE16(lyt + 12);
    const uint16_t sections = BE16(lyt + 14);
    size_t pos = headerSize;
    for (uint16_t s = 0; s < sections && pos + 8 <= lytSize; ++s) {
        const uint8_t* sec = lyt + pos;
        const uint32_t kind = BE32(sec);
        const uint32_t secSize = BE32(sec + 4);
        if (secSize < 8 || pos + secSize > lytSize) {
            break;
        }
        const uint8_t* body = sec + 8;
        switch (kind) {
        case 0x74786C31u:    // txl1
        case 0x666E6C31u: {  // fnl1
            const uint16_t count = BE16(body);
            for (uint16_t i = 0; i < count; ++i) {
                const uint32_t offset = BE32(body + 4 + i * 8);
                std::string name(reinterpret_cast<const char*>(body + 4 + offset));
                (kind == 0x74786C31u ? textureNames : fontNames).push_back(name);
            }
            if (kind == 0x666E6C31u) {
                for (const auto& name : fontNames) {
                    m_fonts.push_back(LoadFont(name));
                }
            }
            break;
        }
        case 0x6D617431u: {  // mat1
            const uint16_t count = BE16(body);
            for (uint16_t i = 0; i < count; ++i) {
                const uint8_t* m = sec + BE32(body + 4 + i * 4);
                auto mat = std::make_unique<Material>();
                mat->name.assign(reinterpret_cast<const char*>(m), strnlen(reinterpret_cast<const char*>(m), 20));
                const auto s10 = [&](int color) {
                    const uint8_t* c = m + 20 + color * 8;
                    return Rgba{BES16(c) / 255.0f, BES16(c + 2) / 255.0f, BES16(c + 4) / 255.0f, BES16(c + 6) / 255.0f};
                };
                mat->black = s10(0);
                mat->white = s10(1);
                const uint32_t flags = BE32(m + 60);
                const uint8_t* p = m + 64;
                const uint32_t texCount = flags & 15;
                const uint32_t srtCount = (flags >> 4) & 15;
                const uint32_t coordCount = (flags >> 8) & 15;
                int texIndex = -1;
                for (uint32_t t = 0; t < texCount; ++t, p += 4) {
                    if (t == 0) {
                        texIndex = BE16(p);
                        mat->wrapS = p[2];
                        mat->wrapT = p[3];
                    }
                }
                for (uint32_t t = 0; t < srtCount; ++t, p += 20) {
                    if (t == 0) {
                        mat->srtTx = BEF(p);
                        mat->srtTy = BEF(p + 4);
                        mat->srtSx = BEF(p + 12);
                        mat->srtSy = BEF(p + 16);
                    }
                }
                p += coordCount * 4;
                if (flags & (1u << 25)) {  // channel control: colour / alpha source
                    mat->colorFromMaterial = p[0] == 0;  // GX_SRC_REG
                    p += 4;
                }
                if (flags & (1u << 27)) {
                    mat->hasMatColor = true;
                    mat->matColor = FromRgba8(BE32(p));
                    p += 4;
                }
                if (texIndex >= 0 && texIndex < static_cast<int>(textureNames.size())) {
                    if (const Image* src = image(textureNames[texIndex])) {
                        // Bake the material's black-to-white interpolation into a texture of its own.
                        Image baked = *src;
                        const bool identity = mat->black.r == 0 && mat->black.g == 0 && mat->black.b == 0 &&
                                              mat->black.a == 0 && mat->white.r >= 1 && mat->white.g >= 1 &&
                                              mat->white.b >= 1 && mat->white.a >= 1;
                        if (!identity) {
                            for (size_t px = 0; px < baked.rgba.size(); px += 4) {
                                const float c[4] = {baked.rgba[px] / 255.0f, baked.rgba[px + 1] / 255.0f,
                                                    baked.rgba[px + 2] / 255.0f, baked.rgba[px + 3] / 255.0f};
                                const float bk[4] = {mat->black.r, mat->black.g, mat->black.b, mat->black.a};
                                const float wh[4] = {mat->white.r, mat->white.g, mat->white.b, mat->white.a};
                                for (int k = 0; k < 4; ++k) {
                                    const float v = bk[k] + (wh[k] - bk[k]) * c[k];
                                    baked.rgba[px + k] = static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
                                }
                            }
                        }
                        mat->texture = Upload(baked);
                        mat->textureWidth = baked.width;
                        mat->textureHeight = baked.height;
                        mat->hasTexture = true;
                    }
                }
                m_materials.push_back(std::move(mat));
            }
            break;
        }
        case 0x70616E31u:    // pan1
        case 0x70696331u:    // pic1
        case 0x74787431u:    // txt1
        case 0x776E6431u:    // wnd1
        case 0x626E6431u: {  // bnd1
            auto pane = std::make_unique<Pane>();
            pane->kind = kind == 0x70696331u   ? Pane::kPicture
                         : kind == 0x74787431u ? Pane::kText
                         : kind == 0x776E6431u ? Pane::kWindow
                         : kind == 0x626E6431u ? Pane::kBounding
                                               : Pane::kNull;
            pane->flags = body[0];
            pane->origin = body[1];
            pane->alpha = body[2];
            pane->name.assign(reinterpret_cast<const char*>(body + 4), strnlen(reinterpret_cast<const char*>(body + 4), 16));
            pane->tx = BEF(body + 28);
            pane->ty = BEF(body + 32);
            pane->rot = BEF(body + 48);
            pane->sx = BEF(body + 52);
            pane->sy = BEF(body + 56);
            pane->w = BEF(body + 60);
            pane->h = BEF(body + 64);
            const uint8_t* extra = body + 68;
            const auto readContent = [&](const uint8_t* c) {
                for (int i = 0; i < 4; ++i) {
                    pane->vtx[i] = BE32(c + i * 4);
                }
                pane->material = BE16(c + 16);
                const uint8_t coords = c[18];
                if (coords > 0) {
                    for (int i = 0; i < 4; ++i) {
                        pane->uv[i][0] = BEF(c + 20 + i * 8);
                        pane->uv[i][1] = BEF(c + 24 + i * 8);
                    }
                }
            };
            if (pane->kind == Pane::kPicture) {
                readContent(extra);
            } else if (pane->kind == Pane::kText) {
                const uint16_t stringSize = BE16(extra + 2);
                pane->material = BE16(extra + 4);
                pane->font = BE16(extra + 6);
                pane->textPosition = extra[8];
                pane->textAlignment = extra[9];
                const uint32_t stringOffset = BE32(extra + 12);
                pane->topColor = BE32(extra + 16);
                pane->bottomColor = BE32(extra + 20);
                pane->fontW = BEF(extra + 24);
                pane->fontH = BEF(extra + 28);
                pane->charSpace = BEF(extra + 32);
                pane->lineSpace = BEF(extra + 36);
                for (uint32_t i = 0; i + 1 < stringSize && stringOffset + i + 1 < secSize; i += 2) {
                    const char16_t c = static_cast<char16_t>(BE16(sec + stringOffset + i));
                    if (c == 0) {
                        break;
                    }
                    pane->text.push_back(c);
                }
            } else if (pane->kind == Pane::kWindow) {
                for (int i = 0; i < 4; ++i) {
                    pane->overflow[i] = BEF(extra + i * 4);
                }
                const uint8_t frameCount = extra[16];
                const uint32_t contentOffset = BE32(extra + 20);
                const uint32_t frameTable = BE32(extra + 24);
                if (contentOffset < secSize) {
                    readContent(sec + contentOffset);
                }
                for (uint8_t f = 0; f < frameCount && frameTable + f * 4 + 4 <= secSize; ++f) {
                    const uint8_t* fr = sec + BE32(sec + frameTable + f * 4);
                    pane->frames.emplace_back(BE16(fr), fr[2]);
                }
            }
            last = pane.get();
            if (!stack.empty()) {
                stack.back()->children.push_back(pane.get());
            }
            if (!pane->name.empty()) {
                m_byName.emplace(pane->name, pane.get());
            }
            m_panes.push_back(std::move(pane));
            break;
        }
        case 0x70617331u:  // pas1
            if (last != nullptr) {
                stack.push_back(last);
            }
            break;
        case 0x70616531u:  // pae1
            if (!stack.empty()) {
                stack.pop_back();
            }
            break;
        default:
            break;
        }
        pos += secSize;
    }
    m_loaded = !m_panes.empty();
    if (!m_loaded) {
        RT_LOGF(RT_TAG_RUNTIME, "netplay ui: no panes in %s\n", arcPath.c_str());
    }
    return m_loaded;
}

bool Layout::PaneRect(const std::string& paneName, float& x, float& y, float& w, float& h) const {
    const auto it = m_byName.find(paneName);
    if (it == m_byName.end()) {
        return false;
    }
    x = it->second->tx;
    y = it->second->ty;
    w = it->second->w * it->second->sx;
    h = it->second->h * it->second->sy;
    return true;
}

namespace {
// Translation and scale (rotation ignored) accumulated from the root down to `target`.
bool WorldOf(const Layout::Pane* pane, const Layout::Pane* target, float px, float py, float psx, float psy, float& x,
             float& y, float& sx, float& sy) {
    const float wx = px + pane->tx * psx, wy = py + pane->ty * psy;
    const float wsx = psx * pane->sx, wsy = psy * pane->sy;
    if (pane == target) {
        x = wx;
        y = wy;
        sx = wsx;
        sy = wsy;
        return true;
    }
    for (const Layout::Pane* child : pane->children) {
        if (WorldOf(child, target, wx, wy, wsx, wsy, x, y, sx, sy)) {
            return true;
        }
    }
    return false;
}
} // namespace

bool Layout::PaneWorldRect(const std::string& paneName, float& x, float& y, float& w, float& h) const {
    const auto it = m_byName.find(paneName);
    if (!m_loaded || it == m_byName.end()) {
        return false;
    }
    float sx = 1, sy = 1;
    if (!WorldOf(m_panes[0].get(), it->second, 0, 0, 1, 1, x, y, sx, sy)) {
        return false;
    }
    w = it->second->w * sx;
    h = it->second->h * sy;
    return true;
}

std::vector<std::string> Layout::PaneNames() const {
    std::vector<std::string> names;
    for (const auto& pane : m_panes) {
        names.push_back(pane->name);
    }
    return names;
}

void Layout::Draw(ImDrawList* list, const View& view, const std::string& paneName, const DrawParams& params) const {
    if (!m_loaded || list == nullptr) {
        return;
    }
    const auto it = m_byName.find(paneName);
    if (it == m_byName.end()) {
        return;
    }
    const float identity[6] = {1, 0, 0, 1, 0, 0};
    DrawPane(list, view, *it->second, identity, params.alpha, true, params, true);
}

void Layout::DrawPane(ImDrawList* list, const View& view, const Pane& pane, const float* parentMtx, float parentAlpha,
                      bool parentInfluences, const DrawParams& params, bool root) const {
    bool visible = (pane.flags & 1) != 0;
    if (const auto v = params.visible.find(pane.name); v != params.visible.end()) {
        visible = v->second;
    }
    if (!visible) {
        return;
    }
    float ownAlpha = pane.alpha / 255.0f;
    if (const auto a = params.paneAlpha.find(pane.name); a != params.paneAlpha.end()) {
        ownAlpha = a->second;
    }
    const float alpha = ownAlpha * (parentInfluences ? parentAlpha : 1.0f);
    float sx = pane.sx, sy = pane.sy;
    if (const auto s = params.paneScale.find(pane.name); s != params.paneScale.end()) {
        sx = sy = s->second;
    }
    float tx = pane.tx, ty = pane.ty;
    if (root) {
        tx += params.offsetX;
        ty += params.offsetY;
        sx *= params.scale;
        sy *= params.scale;
    }
    const Mtx parent{parentMtx[0], parentMtx[1], parentMtx[2], parentMtx[3], parentMtx[4], parentMtx[5]};
    const Mtx m = Compose(parent, tx, ty, pane.rot, sx, sy);
    float paneW = pane.w, paneH = pane.h;
    if (const auto z = params.paneSize.find(pane.name); z != params.paneSize.end()) {
        paneW = z->second.first;
        paneH = z->second.second;
    }
    const int posH = pane.origin % 3;
    const int posV = pane.origin / 3;
    const float x0 = posH == 0 ? 0.0f : posH == 1 ? -paneW / 2 : -paneW;
    const float y0 = posV == 0 ? 0.0f : posV == 1 ? paneH / 2 : paneH;

    uint32_t tint = 0xFFFFFFFFu;
    if (const auto t = params.vertexTint.find(pane.name); t != params.vertexTint.end()) {
        tint = t->second;
    }
    const Rgba tintColor = FromRgba8(tint);

    const auto quad = [&](const Material* mat, const uint32_t vtx[4], float ax0, float ay0, float ax1, float ay1,
                          const float uvIn[4][2], uint8_t flip) {
        // uvIn: TL, TR, BL, BR (nw4r order)
        float uv[4][2];
        std::memcpy(uv, uvIn, sizeof(uv));
        if (mat != nullptr && (mat->srtSx != 1 || mat->srtSy != 1 || mat->srtTx != 0 || mat->srtTy != 0)) {
            for (auto& c : uv) {
                c[0] = (c[0] - 0.5f) * mat->srtSx + 0.5f + mat->srtTx;
                c[1] = (c[1] - 0.5f) * mat->srtSy + 0.5f - mat->srtTy;
            }
        }
        if (flip & 1) {  // horizontal
            std::swap(uv[0], uv[1]);
            std::swap(uv[2], uv[3]);
        }
        if (flip & 2) {  // vertical
            std::swap(uv[0], uv[2]);
            std::swap(uv[1], uv[3]);
        }
        Rgba colors[4];
        for (int i = 0; i < 4; ++i) {
            Rgba c = FromRgba8(vtx[i]);
            if (mat != nullptr && mat->hasMatColor && mat->colorFromMaterial) {
                c = mat->matColor;
            }
            c = Mul(c, tintColor);
            c.a *= alpha;
            colors[i] = c;
        }
        const uint64_t texture = mat != nullptr && mat->hasTexture ? mat->texture : WhiteTexture();
        if (!(mat != nullptr && mat->hasTexture)) {
            if (mat != nullptr) {
                // No texture: the material's white colour (the TEV constant) shades the quad.
                for (auto& c : colors) {
                    c = Mul(c, mat->white);
                }
            }
        }
        // Tile repeating textures: the ImGui sampler clamps, so split the quad at whole texture
        // repeats. Panes here are axis-aligned in texture space (no texture rotation).
        const float u0 = uv[0][0], u1 = uv[1][0], v0 = uv[0][1], v1 = uv[2][1];
        const float uMin = std::min(u0, u1), uMax = std::max(u0, u1);
        const float vMin = std::min(v0, v1), vMax = std::max(v0, v1);
        const bool tileU = mat != nullptr && mat->hasTexture && mat->wrapS != 0 && (uMin < -0.001f || uMax > 1.001f);
        const bool tileV = mat != nullptr && mat->hasTexture && mat->wrapT != 0 && (vMin < -0.001f || vMax > 1.001f);
        std::vector<float> us{0.0f, 1.0f}, vs{0.0f, 1.0f};  // fractions of the quad
        if (tileU && u1 != u0) {
            us.clear();
            us.push_back(0.0f);
            for (float k = std::floor(uMin) + 1; k < uMax; k += 1.0f) {
                us.push_back((k - u0) / (u1 - u0));
            }
            us.push_back(1.0f);
            std::sort(us.begin(), us.end());
        }
        if (tileV && v1 != v0) {
            vs.clear();
            vs.push_back(0.0f);
            for (float k = std::floor(vMin) + 1; k < vMax; k += 1.0f) {
                vs.push_back((k - v0) / (v1 - v0));
            }
            vs.push_back(1.0f);
            std::sort(vs.begin(), vs.end());
        }
        const auto lerpC = [](const Rgba& a, const Rgba& b, float t) {
            return Rgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
        };
        const auto colorAt = [&](float fx, float fy) {
            return lerpC(lerpC(colors[0], colors[1], fx), lerpC(colors[2], colors[3], fx), fy);
        };
        const auto wrap = [](float t, float tileStart, uint8_t mode) {
            float f = t - std::floor(tileStart);
            if (mode == 2 && (static_cast<int>(std::floor(tileStart)) & 1)) {  // mirror
                f = 1.0f - f;
            }
            return f;
        };
        for (size_t yi = 0; yi + 1 < vs.size(); ++yi) {
            for (size_t xi = 0; xi + 1 < us.size(); ++xi) {
                const float fx0 = us[xi], fx1 = us[xi + 1], fy0 = vs[yi], fy1 = vs[yi + 1];
                const float px0 = ax0 + (ax1 - ax0) * fx0, px1 = ax0 + (ax1 - ax0) * fx1;
                const float py0 = ay0 + (ay1 - ay0) * fy0, py1 = ay0 + (ay1 - ay0) * fy1;
                float tu0 = u0 + (u1 - u0) * fx0, tu1 = u0 + (u1 - u0) * fx1;
                float tv0 = v0 + (v1 - v0) * fy0, tv1 = v0 + (v1 - v0) * fy1;
                if (tileU) {
                    const float start = std::min(tu0, tu1) + 0.0001f;
                    tu0 = wrap(tu0, start, mat->wrapS);
                    tu1 = wrap(tu1, start, mat->wrapS);
                }
                if (tileV) {
                    const float start = std::min(tv0, tv1) + 0.0001f;
                    tv0 = wrap(tv0, start, mat->wrapT);
                    tv1 = wrap(tv1, start, mat->wrapT);
                }
                const ImVec2 p[4] = {ToScreen(view, m, px0, py0), ToScreen(view, m, px1, py0), ToScreen(view, m, px1, py1),
                                     ToScreen(view, m, px0, py1)};
                const ImVec2 t[4] = {ImVec2(tu0, tv0), ImVec2(tu1, tv0), ImVec2(tu1, tv1), ImVec2(tu0, tv1)};
                const ImU32 c[4] = {ToImU32(colorAt(fx0, fy0)), ToImU32(colorAt(fx1, fy0)), ToImU32(colorAt(fx1, fy1)),
                                    ToImU32(colorAt(fx0, fy1))};
                PushQuad(list, texture, p, t, c);
            }
        }
    };
    const auto material = [&](int index) -> const Material* {
        return index >= 0 && index < static_cast<int>(m_materials.size()) ? m_materials[index].get() : nullptr;
    };

    if (pane.kind == Pane::kPicture && alpha > 0.001f) {
        quad(material(pane.material), pane.vtx, x0, y0, x0 + paneW, y0 - paneH, pane.uv, 0);
    } else if (pane.kind == Pane::kWindow && alpha > 0.001f) {
        // Content, grown by its overflow, then nw4r's frame: four strips laid round the border like a
        // pinwheel (top from the left corner, right from the top, bottom from the right, left from the
        // bottom), each showing its frame texture anchored at its own corner at a texel per unit and
        // clamped past the texture's edge, which carries the border on along the side.
        quad(material(pane.material), pane.vtx, x0 - pane.overflow[0], y0 + pane.overflow[2],
             x0 + paneW + pane.overflow[1], y0 - paneH - pane.overflow[3], pane.uv, 0);
        if (!pane.frames.empty()) {
            const uint32_t white[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
            const bool single = pane.frames.size() == 1;
            const Material* mats[4];  // LT, RT, LB, RB, the order frames are stored in
            uint8_t flips[4];         // bit 0 mirrors u, bit 1 mirrors v
            for (int i = 0; i < 4; ++i) {
                const auto& frame = pane.frames[single ? 0 : std::min<size_t>(i, pane.frames.size() - 1)];
                mats[i] = material(frame.first);
                if (single) {
                    flips[i] = static_cast<uint8_t>(i == 0 ? 0 : i == 1 ? 1 : i == 2 ? 2 : 3);
                } else {
                    // BRLYT: 0 none, 1 flip H, 2 flip V, 4 rotate 180 (90/270 are not used by menus).
                    flips[i] = static_cast<uint8_t>(frame.second == 1 ? 1 : frame.second == 2 ? 2 : frame.second == 4 ? 3 : 0);
                }
            }
            const auto texW = [&](int i) { return mats[i] != nullptr && mats[i]->hasTexture ? static_cast<float>(mats[i]->textureWidth) : 16.0f; };
            const auto texH = [&](int i) { return mats[i] != nullptr && mats[i]->hasTexture ? static_cast<float>(mats[i]->textureHeight) : 16.0f; };
            const float L = x0, R = x0 + paneW, T = y0, B = y0 - paneH;
            const float W = paneW, H = paneH;
            const float t = std::min(texH(0), H), r = std::min(texW(1), W), l = std::min(texW(2), W), b = std::min(texH(3), H);
            const auto draw = [&](int i, float qx0, float qy0, float qx1, float qy1, float u0, float u1, float v0, float v1) {
                if ((flips[i] & 1) != 0) {
                    u0 = 1.0f - u0;
                    u1 = 1.0f - u1;
                }
                if ((flips[i] & 2) != 0) {
                    v0 = 1.0f - v0;
                    v1 = 1.0f - v1;
                }
                const float uv[4][2] = {{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}};
                quad(mats[i], white, qx0, qy0, qx1, qy1, uv, 0);
            };
            // Top strip, anchored top-left; right strip, top-right; bottom strip, bottom-right;
            // left strip, bottom-left.
            draw(0, L, T, R - r, T - t, 0.0f, (W - r) / texW(0), 0.0f, t / texH(0));
            draw(1, R - r, T, R, B + b, 1.0f - r / texW(1), 1.0f, 0.0f, (H - b) / texH(1));
            draw(3, L + l, B + b, R, B, 1.0f - (W - l) / texW(3), 1.0f, 1.0f - b / texH(3), 1.0f);
            draw(2, L, T - t, L + l, B, 0.0f, l / texW(2), 1.0f - (H - t) / texH(2), 1.0f);
        }
    } else if (pane.kind == Pane::kText && alpha > 0.001f && pane.font >= 0 &&
               pane.font < static_cast<int>(m_fonts.size()) && m_fonts[pane.font]) {
        const std::u16string* text = &pane.text;
        if (const auto t = params.text.find(pane.name); t != params.text.end()) {
            text = &t->second;
        }
        Rgba top = Mul(FromRgba8(pane.topColor), tintColor), bottom = Mul(FromRgba8(pane.bottomColor), tintColor);
        if (const Material* mat = material(pane.material)) {
            top = Mul(top, mat->white);
            bottom = Mul(bottom, mat->white);
            top.a = FromRgba8(pane.topColor).a * tintColor.a;
            bottom.a = FromRgba8(pane.bottomColor).a * tintColor.a;
        }
        top.a *= alpha;
        bottom.a *= alpha;
        DrawTextLines(list, view, m, *m_fonts[pane.font], *text, x0, y0, paneW, paneH, pane.textPosition,
                      pane.textAlignment, pane.fontW, pane.fontH, pane.charSpace, pane.lineSpace, top, bottom);
    }

    const float mtx[6] = {m.a, m.b, m.c, m.d, m.tx, m.ty};
    const bool influences = (pane.flags & 2) != 0;
    for (const Pane* child : pane.children) {
        DrawPane(list, view, *child, mtx, alpha, influences, params, false);
    }
}

View GameView() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    View view;
    float w = display.x, h = display.y;
    const auto settings = DisplaySettings::Current();
    const bool wide = DisplaySettings::GameWidescreen();
    float aspect = 0.0f;  // 0 = fills the window
    if (!wide || settings.aspect == DisplaySettings::kAspect4x3) {
        aspect = 4.0f / 3.0f;
    } else if (settings.aspect == DisplaySettings::kAspect16x9 || w / std::max(h, 1.0f) <= 16.0f / 9.0f) {
        aspect = 16.0f / 9.0f;
    }
    if (aspect > 0.0f) {
        if (w / std::max(h, 1.0f) > aspect) {
            w = h * aspect;
        } else {
            h = w / aspect;
        }
    }
    view.centerX = display.x / 2;
    view.centerY = display.y / 2;
    view.scale = h / 456.0f;
    // The game's layouts keep square pixels on a widened canvas too (display_settings.cpp).
    view.gameScaleX = view.scale;
    return view;
}

std::u16string Utf16(const std::string& utf8) {
    std::u16string out;
    for (size_t i = 0; i < utf8.size();) {
        const uint8_t c = static_cast<uint8_t>(utf8[i]);
        uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 6) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c >> 4) == 14) {
            cp = c & 0x0F;
            extra = 2;
        } else {
            cp = c & 0x07;
            extra = 3;
        }
        ++i;
        for (int k = 0; k < extra && i < utf8.size(); ++k, ++i) {
            cp = (cp << 6) | (static_cast<uint8_t>(utf8[i]) & 0x3F);
        }
        out.push_back(cp < 0x10000 ? static_cast<char16_t>(cp) : u'?');
    }
    return out;
}

} // namespace GameLayout
