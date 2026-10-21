// Companion-screen art for New Super Mario Bros. Wii (experiments/nsmbw-thor/Extract-ThorHud.py)
// and Super Mario Sunshine (experiments/sunshine-thor/Extract-ThorHud.py), ported 1:1.
#include "wiids_art_profiles.h"
#include "art_internal.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>

namespace wiids::art {
namespace {

namespace fs = std::filesystem;

[[noreturn]] void Fail(const std::string& message) { throw std::runtime_error(message); }
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

std::string Latin1ToUtf8(uint32_t code) {
    std::string s;
    if (code < 0x80) s += char(code);
    else { s += char(0xC0 | (code >> 6)); s += char(0x80 | (code & 0x3F)); }
    return s;
}

Json PngRecord(const std::string& out, const std::string& name, const Image& image) {
    const Bytes png = EncodePng(image);
    WriteFile(out + "/" + name, png.data(), png.size());
    return Json::Object().Set("file", Json::Str(name)).Set("sha256", Json::Str(Sha256Hex(png)))
        .Set("width", Json::Num(image.width)).Set("height", Json::Num(image.height))
        .Set("rgbaSha256", Json::Str(Sha256Hex(image.rgba)));
}

// ---------------------------------------------------------------- NSMB Wii

Json NsmbFont(const std::string& out, const std::string& key, const Bytes& data, const std::string& source) {
    const FontAtlas font = BuildFontAtlas(data);
    Json record = PngRecord(out, "font_" + key + ".png", font.atlas);
    record.Set("lineHeight", Json::Num(font.lineHeight)).Set("ascent", Json::Num(font.ascent))
        .Set("cellWidth", Json::Num(font.cellWidth)).Set("cellHeight", Json::Num(font.cellHeight))
        .Set("baseline", Json::Num(font.baseline));
    Json glyphs = Json::Object();
    for (const auto& [code, g] : font.glyphs)
        glyphs.Set(Latin1ToUtf8(code), Json::Object().Set("x", Json::Num(g.x)).Set("y", Json::Num(g.y))
            .Set("w", Json::Num(g.w)).Set("h", Json::Num(g.h)).Set("left", Json::Num(g.left))
            .Set("advance", Json::Num(g.advance)).Set("sourceGlyphIndex", Json::Num(g.sourceGlyphIndex))
            .Set("rgbaSha256", Json::Str(g.rgbaSha256)));
    record.Set("glyphs", std::move(glyphs)).Set("source", Json::Str(source)).Set("sourceSha256", Json::Str(Sha256Hex(data)))
        .Set("pixelRule", Json::Str("Native straight RGBA; no scaling, recoloring, extra outlines or font substitution."))
        .Set("drawRule", Json::Str("Draw at penX+left,topY; advance by advance. Scale all metrics together. Baseline uses ascent."));
    return record;
}

// ---------------------------------------------------------------- Sunshine (its own decoder rules)

struct C { uint8_t r, g, b, a; };
C S565(uint32_t v) { return {uint8_t((v >> 11 & 31) * 255 / 31), uint8_t((v >> 5 & 63) * 255 / 63), uint8_t((v & 31) * 255 / 31), 255}; }
C S5a3(uint32_t v) {
    if (v & 0x8000) return {uint8_t((v >> 10 & 31) * 255 / 31), uint8_t((v >> 5 & 31) * 255 / 31), uint8_t((v & 31) * 255 / 31), 255};
    return {uint8_t((v >> 8 & 15) * 17), uint8_t((v >> 4 & 15) * 17), uint8_t((v & 15) * 17), uint8_t((v >> 12 & 7) * 255 / 7)};
}

Image SunshineBti(const Bytes& d) {
    if (d.size() < 0x20) Fail("Truncated BTI");
    const int fmt = d[0], w = Be16(&d[2]), h = Be16(&d[4]);
    const int palFmt = d[9], palN = Be16(&d[0xA]);
    const uint32_t palOff = Be32(&d[0xC]), imgOff = Be32(&d[0x1C]);
    struct B { int w, h, bpp; };
    B b;
    switch (fmt) {
        case 0: b = {8, 8, 4}; break; case 1: b = {8, 4, 8}; break; case 2: b = {8, 4, 8}; break;
        case 3: b = {4, 4, 16}; break; case 4: b = {4, 4, 16}; break; case 5: b = {4, 4, 16}; break;
        case 6: b = {4, 4, 32}; break; case 8: b = {8, 8, 4}; break; case 9: b = {8, 4, 8}; break;
        case 14: b = {8, 8, 4}; break;
        default: Fail("Unsupported BTI format");
    }
    std::vector<C> pal;
    if (fmt == 8 || fmt == 9) {
        if (palOff + size_t(palN) * 2 > d.size()) Fail("Truncated BTI palette");
        for (int i = 0; i < palN; ++i) {
            const uint32_t v = Be16(&d[palOff + 2 * size_t(i)]);
            pal.push_back(palFmt == 2 ? S5a3(v) : palFmt == 1 ? S565(v) : C{uint8_t(v >> 8), uint8_t(v >> 8), uint8_t(v >> 8), uint8_t(v & 255)});
        }
    }
    Image im(w, h);
    size_t p = imgOff;
    auto need = [&](size_t n) { if (p + n > d.size()) Fail("Truncated BTI"); };
    auto put = [&](int x, int y, C c) {
        if (x < w && y < h) { uint8_t* px = im.At(x, y); px[0] = c.r; px[1] = c.g; px[2] = c.b; px[3] = c.a; }
    };
    auto palette = [&](size_t i) { if (i >= pal.size()) Fail("BTI palette index out of range"); return pal[i]; };
    for (int by = 0; by < h; by += b.h) {
        for (int bx = 0; bx < w; bx += b.w) {
            if (fmt == 14) {
                for (int sy = 0; sy < 8; sy += 4)
                    for (int sx = 0; sx < 8; sx += 4) {
                        need(8);
                        const uint32_t c0 = Be16(&d[p]), c1 = Be16(&d[p + 2]), bits = Be32(&d[p + 4]);
                        p += 8;
                        const C a = S565(c0), z = S565(c1);
                        C cols[4] = {a, z, {}, {}};
                        if (c0 > c1) {
                            cols[2] = {uint8_t((2 * a.r + z.r) / 3), uint8_t((2 * a.g + z.g) / 3), uint8_t((2 * a.b + z.b) / 3), 255};
                            cols[3] = {uint8_t((a.r + 2 * z.r) / 3), uint8_t((a.g + 2 * z.g) / 3), uint8_t((a.b + 2 * z.b) / 3), 255};
                        } else {
                            cols[2] = {uint8_t((a.r + z.r) / 2), uint8_t((a.g + z.g) / 2), uint8_t((a.b + z.b) / 2), 255};
                            cols[3] = {0, 0, 0, 0};
                        }
                        for (int i = 0; i < 16; ++i) put(bx + sx + i % 4, by + sy + i / 4, cols[bits >> (30 - 2 * i) & 3]);
                    }
                continue;
            }
            if (fmt == 6) {
                need(64);
                for (int i = 0; i < 16; ++i)
                    put(bx + i % 4, by + i / 4, {d[p + 2 * i + 1], d[p + 32 + 2 * i], d[p + 32 + 2 * i + 1], d[p + 2 * i]});
                p += 64;
                continue;
            }
            for (int y = by; y < by + b.h; ++y)
                for (int x = bx; x < bx + b.w; ++x) {
                    C c;
                    if (b.bpp == 4) {
                        need(1);
                        const uint32_t v = (x - bx) % 2 == 0 ? d[p] >> 4 : d[p] & 15;
                        if ((x - bx) % 2 == 1) ++p;
                        c = fmt == 8 ? palette(v) : C{uint8_t(v * 17), uint8_t(v * 17), uint8_t(v * 17), 255};
                    } else if (b.bpp == 8) {
                        need(1);
                        const uint32_t v = d[p++];
                        if (fmt == 2) c = {uint8_t(v), uint8_t(v), uint8_t(v), uint8_t(v)};
                        else if (fmt == 9) c = palette(v);
                        else { const uint8_t g = uint8_t((v >> 4) * 17); c = {g, g, g, uint8_t((v & 15) * 17)}; }
                    } else {
                        need(2);
                        const uint32_t v = Be16(&d[p]);
                        p += 2;
                        c = fmt == 3 ? C{d[p - 1], d[p - 1], d[p - 1], d[p - 2]} : fmt == 4 ? S565(v) : S5a3(v);
                    }
                    put(x, y, c);
                }
        }
    }
    return im;
}

std::map<std::string, Bytes> Rarc(const Bytes& d) {
    if (d.size() < 0x40 || std::memcmp(d.data(), "RARC", 4) != 0) Fail("Expected a RARC archive");
    const size_t dataOff = Be32(&d[0xC]) + 0x20;
    const size_t nodesOff = Be32(&d[0x24]) + 0x20, entOff = Be32(&d[0x2C]) + 0x20, strOff = Be32(&d[0x34]) + 0x20;
    std::map<std::string, Bytes> files;
    auto name = [&](size_t off) {
        const size_t at = strOff + off;
        if (at >= d.size()) Fail("Damaged RARC archive");
        const auto end = std::find(d.begin() + std::ptrdiff_t(at), d.end(), uint8_t(0));
        return std::string(d.begin() + std::ptrdiff_t(at), end);
    };
    int depth = 0;
    std::function<void(uint32_t, const std::string&)> walk = [&](uint32_t node, const std::string& prefix) {
        if (++depth > 64) Fail("Damaged RARC archive");
        const size_t n = nodesOff + size_t(node) * 16;
        if (n + 16 > d.size()) Fail("Damaged RARC archive");
        const uint16_t count = Be16(&d[n + 10]);
        const uint32_t first = Be32(&d[n + 12]);
        for (uint32_t i = 0; i < count; ++i) {
            const size_t e = entOff + size_t(first + i) * 20;
            if (e + 20 > d.size()) Fail("Damaged RARC archive");
            const uint16_t type = Be16(&d[e + 4]), nameOff = Be16(&d[e + 6]);
            const uint32_t a = Be32(&d[e + 8]), b = Be32(&d[e + 12]);
            const std::string nm = name(nameOff);
            if (nm == "." || nm == "..") continue;
            if (nm.find('/') != std::string::npos || nm.find('\\') != std::string::npos) Fail("Unsafe archive member");
            if (type & 0x0200) {
                walk(a, prefix + nm + "/");
            } else {
                if (dataOff + a + b > d.size()) Fail("Damaged RARC archive");
                files[prefix + nm] = Bytes(d.begin() + std::ptrdiff_t(dataOff + a), d.begin() + std::ptrdiff_t(dataOff + a + b));
            }
        }
        --depth;
    };
    walk(0, "");
    return files;
}

// Reads one file from a 1:1 GameCube disc image through its file table.
Bytes GameCubeFile(const std::string& isoPath, const std::string& wanted) {
    std::ifstream iso(fs::u8path(isoPath), std::ios::binary);
    if (!iso) Fail("Missing disc image: " + isoPath);
    auto read = [&](uint64_t at, size_t size) {
        Bytes out(size);
        iso.seekg(std::streamoff(at));
        if (!iso.read(reinterpret_cast<char*>(out.data()), std::streamsize(size))) Fail("The disc image is damaged.");
        return out;
    };
    const Bytes boot = read(0, 0x440);
    const uint32_t fstAt = Be32(&boot[0x424]), fstSize = Be32(&boot[0x428]);
    if (fstSize < 12 || fstSize > (16u << 20)) Fail("The disc's file table is damaged.");
    const Bytes fst = read(fstAt, fstSize);
    const uint32_t count = Be32(&fst[8]);
    if (uint64_t(count) * 12 > fst.size()) Fail("The disc's file table is damaged.");
    const size_t names = size_t(count) * 12;
    auto entryName = [&](uint32_t i) {
        const size_t at = names + (Be32(&fst[12 * size_t(i)]) & 0xFFFFFF);
        if (at >= fst.size()) Fail("The disc's file table is damaged.");
        return std::string(reinterpret_cast<const char*>(&fst[at]), strnlen(reinterpret_cast<const char*>(&fst[at]), fst.size() - at));
    };
    // Walk the path one component at a time.
    uint32_t dirStart = 1, dirEnd = count;
    size_t pos = 0;
    while (true) {
        const size_t slash = wanted.find('/', pos);
        const std::string part = wanted.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        bool found = false;
        for (uint32_t i = dirStart; i < dirEnd;) {
            const uint8_t* e = &fst[12 * size_t(i)];
            const bool dir = e[0] != 0;
            const uint32_t next = dir ? Be32(e + 8) : i + 1;
            if (next <= i || next > dirEnd) Fail("The disc's file table is damaged.");
            if (entryName(i) == part) {
                if (slash == std::string::npos) {
                    if (dir) Fail("Not a file: " + wanted);
                    return read(Be32(e + 4), Be32(e + 8));
                }
                if (!dir) Fail("Not a folder: " + wanted);
                dirStart = i + 1; dirEnd = next; found = true;
                break;
            }
            i = next;
        }
        if (!found) Fail("The disc has no " + wanted);
        pos = slash + 1;
    }
}

} // namespace

std::string BuildNsmbHud(const std::string& discRoot, const std::string& out) {
    const Bytes boot = ReadFile(discRoot + "/sys/boot.bin");
    if (boot.size() < 8 || std::memcmp(boot.data(), "SMNE01", 6) != 0 || boot[7] != 1)
        Fail("This art needs the USA revision 1 New Super Mario Bros. Wii disc.");
    fs::create_directories(fs::u8path(out));
    const std::string files = discRoot + "/files";
    const Bytes scene = ReadFile(files + "/Layout/gameScene/gameScene.arc");
    const auto members = U8Files(scene);
    Json images = Json::Object(), fonts = Json::Object();
    Json archives = Json::Object().Set("files/Layout/gameScene/gameScene.arc", Json::Str(Sha256Hex(scene)));
    auto addMember = [&](const std::string& key, const std::map<std::string, Bytes>& arc, const std::string& relative, const std::string& member) {
        const auto it = arc.find(member);
        if (it == arc.end()) Fail("The disc is missing " + relative + "!" + member);
        Json record = PngRecord(out, key + ".png", DecodeTpl(it->second));
        record.Set("source", Json::Str(relative + "!" + member)).Set("sourceSha256", Json::Str(Sha256Hex(it->second)));
        images.Set(key, std::move(record));
    };
    auto add = [&](const std::string& key, const std::map<std::string, Bytes>& arc, const std::string& relative, const std::string& stem) {
        addMember(key, arc, relative, "arc/timg/" + stem + ".tpl");
    };
    const std::pair<const char*, const char*> selection[] = {
        {"coin", "im_coin_01"}, {"clock", "im_timer_00"}, {"character0", "im_marioIcon_01"},
        {"character1", "im_luijiIcon_01"}, {"character2", "im_kinopioIcon_Blue_01"},
        {"character3", "im_kinopioIcon_Yellow_01"}, {"starCoin", "im_collectionCoin_01"},
        {"starCoinOff", "im_collectionOff_00"}};
    for (const auto& [key, stem] : selection) add(key, members, "files/Layout/gameScene/gameScene.arc", stem);
    std::vector<std::tuple<std::string, std::string, std::string>> extra = {
        {"panel", "gameScene", "im_miniWindowBar_32_400_00"}, {"reserve0", "stockItem", "im_kinoko_02"},
        {"reserve1", "stockItem", "im_fireFlower_02"}, {"reserve4", "stockItem", "im_iceFlower_02"},
        {"reserve11", "stockItem", "im_propeller_02"}, {"reserve13", "stockItem", "im_mameKinoko_02"},
        {"reserve14", "stockItem", "im_penguin_02"}, {"mapTile", "worldSelectMap", "im_block128_00"},
        {"frameCorner", "stockItem", "im_kadomaruFlame_00"}, {"pill", "stockItem", "im_miniWindow_32_220_R_00"},
        {"heroMario", "pointResult", "im_mario_00"}, {"goalFlag", "corseSelectUIGuide", "im_middleFlagSkull_05"}};
    const char* worlds[] = {"im_worldBase_1_00", "im_worldBase_2_01", "im_worldBase_3_02", "im_worldBase_4_01",
                            "im_worldBase_5_00", "im_worldBase_6_00", "im_worldBase_7_00", "im_worldBase_8_01", "im_worldBase_9_00"};
    for (int i = 0; i < 9; ++i) extra.emplace_back("world" + std::to_string(i + 1), "preGame", worlds[i]);
    std::map<std::string, std::map<std::string, Bytes>> cache;
    for (const auto& [key, group, stem] : extra) {
        const std::string relative = "files/Layout/" + group + "/" + group + ".arc";
        if (!cache.count(group)) {
            const Bytes data = ReadFile(discRoot + "/" + relative);
            archives.Set(relative, Json::Str(Sha256Hex(data)));
            cache[group] = U8Files(data);
        }
        add(key, cache[group], relative, stem);
    }
    // NSMB2-style world bubbles: each world's first course picture from the free-mode course select.
    {
        const std::string relative = "files/Layout/textures/MultiCorseSelectTexture.arc";
        const Bytes data = ReadFile(discRoot + "/" + relative);
        archives.Set(relative, Json::Str(Sha256Hex(data)));
        const auto arc = U8Files(data);
        for (int i = 1; i <= 9; ++i)
            addMember("course" + std::to_string(i), arc, relative, "MultiCorseSelectTexture/im_W" + std::to_string(i) + "_1.tpl");
    }
    const std::pair<const char*, const char*> fontFiles[] = {
        {"main", "mj2d01_marioFont_64_IA4.brfnt"}, {"number", "mj2d00_numberFont_I4A.brfnt"},
        {"message", "mj2d00_MessageFont_32_I4.brfnt"}};
    for (const auto& [key, name] : fontFiles) {
        const std::string relative = std::string("US/EngUS/Font/") + name;
        fonts.Set(key, NsmbFont(out, key, ReadFile(files + "/" + relative), "files/" + relative));
    }
    Json order = Json::Array();
    for (const char* who : {"Mario", "Luigi", "Blue Toad", "Yellow Toad"}) order.Push(Json::Str(who));
    Json manifest = Json::Object().Set("schema", Json::Num(1)).Set("kind", Json::Str("private-nsmbw-retail-hud"))
        .Set("gameId", Json::Str("SMNE01")).Set("revision", Json::Num(1)).Set("images", std::move(images))
        .Set("fonts", std::move(fonts)).Set("sourceArchives", std::move(archives)).Set("characterOrder", std::move(order))
        .Set("presentationNote", Json::Str("Independent passive lower HUD using original game pixels; placement is an adaptation. Upper HUD remains intact."))
        .Set("builtBy", Json::Str("WiiDS on-device art builder"));
    const std::string text = manifest.Dump() + "\n";
    WriteFile(out + "/manifest.json", text.data(), text.size());
    return "NSMB HUD art built";
}

std::string BuildSunshineHud(const std::string& isoPath, const std::string& out) {
    {
        std::ifstream iso(fs::u8path(isoPath), std::ios::binary);
        char id[8] = {};
        if (!iso.read(id, 8) || (std::memcmp(id, "GMSE01", 6) != 0 && std::memcmp(id, "GMSE04", 6) != 0) || id[7] != 0)
            Fail("This art needs the USA Sunshine disc or the supported Eclipse 1.1.0 disc.");
    }
    fs::create_directories(fs::u8path(out));
    struct Want { const char* archive; std::vector<std::string> prefixes; };
    const Want wants[] = {
        {"game_6.szs", {}},
        {"select.szs", {"sc_map_", "sc_shine", "sc_coin", "sc_mario", "sc_story", "sc_number_", "sc_question"}},
        {"guide.szs", {"guide_picture_", "guide_map_pinna", "guide_back", "guide_draw_palmtree_1", "guide_draw_ship_1"}},
        {"option.szs", {"title_logo", "windows_kumo05", "stage_airport", "stage_dolpic"}},
    };
    Json manifest = Json::Object();
    int count = 0;
    for (const Want& want : wants) {
        Bytes raw = GameCubeFile(isoPath, std::string("data/") + want.archive);
        if (raw.size() >= 4 && std::memcmp(raw.data(), "Yaz0", 4) == 0) raw = Yaz0Decompress(raw);
        const auto files = Rarc(raw);
        for (const auto& [path, blob] : files) {
            if (path.size() < 4 || path.compare(path.size() - 4, 4, ".bti") != 0) continue;
            const size_t slash = path.rfind('/');
            const std::string stem = path.substr(slash == std::string::npos ? 0 : slash + 1, std::string::npos);
            const std::string base = stem.substr(0, stem.size() - 4);
            if (!want.prefixes.empty() && std::none_of(want.prefixes.begin(), want.prefixes.end(),
                    [&](const std::string& p) { return base.compare(0, p.size(), p) == 0; }))
                continue;
            Image image;
            try { image = SunshineBti(blob); } catch (const std::exception&) { continue; }
            const std::string archive = want.archive;
            const std::string name = archive.substr(0, archive.find('.')) + "-" + base + ".png";
            const Bytes png = EncodePng(image);
            WriteFile(out + "/" + name, png.data(), png.size());
            Json size = Json::Array();
            size.Push(Json::Num(image.width)).Push(Json::Num(image.height));
            manifest.Set(name, Json::Object().Set("archive", Json::Str(archive)).Set("entry", Json::Str(path))
                .Set("size", std::move(size)).Set("rgbaSha256", Json::Str(Sha256Hex(image.rgba))));
            ++count;
        }
    }
    const std::string text = manifest.Dump(1);
    WriteFile(out + "/manifest.json", text.data(), text.size());
    return std::to_string(count) + " Sunshine images built";
}

} // namespace wiids::art

namespace wiids::art::detail {
Bytes GameCubeFile(const std::string& isoPath, const std::string& path) { return wiids::art::GameCubeFile(isoPath, path); }
std::map<std::string, Bytes> Rarc(const Bytes& data) { return wiids::art::Rarc(data); }
Image SunshineBti(const Bytes& data) { return wiids::art::SunshineBti(data); }
} // namespace wiids::art::detail
