// DriftDroid's Switch launcher: install the game (pick and unpack a disc image) and browse the SD
// card. Runs on aurora's frame loop after the graphics start and before the guest does, drawing
// with ImGui and the Switch system fonts. Layout is in 1280x720 reference units scaled to the
// actual surface, so docked (1080p) and handheld (720p) look the same.
#if defined(__SWITCH__)

#include "switch_launcher.h"

#include <switch.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "retro_rewind_download.h"
#include "runtime_config.h"
#include "runtime_product.h"
#include "switch_mii_data.h"
#include "switch_mods.h"
#include "switch_rom_import.h"
#include "zip_extract.h"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using SwitchImport::DataState;
using SwitchImport::DiscImage;

namespace {

// ---------------------------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------------------------

// The font is baked large and drawn at whatever size is asked for, so one atlas serves both the
// launcher (explicit pixel sizes) and the in-game settings panel (scaled down below).
constexpr float kFontBakePx = 40.0f;
constexpr float kInGameFontScale = 0.4f;  // 16 px: close to the old default font's size
bool g_haveButtonGlyphs = false;

// ---------------------------------------------------------------------------------------------
// Look
// ---------------------------------------------------------------------------------------------

// Palette of the Android launcher (ModePickerActivity.kt): a dark grey checkerboard, grey utility
// buttons and Wii-menu blue for the main action.
constexpr ImU32 kCheckLight = IM_COL32(0x3A, 0x3A, 0x3A, 255);
constexpr ImU32 kCheckDark = IM_COL32(0x22, 0x22, 0x22, 255);
constexpr ImU32 kPanel = IM_COL32(0x3A, 0x3A, 0x3A, 255);
constexpr ImU32 kPanelHi = IM_COL32(0x5A, 0x5A, 0x5A, 255);
constexpr ImU32 kBorder = IM_COL32(0x66, 0x66, 0x66, 255);
constexpr ImU32 kText = IM_COL32(236, 240, 247, 255);
constexpr ImU32 kMuted = IM_COL32(0xB4, 0xB4, 0xB4, 255);
constexpr ImU32 kAccent = IM_COL32(0x1C, 0x5F, 0xD1, 255);
constexpr ImU32 kAccentHi = IM_COL32(0x4F, 0xA6, 0xFF, 255);
constexpr ImU32 kGood = IM_COL32(63, 185, 80, 255);
constexpr ImU32 kWarn = IM_COL32(232, 171, 42, 255);
constexpr ImU32 kBad = IM_COL32(248, 81, 73, 255);
constexpr ImU32 kBadDark = IM_COL32(110, 34, 34, 255);

enum class Tone { Primary, Normal, Danger };

// Per-frame drawing context in reference space.
struct Ctx {
    ImDrawList* dl = nullptr;
    ImFont* font = nullptr;
    float s = 1.0f;      // reference unit -> pixels
    float ox = 0.0f;     // horizontal centring offset (the layout is 16:9)
    ImVec2 display{};
    ImVec2 P(float x, float y) const { return ImVec2(ox + x * s, y * s); }
    ImVec2 Sz(float w, float h) const { return ImVec2(w * s, h * s); }
};
Ctx g;

// ---------------------------------------------------------------------------------------------
// Input. The launcher does its own navigation (ImGui's gamepad navigation is switched off while it
// is open): the D-pad or left stick moves the highlight, A activates, B goes back.
// ---------------------------------------------------------------------------------------------

enum Dir { kUp, kDown, kLeft, kRight };

struct PadInput {
    bool dpad[4] = {};
    bool stick[4] = {};
    bool held[4] = {};
    bool edge[4] = {};
    Clock::time_point nextRepeat[4] = {};
    bool aEdge = false;
    bool bEdge = false;
    bool xEdge = false;
    bool yEdge = false;
    // Resolved once per frame.
    int moveX = 0;
    int moveY = 0;
    bool activate = false;
    bool back = false;
    bool x = false;
    bool y = false;
    // Presses in the first moment after start are ignored: A is usually still held from hbmenu.
    Clock::time_point armedAt{};
};
PadInput g_in;

void UpdateHeld(Dir d) {
    const bool now = g_in.dpad[d] || g_in.stick[d];
    if (now && !g_in.held[d]) {
        g_in.edge[d] = true;
    }
    g_in.held[d] = now;
}

// Called for every SDL event while the launcher is open.
void FeedInput(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        switch (event.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: g_in.dpad[kUp] = down; UpdateHeld(kUp); break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: g_in.dpad[kDown] = down; UpdateHeld(kDown); break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: g_in.dpad[kLeft] = down; UpdateHeld(kLeft); break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: g_in.dpad[kRight] = down; UpdateHeld(kRight); break;
        case SDL_GAMEPAD_BUTTON_SOUTH: if (down) g_in.aEdge = true; break;  // A
        case SDL_GAMEPAD_BUTTON_EAST: if (down) g_in.bEdge = true; break;   // B
        case SDL_GAMEPAD_BUTTON_WEST: if (down) g_in.xEdge = true; break;   // X
        case SDL_GAMEPAD_BUTTON_NORTH: if (down) g_in.yEdge = true; break;  // Y
        default: break;
        }
    } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
               (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)) {
        // Hysteresis so a stick resting near the threshold does not flutter.
        constexpr int kOn = 18000;
        constexpr int kOff = 11000;
        const int v = event.gaxis.value;
        const bool horizontal = event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX;
        const Dir negative = horizontal ? kLeft : kUp;
        const Dir positive = horizontal ? kRight : kDown;
        g_in.stick[negative] = g_in.stick[negative] ? v < -kOff : v < -kOn;
        g_in.stick[positive] = g_in.stick[positive] ? v > kOff : v > kOn;
        UpdateHeld(negative);
        UpdateHeld(positive);
    }
}

// Turns this frame's presses (and held-key repeats) into one move and the A/B flags.
void ResolveInput() {
    const auto now = Clock::now();
    g_in.moveX = 0;
    g_in.moveY = 0;
    const bool armed = now >= g_in.armedAt;
    for (int d = 0; d < 4; ++d) {
        bool step = false;
        if (g_in.edge[d]) {
            step = true;
            g_in.nextRepeat[d] = now + std::chrono::milliseconds(380);
        } else if (g_in.held[d] && now >= g_in.nextRepeat[d]) {
            step = true;
            g_in.nextRepeat[d] = now + std::chrono::milliseconds(90);
        }
        g_in.edge[d] = false;
        if (!step || !armed) {
            continue;
        }
        switch (d) {
        case kUp: --g_in.moveY; break;
        case kDown: ++g_in.moveY; break;
        case kLeft: --g_in.moveX; break;
        default: ++g_in.moveX; break;
        }
    }
    g_in.activate = armed && g_in.aEdge;
    g_in.back = armed && g_in.bEdge;
    g_in.x = armed && g_in.xEdge;
    g_in.y = armed && g_in.yEdge;
    g_in.aEdge = false;
    g_in.bEdge = false;
    g_in.xEdge = false;
    g_in.yEdge = false;
}

float TextWidth(float px, const char* text) {
    return g.font->CalcTextSizeA(px * g.s, FLT_MAX, 0.0f, text).x;
}

void Text(float x, float y, float px, ImU32 color, const char* text) {
    g.dl->AddText(g.font, px * g.s, g.P(x, y), color, text);
}

void TextCentered(float centerX, float y, float px, ImU32 color, const char* text) {
    Text(centerX - TextWidth(px, text) / g.s * 0.5f, y, px, color, text);
}

void TextRight(float rightX, float y, float px, ImU32 color, const char* text) {
    Text(rightX - TextWidth(px, text) / g.s, y, px, color, text);
}

// Shortens text with "..." so it fits maxWidth (reference units). keepEnd keeps the tail of a path.
std::string Fit(float px, const std::string& text, float maxWidth, bool keepEnd) {
    if (TextWidth(px, text.c_str()) / g.s <= maxWidth) {
        return text;
    }
    std::string s = text;
    while (s.size() > 4) {
        if (keepEnd) {
            size_t cut = 1;
            while (cut < s.size() && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
                ++cut;
            }
            s.erase(0, cut);
        } else {
            size_t end = s.size() - 1;
            while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) {
                --end;
            }
            s.erase(end);
        }
        const std::string candidate = keepEnd ? "..." + s : s + "...";
        if (TextWidth(px, candidate.c_str()) / g.s <= maxWidth) {
            return candidate;
        }
    }
    return keepEnd ? "..." + s : s + "...";
}

std::string FormatBytes(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1'000'000'000ull) {
        std::snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(bytes) / 1e9);
    } else if (bytes >= 1'000'000ull) {
        std::snprintf(buf, sizeof(buf), "%.0f MB", static_cast<double>(bytes) / 1e6);
    } else if (bytes >= 1'000ull) {
        std::snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1e3);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return buf;
}

std::string ShortPath(const std::string& path) {
    return path.rfind("sdmc:", 0) == 0 ? path.substr(5) : path;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Nintendo's controller glyphs live in the system "extended" font; without it, plain letters.
const char* Glyph(char button) {
    if (g_haveButtonGlyphs) {
        switch (button) {
        case 'A': return "\xEE\x83\xA0";
        case 'B': return "\xEE\x83\xA1";
        case 'X': return "\xEE\x83\xA2";
        case 'Y': return "\xEE\x83\xA3";
        case 'L': return "\xEE\x83\xA4";
        default: break;
        }
    }
    static char fallback[2][2] = {};
    fallback[0][0] = button;
    return fallback[0];
}

// ---------------------------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------------------------

// The Android launcher's background: a dark checkerboard drifting slowly down and to the right.
void DrawBackground() {
    constexpr float kTile = 42.0f;         // reference units (28dp on Android)
    constexpr float kTilesPerSecond = 0.1f;
    static const Clock::time_point start = Clock::now();
    const float seconds = std::chrono::duration<float>(Clock::now() - start).count();
    const float tile = kTile * g.s;
    const float period = tile * 2.0f;
    const float offX = std::fmod(seconds * kTilesPerSecond * tile, period);
    const float offY = std::fmod(seconds * kTilesPerSecond * tile * 0.6f, period);
    g.dl->AddRectFilled(ImVec2(0, 0), g.display, kCheckDark);
    // Filled as anti-aliased polygons, not plain rectangles: a plain rectangle's edge snaps to whole
    // pixels, and at this speed (a few pixels a second) that showed as the pattern creeping forward
    // in visible single-pixel jumps, at different moments across and down. The blended edge moves
    // by fractions of a pixel.
    const ImDrawListFlags savedFlags = g.dl->Flags;
    g.dl->Flags |= ImDrawListFlags_AntiAliasedFill;
    int row = 0;
    for (float y = offY - period; y < g.display.y; y += tile, ++row) {
        // Light squares sit where row and column have the same parity.
        for (float x = offX - period + ((row & 1) != 0 ? tile : 0.0f); x < g.display.x; x += period) {
            g.dl->PathRect(ImVec2(x, y), ImVec2(x + tile, y + tile));
            g.dl->PathFillConvex(kCheckLight);
        }
    }
    g.dl->Flags = savedFlags;
}

// Banner art shared with the Android launcher (res/drawable-*), resized and stored as raw RGBA so
// no image decoder is needed here.
struct Art {
    ImTextureID texture = 0;
    uint32_t width;
    uint32_t height;
    const unsigned char* pixels;
};
constexpr unsigned char kLogoPixels[] = {
#embed "../assets/switch_launcher/logo_720x134.rgba"
};
constexpr unsigned char kBaseBannerPixels[] = {
#embed "../assets/switch_launcher/channel_mkwii_840x192.rgba"
};
constexpr unsigned char kRetroBannerPixels[] = {
#embed "../assets/switch_launcher/channel_retro_rewind_840x192.rgba"
};
Art g_logo{0, 720, 134, kLogoPixels};
Art g_baseBanner{0, 840, 192, kBaseBannerPixels};
Art g_retroBanner{0, 840, 192, kRetroBannerPixels};

ImTextureID ArtTexture(Art& art) {
    if (art.texture == 0) {
        art.texture = aurora_imgui_add_texture(art.width, art.height, art.pixels);
    }
    return art.texture;
}

// Title block shared by every screen.
void DrawHeader(const char* title, const char* subtitle) {
    Text(64, 40, 40, kText, title);
    g.dl->AddRectFilled(g.P(64, 94), g.P(64 + 56, 98), kAccent, 2.0f * g.s);
    if (subtitle != nullptr && subtitle[0] != 0) {
        Text(64, 106, 22, kMuted, subtitle);
    }
}

struct Hint {
    char button;
    const char* label;
};

void DrawFooter(const std::vector<Hint>& hints, const std::string& rightText) {
    g.dl->AddLine(g.P(64, 650), g.P(1216, 650), kBorder, 1.0f * g.s);
    float x = 64;
    for (const Hint& hint : hints) {
        Text(x, 664, 26, kText, Glyph(hint.button));
        x += TextWidth(26, Glyph(hint.button)) / g.s + 10;
        Text(x, 667, 20, kMuted, hint.label);
        x += TextWidth(20, hint.label) / g.s + 34;
    }
    if (!rightText.empty()) {
        TextRight(1216, 667, 20, kMuted, rightText.c_str());
    }
}

// Selection helpers; defined after the launcher state below.
int NextItem(bool enabled);
int SelectedItem();

// A large selectable button. Returns true when activated (A button or touch).
bool Button(const char* id, const char* label, const char* sub, float x, float y, float w, float h, Tone tone,
            bool enabled = true, bool /*unused*/ = false) {
    const int index = NextItem(enabled);
    ImGui::SetCursorScreenPos(g.P(x, y));
    if (!enabled) {
        ImGui::BeginDisabled();
    }
    const bool clicked = ImGui::InvisibleButton(id, g.Sz(w, h));
    if (!enabled) {
        ImGui::EndDisabled();
    }
    const bool selected = enabled && index == SelectedItem();
    const bool pressed = enabled && (clicked || (g_in.activate && selected));
    const bool active = selected || (enabled && ImGui::IsItemActive());
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    const float radius = 16.0f * g.s;

    ImU32 fill = kPanel;
    ImU32 textColor = kText;
    ImU32 subColor = kMuted;
    if (!enabled) {
        fill = IM_COL32(0x2A, 0x2A, 0x2A, 255);
        textColor = IM_COL32(0x77, 0x77, 0x77, 255);
        subColor = IM_COL32(0x66, 0x66, 0x66, 255);
    } else if (tone == Tone::Primary) {
        fill = active ? kAccentHi : kAccent;
        subColor = IM_COL32(222, 235, 255, 255);
    } else if (tone == Tone::Danger) {
        fill = active ? kBad : kBadDark;
        subColor = IM_COL32(255, 214, 210, 255);
    } else {
        fill = active ? kPanelHi : kPanel;
    }
    g.dl->AddRectFilled(a, b, fill, radius);
    if (active) {
        g.dl->AddRect(ImVec2(a.x - 3 * g.s, a.y - 3 * g.s), ImVec2(b.x + 3 * g.s, b.y + 3 * g.s),
                      tone == Tone::Danger ? kBad : IM_COL32(255, 255, 255, 235), radius + 3 * g.s, 0, 3.0f * g.s);
    } else if (enabled && tone == Tone::Normal) {
        g.dl->AddRect(a, b, kBorder, radius, 0, 1.0f * g.s);
    }
    const bool hasSub = sub != nullptr && sub[0] != 0;
    const float labelY = hasSub ? y + h * 0.5f - 31 : y + h * 0.5f - 15;
    Text(x + 28, labelY, 30, textColor, label);
    if (hasSub) {
        Text(x + 28, y + h * 0.5f + 6, 20, subColor, sub);
    }
    return pressed;
}

// A game banner, Wii-channel style: the art fills the button, rounded. Locked banners are dimmed
// and carry a label instead. Returns true when activated.
bool BannerButton(const char* id, Art& art, float x, float y, float w, float h, const char* lockedLabel) {
    const int index = NextItem(true);
    ImGui::SetCursorScreenPos(g.P(x, y));
    const bool clicked = ImGui::InvisibleButton(id, g.Sz(w, h));
    const bool selected = index == SelectedItem();
    const bool pressed = clicked || (g_in.activate && selected);
    const bool held = ImGui::IsItemActive();
    // The selected banner grows a little; a held one squashes like the Android press bounce.
    const float grow = held ? -0.03f : (selected ? 0.025f : 0.0f);
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const ImVec2 pad((mx.x - mn.x) * grow * 0.5f, (mx.y - mn.y) * grow * 0.5f);
    const ImVec2 a(mn.x - pad.x, mn.y - pad.y);
    const ImVec2 b(mx.x + pad.x, mx.y + pad.y);
    const float radius = 22.0f * g.s;
    g.dl->AddRectFilled(ImVec2(a.x, a.y + 5 * g.s), ImVec2(b.x, b.y + 5 * g.s), IM_COL32(0, 0, 0, 90), radius);
    g.dl->AddImageRounded(ArtTexture(art), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, radius);
    if (lockedLabel != nullptr) {
        g.dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 175), radius);
        const float px = 28.0f;
        const float cx = ((a.x + b.x) * 0.5f - g.ox) / g.s;
        const float cy = (a.y + b.y) * 0.5f / g.s - px * 0.5f - 2;
        TextCentered(cx + 1.5f, cy + 2, px, IM_COL32(0, 0, 0, 200), lockedLabel);
        TextCentered(cx, cy, px, IM_COL32_WHITE, lockedLabel);
    }
    if (selected || held) {
        g.dl->AddRect(ImVec2(a.x - 3 * g.s, a.y - 3 * g.s), ImVec2(b.x + 3 * g.s, b.y + 3 * g.s),
                      IM_COL32(255, 255, 255, 240), radius + 3 * g.s, 0, 3.5f * g.s);
    }
    return pressed;
}

// The square "three dots" button beside a banner, as on the Android launcher.
bool DotsButton(const char* id, float x, float y, float w, float h) {
    const int index = NextItem(true);
    ImGui::SetCursorScreenPos(g.P(x, y));
    const bool clicked = ImGui::InvisibleButton(id, g.Sz(w, h));
    const bool selected = index == SelectedItem();
    const bool held = ImGui::IsItemActive();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    const float radius = 16.0f * g.s;
    g.dl->AddRectFilled(ImVec2(a.x, a.y + 4 * g.s), ImVec2(b.x, b.y + 4 * g.s), IM_COL32(0, 0, 0, 90), radius);
    g.dl->AddRectFilledMultiColor(a, b, IM_COL32(0xF2, 0x5C, 0x4C, 255), IM_COL32(0xF2, 0x5C, 0x4C, 255),
                                  IM_COL32(0xD1, 0x2A, 0x1E, 255), IM_COL32(0xD1, 0x2A, 0x1E, 255));
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    for (int i = -1; i <= 1; ++i) {
        g.dl->AddCircleFilled(ImVec2(c.x, c.y + static_cast<float>(i) * 16.0f * g.s), (held ? 4.0f : 5.0f) * g.s,
                              IM_COL32_WHITE);
    }
    if (selected || held) {
        g.dl->AddRect(ImVec2(a.x - 3 * g.s, a.y - 3 * g.s), ImVec2(b.x + 3 * g.s, b.y + 3 * g.s),
                      IM_COL32(255, 255, 255, 240), 3 * g.s, 0, 3.5f * g.s);
    }
    return clicked || (g_in.activate && selected);
}

// A small grey utility button with a top-to-bottom gradient and a centred label.
bool GreyButton(const char* id, const char* label, float x, float y, float w, float h) {
    const int index = NextItem(true);
    ImGui::SetCursorScreenPos(g.P(x, y));
    const bool clicked = ImGui::InvisibleButton(id, g.Sz(w, h));
    const bool selected = index == SelectedItem();
    const bool pressed = clicked || (g_in.activate && selected);
    const bool active = selected || ImGui::IsItemActive();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    const float radius = 14.0f * g.s;
    const ImU32 top = active ? IM_COL32(0x78, 0x78, 0x78, 255) : IM_COL32(0x5A, 0x5A, 0x5A, 255);
    const ImU32 bottom = active ? IM_COL32(0x52, 0x52, 0x52, 255) : IM_COL32(0x3A, 0x3A, 0x3A, 255);
    g.dl->AddRectFilled(ImVec2(a.x, a.y + 4 * g.s), ImVec2(b.x, b.y + 4 * g.s), IM_COL32(0, 0, 0, 80), radius);
    const int firstVertex = g.dl->VtxBuffer.Size;
    g.dl->AddRectFilled(a, b, IM_COL32_WHITE, radius);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(g.dl, firstVertex, g.dl->VtxBuffer.Size, a, ImVec2(a.x, b.y), top,
                                                  bottom);
    if (active) {
        g.dl->AddRect(ImVec2(a.x - 3 * g.s, a.y - 3 * g.s), ImVec2(b.x + 3 * g.s, b.y + 3 * g.s),
                      IM_COL32(255, 255, 255, 235), radius + 3 * g.s, 0, 3.0f * g.s);
    }
    TextCentered(x + w * 0.5f, y + h * 0.5f - 12, 21, active ? IM_COL32_WHITE : IM_COL32(0xDD, 0xDD, 0xDD, 255), label);
    return pressed;
}

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------

enum class Screen { Home, Import, Dialog, Progress, Done, Browse, RetroDialog, RetroProgress, Mods, Mii };

struct BrowseEntry {
    std::string name;
    bool isDir = false;
    bool isUp = false;
    bool isDisc = false;
    uint64_t size = 0;
    DiscImage disc;  // valid when isDisc
};

struct Ui {
    Screen screen = Screen::Home;
    bool focusPending = true;
    // Highlight position: index among this frame's selectable items, in drawing order.
    int sel = 0;
    int itemCounter = 0;                 // items drawn so far this frame
    int itemCount = 0;                   // items on the previous frame's screen (0 = unknown)
    bool selMoved = false;               // the highlight moved this frame: scroll it into view
    bool navHorizontal = false;          // left/right moves the highlight instead of up/down
    std::vector<char> enabled;           // this frame, per item
    std::vector<char> prevEnabled;       // previous frame, used to skip disabled items
    bool starting = false;   // Play pressed: show the splash, then hand over to the game
    int startFrames = 0;
    bool quit = false;

    fs::path dataRoot;
    DataState dataState = DataState::Missing;
    bool retroRewindApp = false;   // RetroRewind.nro is beside this app (base game only)
    bool retroRewindPack = false;  // the RetroRewind6 folder is on the card

    // Import list
    std::vector<DiscImage> images;

    // Confirmation dialog
    Screen dialogBack = Screen::Home;
    DiscImage pendingImage;

    // Install progress / result
    std::unique_ptr<SwitchImport::ImportJob> job;
    DiscImage installing;
    std::deque<std::pair<Clock::time_point, uint64_t>> samples;
    bool resultOk = false;
    std::string resultMessage;

    // Mods
    std::vector<SwitchMods::Mod> mods;
    bool modsLoaded = false;
    bool modsChanged = false;
    std::string modDeleteArmed;  // Y was pressed once on this mod; a second press deletes it
    // Mods copied over as .zip files are unpacked, one at a time, when the screen opens.
    std::unique_ptr<ZipExtract::Job> zipJob;
    std::string zipName;
    std::vector<std::string> zipFailed;

    // Retro Rewind pack download
    std::unique_ptr<RetroRewindDownload::Job> retroJob;
    bool retroResult = false;  // the result screen is showing a download, not a game import
    bool retroStopping = false;
    bool retroNetwork = false;
    NifmRequest retroInternet{};

    // Browser
    std::string cwd = "sdmc:/";
    std::vector<BrowseEntry> entries;
    bool entriesLoaded = false;
    std::string focusName;

    // Transient message above the footer
    std::string message;
    ImU32 messageColor = kWarn;
    Clock::time_point messageUntil{};
};

Ui* g_ui = nullptr;

void Go(Ui& ui, Screen screen) {
    ui.screen = screen;
    ui.focusPending = true;
    ui.sel = 0;
    ui.selMoved = true;
    ui.navHorizontal = screen == Screen::Dialog || screen == Screen::RetroDialog;
}

// Claims the next selectable item slot for this frame.
int NextItem(bool enabled) {
    g_ui->enabled.push_back(enabled ? 1 : 0);
    return g_ui->itemCounter++;
}

int SelectedItem() {
    return g_ui->sel;
}

void Notify(Ui& ui, const std::string& text, ImU32 color = kWarn, int seconds = 5) {
    ui.message = text;
    ui.messageColor = color;
    ui.messageUntil = Clock::now() + std::chrono::seconds(seconds);
}

void DrawMessage(Ui& ui) {
    if (ui.message.empty()) {
        return;
    }
    if (Clock::now() > ui.messageUntil) {
        ui.message.clear();
        return;
    }
    const std::string text = Fit(22, ui.message, 1100, false);
    TextCentered(640, 612, 22, ui.messageColor, text.c_str());
}

std::string FreeSpaceText() {
    // statvfs is a real SD card query; refresh it every couple of seconds, not every frame.
    static std::string cached;
    static Clock::time_point refreshed{};
    const auto now = Clock::now();
    if (cached.empty() || now - refreshed > std::chrono::seconds(2)) {
        const uint64_t free = SwitchImport::FreeSpaceBytes();
        cached = free == UINT64_MAX ? std::string() : "SD card: " + FormatBytes(free) + " free";
        refreshed = now;
    }
    return cached;
}

// Retro Rewind is its own program (its game code is a separate build). The base game's launcher
// starts it by asking the homebrew loader to run it next, and leaves this file behind so it goes
// straight into the game instead of showing a second launcher.
constexpr const char* kRetroRewindNro = "sdmc:/switch/WiiCompiled/RetroRewind.nro";
constexpr const char* kSkipLauncherFlag = "sdmc:/switch/WiiCompiled/Cache/start_directly.flag";
// Where a downloaded pack goes: the first place SwitchFindRetroRewindRoot looks.
constexpr const char* kRetroRewindInstallDir = "sdmc:/switch/WiiCompiled/RetroRewind6";
constexpr const char* kRetroRewindPartialDir = "sdmc:/switch/WiiCompiled/RetroRewind6.partial";
constexpr const char* kRetroRewindDownloadLabel = "Download Retro Rewind (1.8 GB)";

bool FileExists(const char* path) {
    if (FILE* file = std::fopen(path, "rb")) {
        std::fclose(file);
        return true;
    }
    return false;
}

void RefreshData(Ui& ui) {
    ui.dataState = SwitchImport::GetDataState(ui.dataRoot);
    ui.retroRewindPack = !SwitchFindRetroRewindRoot().empty();
    ui.retroRewindApp = !RuntimeProduct::IsRetroRewind() && FileExists(kRetroRewindNro);
}

// Queues RetroRewind.nro to run when this program exits. False when the loader cannot do that.
bool QueueRetroRewind() {
    if (!envHasNextLoad()) {
        return false;
    }
    if (FILE* flag = std::fopen(kSkipLauncherFlag, "wb")) {
        std::fclose(flag);
    }
    char args[256];
    std::snprintf(args, sizeof(args), "\"%s\"", kRetroRewindNro);
    return R_SUCCEEDED(envSetNextLoad(kRetroRewindNro, args));
}

// ---------------------------------------------------------------------------------------------
// Home
// ---------------------------------------------------------------------------------------------

// Laid out like the Android launcher: logo, "Kart Channel", the game banners, then the small grey
// buttons and a status line.
void DrawHome(Ui& ui) {
    const bool retroRewind = RuntimeProduct::IsRetroRewind();
    const bool installed = ui.dataState == DataState::Ready;

    g.dl->AddImage(ArtTexture(g_logo), g.P(640 - 215, 22), g.P(640 + 215, 22 + 80));
    TextCentered(640, 98, 20, IM_COL32(0xDD, 0xDD, 0xDD, 230), "Kart Channel");

    const char* status = "Import your own Mario Kart Wii (PAL) disc image to get started.";
    ImU32 dot = kWarn;
    switch (ui.dataState) {
    case DataState::Ready:
        status = "Mario Kart Wii (PAL) is ready to play.";
        dot = kGood;
        break;
    case DataState::Partial:
        status = "A previous import did not complete. Import again to finish it.";
        break;
    case DataState::Outdated:
        status = "This version needs your game files unpacked again.";
        break;
    case DataState::Missing:
        break;
    }
    if (retroRewind && installed) {
        if (ui.retroRewindPack) {
            status = "Mario Kart Wii (PAL) and the Retro Rewind pack are ready.";
        } else {
            status = "The Retro Rewind pack is not on the SD card yet. Select the banner to download it.";
            dot = kWarn;
        }
    }

    const auto openImport = [&ui] {
        ui.images = SwitchImport::FindDiscImages();
        Go(ui, Screen::Import);
    };

    if (ui.focusPending) {
        ui.sel = 0;  // the first banner: Play, or the way into Import when nothing is installed
    }
    const float bw = 520, bh = 119, bx = 640 - bw * 0.5f;
    float y = 138;
    if (!retroRewind) {
        if (BannerButton("##play", g_baseBanner, bx, y, bw, bh, installed ? nullptr : "Install with ISO/WBFS")) {
            if (installed) {
                ui.starting = true;
            } else {
                openImport();
            }
        }
        y += bh + 16;
        const char* locked = nullptr;
        if (!ui.retroRewindApp) {
            locked = "Needs RetroRewind.nro";
        } else if (!ui.retroRewindPack) {
            locked = kRetroRewindDownloadLabel;
        } else if (!installed) {
            locked = "Install the game first";
        }
        if (BannerButton("##retro", g_retroBanner, bx, y, bw, bh, locked)) {
            if (locked != nullptr) {
                if (!ui.retroRewindApp) {
                    Notify(ui, "Put RetroRewind.nro in /switch/WiiCompiled on the SD card.");
                } else if (!ui.retroRewindPack) {
                    Go(ui, Screen::RetroDialog);
                } else {
                    Notify(ui, "Install Mario Kart Wii first.");
                }
            } else if (QueueRetroRewind()) {
                ui.quit = true;
            } else {
                Notify(ui, "Could not start Retro Rewind from here.");
            }
        }
        if (ui.retroRewindApp && ui.retroRewindPack && DotsButton("##retro-more", bx + bw + 14, y + 12, 64, bh - 24)) {
            Go(ui, Screen::RetroDialog);
        }
        y += bh + 22;
    } else {
        const char* locked = nullptr;
        if (!installed) {
            locked = "Install with ISO/WBFS";
        } else if (!ui.retroRewindPack) {
            locked = kRetroRewindDownloadLabel;
        }
        if (BannerButton("##play", g_retroBanner, bx, y, bw, bh, locked)) {
            if (locked == nullptr) {
                ui.starting = true;
            } else if (!installed) {
                openImport();
            } else {
                Go(ui, Screen::RetroDialog);
            }
        }
        if (ui.retroRewindPack && DotsButton("##retro-more", bx + bw + 14, y + 12, 64, bh - 24)) {
            Go(ui, Screen::RetroDialog);
        }
        y += bh + 22;
    }

    // Two columns of small buttons, as the Android launcher's utility rows; up/down walks them
    // left to right, top to bottom.
    const float gw = 480, gh = 46, gx = 640 - gw * 0.5f, gstep = 54;
    const float half = (gw - 12) * 0.5f, gx2 = gx + half + 12;
    if (GreyButton("##import", installed ? "Reinstall game..." : "Import game...", gx, y, half, gh)) {
        openImport();
    }
    if (GreyButton("##mods", "Mods...", gx2, y, half, gh)) {
        ui.modsLoaded = false;
        Go(ui, Screen::Mods);
    }
    y += gstep;
    if (GreyButton("##mii", "Mii data...", gx, y, half, gh)) {
        Go(ui, Screen::Mii);
    }
    if (GreyButton("##browse", "Browse SD card...", gx2, y, half, gh)) {
        Go(ui, Screen::Browse);
        ui.entriesLoaded = false;
        ui.focusName.clear();
    }
    y += gstep;
    if (GreyButton("##quit", "Quit", gx, y, gw, gh)) {
        ui.quit = true;
    }
    y += gstep + 6;

    // Status line, centred, with the coloured dot in front (hidden while a message shows there).
    if (ui.message.empty()) {
        const std::string text = Fit(19, status, 900, false);
        const float width = TextWidth(19, text.c_str()) / g.s;
        const float sy = std::min(y, 614.0f);
        g.dl->AddCircleFilled(g.P(640 - width * 0.5f - 16, sy + 12), 6.0f * g.s, dot);
        TextCentered(640, sy, 19, IM_COL32(0xDD, 0xDD, 0xDD, 255), text.c_str());
    }
    ui.focusPending = false;
    DrawMessage(ui);
    DrawFooter({{'A', "Select"}}, FreeSpaceText());
}

// ---------------------------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------------------------

// A two-line list row. Returns true when activated.
bool ListRow(const char* id, float width, float height, ImU32 stripe, const char* title, const std::string& detail,
             const char* rightTop, const char* rightBottom, bool /*unused*/) {
    const int index = NextItem(true);
    const bool clicked = ImGui::InvisibleButton(id, g.Sz(width, height));
    const bool selected = index == g_ui->sel;
    if (selected && g_ui->selMoved) {
        ImGui::SetScrollHereY(0.5f);
    }
    const bool pressed = clicked || (g_in.activate && selected);
    const bool active = selected || ImGui::IsItemActive();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    const float radius = 14.0f * g.s;
    g.dl->AddRectFilled(a, b, active ? kPanelHi : kPanel, radius);
    if (active) {
        g.dl->AddRect(ImVec2(a.x - 2 * g.s, a.y - 2 * g.s), ImVec2(b.x + 2 * g.s, b.y + 2 * g.s),
                      IM_COL32(255, 255, 255, 235), radius + 2 * g.s, 0, 3.0f * g.s);
    } else {
        g.dl->AddRect(a, b, kBorder, radius, 0, 1.0f * g.s);
    }
    if (stripe != 0) {
        g.dl->AddRectFilled(ImVec2(a.x + 14 * g.s, a.y + 16 * g.s), ImVec2(a.x + 20 * g.s, b.y - 16 * g.s), stripe,
                            3.0f * g.s);
    }
    const float left = (a.x - g.ox) / g.s + (stripe != 0 ? 40.0f : 28.0f);
    const float top = a.y / g.s;
    const float rightWidth = (rightTop != nullptr ? std::max(TextWidth(24, rightTop), TextWidth(18, rightBottom ? rightBottom : "")) / g.s : 0.0f) + 24;
    const float textWidth = width - (left - (a.x - g.ox) / g.s) - rightWidth - 28;
    const std::string fitTitle = Fit(28, title, textWidth, false);
    Text(left, top + 14, 28, kText, fitTitle.c_str());
    if (!detail.empty()) {
        const std::string fitDetail = Fit(19, detail, textWidth, true);
        Text(left, top + 54, 19, kMuted, fitDetail.c_str());
    }
    const float rightEdge = (b.x - g.ox) / g.s - 26;
    if (rightTop != nullptr) {
        TextRight(rightEdge, top + 16, 24, kText, rightTop);
    }
    if (rightBottom != nullptr) {
        TextRight(rightEdge, top + 54, 18, kMuted, rightBottom);
    }
    return pressed;
}

void StartInstallDialog(Ui& ui, const DiscImage& image, Screen back) {
    ui.pendingImage = image;
    ui.dialogBack = back;
    Go(ui, Screen::Dialog);
}

void DrawImport(Ui& ui) {
    DrawHeader("Import game", "Choose your Mario Kart Wii (PAL) disc image");

    ImGui::SetCursorScreenPos(g.P(64, 134));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 12.0f * g.s));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8.0f * g.s);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, kBorder);
    ImGui::BeginChild("##images", g.Sz(1152, 462), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);

    const float width = 1138;
    bool focusFirst = ui.focusPending;
    if (ui.images.empty()) {
        // Space for the explanation is reserved above the Browse row.
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float baseY = (origin.y) / g.s;
        const float lx = 64 + 8;
        Text(lx, baseY + 2, 28, kText, "No disc images found");
        Text(lx, baseY + 44, 21, kMuted, "Copy your Mario Kart Wii (PAL) .wbfs or .iso file to the SD card,");
        Text(lx, baseY + 74, 21, kMuted, "for example into /wbfs or /switch/WiiCompiled, then open this screen again.");
        Text(lx, baseY + 104, 21, kMuted, "Or use Browse below to pick a file stored anywhere on the card.");
        ImGui::Dummy(g.Sz(width, 148));
    }
    for (size_t i = 0; i < ui.images.size(); ++i) {
        const DiscImage& image = ui.images[i];
        ImGui::PushID(static_cast<int>(i));
        std::string title;
        ImU32 stripe = kBad;
        if (image.Supported()) {
            title = "Mario Kart Wii (PAL)";
            stripe = kGood;
        } else if (image.gameId.empty()) {
            title = "Not a readable Wii disc image";
        } else {
            title = "Unsupported game (ID " + image.gameId + ")";
        }
        const std::string size = FormatBytes(image.totalSize);
        const std::string parts = image.parts.size() > 1 ? std::to_string(image.parts.size()) + " parts" : "";
        if (ListRow("##row", width, 92, stripe, title.c_str(), ShortPath(image.parts[0]), size.c_str(),
                    parts.empty() ? nullptr : parts.c_str(), focusFirst && i == 0)) {
            if (image.Supported()) {
                StartInstallDialog(ui, image, Screen::Import);
            } else {
                Notify(ui, "Only PAL Mario Kart Wii (game ID RMCP01) is supported.");
            }
        }
        ImGui::PopID();
        if (i == 0) {
            focusFirst = false;
        }
    }
    if (ListRow("##browse", width, 92, kAccent, "Browse the SD card...", "Pick a disc image stored somewhere else",
                nullptr, nullptr, focusFirst)) {
        Go(ui, Screen::Browse);
        ui.entriesLoaded = false;
        ui.focusName.clear();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ui.focusPending = false;

    if (g_in.back) {
        Go(ui, Screen::Home);
    }
    DrawMessage(ui);
    DrawFooter({{'A', "Select"}, {'B', "Back"}}, FreeSpaceText());
}

// ---------------------------------------------------------------------------------------------
// Confirmation dialogs
// ---------------------------------------------------------------------------------------------

void BeginInstall(Ui& ui) {
    const uint64_t free = SwitchImport::FreeSpaceBytes();
    // A fresh install needs the whole game's worth; reinstalling overwrites the files already there.
    const bool replacing = ui.dataState != DataState::Missing;
    const uint64_t needed = replacing ? SwitchImport::kReplaceFreeBytes : SwitchImport::kRequiredFreeBytes;
    if (free < needed) {
        ui.resultOk = false;
        ui.resultMessage = "Not enough free space on the SD card: about " + FormatBytes(needed) + " is needed, " +
                           FormatBytes(free) + " is free. Free up some space on the SD card and try again.";
        Go(ui, Screen::Done);
        return;
    }
    ui.installing = ui.pendingImage;
    ui.job = std::make_unique<SwitchImport::ImportJob>();
    const std::string error = ui.job->Start(ui.installing, ui.dataRoot);
    if (!error.empty()) {
        ui.job.reset();
        ui.resultOk = false;
        ui.resultMessage = error;
        Go(ui, Screen::Done);
        return;
    }
    ui.samples.clear();
    ui.samples.emplace_back(Clock::now(), 0);
    Go(ui, Screen::Progress);
}

void DrawDialog(Ui& ui) {
    const float x = 200, y = 150, w = 880, h = 380;
    g.dl->AddRectFilled(g.P(x, y), g.P(x + w, y + h), kPanel, 22.0f * g.s);
    g.dl->AddRect(g.P(x, y), g.P(x + w, y + h), kBorder, 22.0f * g.s, 0, 1.0f * g.s);

    Text(x + 44, y + 40, 38, kText, "Install Mario Kart Wii?");
    const std::string fit1 = Fit(24, ShortPath(ui.pendingImage.parts[0]), w - 88, true);
    Text(x + 44, y + 110, 24, kText, fit1.c_str());
    const std::string line2 = ui.dataState == DataState::Missing
                                  ? "Unpacks to " + ShortPath(ui.dataRoot.string()) + " and uses about 2.7 GB."
                                  : "Unpacks over " + ShortPath(ui.dataRoot.string()) + "; little extra space is needed.";
    const std::string fit2 = Fit(21, line2, w - 88, false);
    Text(x + 44, y + 154, 21, kMuted, fit2.c_str());
    const char* line3 = ui.dataState == DataState::Ready ? "This replaces the game data that is already installed."
                                                         : "It takes about 6 minutes. Keep the console on.";
    Text(x + 44, y + 190, 21, kMuted, line3);

    const float by = y + h - 112;
    if (ui.focusPending) {
        ui.sel = 1;  // Install
    }
    if (Button("##cancel", "Cancel", nullptr, x + 44, by, 380, 76, Tone::Normal)) {
        Go(ui, ui.dialogBack);
    }
    if (Button("##confirm", "Install", nullptr, x + w - 44 - 380, by, 380, 76, Tone::Primary, true,
               ui.focusPending)) {
        BeginInstall(ui);
    }
    ui.focusPending = false;
    if (g_in.back) {
        Go(ui, ui.dialogBack);
    }
    DrawFooter({{'A', "Select"}, {'B', "Cancel"}}, "");
}

// ---------------------------------------------------------------------------------------------
// Install progress and result
// ---------------------------------------------------------------------------------------------

void DrawProgressBar(float x, float y, float w, float h, double fraction) {
    const float radius = h * 0.5f;
    g.dl->AddRectFilled(g.P(x, y), g.P(x + w, y + h), IM_COL32(0x2A, 0x2A, 0x2A, 255), radius * g.s);
    const float fill = static_cast<float>(std::clamp(fraction, 0.0, 1.0)) * w;
    if (fill > 1.0f) {
        g.dl->AddRectFilled(g.P(x, y), g.P(x + std::max(fill, h), y + h), kAccent, radius * g.s);
    }
}

// The big percentage, bar, amounts and speed shared by the import and download screens.
void DrawProgressBody(Ui& ui, uint64_t done, uint64_t total) {
    const double fraction = total != 0 ? std::min(1.0, static_cast<double>(done) / static_cast<double>(total)) : 0.0;

    // Speed over roughly the last five seconds.
    const auto now = Clock::now();
    if (now - ui.samples.back().first >= std::chrono::milliseconds(500)) {
        ui.samples.emplace_back(now, done);
    }
    while (ui.samples.size() > 2 && now - ui.samples[1].first > std::chrono::seconds(5)) {
        ui.samples.pop_front();
    }
    double bytesPerSecond = 0.0;
    if (ui.samples.size() >= 2) {
        const double dt = std::chrono::duration<double>(ui.samples.back().first - ui.samples.front().first).count();
        if (dt > 0.5 && ui.samples.back().second >= ui.samples.front().second) {
            bytesPerSecond = static_cast<double>(ui.samples.back().second - ui.samples.front().second) / dt;
        }
    }

    char percent[16];
    std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(fraction * 100.0));
    TextCentered(640, 190, 84, kText, total != 0 ? percent : "...");
    DrawProgressBar(160, 320, 960, 34, fraction);

    char line[96];
    if (total != 0) {
        std::snprintf(line, sizeof(line), "%s of %s", FormatBytes(done).c_str(), FormatBytes(total).c_str());
    } else {
        std::snprintf(line, sizeof(line), "Preparing...");
    }
    TextCentered(640, 376, 26, kText, line);
    if (bytesPerSecond > 0.0 && total > done) {
        const int eta = static_cast<int>(static_cast<double>(total - done) / bytesPerSecond);
        std::snprintf(line, sizeof(line), "%.1f MB/s  -  about %d:%02d left", bytesPerSecond / 1e6, eta / 60, eta % 60);
        TextCentered(640, 418, 21, kMuted, line);
    }
    TextCentered(640, 540, 21, kWarn, "Keep the console on and leave the SD card in.");
}

void DrawProgress(Ui& ui) {
    DrawHeader("Installing", ShortPath(ui.installing.parts[0]).c_str());
    DrawProgressBody(ui, ui.job ? ui.job->BytesDone() : 0, ui.job ? ui.job->BytesTotal() : 0);

    if (ui.job && ui.job->Finished()) {
        const std::string error = ui.job->Finish();
        ui.job.reset();
        ui.resultOk = error.empty();
        ui.resultMessage = error;
        ui.retroResult = false;
        RefreshData(ui);
        Go(ui, Screen::Done);
    }
    DrawFooter({}, "");
}

// ---------------------------------------------------------------------------------------------
// Retro Rewind pack download
// ---------------------------------------------------------------------------------------------

// Opens what a download needs and nothing else does: sockets, an internet request (so the console
// keeps Wi-Fi up) and no auto-sleep. All of it is closed again when the download ends, because the
// launcher may hand the process to another program, which must not inherit an open session.
void OpenRetroNetwork(Ui& ui) {
    if (ui.retroNetwork) {
        return;
    }
    socketInitializeDefault();
    if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
        if (R_SUCCEEDED(nifmCreateRequest(&ui.retroInternet, true))) {
            nifmRequestSubmit(&ui.retroInternet);
        }
    }
    appletSetAutoSleepDisabled(true);
    ui.retroNetwork = true;
}

void CloseRetroNetwork(Ui& ui) {
    if (!ui.retroNetwork) {
        return;
    }
    appletSetAutoSleepDisabled(false);
    nifmRequestClose(&ui.retroInternet);
    ui.retroInternet = NifmRequest{};
    nifmExit();
    socketExit();
    ui.retroNetwork = false;
}

void BeginRetroDownload(Ui& ui) {
    ui.retroResult = true;
    // A download that was started before already holds most of its space.
    const uint64_t needed = FileExists(kRetroRewindPartialDir) ? (300ull << 20) : RetroRewindDownload::RequiredFreeBytes();
    const uint64_t free = SwitchImport::FreeSpaceBytes();
    if (free < needed) {
        ui.resultOk = false;
        ui.resultMessage = "Not enough free space on the SD card: about " + FormatBytes(needed) + " is needed, " +
                           FormatBytes(free) + " is free. Free up some space on the SD card and try again.";
        Go(ui, Screen::Done);
        return;
    }
    OpenRetroNetwork(ui);
    ui.retroStopping = false;
    ui.retroJob = std::make_unique<RetroRewindDownload::Job>();
    ui.retroJob->Start(kRetroRewindInstallDir, RuntimeConfigFile::ApplicationDataDirectory() / "cacert.pem");
    ui.samples.clear();
    ui.samples.emplace_back(Clock::now(), 0);
    Go(ui, Screen::RetroProgress);
}

void DrawRetroDialog(Ui& ui) {
    const float x = 200, y = 150, w = 880, h = 380;
    g.dl->AddRectFilled(g.P(x, y), g.P(x + w, y + h), kPanel, 22.0f * g.s);
    g.dl->AddRect(g.P(x, y), g.P(x + w, y + h), kBorder, 22.0f * g.s, 0, 1.0f * g.s);

    const bool resume = FileExists(kRetroRewindPartialDir);
    Text(x + 44, y + 40, 38, kText, ui.retroRewindPack ? "Download Retro Rewind again?" : "Download Retro Rewind?");
    const std::string line1 = std::string("Version ") + RetroRewindDownload::PinnedVersion() +
                              ", straight from the official Retro Rewind server.";
    Text(x + 44, y + 110, 24, kText, line1.c_str());
    Text(x + 44, y + 154, 21, kMuted,
         resume ? "Continues the download that was started before."
                : "About 1.8 GB to download; needs Wi-Fi and 2.3 GB free on the SD card.");
    Text(x + 44, y + 190, 21, kMuted, "It can take 10 to 30 minutes. Keep the console on.");
    if (ui.retroRewindPack) {
        Text(x + 44, y + 226, 21, kMuted, "The pack you have now stays in use until the new one is complete.");
    }

    const float by = y + h - 112;
    if (ui.focusPending) {
        ui.sel = 1;  // Download
    }
    if (Button("##cancel", "Cancel", nullptr, x + 44, by, 380, 76, Tone::Normal)) {
        Go(ui, Screen::Home);
    }
    if (Button("##confirm", resume ? "Continue" : "Download", nullptr, x + w - 44 - 380, by, 380, 76, Tone::Primary,
               true, ui.focusPending)) {
        BeginRetroDownload(ui);
    }
    ui.focusPending = false;
    if (g_in.back) {
        Go(ui, Screen::Home);
    }
    DrawFooter({{'A', "Select"}, {'B', "Cancel"}}, FreeSpaceText());
}

void DrawRetroProgress(Ui& ui) {
    DrawHeader("Downloading Retro Rewind", RetroRewindDownload::PinnedVersion());
    DrawProgressBody(ui, ui.retroJob ? ui.retroJob->BytesDone() : 0, ui.retroJob ? ui.retroJob->BytesTotal() : 0);
    if (ui.retroStopping) {
        TextCentered(640, 470, 24, kText, "Stopping...");
    } else if (ui.retroJob && ui.retroJob->Reconnecting()) {
        TextCentered(640, 470, 24, kWarn, "Connection lost. Trying again...");
    }

    if (g_in.back && ui.retroJob && !ui.retroStopping) {
        ui.retroJob->Cancel();
        ui.retroStopping = true;
    }
    if (ui.retroJob && ui.retroJob->Finished()) {
        const std::string error = ui.retroJob->Finish();
        ui.retroJob.reset();
        CloseRetroNetwork(ui);
        ui.resultOk = error.empty();
        ui.resultMessage = error;
        ui.retroResult = true;
        RefreshData(ui);
        Go(ui, Screen::Done);
    }
    DrawFooter(ui.retroStopping ? std::vector<Hint>{} : std::vector<Hint>{{'B', "Stop (progress is kept)"}}, "");
}

void DrawDone(Ui& ui) {
    const bool retro = ui.retroResult;
    DrawHeader(ui.resultOk ? "Installed" : retro ? "Download not finished" : "Install failed", nullptr);

    // Status badge.
    const ImU32 color = ui.resultOk ? kGood : kBad;
    g.dl->AddCircleFilled(g.P(640, 220), 56.0f * g.s, color);
    if (ui.resultOk) {
        g.dl->AddPolyline(std::array<ImVec2, 3>{g.P(614, 222), g.P(634, 244), g.P(670, 198)}.data(), 3,
                          IM_COL32(255, 255, 255, 255), 0, 9.0f * g.s);
    } else {
        g.dl->AddLine(g.P(616, 196), g.P(664, 244), IM_COL32(255, 255, 255, 255), 9.0f * g.s);
        g.dl->AddLine(g.P(664, 196), g.P(616, 244), IM_COL32(255, 255, 255, 255), 9.0f * g.s);
    }
    if (ui.resultOk) {
        TextCentered(640, 296, 34, kText, retro ? "Retro Rewind is ready." : "Mario Kart Wii is ready to play.");
    } else {
        // Wrap the message over two lines.
        std::string message = ui.resultMessage;
        std::string second;
        const size_t maxChars = 78;
        if (message.size() > maxChars) {
            size_t cut = message.rfind(' ', maxChars);
            if (cut == std::string::npos) {
                cut = maxChars;
            }
            second = message.substr(cut + 1);
            message = message.substr(0, cut);
        }
        TextCentered(640, 296, 24, kText, message.c_str());
        if (!second.empty()) {
            const std::string fit = Fit(24, second, 1100, false);
            TextCentered(640, 332, 24, kText, fit.c_str());
        }
    }

    const float bx = 380, bw = 520;
    float by = 392;
    if (retro) {
        // Playing Retro Rewind is the banner's job (from the regular game it means switching
        // programs), so a finished download just goes back to the menu.
        if (!ui.resultOk) {
            if (Button("##retry", "Try again", nullptr, bx, by, bw, 74, Tone::Primary, true, ui.focusPending)) {
                Go(ui, Screen::RetroDialog);
            }
            by += 86;
        }
        if (Button("##menu", "Back to menu", nullptr, bx, by, bw, 74, ui.resultOk ? Tone::Primary : Tone::Normal, true,
                   ui.resultOk && ui.focusPending)) {
            Go(ui, Screen::Home);
        }
    } else if (ui.resultOk) {
        if (Button("##play", "Play now", nullptr, bx, by, bw, 74, Tone::Primary, true, ui.focusPending)) {
            ui.starting = true;
        }
        by += 86;
        if (Button("##menu", "Back to menu", nullptr, bx, by, bw, 74, Tone::Normal)) {
            Go(ui, Screen::Home);
        }
    } else {
        if (Button("##retry", "Back to import", nullptr, bx, by, bw, 74, Tone::Primary, true, ui.focusPending)) {
            ui.images = SwitchImport::FindDiscImages();
            Go(ui, Screen::Import);
        }
        by += 86;
        if (Button("##menu", "Back to menu", nullptr, bx, by, bw, 74, Tone::Normal)) {
            Go(ui, Screen::Home);
        }
    }
    ui.focusPending = false;
    DrawMessage(ui);
    DrawFooter({{'A', "Select"}}, FreeSpaceText());
}

// ---------------------------------------------------------------------------------------------
// Mods
// ---------------------------------------------------------------------------------------------

// The next .zip in the Mods folder that has no unpacked folder yet, "" when there is none.
std::string NextModZip(const Ui& ui) {
    std::vector<std::string> zips;
    std::error_code ec;
    for (fs::directory_iterator it(SwitchMods::kFolder, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == ".zip") {
            zips.push_back(it->path().filename().string());
        }
    }
    std::sort(zips.begin(), zips.end());
    for (const std::string& zip : zips) {
        const std::string folder = std::string(SwitchMods::kFolder) + "/" + fs::path(zip).stem().string();
        if (!fs::exists(folder, ec) &&
            std::find(ui.zipFailed.begin(), ui.zipFailed.end(), zip) == ui.zipFailed.end()) {
            return zip;
        }
    }
    return {};
}

void DrawMods(Ui& ui) {
    if (ui.zipJob) {
        DrawHeader("Mods", "Unpacking a mod that was copied over as a .zip file.");
        const std::string name = Fit(26, ui.zipName, 1000, false);
        TextCentered(640, 150, 26, kText, name.c_str());
        DrawProgressBody(ui, ui.zipJob->BytesDone(), ui.zipJob->BytesTotal());
        if (ui.zipJob->Finished()) {
            const std::string error = ui.zipJob->Finish();
            ui.zipJob.reset();
            if (!error.empty()) {
                ui.zipFailed.push_back(ui.zipName);
                Notify(ui, ui.zipName + ": " + error, kWarn, 10);
            } else {
                ui.modsChanged = true;
            }
            ui.modsLoaded = false;  // look again: another zip may be waiting
        }
        DrawFooter({}, "");
        return;
    }
    if (!ui.modsLoaded) {
        std::error_code ec;
        fs::create_directories(SwitchMods::kFolder, ec);
        if (const std::string zip = NextModZip(ui); !zip.empty()) {
            ui.zipName = zip;
            ui.zipJob = std::make_unique<ZipExtract::Job>();
            ui.zipJob->Start(std::string(SwitchMods::kFolder) + "/" + zip,
                             std::string(SwitchMods::kFolder) + "/" + fs::path(zip).stem().string());
            ui.samples.clear();
            ui.samples.emplace_back(Clock::now(), 0);
            return;
        }
        ui.mods = SwitchMods::List();
        ui.modsLoaded = true;
        ui.modDeleteArmed.clear();
    }
    DrawHeader("Mods", "Folders in /switch/WiiCompiled/Mods. The one at the top wins when two change the same file.");

    if (ui.mods.empty()) {
        TextCentered(640, 250, 28, kText, "No mods yet.");
        TextCentered(640, 300, 21, kMuted, "Copy each mod, as a .zip or as its folder (the one with \"riivolution\" inside),");
        TextCentered(640, 330, 21, kMuted, "into /switch/WiiCompiled/Mods on the SD card, then come back here.");
        ui.focusPending = false;
        if (g_in.back) {
            Go(ui, Screen::Home);
        }
        DrawMessage(ui);
        DrawFooter({{'B', "Back"}}, FreeSpaceText());
        return;
    }

    if (ui.focusPending) {
        ui.sel = 0;
        ui.focusPending = false;
    }
    const int count = static_cast<int>(ui.mods.size());
    const int sel = std::clamp(ui.sel, 0, count - 1);
    // Six rows fit above the message line; the window follows the highlight.
    constexpr int kRows = 6;
    const int first = std::clamp(sel - kRows / 2, 0, std::max(0, count - kRows));
    // Rows above the window still count as items, so the highlight index stays the list index.
    for (int i = 0; i < first; ++i) {
        NextItem(true);
    }
    float y = 150;
    int toggled = -1;
    for (int i = first; i < std::min(count, first + kRows); ++i) {
        const SwitchMods::Mod& mod = ui.mods[static_cast<size_t>(i)];
        const std::string label = Fit(30, mod.name, 760, false);
        char sub[64];
        std::snprintf(sub, sizeof(sub), "%s  -  priority %d of %d", mod.enabled ? "On" : "Off", i + 1, count);
        ImGui::PushID(i);
        if (Button("##mod", label.c_str(), sub, 160, y, 960, 62, mod.enabled ? Tone::Primary : Tone::Normal)) {
            toggled = i;
        }
        ImGui::PopID();
        y += 68;
    }
    for (int i = first + kRows; i < count; ++i) {
        NextItem(true);
    }

    SwitchMods::Mod& current = ui.mods[static_cast<size_t>(sel)];
    if (toggled >= 0) {
        SwitchMods::Mod& mod = ui.mods[static_cast<size_t>(toggled)];
        mod.enabled = !mod.enabled;
        SwitchMods::SetEnabled(mod, mod.enabled);
        ui.sel = toggled;
        ui.modsChanged = true;
        ui.modDeleteArmed.clear();
    } else if (g_in.x && sel > 0) {
        std::swap(ui.mods[static_cast<size_t>(sel)], ui.mods[static_cast<size_t>(sel - 1)]);
        SwitchMods::SaveOrder(ui.mods);
        ui.sel = sel - 1;
        ui.modsChanged = true;
        ui.modDeleteArmed.clear();
    } else if (g_in.y) {
        if (ui.modDeleteArmed == current.name) {
            const std::string name = current.name;
            if (SwitchMods::Delete(current)) {
                Notify(ui, "Deleted " + name + ".", kGood);
            } else {
                Notify(ui, "Could not delete " + name + ".");
            }
            ui.modsLoaded = false;
            ui.modsChanged = true;
        } else {
            ui.modDeleteArmed = current.name;
            Notify(ui, "Press Y again to delete " + current.name + " from the SD card.", kWarn, 4);
        }
    } else if (g_in.moveY != 0) {
        ui.modDeleteArmed.clear();
    }
    if (g_in.back) {
        if (ui.modsChanged) {
            Notify(ui, "Mod changes apply the next time the game starts.", kGood);
            ui.modsChanged = false;
        }
        Go(ui, Screen::Home);
    }
    DrawMessage(ui);
    DrawFooter({{'A', "On / off"}, {'X', "Move up"}, {'Y', "Delete"}, {'B', "Back"}}, FreeSpaceText());
}

// ---------------------------------------------------------------------------------------------
// Mii data
// ---------------------------------------------------------------------------------------------

void DrawMii(Ui& ui) {
    DrawHeader("Mii data", "Everything goes through the folder /switch/WiiCompiled/Mii on the SD card.");
    const fs::path database =
        RuntimeConfigFile::ApplicationDataDirectory() / "NAND" / "shared2" / "menu" / "FaceLib" / "RFL_DB.dat";
    const fs::path folder = "sdmc:/switch/WiiCompiled/Mii";
    const auto report = [&ui](const SwitchMiiData::Result& result) {
        Notify(ui, result.message, result.ok ? kGood : kWarn, 8);
    };

    const float bx = 240, bw = 800, bh = 86;
    float y = 170;
    if (Button("##export", "Save my Miis to the SD card", "Copies the game's Mii database there as RFL_DB.dat.", bx, y, bw,
               bh, Tone::Normal, true, ui.focusPending)) {
        report(SwitchMiiData::Export(database, folder));
    }
    y += bh + 14;
    if (Button("##add", "Add single Miis", "Adds every .rcd, .mii or .miigx file in the folder to a free slot.", bx, y,
               bw, bh, Tone::Normal)) {
        report(SwitchMiiData::AddSingleMiis(database, folder));
    }
    y += bh + 14;
    if (Button("##replace", "Replace all my Miis", "Uses the RFL_DB.dat in the folder instead. The old one is kept as a backup.",
               bx, y, bw, bh, Tone::Danger)) {
        report(SwitchMiiData::Replace(database, folder));
    }
    ui.focusPending = false;
    if (g_in.back) {
        Go(ui, Screen::Home);
    }
    DrawMessage(ui);
    DrawFooter({{'A', "Select"}, {'B', "Back"}}, "");
}

// ---------------------------------------------------------------------------------------------
// SD card browser
// ---------------------------------------------------------------------------------------------

std::string ChildPath(const std::string& dir, const std::string& name) {
    return !dir.empty() && dir.back() == '/' ? dir + name : dir + "/" + name;
}

std::string ParentPath(const std::string& dir) {
    if (dir.size() <= 6) {  // "sdmc:/"
        return "sdmc:/";
    }
    std::string p = dir;
    while (!p.empty() && p.back() == '/') {
        p.pop_back();
    }
    const size_t slash = p.find_last_of('/');
    if (slash == std::string::npos) {
        return "sdmc:/";
    }
    p.erase(slash);
    return p.size() < 6 ? "sdmc:/" : p;
}

bool IsRoot(const std::string& dir) {
    return dir.size() <= 6;
}

void LoadEntries(Ui& ui) {
    ui.entries.clear();
    if (!IsRoot(ui.cwd)) {
        BrowseEntry up;
        up.name = "Up one folder";
        up.isDir = true;
        up.isUp = true;
        ui.entries.push_back(std::move(up));
    }
    std::vector<BrowseEntry> dirs;
    std::vector<BrowseEntry> files;
    std::error_code ec;
    for (auto it = fs::directory_iterator(ui.cwd, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code typeEc;
        BrowseEntry entry;
        entry.name = it->path().filename().string();
        if (entry.name.empty()) {
            continue;
        }
        entry.isDir = it->is_directory(typeEc);
        if (entry.isDir) {
            dirs.push_back(std::move(entry));
            continue;
        }
        const std::string ext = Lower(it->path().extension().string());
        const bool discExt = ext == ".iso" || ext == ".gcm" || ext == ".wbfs";
        if (!discExt) {
            continue;
        }
        entry.size = fs::file_size(it->path(), typeEc);
        if (discExt) {
            DiscImage disc;
            if (SwitchImport::InspectDiscFile(ChildPath(ui.cwd, entry.name), disc)) {
                entry.isDisc = true;
                entry.disc = std::move(disc);
            }
        }
        files.push_back(std::move(entry));
    }
    const auto byName = [](const BrowseEntry& a, const BrowseEntry& b) { return Lower(a.name) < Lower(b.name); };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    for (auto& e : dirs) ui.entries.push_back(std::move(e));
    for (auto& e : files) ui.entries.push_back(std::move(e));
}

// Small icons drawn with primitives (the system font has no folder/file glyphs).
void DrawEntryIcon(const BrowseEntry& e, ImVec2 topLeft, float size) {
    const ImVec2 a = topLeft;
    const ImVec2 b(a.x + size, a.y + size * 0.8f);
    if (e.isUp) {
        g.dl->AddTriangleFilled(ImVec2(a.x + size * 0.5f, a.y), ImVec2(a.x, a.y + size * 0.6f),
                                ImVec2(a.x + size, a.y + size * 0.6f), kMuted);
        g.dl->AddRectFilled(ImVec2(a.x + size * 0.35f, a.y + size * 0.55f), ImVec2(a.x + size * 0.65f, a.y + size * 0.9f),
                            kMuted);
    } else if (e.isDir) {
        g.dl->AddRectFilled(ImVec2(a.x, a.y + size * 0.08f), ImVec2(a.x + size * 0.45f, a.y + size * 0.3f), kWarn,
                            3.0f * g.s);
        g.dl->AddRectFilled(ImVec2(a.x, a.y + size * 0.2f), b, kWarn, 4.0f * g.s);
    } else if (e.isDisc) {
        const ImU32 c = e.disc.Supported() ? kGood : kMuted;
        const ImVec2 center(a.x + size * 0.5f, a.y + size * 0.5f);
        g.dl->AddCircleFilled(center, size * 0.5f, c);
        g.dl->AddCircleFilled(center, size * 0.16f, kPanel);
    } else {
        g.dl->AddRectFilled(ImVec2(a.x + size * 0.12f, a.y), ImVec2(a.x + size * 0.88f, a.y + size), kBorder,
                            4.0f * g.s);
        g.dl->AddRectFilled(ImVec2(a.x + size * 0.28f, a.y + size * 0.3f), ImVec2(a.x + size * 0.72f, a.y + size * 0.38f),
                            kMuted);
        g.dl->AddRectFilled(ImVec2(a.x + size * 0.28f, a.y + size * 0.5f), ImVec2(a.x + size * 0.72f, a.y + size * 0.58f),
                            kMuted);
    }
}

void DrawBrowse(Ui& ui) {
    if (!ui.entriesLoaded) {
        LoadEntries(ui);
        ui.entriesLoaded = true;
    }
    DrawHeader("Browse SD card", nullptr);
    const std::string pathText = Fit(22, IsRoot(ui.cwd) ? "SD card" : "SD card" + ShortPath(ui.cwd), 1150, true);
    Text(64, 106, 22, kMuted, pathText.c_str());

    const float rowH = 64;
    const float gap = 8;
    ImGui::SetCursorScreenPos(g.P(64, 142));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, gap * g.s));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8.0f * g.s);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, kBorder);
    ImGui::BeginChild("##browse", g.Sz(1152, 456), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);

    const float width = 1138;
    int focusIndex = -1;
    if (ui.focusPending || !ui.focusName.empty()) {
        focusIndex = 0;
        for (size_t i = 0; i < ui.entries.size(); ++i) {
            if (!ui.focusName.empty() && ui.entries[i].name == ui.focusName) {
                focusIndex = static_cast<int>(i);
                break;
            }
        }
        // First entry that is not the "up" row is the natural landing spot in a fresh folder.
        if (ui.focusName.empty() && ui.entries.size() > 1 && ui.entries[0].isUp) {
            focusIndex = 1;
        }
    }

    if (focusIndex >= 0) {
        ui.sel = focusIndex;
        ui.selMoved = true;
    }
    ui.sel = std::clamp(ui.sel, 0, std::max(0, static_cast<int>(ui.entries.size()) - 1));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(ui.entries.size()), (rowH + gap) * g.s);
    clipper.IncludeItemByIndex(ui.sel);
    std::string enterDir;
    bool goUp = false;
    std::string pickedFileName;
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const BrowseEntry& e = ui.entries[static_cast<size_t>(i)];
            ImGui::PushID(i);
            const bool clicked = ImGui::InvisibleButton("##entry", g.Sz(width, rowH));
            const bool selected = i == ui.sel;
            if (selected && ui.selMoved) {
                ImGui::SetScrollHereY(0.5f);
            }
            const bool pressed = clicked || (g_in.activate && selected);
            const bool active = selected || ImGui::IsItemActive();
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            const float radius = 12.0f * g.s;
            g.dl->AddRectFilled(a, b, active ? kPanelHi : kPanel, radius);
            if (active) {
                g.dl->AddRect(ImVec2(a.x - 2 * g.s, a.y - 2 * g.s), ImVec2(b.x + 2 * g.s, b.y + 2 * g.s),
                              IM_COL32(255, 255, 255, 235), radius + 2 * g.s, 0, 3.0f * g.s);
            }
            DrawEntryIcon(e, ImVec2(a.x + 20 * g.s, a.y + 14 * g.s), 36.0f * g.s);
            const float left = (a.x - g.ox) / g.s + 76;
            const float top = a.y / g.s;
            std::string right;
            ImU32 rightColor = kMuted;
            if (e.isDisc) {
                right = e.disc.Supported() ? "Mario Kart Wii (PAL)  -  " + FormatBytes(e.disc.totalSize)
                                           : "Game ID " + (e.disc.gameId.empty() ? std::string("unknown") : e.disc.gameId) +
                                                 "  -  " + FormatBytes(e.disc.totalSize);
                rightColor = e.disc.Supported() ? kGood : kMuted;
            } else if (!e.isDir) {
                right = FormatBytes(e.size);
            }
            const float rightWidth = right.empty() ? 0.0f : TextWidth(20, right.c_str()) / g.s + 20;
            const std::string name = Fit(26, e.name, width - 76 - rightWidth - 28, false);
            Text(left, top + 17, 26, e.isUp ? kMuted : kText, name.c_str());
            if (!right.empty()) {
                TextRight((b.x - g.ox) / g.s - 24, top + 21, 20, rightColor, right.c_str());
            }
            if (pressed) {
                if (e.isUp) {
                    goUp = true;
                } else if (e.isDir) {
                    enterDir = e.name;
                } else {
                    pickedFileName = e.name;
                }
            }
            ImGui::PopID();
        }
    }
    clipper.End();
    ui.itemCounter = static_cast<int>(ui.entries.size());
    ui.enabled.assign(ui.entries.size(), 1);
    if (ui.entries.empty()) {
        Text(64 + 8, 142 / 1.0f + 4, 24, kMuted, "This folder is empty.");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ui.focusPending = false;
    ui.focusName.clear();

    // Actions.
    if (goUp || g_in.back) {
        if (IsRoot(ui.cwd)) {
            Go(ui, Screen::Home);
        } else {
            const std::string cameFrom = fs::path(ui.cwd).filename().string();
            ui.cwd = ParentPath(ui.cwd);
            ui.focusName = cameFrom;
            ui.entriesLoaded = false;
            ui.focusPending = true;
        }
    } else if (!enterDir.empty()) {
        ui.cwd = ChildPath(ui.cwd, enterDir);
        ui.entriesLoaded = false;
        ui.focusName.clear();
        ui.focusPending = true;
    } else if (!pickedFileName.empty()) {
        for (const BrowseEntry& e : ui.entries) {
            if (e.name != pickedFileName) {
                continue;
            }
            if (e.isDisc && e.disc.Supported()) {
                StartInstallDialog(ui, e.disc, Screen::Browse);
            } else if (e.isDisc) {
                Notify(ui, "Only PAL Mario Kart Wii (game ID RMCP01) is supported.");
            } else {
                Notify(ui, "That is not a Wii disc image (.iso, .wbfs or .gcm).", kMuted);
            }
            break;
        }
    }
    DrawMessage(ui);
    DrawFooter({{'A', "Open"}, {'B', "Back"}}, FreeSpaceText());
}

// ---------------------------------------------------------------------------------------------
// Input and frame loop
// ---------------------------------------------------------------------------------------------

void HandleEvents(const AuroraEvent* events) {
    if (events == nullptr) {
        return;
    }
    for (const AuroraEvent* ev = events; ev->type != AURORA_NONE; ++ev) {
        if (ev->type == AURORA_EXIT) {
            // Closed from the home menu: end the process directly, as the in-game exit does.
            std::_Exit(EXIT_SUCCESS);
        }
        if (ev->type == AURORA_SDL_EVENT) {
            FeedInput(ev->sdl);
        }
    }
}

// Full-screen splash shown from Play until the game draws its first frame (the game takes several
// seconds to start, and this stays on screen the whole time).
void DrawStarting() {
    // Same look as the game's own startup screen, which takes over from here.
    g.dl->AddRectFilled(ImVec2(0, 0), g.display, IM_COL32(0, 0, 0, 255));
    TextCentered(640, 340, 40, IM_COL32(255, 255, 255, 255), "DriftDroid");
}

void DrawFrame(Ui& ui) {
    const ImGuiIO& io = ImGui::GetIO();
    g.display = io.DisplaySize;
    g.s = std::max(0.25f, io.DisplaySize.y / 720.0f);
    g.ox = std::max(0.0f, (io.DisplaySize.x - 1280.0f * g.s) * 0.5f);
    g.font = ImGui::GetFont();

    // Move the highlight (up/down in lists, left/right in dialogs), skipping disabled items.
    const Screen screenAtStart = ui.screen;
    {
        const int step = ui.navHorizontal ? g_in.moveX : g_in.moveY;
        if (step != 0 && ui.itemCount > 0) {
            const int dir = step > 0 ? 1 : -1;
            int next = ui.sel;
            do {
                next += dir;
            } while (next >= 0 && next < ui.itemCount &&
                     static_cast<size_t>(next) < ui.prevEnabled.size() && !ui.prevEnabled[static_cast<size_t>(next)]);
            if (next >= 0 && next < ui.itemCount) {
                ui.sel = next;
                ui.selMoved = true;
            }
        }
    }
    ui.itemCounter = 0;
    ui.enabled.clear();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##driftdroid-launcher", nullptr, kFlags)) {
        g.dl = ImGui::GetWindowDrawList();
        if (ui.starting) {
            DrawStarting();
            ++ui.startFrames;
        } else {
        DrawBackground();
        switch (ui.screen) {
        case Screen::Home: DrawHome(ui); break;
        case Screen::Import: DrawImport(ui); break;
        case Screen::Dialog: DrawDialog(ui); break;
        case Screen::Progress: DrawProgress(ui); break;
        case Screen::Done: DrawDone(ui); break;
        case Screen::Browse: DrawBrowse(ui); break;
        case Screen::RetroDialog: DrawRetroDialog(ui); break;
        case Screen::RetroProgress: DrawRetroProgress(ui); break;
        case Screen::Mods: DrawMods(ui); break;
        case Screen::Mii: DrawMii(ui); break;
        }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    if (ui.screen == screenAtStart) {
        ui.itemCount = ui.itemCounter;
        ui.prevEnabled.swap(ui.enabled);
        if (ui.itemCount > 0) {
            ui.sel = std::clamp(ui.sel, 0, ui.itemCount - 1);
        }
    } else {
        ui.itemCount = 0;  // screen changed: no movement until it has drawn once
        ui.prevEnabled.clear();
    }
    ui.selMoved = false;
}

}  // namespace

void SwitchLauncherInitFonts(const AuroraWindowSize*) {
    ImGuiIO& io = ImGui::GetIO();
    if (R_FAILED(plInitialize(PlServiceType_User))) {
        return;  // keep ImGui's built-in font
    }
    // The shared font memory must stay valid for the life of the process, so the service is never
    // shut down and the atlas does not take ownership of the data.
    PlFontData standard{};
    if (R_FAILED(plGetSharedFontByType(&standard, PlSharedFontType_Standard)) || standard.address == nullptr) {
        return;
    }
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = 2;
    config.OversampleV = 1;
    io.Fonts->AddFontFromMemoryTTF(standard.address, static_cast<int>(standard.size), kFontBakePx, &config,
                                   io.Fonts->GetGlyphRangesDefault());
    PlFontData extended{};
    if (R_SUCCEEDED(plGetSharedFontByType(&extended, PlSharedFontType_NintendoExt)) && extended.address != nullptr) {
        static const ImWchar kButtonRange[] = {0xE000, 0xE153, 0};
        ImFontConfig merge = config;
        merge.MergeMode = true;
        io.Fonts->AddFontFromMemoryTTF(extended.address, static_cast<int>(extended.size), kFontBakePx, &merge,
                                       kButtonRange);
        g_haveButtonGlyphs = true;
    }
    // The baked size is large; the in-game settings panel uses ImGui's default scale, so bring it
    // back down to roughly the size the old built-in font had.
    io.FontGlobalScale = kInGameFontScale;
}

bool SwitchRunLauncher() {
    Ui ui;
    ui.dataRoot = RuntimeConfigFile::ResolvedDvdRoot();
    if (ui.dataRoot.empty()) {
        return true;
    }
    RefreshData(ui);
    if (ui.dataState != DataState::Ready) {
        ui.images = SwitchImport::FindDiscImages();
    }
    // Started from the base game's launcher: go straight in, as long as everything is in place.
    if (FileExists(kSkipLauncherFlag)) {
        std::remove(kSkipLauncherFlag);
        if (RuntimeProduct::IsRetroRewind() && ui.dataState == DataState::Ready && ui.retroRewindPack) {
            while (!aurora_begin_frame()) {
                aurora_update();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return true;
        }
    }

    g_ui = &ui;
    g_in = PadInput{};
    g_in.armedAt = Clock::now() + std::chrono::milliseconds(600);
    // ImGui's own gamepad navigation (and its window switcher on X) stays off while the launcher
    // is open; restored below for the in-game settings panel.
    ImGuiIO& launcherIo = ImGui::GetIO();
    const ImGuiConfigFlags savedConfigFlags = launcherIo.ConfigFlags;
    launcherIo.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;

    auto nextFrame = Clock::now();
    constexpr auto kFrame = std::chrono::microseconds(16667);
    while (!ui.quit && !(ui.starting && ui.startFrames >= 3)) {
        HandleEvents(aurora_update());
        ResolveInput();
        if (!aurora_begin_frame()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        aurora_wait_for_frame_worker();
        DrawFrame(ui);
        aurora_end_frame();

        // Hold roughly 60 fps; a frame that ran long just starts the next one right away.
        nextFrame += kFrame;
        const auto now = Clock::now();
        if (nextFrame > now) {
            std::this_thread::sleep_until(nextFrame);
        } else {
            nextFrame = now;
        }
    }
    // Hand over with the next frame already begun. The frame worker only reports a frame as sealed
    // once the following frame has been allowed to begin, and startup drains GX (which waits for
    // "sealed") before the guest begins a frame of its own - returning straight after end_frame
    // left the two waiting on each other forever. The caller marks the runtime's frame as active.
    if (ui.starting) {
        while (!aurora_begin_frame()) {
            HandleEvents(aurora_update());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    // Leaving with a download still running (the HOME menu closing the app): stop it and release
    // the network before anything else runs.
    ui.retroJob.reset();
    CloseRetroNetwork(ui);
    launcherIo.ConfigFlags = savedConfigFlags;
    g_ui = nullptr;
    g_in = PadInput{};
    return ui.starting;
}

std::string SwitchFindRetroRewindRoot() {
    static constexpr const char* kCandidates[] = {
        "sdmc:/switch/WiiCompiled/RetroRewind6",
        "sdmc:/switch/WiiCompiled/RetroRewind/RetroRewind6",
        "sdmc:/RetroRewind6",
    };
    for (const char* candidate : kCandidates) {
        if (FileExists((std::string(candidate) + "/Binaries/Code.pul").c_str())) {
            return candidate;
        }
    }
    return {};
}

#endif  // __SWITCH__
