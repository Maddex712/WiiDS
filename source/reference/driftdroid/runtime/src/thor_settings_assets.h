#pragma once

// Private, owner-extracted MKW pixels. No Nintendo assets are compiled into this
// runtime. The extraction receipt accompanies ThorMap/thor_settings/settings.tsv.
namespace thor_settings_assets {
struct Image {
    int width = 0, height = 0;
    ImTextureID texture = 0;
    std::vector<unsigned char> rgba;
};
struct Glyph {
    unsigned code = 0;
    int x = 0, y = 0, width = 0, height = 0, rect = -1;
    float left = 0, advance = 0;
};
inline std::map<std::string, Image> images;
inline ImFont* font = nullptr;
inline bool ready = false;

inline bool ReadImage(const std::filesystem::path& folder, const std::string& file, Image& image) {
    if (image.width <= 0 || image.height <= 0 || image.width > 4096 || image.height > 4096 ||
        file.empty() || file.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") !=
                            std::string::npos || std::filesystem::path(file).extension() != ".rgba") {
        return false;
    }
    std::ifstream stream(folder / file, std::ios::binary | std::ios::ate);
    const size_t bytes = static_cast<size_t>(image.width) * image.height * 4;
    if (!stream || stream.tellg() != static_cast<std::streamoff>(bytes)) return false;
    image.rgba.resize(bytes);
    stream.seekg(0);
    return static_cast<bool>(stream.read(reinterpret_cast<char*>(image.rgba.data()), bytes));
}

inline bool Load(const std::filesystem::path& folder) {
    std::ifstream manifest(folder / "settings.tsv");
    std::string magic;
    unsigned schema = 0;
    if (!(manifest >> magic >> schema) || magic != "MKW_SETTINGS" || schema != 1) return false;
    Image atlas;
    int lineHeight = 0, ascent = 0;
    std::vector<Glyph> glyphs;
    std::map<std::string, Image> loaded;
    std::string line;
    std::getline(manifest, line);
    while (std::getline(manifest, line)) {
        std::istringstream fields(line);
        std::string kind, key, file;
        if (!(fields >> kind) || kind[0] == '#') continue;
        if (kind == "texture") {
            Image image;
            if (!(fields >> key >> image.width >> image.height >> file) ||
                loaded.size() >= 16 || loaded.contains(key) || !ReadImage(folder, file, image)) return false;
            loaded.emplace(key, std::move(image));
        } else if (kind == "font") {
            if (!atlas.rgba.empty() || !(fields >> atlas.width >> atlas.height >> lineHeight >> ascent >> file) ||
                !ReadImage(folder, file, atlas)) return false;
        } else if (kind == "glyph") {
            Glyph glyph;
            if (!(fields >> glyph.code >> glyph.x >> glyph.y >> glyph.width >> glyph.height >> glyph.left >> glyph.advance) ||
                glyph.code > 0xFFFE || glyph.width <= 0 || glyph.height <= 0 || glyph.x < 0 || glyph.y < 0 ||
                !std::isfinite(glyph.left) || !std::isfinite(glyph.advance) || glyph.advance < 0) return false;
            glyphs.push_back(glyph);
        } else {
            return false;
        }
    }
    if (atlas.rgba.empty() || lineHeight <= 0 || lineHeight > 128 || ascent < 0 || ascent > lineHeight ||
        glyphs.size() < 95 || glyphs.size() > 1024 || !loaded.contains("button") ||
        !loaded.contains("button_focus") || !loaded.contains("panel") ||
        !loaded.contains("header") || !loaded.contains("footer")) return false;
    std::array<bool, 65536> seen{};
    for (const auto& glyph : glyphs) {
        if (seen[glyph.code] || glyph.width > atlas.width || glyph.height > atlas.height ||
            glyph.x > atlas.width - glyph.width || glyph.y > atlas.height - glyph.height) return false;
        seen[glyph.code] = true;
    }
    for (unsigned code = 32; code <= 126; ++code) if (!seen[code]) return false;

    ImGuiIO& io = ImGui::GetIO();
    // Seed only an invisible space: every visible glyph comes from the original
    // BRFNT, including its intrinsic outline. No visible ImGui/system fallback.
    static const ImWchar seedRange[] = {32, 32, 0};
    ImFontConfig config;
    config.SizePixels = static_cast<float>(lineHeight);
    config.GlyphRanges = seedRange;
    config.OversampleH = config.OversampleV = 1;
    ImFormatString(config.Name, IM_ARRAYSIZE(config.Name), "MKW original bitmap font");
    io.Fonts->Clear();
    font = io.Fonts->AddFontDefault(&config);
    for (auto& glyph : glyphs) {
        glyph.rect = io.Fonts->AddCustomRectFontGlyph(font, static_cast<ImWchar>(glyph.code), glyph.width + 2,
            glyph.height + 2, glyph.advance, ImVec2(glyph.left - 1, -1));
        io.Fonts->GetCustomRectByIndex(glyph.rect)->GlyphColored = true;
    }
    if (!io.Fonts->Build()) return false;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0) return false;
    for (const auto& glyph : glyphs) {
        const ImFontAtlasCustomRect* rect = io.Fonts->GetCustomRectByIndex(glyph.rect);
        if (!rect || !rect->IsPacked()) return false;
        // Transparent packing gutters prevent neighbouring glyphs bleeding
        // during bilinear sampling; original glyph pixels/metrics are unchanged.
        for (int row = 0; row < glyph.height + 2; ++row) {
            std::memset(pixels + (static_cast<size_t>(rect->Y + row) * width + rect->X) * 4, 0,
                        static_cast<size_t>(glyph.width + 2) * 4);
        }
        for (int row = 0; row < glyph.height; ++row) {
            std::memcpy(pixels + (static_cast<size_t>(rect->Y + row + 1) * width + rect->X + 1) * 4,
                atlas.rgba.data() + (static_cast<size_t>(glyph.y + row) * atlas.width + glyph.x) * 4,
                static_cast<size_t>(glyph.width) * 4);
        }
        if (ImFontGlyph* output = const_cast<ImFontGlyph*>(font->FindGlyphNoFallback(static_cast<ImWchar>(glyph.code)))) {
            output->Colored = true;
        }
    }
    io.Fonts->TexPixelsUseColors = true;
    font->FallbackChar = '?';
    font->FallbackGlyph = font->FindGlyphNoFallback('?');
    font->Ascent = static_cast<float>(ascent);
    font->Descent = static_cast<float>(ascent - lineHeight);
    font->Scale = 0.65f;
    io.FontDefault = font;
    io.FontGlobalScale = 1.0f;
    images = std::move(loaded);
    ready = true;
    return true;
}

inline ImTextureID Texture(const char* key) {
    const auto entry = images.find(key);
    if (entry == images.end()) return 0;
    Image& image = entry->second;
    if (!image.texture && !image.rgba.empty()) {
        image.texture = aurora_imgui_add_texture(image.width, image.height, image.rgba.data());
        image.rgba.clear();
        image.rgba.shrink_to_fit();
    }
    return image.texture;
}

inline void DrawImage(const char* key, ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                      float rounding = 0) {
    if (const ImTextureID texture = Texture(key)) {
        const ImU32 color = ImGui::GetColorU32(ImVec4(1, 1, 1, 1));
        if (rounding > 0)
            draw->AddImageRounded(texture, min, max, ImVec2(0, 0), ImVec2(1, 1), color, rounding);
        else
            draw->AddImage(texture, min, max, ImVec2(0, 0), ImVec2(1, 1), color);
    }
}

inline void DrawButton(const char* key, ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                       float rounding) {
    const auto entry = images.find(key);
    if (entry == images.end()) return;
    const Image& image = entry->second;
    const ImTextureID texture = Texture(key);
    if (!texture || max.x <= min.x || max.y <= min.y) return;
    // Adapt the original pixels with proportional end caps. Stretching only
    // the middle keeps the authored side bevel narrow on wide settings rows.
    const float sourceCap = std::min(image.height * 0.5f, image.width * 0.25f);
    const float cap = sourceCap * (max.y - min.y) / image.height;
    const float left = std::round(min.x + cap), right = std::round(max.x - cap);
    if (right <= left || left - min.x <= 2 || max.x - right <= 2) {
        DrawImage(key, draw, min, max, rounding);
        return;
    }
    const float u = sourceCap / image.width;
    const ImU32 color = ImGui::GetColorU32(ImVec4(1, 1, 1, 1));
    const ImVec2 clipMin = draw->GetClipRectMin(), clipMax = draw->GetClipRectMax();
    // Extend each cap beyond its internal scissor edge. The rounded geometry's
    // antialias fringe then lies outside the join, avoiding doubled-alpha seams.
    constexpr float overlap = 2.0f;
    draw->PushClipRect(clipMin, ImVec2(std::min(left, clipMax.x), clipMax.y), true);
    draw->AddImageRounded(texture, min, ImVec2(left + overlap, max.y), ImVec2(0, 0),
        ImVec2(u * (1.0f + overlap / (left - min.x)), 1), color, rounding, ImDrawFlags_RoundCornersLeft);
    draw->PopClipRect();
    draw->AddImage(texture, ImVec2(left, min.y), ImVec2(right, max.y), ImVec2(u, 0), ImVec2(1 - u, 1), color);
    draw->PushClipRect(ImVec2(std::max(right, clipMin.x), clipMin.y), clipMax, true);
    draw->AddImageRounded(texture, ImVec2(right - overlap, min.y), max,
        ImVec2(1 - u * (1.0f + overlap / (max.x - right)), 0), ImVec2(1, 1), color, rounding,
        ImDrawFlags_RoundCornersRight);
    draw->PopClipRect();
}
} // namespace thor_settings_assets
