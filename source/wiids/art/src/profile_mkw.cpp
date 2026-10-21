// Mario Kart Wii companion art: HUD (scripts/Extract-ThorHud.py), Wii-menu chrome
// (scripts/Extract-ThorMenu.py), map faces and held-item icons (scripts/Bake-ThorMaps.py icons()
// and item_icons()). Reads the player's imported PAL disc; writes the ThorMap folder.
#include "wiids_art_layout.h"
#include "wiids_art_profiles.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace wiids::art {
namespace {

namespace fs = std::filesystem;

[[noreturn]] void Fail(const std::string& message) { throw std::runtime_error(message); }
uint16_t Be16(const Bytes& d, size_t at) { if (at + 2 > d.size()) Fail("Damaged game file"); return uint16_t(d[at] << 8 | d[at + 1]); }
uint32_t Be32(const Bytes& d, size_t at) {
    if (at + 4 > d.size()) Fail("Damaged game file");
    return uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3];
}

const char* const kFaces[24] = {"mario", "baby_peach", "waluigi", "koopa", "baby_daisy", "karon", "baby_mario", "luigi",
                                "kinopio", "donky", "yoshi", "wario", "baby_luigi", "kinopico", "noko", "daisy",
                                "peach", "catherine", "didy", "teresa", "koopa_jr", "hone_koopa", "fuky", "roseta"};
struct Item { const char *name, *pane, *texture; };
const Item kItems[20] = {
    {"Green Shell", "kame_green", "tt_item_kame_green"}, {"Red Shell", "kame_red", "tt_item_kame_red"},
    {"Banana", "banana", "tt_item_banana"}, {"Fake Item Box", "dummybox", "tt_item_dummybox"},
    {"Mushroom", "kinoko", "tt_item_kinoko"}, {"Triple Mushrooms", "kinoko_3", "tt_item_kinoko_3"},
    {"Bob-omb", "bomb_hei", "tt_item_bomb_hei"}, {"Spiny Shell", "kame_wing", "tt_item_kame_wing"},
    {"Lightning", "thunder", "tt_item_thunder"}, {"Star", "star", "tt_item_star"},
    {"Golden Mushroom", "GoldenKinoko", "tt_item_GoldenKinoko"}, {"Mega Mushroom", "kinoko_big", "fm_item_kinoko_l"},
    {"Blooper", "gesso", "fm_item_gesso"}, {"POW Block", "pow", "fm_item_pow"},
    {"Thunder Cloud", "thunder_c", "fm_item_pikakumo"}, {"Bullet Bill", "killer", "tt_item_killer"},
    {"Triple Green Shells", "kame_green_3", "tt_item_kame_green_3"}, {"Triple Red Shells", "kame_red_3", "tt_item_kame_red_3"},
    {"Triple Bananas", "banana_3", "item_banana_3"}, {"Two Mushrooms", "kinoko_2", "tt_item_kinoko_2"}};

struct Out {
    std::string dir;
    Json Png(const std::string& name, const Image& image) const {
        const Bytes png = EncodePng(image);
        WriteFile(dir + "/" + name, png.data(), png.size());
        Json size = Json::Array();
        size.Push(Json::Num(image.width)).Push(Json::Num(image.height));
        return Json::Object().Set("texture", Json::Str(name)).Set("size", std::move(size))
            .Set("sha256", Json::Str(Sha256Hex(png))).Set("rgba_sha256", Json::Str(Sha256Hex(image.rgba)));
    }
};

Json Pair(double a, double b) { Json j = Json::Array(); j.Push(Json::Real(a)).Push(Json::Real(b)); return j; }

std::string Utf8(uint32_t code) {
    std::string s;
    if (code < 0x80) s += char(code);
    else if (code < 0x800) { s += char(0xC0 | (code >> 6)); s += char(0x80 | (code & 0x3F)); }
    else { s += char(0xE0 | (code >> 12)); s += char(0x80 | ((code >> 6) & 0x3F)); s += char(0x80 | (code & 0x3F)); }
    return s;
}

// BMG message text by message ID, skipping control escapes (menu_layout.messages).
std::map<uint32_t, std::u16string> Messages(const Bytes& d) {
    if (d.size() < 32 || std::memcmp(d.data(), "MESGbmg1", 8) != 0) Fail("Invalid BMG");
    std::map<std::string, size_t> blocks;
    size_t offset = 32;
    for (uint32_t i = 0; i < Be32(d, 12); ++i) {
        const uint32_t size = Be32(d, offset + 4);
        if (size < 8 || offset + size > d.size()) Fail("Invalid BMG block");
        blocks[std::string(reinterpret_cast<const char*>(&d[offset]), 4)] = offset;
        offset += size;
    }
    const size_t info = blocks.at("INF1"), strings = blocks.at("DAT1") + 8;
    const uint16_t count = Be16(d, info + 8), stride = Be16(d, info + 10);
    std::map<uint32_t, std::u16string> out;
    for (uint32_t i = 0; i < count; ++i) {
        size_t at = strings + Be32(d, info + 16 + size_t(i) * stride);
        std::u16string text;
        while (at + 2 <= d.size() && !(d[at] == 0 && d[at + 1] == 0)) {
            if (d[at] == 0 && d[at + 1] == 0x1A) {
                const uint8_t size = d.at(at + 2);
                if (size < 4) Fail("Invalid BMG control command");
                at += size;
                continue;
            }
            text += char16_t(Be16(d, at));
            at += 2;
        }
        const uint32_t id = blocks.count("MID1") ? Be32(d, blocks["MID1"] + 16 + size_t(i) * 4) : i;
        out[id] = text;
    }
    return out;
}

// Python round(): halves to even.
double PyRound(double v) { return std::nearbyint(v); }

Image TimerTint(const Image& image, const double top[3], const double bottom[3]) {
    Image out = image;
    for (int y = 0; y < out.height; ++y) {
        const double weight = (y + .5) / out.height;
        double rgb[3];
        for (int i = 0; i < 3; ++i) rgb[i] = (1 - weight) * top[i] + weight * bottom[i];
        for (int x = 0; x < out.width; ++x) {
            uint8_t* p = out.At(x, y);
            for (int i = 0; i < 3; ++i) p[i] = uint8_t(PyRound(p[i] * rgb[i] / 255));
        }
    }
    return out;
}

// The six HUD colour-table slots in StaticR (0x80895CC0, 24-byte stride), as signed 16-bit RGBA.
std::array<std::array<std::array<int, 4>, 3>, 6> HudPalette(const Bytes& staticR) {
    constexpr size_t kOffset = 0x80895CC0u - 0x805102E0u;
    std::array<std::array<std::array<int, 4>, 3>, 6> slots{};
    for (int slot = 0; slot < 6; ++slot)
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 4; ++k) slots[slot][i][k] = int16_t(Be16(staticR, kOffset + size_t(slot) * 24 + size_t(i) * 8 + size_t(k) * 2));
    return slots;
}

Json FontRecord(const Out& out, const FontAtlas& font) {
    Json record = out.Png("hud_font_main.png", font.atlas);
    record.Set("lineHeight", Json::Num(font.lineHeight)).Set("ascent", Json::Num(font.ascent))
        .Set("cellWidth", Json::Num(font.cellWidth)).Set("cellHeight", Json::Num(font.cellHeight))
        .Set("baseline", Json::Num(font.baseline));
    Json glyphs = Json::Object();
    for (const auto& [code, g] : font.glyphs)
        glyphs.Set(Utf8(code), Json::Object().Set("x", Json::Num(g.x)).Set("y", Json::Num(g.y)).Set("w", Json::Num(g.w))
            .Set("h", Json::Num(g.h)).Set("left", Json::Num(g.left)).Set("advance", Json::Num(g.advance))
            .Set("sourceGlyphIndex", Json::Num(g.sourceGlyphIndex)).Set("rgba_sha256", Json::Str(g.rgbaSha256)));
    record.Set("glyphs", std::move(glyphs))
        .Set("pixels", Json::Str("Native straight RGBA, no tint/scaling"))
        .Set("drawRule", Json::Str("At pen(x,y): draw glyph at x+left,y; advance pen by advance. For baseline coordinates subtract ascent from y. Scale all metrics together."));
    return record;
}

void WriteText(const std::string& path, const std::string& text) { WriteFile(path, text.data(), text.size()); }

} // namespace

std::string BuildMarioKartArt(const std::string& discRoot, const std::string& outDir) {
    const Bytes boot = ReadFile(discRoot + "/sys/boot.bin");
    if (boot.size() < 8 || std::memcmp(boot.data(), "RMCP01", 6) != 0 || boot[7] != 0)
        Fail("This art needs the Europe (PAL) Mario Kart Wii disc, revision 0.");
    fs::create_directories(fs::u8path(outDir));
    const Out out{outDir};
    const std::string ui = discRoot + "/files/Scene/UI/";
    const Archive race = OpenArchive(ReadFile(ui + "Race.szs"));
    const Archive raceE = OpenArchive(ReadFile(ui + "Race_E.szs"));
    const Archive fontArc = OpenArchive(ReadFile(ui + "Font.szs"));
    const Archive menuOther = OpenArchive(ReadFile(ui + "MenuOther.szs"));
    const Archive menuSingle = OpenArchive(ReadFile(ui + "MenuSingle.szs"));
    const auto palette = HudPalette(ReadFile(discRoot + "/files/rel/StaticR.rel"));

    // ---- map faces (32x32) and held-item icons
    Json faceRecords = Json::Array();
    for (int i = 0; i < 24; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "face_%02d.png", i);
        const Image image = DecodeTpl(Member(race, std::string("game_image/timg/st_") + kFaces[i] + "_32x32.tpl"));
        const Bytes png = EncodePng(image);
        WriteFile(outDir + "/" + name, png.data(), png.size());
        faceRecords.Push(Json::Object().Set("id", Json::Num(i)).Set("source_name", Json::Str(kFaces[i]))
            .Set("file", Json::Str(name)).Set("sha256", Json::Str(Sha256Hex(png))));
    }
    WriteText(outDir + "/faces.json", Json::Object().Set("source", Json::Str("Scene/UI/Race.szs"))
        .Set("faces", std::move(faceRecords)).Dump() + "\n");
    {
        // item.brlyt binds each held-item pane to its texture; check the binding as the PC tool does.
        const Bytes& layout = Member(race, "game_image/blyt/item.brlyt");
        std::vector<std::string> textures, materials;
        std::map<std::string, std::string> bindings;
        size_t offset = Be16(layout, 12);
        auto text = [&](size_t at) { std::string s; while (at < layout.size() && layout[at]) s += char(layout[at++]); return s; };
        while (offset < layout.size()) {
            const std::string tag(reinterpret_cast<const char*>(&layout[offset]), 4);
            const uint32_t length = Be32(layout, offset + 4);
            if (length < 8 || offset + length > layout.size()) Fail("Invalid BRLYT section length");
            const size_t payload = offset + 8;
            if (tag == "txl1") {
                const size_t table = payload + 4;
                for (uint32_t i = 0; i < Be16(layout, payload); ++i) textures.push_back(text(table + Be32(layout, table + i * 8)));
            } else if (tag == "mat1") {
                for (uint32_t i = 0; i < Be16(layout, payload); ++i) {
                    const size_t material = offset + Be32(layout, payload + 4 + i * 4);
                    materials.push_back((Be32(layout, material + 60) & 15) ? textures.at(Be16(layout, material + 64)) : std::string());
                }
            } else if (tag == "pic1") {
                bindings[text(payload + 4)] = materials.at(Be16(layout, payload + 84));
            }
            offset += length;
        }
        Json records = Json::Array();
        for (int i = 0; i < 20; ++i) {
            if (bindings[kItems[i].pane] != std::string(kItems[i].texture) + ".tpl")
                Fail(std::string("Item pane/texture mismatch: ") + kItems[i].pane);
            const Image image = DecodeTpl(Member(race, std::string("game_image/timg/") + kItems[i].texture + ".tpl"));
            char name[32];
            std::snprintf(name, sizeof(name), "item_%02d.png", i);
            const Bytes png = EncodePng(image);
            WriteFile(outDir + "/" + name, png.data(), png.size());
            records.Push(Json::Object().Set("id", Json::Num(i)).Set("name", Json::Str(kItems[i].name))
                .Set("raw_kind", Json::Num(i == 19 ? 5 : i)).Set("count", i == 19 ? Json::Num(2) : Json::Null())
                .Set("pane", Json::Str(kItems[i].pane)).Set("file", Json::Str(name)).Set("sha256", Json::Str(Sha256Hex(png)))
                .Set("rgba_sha256", Json::Str(Sha256Hex(image.rgba))));
        }
        Json counts = Json::Object().Set("1", Json::Num(4)).Set("2", Json::Num(19)).Set("3", Json::Num(5));
        WriteText(outDir + "/items.json", Json::Object().Set("schema", Json::Num(1)).Set("source", Json::Str("Scene/UI/Race.szs"))
            .Set("none_icon", Json::Num(-1)).Set("triple_mushroom_counts", std::move(counts))
            .Set("items", std::move(records)).Dump() + "\n");
    }

    // ---- HUD
    const FontAtlas font = BuildFontAtlas(Member(fontArc, "kart_kanji_font.brfnt"), 1024, 1024);
    Json hud = Json::Object().Set("schema", Json::Num(1)).Set("kind", Json::Str("private-retail-wii-hud"));
    Json fonts = Json::Object().Set("main", FontRecord(out, font));
    Json rows = Json::Object();
    const std::vector<std::string> background = {"line", "hight_light", "select_gold_01", "select_gold_02",
                                                 "select_base", "team_color_c", "grade"};
    for (const auto& [key, clip] : {std::pair<const char*, const char*>{"normal", "c_off"}, {"selected", "1p_color"}}) {
        LayoutRequest request;
        request.layout = "result/blyt/position_12players.brlyt";
        request.animations = {std::string("result/anim/position_12players_") + clip + ".brlan",
                              "result/anim/position_12players_icon_wait_position.brlan"};
        request.hasBounds = true;
        const double bounds[4] = {-305, -23, 303, 21};
        std::memcpy(request.bounds, bounds, sizeof(bounds));
        request.include = background;
        double used[4];
        const Image image = RenderLayout(race, request, used);
        rows.Set(key, out.Png(std::string("hud_row_") + key + ".png", image));
    }
    Json images = Json::Object();
    std::map<std::string, Image> raw;
    for (int digit = 0; digit < 10; ++digit) {
        char stem[48];
        std::snprintf(stem, sizeof(stem), "tt_d_number_3d_%02d", digit);
        raw["digit_" + std::to_string(digit)] = DecodeTpl(Member(raceE, std::string("game_image/timg/") + stem + ".tpl"));
    }
    raw["time"] = DecodeTpl(Member(raceE, "game_image/timg/tt_time_E.tpl"));
    raw["colon"] = DecodeTpl(Member(raceE, "game_image/timg/tt_d_number_3d_coron_00.tpl"));
    raw["dot"] = DecodeTpl(Member(raceE, "game_image/timg/tt_d_number_3d_coron.tpl"));
    for (int digit = 0; digit < 10; ++digit) {
        const std::string key = "digit_" + std::to_string(digit);
        images.Set(key, out.Png("hud_digit_" + std::to_string(digit) + ".png", raw[key]));
    }
    for (const char* key : {"time", "colon", "dot"}) images.Set(key, out.Png(std::string("hud_") + key + ".png", raw[key]));
    // Timer tint: GetHudSlotIdColor maps slot zero to table entry one; average the first two
    // colours (truncating), then lighten the bottom vertices by (40,130,40), saturating.
    double top[3], bottom[3];
    for (int i = 0; i < 3; ++i) {
        const int sum = palette[1][0][i] + palette[1][1][i];
        top[i] = double(sum >= 0 ? sum / 2 : -((-sum + 1) / 2));
        const int delta[3] = {40, 130, 40};
        bottom[i] = std::min(top[i] + delta[i], 255.0);
    }
    std::vector<std::string> goldKeys = {"time", "colon", "dot"};
    for (int digit = 0; digit < 10; ++digit) goldKeys.push_back("digit_" + std::to_string(digit));
    for (const auto& key : goldKeys) {
        const std::string file = key.compare(0, 6, "digit_") == 0 ? "hud_digit_" + key.substr(6) + "_gold.png" : "hud_" + key + "_gold.png";
        images.Set(key + "_gold", out.Png(file, TimerTint(raw[key], top, bottom)));
    }
    for (const auto& [key, stem] : {std::pair<const char*, const char*>{"searchlight", "tt_map_chara_searchlight"},
                                    {"map_line", "tt_race_chara_name_line_gray_map_ver00"}})
        images.Set(key, out.Png(std::string("hud_") + key + ".png", DecodeTpl(Member(race, std::string("game_image/timg/") + stem + ".tpl"))));
    Json digits = Json::Array(), rawDigits = Json::Array();
    for (int i = 0; i < 10; ++i) {
        digits.Push(Json::Str("hud_digit_" + std::to_string(i) + "_gold.png"));
        rawDigits.Push(Json::Str("hud_digit_" + std::to_string(i) + ".png"));
    }
    Json timer = Json::Object().Set("label", Json::Str("hud_time_gold.png")).Set("digits", std::move(digits))
        .Set("colon", Json::Str("hud_colon_gold.png")).Set("dot", Json::Str("hud_dot_gold.png"))
        .Set("raw", Json::Object().Set("label", Json::Str("hud_time.png")).Set("digits", std::move(rawDigits))
            .Set("colon", Json::Str("hud_colon.png")).Set("dot", Json::Str("hud_dot.png")));
    Json faces = Json::Array();
    for (int i = 0; i < 24; ++i) {
        const std::string stem = std::string(kFaces[i]) == "fuky" ? "funky" : kFaces[i];
        char name[32];
        std::snprintf(name, sizeof(name), "hud_face_%02d.png", i);
        faces.Push(out.Png(name, DecodeTpl(Member(race, "result/timg/tt_" + stem + "_64x64.tpl"))).Set("id", Json::Num(i)));
    }
    const auto messages = Messages(Member(raceE, "message/Race.bmg"));
    Json ranks = Json::Array();
    for (int i = 0; i < 12; ++i) {
        const auto it = messages.find(0x515 + uint32_t(i));
        if (it == messages.end()) Fail("The disc's race messages are incomplete.");
        std::string text;
        for (char16_t c : it->second) {
            if (!font.glyphs.count(c)) Fail("Unmapped retail rank glyph");
            text += Utf8(c);
        }
        ranks.Push(Json::Object().Set("rank", Json::Num(i + 1)).Set("text", Json::Str(text))
            .Set("messageId", Json::Num(0x515 + i)).Set("font", Json::Str("main")));
    }
    hud.Set("fonts", std::move(fonts)).Set("rows", std::move(rows)).Set("timer", std::move(timer))
        .Set("images", std::move(images)).Set("faces", std::move(faces)).Set("ranks", std::move(ranks))
        .Set("builtBy", Json::Str("WiiDS on-device art builder"));
    WriteText(outDir + "/hud.json", hud.Dump() + "\n");

    // ---- Wii-menu chrome
    struct Candidate { const char* role; const Archive* archive; const char* layout; std::vector<std::string> clips; bool bounded; };
    const std::vector<Candidate> candidates = {
        {"background", &menuOther, "bg/blyt/bg.brlyt", {"bg_Loop.brlan"}, true},
        {"panel", &menuOther, "bg/blyt/option_bg.brlyt", {"option_bg_Loop.brlan"}, true},
        {"header", &menuOther, "bg/blyt/obi_top.brlyt", {}, false},
        {"footer", &menuOther, "bg/blyt/obi_bottom.brlyt", {}, false},
        {"button", &menuSingle, "button/blyt/common_w102_option_menu.brlyt", {}, false},
        {"button_focus", &menuSingle, "button/blyt/common_w102_option_menu.brlyt", {}, false},
    };
    const ControllerBindings bindings = ControllerAnimations(Member(menuSingle, "button/ctrl/OptionSelectButton.brctr"));
    Json menuImages = Json::Object();
    std::map<std::string, Image> menuPixels;
    for (const Candidate& c : candidates) {
        LayoutRequest request;
        request.layout = c.layout;
        const std::string folder = std::string(c.layout).substr(0, std::string(c.layout).find('/'));
        for (const auto& clip : c.clips) request.animations.push_back(folder + "/anim/" + clip);
        if (c.bounded) {
            request.hasBounds = true;
            const double bounds[4] = {-416, -228, 416, 228};
            std::memcpy(request.bounds, bounds, sizeof(bounds));
        }
        const std::string role = c.role;
        if (role.compare(0, 6, "button") == 0) {
            const bool focus = role == "button_focus";
            const std::map<std::string, std::string> wanted = {{"Select", focus ? "Select" : "Free"}, {"Loop", "Loop"},
                                                               {"SelectIn", "SelectStop"}, {"OK", "OKStop"}};
            for (const auto& group : bindings.groups) {
                const auto want = wanted.find(group.name);
                if (want == wanted.end()) continue;
                for (const auto& entry : group.animations)
                    if (entry.name == want->second) {
                        request.animations.push_back(folder + "/anim/" + entry.file);
                        request.animationGroups.push_back(group.panes);
                    }
            }
            const auto& slot = palette[focus ? 1 : 5];
            std::array<std::array<double, 4>, 3> fuchi{}, base{};
            for (int k = 0; k < 4; ++k) {
                fuchi[0][k] = slot[0][k]; fuchi[1][k] = slot[1][k]; fuchi[2][k] = 255;
                base[0][k] = slot[2][k]; base[1][k] = slot[2][k]; base[2][k] = 255;
            }
            request.materialOverrides["fuchi_pattern"] = fuchi;
            request.materialOverrides["color_base"] = base;
        }
        double used[4];
        const Image image = RenderLayout(*c.archive, request, used);
        Json record = out.Png("menu_" + role + ".png", image);
        Json bounds = Json::Array();
        for (double b : used) bounds.Push(Json::Real(b));
        record.Set("sourceBounds", std::move(bounds)).Set("source", Json::Str(c.layout));
        menuImages.Set(role, std::move(record));
        menuPixels[role] = image;
    }
    {
        // The button_selected role reuses the focus art.
        Json selected = out.Png("menu_button_focus.png", menuPixels["button_focus"]);
        menuImages.Set("button_selected", std::move(selected));
    }
    // Native settings sidebar: raw RGBA + table (runtime/src/thor_settings_assets.h).
    const std::string settings = outDir + "/thor_settings";
    fs::create_directories(fs::u8path(settings));
    std::string table = "MKW_SETTINGS 1\n";
    for (const char* role : {"background", "panel", "header", "footer", "button", "button_focus"}) {
        const Image& image = menuPixels[role];
        WriteFile(settings + "/" + role + ".rgba", image.rgba.data(), image.rgba.size());
        table += std::string("texture ") + role + " " + std::to_string(image.width) + " " + std::to_string(image.height) + " " + role + ".rgba\n";
    }
    WriteFile(settings + "/font.rgba", font.atlas.rgba.data(), font.atlas.rgba.size());
    table += "font " + std::to_string(font.atlas.width) + " " + std::to_string(font.atlas.height) + " " +
             std::to_string(font.lineHeight) + " " + std::to_string(font.ascent) + " font.rgba\n";
    for (const auto& [code, g] : font.glyphs)
        table += "glyph " + std::to_string(code) + " " + std::to_string(g.x) + " " + std::to_string(g.y) + " " +
                 std::to_string(g.w) + " " + std::to_string(g.h) + " " + std::to_string(g.left) + " " + std::to_string(g.advance) + "\n";
    WriteText(settings + "/settings.tsv", table);
    Json canvas = Json::Object().Set("width", Json::Num(832)).Set("height", Json::Num(456));
    Json menu = Json::Object().Set("schema", Json::Num(1)).Set("kind", Json::Str("private-retail-wii-menu"))
        .Set("images", std::move(menuImages)).Set("canvas", std::move(canvas)).Set("fontManifest", Json::Str("hud.json"))
        .Set("builtBy", Json::Str("WiiDS on-device art builder"));
    // The menu font is the HUD font (ThorMenuArtwork reads hud.json).
    WriteText(outDir + "/menu.json", menu.Dump() + "\n");
    return "Mario Kart HUD, menu, faces and items built";
}

} // namespace wiids::art
