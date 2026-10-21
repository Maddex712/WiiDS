// Launcher game cases (covers and disc labels) composed on the player's device from their own
// disc: the channel banner pictures, the game's logo and fonts. Nothing here ships with WiiDS.
// NSMB follows experiments/nsmbw-thor/compose_case_art.py, MSMBWii compose_msmbwii_art.py and
// Sunshine experiments/sunshine-thor/compose_case_art.py (with the disc's own title logo and
// fonts instead of PC system fonts); Mario Kart is composed in the same style from its banner.
#include "art_internal.h"
#include "wiids_art_layout.h"
#include "wiids_art_profiles.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace wiids::art {
namespace {

using namespace detail;
namespace fs = std::filesystem;
constexpr double kPi = 3.141592653589793;

[[noreturn]] void Fail(const std::string& message) { throw std::runtime_error(message); }

const Image& Pick(const std::map<std::string, Image>& images, const std::string& name) {
    const auto it = images.find(name);
    if (it == images.end()) Fail("The disc banner has no " + name);
    return it->second;
}

void SavePng(const std::string& path, const Image& image) {
    const Bytes png = EncodePng(image);
    WriteFile(path, png.data(), png.size());
}

// The white Wii case header: a band with a curved lower edge, a thin grey rim and the Wii mark.
void WiiHeader(Image& canvas, const FontAtlas* font) {
    const int w = canvas.width, h = canvas.height;
    Mask mask = EmptyMask(canvas);
    MaskRect(mask, w, h, 0, 0, w, 70);
    MaskEllipse(mask, w, h, (-300.0 + w + 420) / 2, -5, (w + 720) / 2.0, 155);
    Mask rim(mask.size(), 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float best = 0;
            for (int oy = -3; oy <= 3; ++oy)
                for (int ox = -3; ox <= 3; ++ox) {
                    const int sx = x + ox, sy = y + oy;
                    if (sx >= 0 && sy >= 0 && sx < w && sy < h) best = std::max(best, mask[size_t(sy) * size_t(w) + size_t(sx)]);
                }
            rim[size_t(y) * size_t(w) + size_t(x)] = std::max(0.0f, best - mask[size_t(y) * size_t(w) + size_t(x)]);
        }
    Paint(canvas, rim, {190, 196, 204, 255});
    Paint(canvas, Blur(mask, w, h, 1), {255, 255, 255, 255});
    if (font) {
        const double height = 82;
        const double width = TextWidth(*font, "Wii", height);
        DrawText(canvas, *font, "Wii", w - 34 - width / 2, 92 - height * 0.86, height, {128, 131, 135, 255});
    }
}

// Round label with a clear hub: everything outside the rim and inside the hole is transparent.
void DiscMask(Image& canvas, double hubFraction = 0.125) {
    const int n = canvas.width;
    const double c = n / 2.0;
    Mask mask = EmptyMask(canvas);
    MaskEllipse(mask, n, n, c, c, c - 2, c - 2);
    MaskEllipse(mask, n, n, c, c, n * hubFraction, n * hubFraction, 0);
    MultiplyAlpha(canvas, mask);
}

Image SilverDisc(int size) {
    Image base(size, size);
    const double c = size / 2.0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const double a = std::atan2(y - c, x - c);
            const int v = 232 + int(18 * (0.5 + 0.5 * std::cos(2 * a + 0.6)));
            uint8_t* p = base.At(x, y);
            p[0] = uint8_t(v); p[1] = uint8_t(v < 253 ? v + 2 : 255); p[2] = 255; p[3] = 255;
        }
    return base;
}

void ClearRing(Image& canvas, Rgba outer = {214, 222, 230, 255}, Rgba inner = {238, 241, 245, 255}) {
    const int n = canvas.width;
    const double c = n / 2.0;
    Mask a = EmptyMask(canvas), b = EmptyMask(canvas);
    MaskEllipse(a, n, n, c, c, n * 0.2, n * 0.2);
    MaskEllipse(b, n, n, c, c, n * 0.16, n * 0.16);
    Paint(canvas, a, outer);
    Paint(canvas, b, inner);
}

FontAtlas FontFrom(const Bytes& brfnt) { return BuildFontAtlas(brfnt, 1024, 2048); }

// ---------------------------------------------------------------- New Super Mario Bros. Wii
Image NsmbCover(const std::map<std::string, Image>& b, const FontAtlas& font) {
    const int w = 750, h = 1066;
    Image canvas(w, h);
    for (int y = 0; y < h; ++y) {
        const double t = double(y) / (h - 1);
        for (int x = 0; x < w; ++x) {
            uint8_t* p = canvas.At(x, y);
            p[0] = uint8_t(40 + 120 * t); p[1] = uint8_t(140 + 85 * t); p[2] = uint8_t(235 + 18 * t); p[3] = 255;
        }
    }
    Mask rays = EmptyMask(canvas);
    for (int k = 0; k < 18; ++k) {
        const double a0 = k * 2 * kPi / 18, a1 = a0 + 2 * kPi / 36;
        MaskPolygon(rays, w, h, {{375, 520}, {375 + 1600 * std::cos(a0), 520 + 1600 * std::sin(a0)}, {375 + 1600 * std::cos(a1), 520 + 1600 * std::sin(a1)}}, 26 / 255.0f);
    }
    Paint(canvas, Blur(rays, w, h, 3), {255, 255, 255, 255});
    const double clouds[5][3] = {{40, 300, 1.6}, {560, 250, 2.0}, {90, 720, 2.4}, {500, 640, 1.4}, {300, 860, 2.8}};
    for (const auto& c : clouds) Over(canvas, Scale(Pick(b, "banner_kumo_01"), c[2]), int(c[0]), int(c[1]), 220 / 255.0);
    if (b.count("banner_yama_set_00")) {
        const Image pillar = Tint(Scale(Pick(b, "banner_yama_set_00"), 1.35), {88, 186, 72, 255}, {226, 246, 214, 255});
        Over(canvas, pillar, -30, h - pillar.height + 60);
        Over(canvas, FlipX(pillar), w - pillar.width + 40, h - pillar.height + 110);
    }
    Mask ground = EmptyMask(canvas), grass = EmptyMask(canvas);
    MaskRect(ground, w, h, 0, h - 80, w, h);
    MaskRect(grass, w, h, 0, h - 88, w, h - 74);
    Paint(canvas, ground, {96, 176, 64, 255});
    Paint(canvas, grass, {140, 214, 90, 255});
    const std::tuple<const char*, double, int, int> people[] = {
        {"banner_Yko_jump_00", 2.0, 80, 640}, {"banner_Bko_jump_00", 2.0, 560, 610}, {"banner_lg_jump_00", 2.6, 90, 430},
        {"banner_Pmr_jump_00", 2.9, 400, 380}, {"banner_mr_jump_00", 2.7, 265, 560}};
    for (const auto& [name, scale, x, y] : people)
        if (b.count(name)) Shadowed(canvas, Scale(b.at(name), scale), x, y, 6, 5, 8, 90 / 255.0);
    WiiHeader(canvas, &font);
    const Image logo = Scale(Pick(b, "banner_wiiMario_Title_logo_US_00"), 1.5);
    Shadowed(canvas, logo, (w - logo.width) / 2, 150, 8, 0, 10, 110 / 255.0);
    const Image nintendo = Scale(Pick(b, "banner_nintendo_112x32"), 1.15);
    Over(canvas, nintendo, w - nintendo.width - 26, h - nintendo.height - 22);
    return canvas;
}

Image NsmbDisc(const std::map<std::string, Image>& b, const FontAtlas& font) {
    const int n = 1024;
    Image canvas = SilverDisc(n);
    ClearRing(canvas);
    const std::tuple<const char*, double, int, int> people[] = {{"banner_lg_jump_00", 3.6, 250, 110}, {"banner_Pmr_jump_00", 3.4, 520, 70}};
    for (const auto& [name, scale, x, y] : people) {
        if (!b.count(name)) continue;
        const Image sprite = Scale(b.at(name), scale);
        Image layer = Solid(sprite.width, sprite.height, {70, 150, 230, 255});
        for (size_t i = 0; i < sprite.rgba.size(); i += 4) {
            const int lum = (sprite.rgba[i] * 299 + sprite.rgba[i + 1] * 587 + sprite.rgba[i + 2] * 114) / 1000;
            const int a = sprite.rgba[i + 3] * 120 / 255;
            layer.rgba[i + 3] = uint8_t(a * (110 + (255 - lum) * 145 / 255) / 255);
        }
        Over(canvas, layer, x, y);
    }
    const Image logo = Scale(Pick(b, "banner_wiiMario_Title_logo_US_00"), 1.3);
    Over(canvas, logo, (n - logo.width) / 2, 640);
    const Image nintendo = Tint(Scale(Pick(b, "banner_nintendo_112x32"), 1.3), {96, 100, 108, 255}, {96, 100, 108, 255});
    Over(canvas, nintendo, (n - nintendo.width) / 2, 900);
    const double width = TextWidth(font, "Wii", 96);
    DrawText(canvas, font, "Wii", 300 - width / 2, 560 - 96 * 0.86, 96, {150, 153, 158, 255});
    DiscMask(canvas);
    return canvas;
}

// A game case with a named band across it (MSMBWii over NSMB, Retro Rewind over Mario Kart).
Image BandCover(const Image& nsmb, const FontAtlas& title, const FontAtlas& text, const std::string& name, const std::string& subtitle) {
    Image im = nsmb;
    const int w = im.width, h = im.height;
    const int top = int(h * 0.79), bottom = int(h * 0.915), line = std::max(3, h / 160);
    Mask band = EmptyMask(im), gold = EmptyMask(im);
    MaskRect(band, w, h, 0, top, w, bottom);
    MaskRect(gold, w, h, 0, top, w, top + line);
    MaskRect(gold, w, h, 0, bottom - line, w, bottom);
    Paint(im, band, {20, 36, 92, 225});
    Paint(im, gold, {255, 206, 64, 255});
    const double mid = top + (bottom - top) * 0.55;
    const double big = h * 0.075, small = h * 0.036;
    DrawText(im, title, name, w / 2.0, top + (mid - top - big) / 2, big, {255, 255, 255, 255}, std::max(2, h / 220), {10, 16, 48, 255});
    DrawText(im, text, subtitle, w / 2.0, mid + (bottom - mid - small) / 2, small, {255, 222, 120, 255});
    return im;
}

Image BandDisc(const Image& nsmb, const FontAtlas& title, const FontAtlas& text, const std::string& name, const std::string& subtitle) {
    Image im = nsmb;
    const int n = im.width;
    const double c = n / 2.0, r = n / 2.0;
    Mask arc = EmptyMask(im);
    std::vector<std::array<double, 2>> wedge = {{c, c}};
    for (int a = 215; a <= 325; a += 2) wedge.push_back({c + r * 0.97 * std::cos(a * kPi / 180), c + r * 0.97 * std::sin(a * kPi / 180)});
    MaskPolygon(arc, n, n, wedge);
    MaskEllipse(arc, n, n, c, c, r * 0.62, r * 0.62, 0);
    Paint(im, arc, {20, 36, 92, 230});
    MultiplyAlpha(im, AlphaOf(nsmb)); // keep the hub and rim clear
    DrawText(im, title, name, c, c - r * 0.90, r * 0.13, {255, 255, 255, 255}, std::max(2, int(r / 180)), {10, 16, 48, 255});
    DrawText(im, text, subtitle, c, c - r * 0.75, r * 0.06, {255, 222, 120, 255});
    return im;
}

// ---------------------------------------------------------------- Mario Kart Wii
Image KartCover(const std::map<std::string, Image>& b, const FontAtlas& font) {
    const int w = 750, h = 1066;
    Image canvas(w, h);
    VerticalGradient(canvas, {{0.0, {255, 255, 255, 255}}, {0.42, {214, 232, 250, 255}}, {1.0, {38, 108, 206, 255}}});
    // Speed lines out from the kart.
    Mask rays = EmptyMask(canvas);
    for (int k = 0; k < 24; ++k) {
        const double a0 = k * 2 * kPi / 24, a1 = a0 + 2 * kPi / 60;
        MaskPolygon(rays, w, h, {{375, 600}, {375 + 1700 * std::cos(a0), 600 + 1700 * std::sin(a0)}, {375 + 1700 * std::cos(a1), 600 + 1700 * std::sin(a1)}}, 0.16f);
    }
    Paint(canvas, Blur(rays, w, h, 2), {255, 255, 255, 255});
    // A chequered finish strip across the lower case.
    Mask black = EmptyMask(canvas), white = EmptyMask(canvas);
    const double cell = 34, slope = -0.12, baseY = 905;
    for (int row = 0; row < 3; ++row)
        for (int col = -1; col * cell < w + cell; ++col) {
            const double x0 = col * cell, x1 = x0 + cell;
            const double y0 = baseY + row * cell + x0 * slope, y1 = baseY + row * cell + x1 * slope;
            const std::vector<std::array<double, 2>> q = {{x0, y0}, {x1, y1}, {x1, y1 + cell}, {x0, y0 + cell}};
            MaskPolygon((row + col) % 2 ? black : white, w, h, q);
        }
    Paint(canvas, white, {250, 250, 250, 255});
    Paint(canvas, black, {24, 24, 28, 255});
    const std::tuple<const char*, double, int, int> items[] = {
        {"banner_tt_item_banana", 1.6, 56, 380}, {"banner_tt_item_kame_green", 1.5, 612, 360}, {"banner_tt_item_star", 1.7, 40, 690},
        {"banner_tt_item_kame_red", 1.5, 628, 640}, {"banner_tt_item_kinoko", 1.4, 150, 800}, {"banner_tt_item_thunder", 1.4, 560, 790},
        {"banner_tt_item_kame_wing", 1.4, 300, 330}, {"banner_tt_item_bomb_hei", 1.3, 470, 300}};
    for (const auto& [name, scale, x, y] : items)
        if (b.count(name)) Shadowed(canvas, Scale(b.at(name), scale), x, y, 5, 4, 6, 70 / 255.0);
    const Image kart = Scale(Pick(b, "icon_tk_kartBody_00"), 2.2);
    Shadowed(canvas, kart, (w - kart.width) / 2, 470, 10, 6, 14, 120 / 255.0);
    WiiHeader(canvas, &font);
    const Image logo = Scale(Pick(b, "banner_tk_logoENG_00"), 1.45);
    Shadowed(canvas, logo, (w - logo.width) / 2, 120, 8, 0, 10, 100 / 255.0);
    const Image nintendo = Scale(Pick(b, "banner_nintendo_120x32"), 1.1);
    Over(canvas, nintendo, w - nintendo.width - 26, h - nintendo.height - 22);
    return canvas;
}

Image KartDisc(const std::map<std::string, Image>& b, const FontAtlas& font) {
    const int n = 1024;
    Image canvas = SilverDisc(n);
    ClearRing(canvas);
    // A chequered band around the lower rim.
    Mask black = EmptyMask(canvas);
    const double c = n / 2.0;
    for (int k = 0; k < 40; ++k) {
        if (k % 2) continue;
        for (int ring = 0; ring < 2; ++ring) {
            const double a0 = (20 + k * 3.5) * kPi / 180, a1 = a0 + 3.5 * kPi / 180;
            const double r0 = n * (0.45 - ring * 0.035), r1 = r0 + n * 0.035;
            const double s = ring % 2 ? 3.5 * kPi / 180 : 0;
            MaskPolygon(black, n, n, {{c + r0 * std::cos(a0 + s), c + r0 * std::sin(a0 + s)}, {c + r1 * std::cos(a0 + s), c + r1 * std::sin(a0 + s)},
                                      {c + r1 * std::cos(a1 + s), c + r1 * std::sin(a1 + s)}, {c + r0 * std::cos(a1 + s), c + r0 * std::sin(a1 + s)}});
        }
    }
    Paint(canvas, black, {40, 42, 48, 255});
    const Image logo = Scale(Pick(b, "banner_tk_logoENG_00"), 1.55);
    Over(canvas, logo, (n - logo.width) / 2, 120);
    const Image kart = Scale(Pick(b, "icon_tk_kartBody_00"), 1.25);
    Shadowed(canvas, kart, 620, 560, 6, 3, 8, 90 / 255.0);
    const Image nintendo = Tint(Scale(Pick(b, "banner_nintendo_120x32"), 1.3), {96, 100, 108, 255}, {96, 100, 108, 255});
    Over(canvas, nintendo, (n - nintendo.width) / 2, 840);
    const double width = TextWidth(font, "Wii", 96);
    DrawText(canvas, font, "Wii", 250 - width / 2, 560 - 96 * 0.86, 96, {150, 153, 158, 255});
    DiscMask(canvas);
    return canvas;
}

// ---------------------------------------------------------------- Super Mario Sunshine
void SunshineSky(Image& im) {
    VerticalGradient(im, {{0.0, {36, 128, 222, 255}}, {0.6, {124, 198, 242, 255}}, {1.0, {196, 236, 248, 255}}});
}

Image SunshineCover(const Image& logo, const Image* cloud) {
    const int w = 900, h = 1280;
    Image im(w, h);
    SunshineSky(im);
    if (cloud) {
        Over(im, Scale(*cloud, 1.4), 40, 140, 0.85);
        Over(im, Scale(*cloud, 1.1), 560, 80, 0.75);
    }
    Mask sea = EmptyMask(im), sand = EmptyMask(im), bar = EmptyMask(im);
    MaskPolygon(sea, w, h, {{0, 980}, {double(w), 940}, {double(w), double(h)}, {0, double(h)}});
    MaskRect(sand, w, h, 0, 1180, w, h);
    MaskRect(bar, w, h, 0, 0, w, 24);
    Paint(im, sea, {24, 150, 214, 255});
    Paint(im, sand, {243, 226, 176, 255});
    Paint(im, bar, {106, 90, 205, 255});
    const Image big = Scale(logo, 820.0 / logo.width);
    Shadowed(im, big, (w - big.width) / 2, 300, 10, 0, 14, 110 / 255.0, {12, 36, 88, 255});
    return im;
}

Image SunshineDisc(const Image& logo) {
    const int n = 1024;
    Image body(n, n);
    SunshineSky(body);
    Mask sea = EmptyMask(body);
    MaskRect(sea, n, n, 0, n * 0.86, n, n);
    Paint(body, sea, {24, 150, 214, 255});
    // Above the hub ring, like a printed disc label.
    const Image small = Scale(logo, 470.0 / logo.width);
    Shadowed(body, small, (n - small.width) / 2, 48, 6, 0, 8, 90 / 255.0, {12, 36, 88, 255});
    DiscMask(body, 70.0 / n);
    const double c = n / 2.0;
    Mask ring = EmptyMask(body), rim = EmptyMask(body);
    MaskEllipse(ring, n, n, c, c, 150, 150);
    MaskEllipse(ring, n, n, c, c, 124, 124, 0);
    MaskEllipse(rim, n, n, c, c, c - 4, c - 4);
    MaskEllipse(rim, n, n, c, c, c - 12, c - 12, 0);
    Paint(body, ring, {230, 236, 242, 255});
    Paint(body, rim, {210, 220, 230, 255});
    return body;
}

uint32_t PngDimension(const Bytes& png, size_t at) {
    return uint32_t(png.at(at)) << 24 | uint32_t(png.at(at + 1)) << 16 | uint32_t(png.at(at + 2)) << 8 | png.at(at + 3);
}

} // namespace

std::string BuildMarioKartCases(const std::string& discRoot, const std::string& out) {
    const auto banner = BannerImages(discRoot + "/files/opening.bnr");
    const Archive fontArc = OpenArchive(ReadFile(discRoot + "/files/Scene/UI/Font.szs"));
    const FontAtlas font = FontFrom(Member(fontArc, "kart_kanji_font.brfnt"));
    fs::create_directories(fs::u8path(out));
    const Image cover = KartCover(banner, font), disc = KartDisc(banner, font);
    SavePng(out + "/original-cover.png", cover);
    SavePng(out + "/original-disc.png", disc);
    // Retro Rewind runs on the same disc: its case is Mario Kart's with a Retro Rewind band.
    SavePng(out + "/retro-rewind-cover.png", BandCover(cover, font, font, "Retro Rewind", "Classic and custom tracks"));
    SavePng(out + "/retro-rewind-disc.png", BandDisc(disc, font, font, "Retro Rewind", "Classic and custom tracks"));
    return UpdateArtworkManifest(out);
}

std::string BuildNsmbCases(const std::string& discRoot, const std::string& out) {
    const auto banner = BannerImages(discRoot + "/files/opening.bnr");
    const std::string fonts = discRoot + "/files/US/EngUS/Font/";
    const FontAtlas message = FontFrom(ReadFile(fonts + "mj2d00_MessageFont_32_I4.brfnt"));
    const FontAtlas title = FontFrom(ReadFile(fonts + "mj2d01_marioFont_64_IA4.brfnt"));
    fs::create_directories(fs::u8path(out));
    const Image cover = NsmbCover(banner, message), disc = NsmbDisc(banner, message);
    SavePng(out + "/nsmbw-cover.png", cover);
    SavePng(out + "/nsmbw-disc.png", disc);
    SavePng(out + "/msmbwii-cover.png", BandCover(cover, title, message, "MSMBWii", "Just a Little Adventure"));
    SavePng(out + "/msmbwii-disc.png", BandDisc(disc, title, message, "MSMBWii", "Just a Little Adventure"));
    return UpdateArtworkManifest(out);
}

std::string BuildSunshineCases(const std::string& isoPath, const std::string& out) {
    Bytes option = GameCubeFile(isoPath, "data/option.szs");
    if (option.size() >= 4 && std::memcmp(option.data(), "Yaz0", 4) == 0) option = Yaz0Decompress(option);
    const auto files = Rarc(option);
    const Image* logo = nullptr;
    std::optional<Image> logoImage, cloudImage;
    for (const auto& [path, blob] : files) {
        const std::string stem = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
        if (stem == "title_logo.bti") logoImage = SunshineBti(blob);
        if (stem == "windows_kumo05.bti") { try { cloudImage = SunshineBti(blob); } catch (const std::exception&) {} }
    }
    if (!logoImage) Fail("The disc has no Super Mario Sunshine title logo.");
    logo = &*logoImage;
    fs::create_directories(fs::u8path(out));
    SavePng(out + "/sunshine-cover.png", SunshineCover(*logo, cloudImage ? &*cloudImage : nullptr));
    SavePng(out + "/sunshine-disc.png", SunshineDisc(*logo));
    return UpdateArtworkManifest(out);
}

std::string UpdateArtworkManifest(const std::string& out) {
    const char* const pairs[][2] = {{"original-cover", "original-disc"}, {"retro-rewind-cover", "retro-rewind-disc"}, {"nsmbw-cover", "nsmbw-disc"},
                                    {"msmbwii-cover", "msmbwii-disc"}, {"sunshine-cover", "sunshine-disc"}};
    Json images = Json::Object();
    int count = 0;
    for (const auto& pair : pairs) {
        if (!fs::is_regular_file(fs::u8path(out + "/" + pair[0] + ".png")) || !fs::is_regular_file(fs::u8path(out + "/" + pair[1] + ".png"))) continue;
        for (const char* stem : pair) {
            const Bytes png = ReadFile(out + "/" + stem + ".png");
            if (png.size() < 24) Fail("Damaged case picture");
            Json size = Json::Array();
            size.Push(Json::Num(PngDimension(png, 16))).Push(Json::Num(PngDimension(png, 20)));
            images.Set(stem, Json::Object().Set("file", Json::Str(std::string(stem) + ".png")).Set("sha256", Json::Str(Sha256Hex(png)))
                .Set("size", std::move(size)).Set("source", Json::Str("composed-from-player-disc")));
            ++count;
        }
    }
    const std::string text = Json::Object().Set("schema", Json::Num(1)).Set("kind", Json::Str("private-launcher-artwork"))
        .Set("images", std::move(images)).Dump() + "\n";
    // Replaced in one step: the launcher polls this file while art is being built.
    WriteFile(out + "/artwork.json.tmp", text.data(), text.size());
    fs::rename(fs::u8path(out + "/artwork.json.tmp"), fs::u8path(out + "/artwork.json"));
    return std::to_string(count) + " case pictures";
}

} // namespace wiids::art
