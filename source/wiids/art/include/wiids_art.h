#pragma once
// wiids_art: builds each game's companion-screen art on the player's device from their own
// imported disc (nothing Nintendo-made ships with WiiDS). Ports the PC tools under
// experiments/*/Extract-ThorHud.py and scripts/Extract-Thor*.py; pixels are decoded exactly as
// those tools do (gclib's integer GX texture rules), so their recorded RGBA hashes still match.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace wiids::art {

using Bytes = std::vector<uint8_t>;

struct Image {
    int width = 0, height = 0;
    Bytes rgba; // straight RGBA, row-major, top to bottom
    Image() = default;
    Image(int w, int h) : width(w), height(h), rgba(size_t(w) * size_t(h) * 4, 0) {}
    uint8_t* At(int x, int y) { return rgba.data() + (size_t(y) * size_t(width) + size_t(x)) * 4; }
    const uint8_t* At(int x, int y) const { return rgba.data() + (size_t(y) * size_t(width) + size_t(x)) * 4; }
    // PIL Image.crop: areas outside the source become transparent black.
    Image Crop(int left, int top, int right, int bottom) const;
    // PIL Image.paste without a mask: replaces pixels.
    void Paste(const Image& source, int x, int y);
};

// GX texture formats (gx_enums.ImageFormat / PaletteFormat values).
enum class GxFormat : uint32_t { I4 = 0, I8 = 1, IA4 = 2, IA8 = 3, RGB565 = 4, RGB5A3 = 5, RGBA32 = 6,
                                 C4 = 8, C8 = 9, C14X2 = 10, CMPR = 14 };
enum class PaletteFormat : uint32_t { IA8 = 0, RGB565 = 1, RGB5A3 = 2 };

size_t GxImageSize(GxFormat format, int width, int height);
Image DecodeGx(GxFormat format, const uint8_t* data, size_t size, int width, int height,
               const uint8_t* palette = nullptr, PaletteFormat paletteFormat = PaletteFormat::IA8, int paletteCount = 0);

Bytes ReadFile(const std::string& path);
void WriteFile(const std::string& path, const void* data, size_t size);

Bytes Yaz0Decompress(const Bytes& data);
// Uncompressed U8 archive -> "dir/sub/name" -> contents (as the PC tools name members).
std::map<std::string, Bytes> U8Files(const Bytes& data);
// Single-image TPL.
Image DecodeTpl(const Bytes& data);

struct Glyph { int x, y, w, h, left, advance, sourceGlyphIndex; std::string rgbaSha256; };
struct FontAtlas {
    Image atlas;
    int lineHeight = 0, ascent = 0, cellWidth = 0, cellHeight = 0, baseline = 0;
    std::map<uint32_t, Glyph> glyphs; // by character code
};
// BRFNT -> packed glyph atlas (Latin-1 printable glyphs), exactly as the PC tools pack it.
FontAtlas BuildFontAtlas(const Bytes& brfnt, int atlasWidth = 1024, int atlasHeight = 2048);

Bytes EncodePng(const Image& image);
std::string Sha256Hex(const void* data, size_t size);
inline std::string Sha256Hex(const Bytes& data) { return Sha256Hex(data.data(), data.size()); }

// Minimal ordered JSON builder (enough for the art manifests).
class Json {
public:
    static Json Object();
    static Json Array();
    static Json Str(const std::string& value);
    static Json Num(long long value);
    static Json Real(double value);
    static Json Bool(bool value);
    static Json Null();
    Json& Set(const std::string& key, Json value);
    Json& Push(Json value);
    std::string Dump(int indent = 2) const;

private:
    enum class Kind { Object, Array, String, Number, Real, Bool, Null } kind_ = Kind::Null;
    std::string text_;
    long long number_ = 0;
    double real_ = 0;
    bool boolean_ = false;
    std::vector<std::pair<std::string, Json>> members_;
    std::vector<Json> items_;
    void DumpTo(std::string& out, int indent, int depth) const;
};

} // namespace wiids::art
