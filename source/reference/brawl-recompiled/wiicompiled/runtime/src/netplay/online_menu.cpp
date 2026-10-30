#include "online_menu.h"

#include "hle/gecko.h"
#include "netplay/game_layout.h"
#include "netplay/net_socket.h"
#include "netplay_lobby.h"
#include "netplay_session.h"
#include "netplay_start.h"
#include "runtime_config.h"
#include "runtime_log.h"

#include <aurora/imgui.h>
#include <dolphin/pad.h>
#include <imgui.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

extern "C" uint8_t* aurora_load_png_rgba(const char* path, uint32_t* width, uint32_t* height);

// Drawn the way the title card is (settings_overlay.cpp): the game's own fonts, read from the disc, in the game's
// 456-unit-tall layout space (GameLayout::View), with the Smash Ball and the card's colours. Every screen works with a
// controller (d-pad or stick, A, B), the keyboard (arrows, Enter, Escape, typing) and the mouse.
namespace OnlineMenu {
namespace {

using NetplayLobby::Phase;
namespace GL = GameLayout;

enum class Screen { Main, Host, Code, Name, Browse };

// The four players' colours, as on the character select.
constexpr uint32_t kPlayer[4] = {0xE0382CFFu, 0x2F6FDCFFu, 0xE8B421FFu, 0x2FA84AFFu};
constexpr uint32_t kOrange = 0xFF8C28FFu, kGold = 0xFFD966FFu, kSnow = 0xF4F4F8FFu, kDim = 0xA8ACBEFFu,
                   kInk = 0x0B0D16FFu, kRed = 0xFF6A52FFu, kGreen = 0x6FDB6FFFu;
constexpr char kCodeLetters[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

ImU32 Col(uint32_t rgba, float alpha = 1.0f) {
    return IM_COL32(rgba >> 24, (rgba >> 16) & 255, (rgba >> 8) & 255, static_cast<int>((rgba & 255) * alpha));
}
uint32_t Shade(uint32_t rgba, float f) {
    auto c = [&](int shift) { return static_cast<uint32_t>(std::clamp(((rgba >> shift) & 255) * f, 0.0f, 255.0f)); };
    return (c(24) << 24) | (c(16) << 16) | (c(8) << 8) | (rgba & 255);
}

struct State {
    bool open = false;
    bool intentDone = false;
    Screen screen = Screen::Main;
    int focus = 0;
    int players = 2;
    int who = 0;  // 0 anyone (listed online), 1 friends with the code, 2 this network only
    std::string name;
    std::string code;     // being typed: a room code, or an address
    int codeCursor = 0;
    std::string message;  // a line in red under the banner
    Netplay::Address pendingHost;
    double pendingSince = 0;  // ImGui time the code was entered
    int browseFirst = 0;
    Phase lastPhase = Phase::Off;
};
State g;

// ---- input: edges of directions and buttons from every controller, the keyboard and the mouse ----
struct Nav {
    bool up = false, down = false, left = false, right = false, confirm = false, back = false;
};

Nav ReadNav() {
    static uint32_t prevHeld = 0;
    static double nextRepeat = 0.0;
    enum : uint32_t { kUp = 1, kDown = 2, kLeft = 4, kRight = 8, kA = 16, kB = 32 };
    uint32_t held = 0;
    // Only the game window in front reads controllers (two games on one PC share them).
    for (u32 i = 0; i < 8 && SDL_GetKeyboardFocus() != nullptr; ++i) {
        SDL_Gamepad* pad = PADGetSDLGamepadForIndex(i);
        if (pad == nullptr) continue;
        auto b = [&](SDL_GamepadButton button) { return SDL_GetGamepadButton(pad, button); };
        const int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        if (b(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -20000) held |= kUp;
        if (b(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 20000) held |= kDown;
        if (b(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -20000) held |= kLeft;
        if (b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 20000) held |= kRight;
        if (b(SDL_GAMEPAD_BUTTON_SOUTH) || b(SDL_GAMEPAD_BUTTON_START)) held |= kA;
        if (b(SDL_GAMEPAD_BUTTON_EAST)) held |= kB;
    }
    const double now = ImGui::GetTime();
    uint32_t pressed = held & ~prevHeld;
    // A held direction repeats, as menus do.
    const uint32_t dirs = held & (kUp | kDown | kLeft | kRight);
    if (dirs != 0 && (dirs & prevHeld) == dirs && now >= nextRepeat) {
        pressed |= dirs;
        nextRepeat = now + 0.11;
    } else if ((pressed & (kUp | kDown | kLeft | kRight)) != 0) {
        nextRepeat = now + 0.4;
    }
    prevHeld = held;
    Nav nav;
    const bool typing = g.screen == Screen::Name || g.screen == Screen::Code;
    nav.up = (pressed & kUp) || ImGui::IsKeyPressed(ImGuiKey_UpArrow);
    nav.down = (pressed & kDown) || ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    nav.left = (pressed & kLeft) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow);
    nav.right = (pressed & kRight) || ImGui::IsKeyPressed(ImGuiKey_RightArrow);
    nav.confirm = (pressed & kA) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
                  (!typing && ImGui::IsKeyPressed(ImGuiKey_Space));
    nav.back = (pressed & kB) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    return nav;
}

// ---- drawing in layout units (456 tall, y up, 0 at the centre) ----
struct Canvas {
    ImDrawList* list = nullptr;
    GL::View view;
    float t = 0.0f;
    ImVec2 P(float x, float y) const { return ImVec2(view.centerX + x * view.scale, view.centerY - y * view.scale); }
};

std::shared_ptr<GL::Font> TitleFont() {
    static const auto font = GL::LoadFont("/system/font/font_hira.brfnt");
    return font;
}
std::shared_ptr<GL::Font> MenuFont() {
    static const auto font = GL::LoadFont("/system/font/font_melee.brfnt");
    return GL::FontReady(font.get()) ? font : TitleFont();
}

void Text(const Canvas& c, const GL::Font& font, const std::string& text, float x, float y, float size, uint32_t top,
          uint32_t bottom, int originH = 1, float maxWidth = 0.0f, bool outline = false) {
    const auto u = GL::Utf16(text);
    if (maxWidth > 0.0f) {
        const float w = GL::MeasureText(font, u, size);
        if (w > maxWidth) size *= maxWidth / w;
    }
    if (outline) {
        for (int j = 0; j < 12; ++j) {
            const float a = j * 0.5236f, d = std::max(1.5f, size * 0.07f);
            GL::DrawText(c.list, c.view, font, u, x + std::cos(a) * d, y + std::sin(a) * d, size, kInk, kInk, 1.0f, originH, 1);
        }
    }
    GL::DrawText(c.list, c.view, font, u, x, y, size, top, bottom, 1.0f, originH, 1);
}

void Body(const Canvas& c, const std::string& text, float x, float y, float size, uint32_t color = kSnow, int originH = 1,
          float maxWidth = 0.0f) {
    Text(c, *MenuFont(), text, x, y, size, color, color, originH, maxWidth);
}

// A small arrow (Brawl's fonts have no arrow glyphs): dir 0 up, 1 down, 2 left, 3 right.
void Arrow(const Canvas& c, float x, float y, float size, int dir, uint32_t color) {
    const float h = size * 0.5f;
    ImVec2 a, b, d;
    switch (dir) {
    case 0: a = c.P(x, y + h); b = c.P(x + h, y - h); d = c.P(x - h, y - h); break;
    case 1: a = c.P(x, y - h); b = c.P(x - h, y + h); d = c.P(x + h, y + h); break;
    case 2: a = c.P(x - h, y); b = c.P(x + h, y + h); d = c.P(x + h, y - h); break;
    default: a = c.P(x + h, y); b = c.P(x - h, y - h); d = c.P(x - h, y + h); break;
    }
    c.list->AddTriangleFilled(a, b, d, Col(color));
}

// A slanted plate, the shape of Brawl's menu buttons and banners: a lighter top band over the base colour, outlined.
void Plate(const Canvas& c, float x0, float y0, float x1, float y1, uint32_t color, float slant = 10.0f, float alpha = 1.0f) {
    const float ym = (y0 + y1) * 0.5f;
    auto Edge = [&](float y) { return slant * (2.0f * (y - y0) / std::max(1.0f, y1 - y0) - 1.0f); };  // -slant..+slant
    const ImVec2 tl = c.P(x0 + Edge(y1), y1), tr = c.P(x1 + Edge(y1), y1), ml = c.P(x0 + Edge(ym), ym),
                 mr = c.P(x1 + Edge(ym), ym), bl = c.P(x0 + Edge(y0), y0), br = c.P(x1 + Edge(y0), y0);
    c.list->AddQuadFilled(tl, tr, mr, ml, Col(Shade(color, 1.22f), alpha));
    c.list->AddQuadFilled(ml, mr, br, bl, Col(color, alpha));
    c.list->AddLine(ml, mr, Col(Shade(color, 1.45f), alpha * 0.5f), 1.0f);
    c.list->AddQuad(tl, tr, br, bl, Col(kInk, alpha), 3.0f);
}

// A button: hovered or focused it glows; true when clicked, or confirmed while focused.
bool Button(const Canvas& c, int index, int& focus, bool confirm, float x0, float y0, float x1, float y1,
            const std::string& label, uint32_t color, bool enabled = true, float textSize = 26.0f) {
    const ImVec2 lo = c.P(x0 - 10, y1), hi = c.P(x1 + 10, y0);
    const bool hovered = ImGui::IsMouseHoveringRect(lo, hi);
    if (hovered && enabled && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0)) focus = index;
    const bool focused = focus == index;
    const float pulse = focused ? 0.5f + 0.5f * std::sin(c.t * 6.0f) : 0.0f;
    const uint32_t body = enabled ? color : 0x3A3F55FFu;
    if (focused && enabled) {
        const float g = 4.0f + pulse * 3.0f;
        c.list->AddQuadFilled(c.P(x0 + 10 - g, y1 + g), c.P(x1 + 10 + g, y1 + g), c.P(x1 - 10 + g, y0 - g),
                              c.P(x0 - 10 - g, y0 - g), Col(0xFFFFFFFFu, 0.55f + 0.35f * pulse));
    }
    Plate(c, x0, y0, x1, y1, body);
    const bool darkText = (color == kPlayer[2] || color == kGold) && enabled;
    Text(c, *TitleFont(), label, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, textSize, darkText ? 0x2A1A00FFu : kSnow,
         darkText ? 0x4A3000FFu : (enabled ? 0xFFE7B0FFu : kDim), 1, (x1 - x0) - 24.0f, !darkText);
    return enabled && ((hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || (focused && confirm));
}

// Left/right selector: "<  value  >".
bool Selector(const Canvas& c, int index, int& focus, const Nav& nav, float y, const std::string& label,
              const std::string& value, int& option, int count) {
    const bool focused = focus == index;
    const ImVec2 lo = c.P(-300, y + 22), hi = c.P(300, y - 22);
    if (ImGui::IsMouseHoveringRect(lo, hi) && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0)) focus = index;
    Plate(c, -300, y - 22, 300, y + 22, focused ? 0x2B3350FFu : 0x1C2133FFu, 8.0f);
    Body(c, label, -280, y, 18, kDim, 0);
    Text(c, *TitleFont(), value, 120, y, 22, kSnow, 0xFFE7B0FFu, 1, 230, true);
    const bool arrows = focused || ImGui::IsMouseHoveringRect(lo, hi);
    if (arrows) {
        Arrow(c, -10, y, 18, 2, kOrange);
        Arrow(c, 250, y, 18, 3, kOrange);
    }
    int step = 0;
    if (focused && nav.left) step = -1;
    if (focused && nav.right) step = 1;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(lo, hi)) {
        step = ImGui::GetIO().MousePos.x < c.P(120, 0).x ? -1 : 1;
    }
    if (step != 0) option = (option + step + count) % count;
    return step != 0;
}

// ---- actions ----
bool ServerReady() { return Netplay::Relay::Enabled(); }

std::string BuildLabel() {
    const std::string build = Gecko::BuildIdentity();
    return build == "vanilla" ? "Vanilla Brawl" : build.substr(0, build.find(' '));
}

void SaveName() {
    std::string name = g.name;
    name.erase(0, name.find_first_not_of(' '));
    name.erase(name.find_last_not_of(' ') + 1);
    if (name.empty()) {
        g.name = Netplay::DeviceName();
        return;
    }
    g.name = name;
    Netplay::SetDeviceName(name);
    RuntimeConfigFile::SetOnlineName(name);
}

void HostRoom() {
    g.message.clear();
    NetplayLobby::Host(static_cast<uint8_t>(g.players), 0);
}

void JoinTyped(const std::string& text) {
    g.message.clear();
    Netplay::Address address;
    if (Netplay::Relay::CodeToAddress(text, address)) {
        if (!ServerReady()) {
            g.message = "Room codes need the online server, and this game has none set.";
            return;
        }
    } else if (!Netplay::ParseAddress(text, Netplay::kDefaultPort, address) &&
               !Netplay::ResolveAddress(text, Netplay::kDefaultPort, address)) {
        g.message = "\"" + text + "\" isn't a room code or an address.";
        return;
    }
    g.pendingHost = address;
    g.pendingSince = ImGui::GetTime();
    NetplayLobby::AddKnownDevice(Netplay::ToString(address));
    NetplayLobby::Browse();
}

void HandleIntent() {
    if (g.intentDone) return;
    g.intentDone = true;
    const char* value = std::getenv("BRAWL_ONLINE");
    if (value == nullptr || *value == '\0' || NetplaySession::Active()) return;
    const std::string intent(value);
#if defined(_WIN32)
    _putenv_s("BRAWL_ONLINE", "");
#else
    unsetenv("BRAWL_ONLINE");
#endif
    g.open = true;
    const auto colon = intent.find(':');
    const std::string what = intent.substr(0, colon), arg = colon == std::string::npos ? "" : intent.substr(colon + 1);
    if (what == "host" || what == "host-public") {
        g.who = what == "host-public" ? 0 : 1;
        g.players = std::clamp(std::atoi(arg.c_str()), 2, 4);
        HostRoom();
    } else if (what == "browse") {
        NetplayLobby::Browse();
    } else if (what == "code") {
        g.screen = Screen::Code;
    } else if (what == "join" && !arg.empty()) {
        g.code = arg;
        JoinTyped(arg);
    }
}

// Typed characters this frame (names, codes, addresses).
void TypeInto(std::string& text, size_t limit, bool codeOnly) {
    ImGuiIO& io = ImGui::GetIO();
    for (ImWchar ch : io.InputQueueCharacters) {
        if (ch < 32 || ch >= 127 || text.size() >= limit) continue;
        char cc = static_cast<char>(ch);
        if (codeOnly && !std::isalnum(static_cast<unsigned char>(cc)) && cc != '.' && cc != ':' && cc != '-') continue;
        text += codeOnly && text.find('.') == std::string::npos ? static_cast<char>(std::toupper(cc)) : cc;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !text.empty()) text.pop_back();
    if (ImGui::IsKeyDown(ImGuiKey_ModCtrl) && ImGui::IsKeyPressed(ImGuiKey_V)) {
        if (const char* clip = ImGui::GetClipboardText()) {
            for (const char* p = clip; *p && text.size() < limit; ++p) {
                if (*p > 32 && *p < 127) text += codeOnly && std::isalpha(static_cast<unsigned char>(*p)) ? static_cast<char>(std::toupper(*p)) : *p;
            }
        }
    }
}

// ---- the screens ----
void Backdrop(const Canvas& c) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 lo = vp->Pos, hi(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    c.list->AddRectFilledMultiColor(lo, hi, IM_COL32(16, 18, 34, 255), IM_COL32(16, 18, 34, 255), IM_COL32(40, 14, 10, 255),
                                    IM_COL32(40, 14, 10, 255));
    // Slow light rays from behind the banner, as on the title card.
    const ImVec2 origin = c.P(0, 330);
    const float reach = std::max(vp->Size.x, vp->Size.y) * 1.4f;
    for (int i = 0; i < 14; ++i) {
        const float a = c.t * 0.05f + i * 0.4488f;
        const ImVec2 p1(origin.x + std::cos(a) * reach, origin.y + std::sin(a) * reach),
            p2(origin.x + std::cos(a + 0.16f) * reach, origin.y + std::sin(a + 0.16f) * reach);
        c.list->AddTriangleFilled(origin, p1, p2, IM_COL32(255, 160, 70, 14));
    }
}

void SmashBall(const Canvas& c, float x, float y, float size, float angle) {
    static ImTextureID texture{};
    static float aspect = 1.0f;
    static bool tried = false;
    if (!tried) {
        tried = true;
        uint32_t w = 0, h = 0;
        if (uint8_t* rgba = aurora_load_png_rgba("startup/smash_ball.png", &w, &h)) {
            texture = aurora_imgui_add_texture(w, h, rgba);
            aspect = h ? float(w) / float(h) : 1.0f;
            std::free(rgba);
        }
    }
    if (!texture) return;
    const float hw = size * aspect * 0.5f, hh = size * 0.5f, co = std::cos(angle), si = std::sin(angle);
    auto Q = [&](float dx, float dy) { return c.P(x + dx * co - dy * si, y + dx * si + dy * co); };
    c.list->AddImageQuad(texture, Q(-hw, hh), Q(hw, hh), Q(hw, -hh), Q(-hw, -hh));
}

void Banner(const Canvas& c, const std::string& subtitle) {
    Plate(c, -470, 168, 40, 214, 0x14182AFFu, 16.0f);
    c.list->AddRectFilled(c.P(-470, 170), c.P(60, 166), Col(kOrange));
    Text(c, *TitleFont(), "ONLINE", -330, 192, 46, 0xFFF3B0FFu, kOrange, 1, 0, true);
    Text(c, *TitleFont(), subtitle, -150, 189, 24, kSnow, 0xFFC86AFFu, 0, 200, true);
    SmashBall(c, 350, 186, 56, c.t * 0.35f);
    // Who and what, under the banner on the right.
    Body(c, g.name, 300, 150, 17, kSnow, 2, 260);
    std::string server = !ServerReady() ? "Network & Tailscale only" : Netplay::Relay::Connected() ? "Online server: connected"
                                                                                                 : "Online server: connecting...";
    Body(c, BuildLabel() + "   |   " + server, 300, 130, 14,
         !ServerReady() ? kDim : Netplay::Relay::Connected() ? kGreen : kGold, 2, 420);
}

void Footer(const Canvas& c, const std::string& left) {
    Plate(c, -470, -228, 470, -200, 0x10131FFFu, 8.0f);
    Body(c, left, -440, -214, 15, kDim, 0, 520);
    Body(c, "A / Enter: select     B / Esc: back     F9: close", 440, -214, 15, kDim, 2, 380);
}

void MainScreen(const Canvas& c, const Nav& nav) {
    Banner(c, "PLAY WITH ANYONE");
    int& f = g.focus;
    if (nav.up) f = (f + 3) % 4;
    if (nav.down) f = (f + 1) % 4;
    if (Button(c, 0, f, nav.confirm, -240, 46, 240, 96, "HOST A ROOM", kPlayer[0])) g.screen = Screen::Host, g.focus = 0;
    if (Button(c, 1, f, nav.confirm, -240, -22, 240, 28, "FIND ROOMS", kPlayer[1])) {
        g.message.clear();
        NetplayLobby::Browse();
        g.browseFirst = 0;
        g.focus = 0;
    }
    if (Button(c, 2, f, nav.confirm, -240, -90, 240, -40, "JOIN WITH A CODE", kPlayer[2])) {
        g.screen = Screen::Code;
        g.code.clear();
        g.focus = 0;
    }
    if (Button(c, 3, f, nav.confirm, -150, -146, 150, -108, "CHANGE NAME", 0x5A6078FFu, true, 20.0f)) {
        g.screen = Screen::Name;
        g.focus = 0;
    }
    const char* hints[4] = {"Open a room and invite friends, or anyone online.", "See the rooms you can join right now.",
                            "A friend told you their room code? Put it in here.", "How other players see you."};
    Body(c, hints[std::clamp(f, 0, 3)], 0, -172, 17, kDim, 1, 760);
    Footer(c, ServerReady() ? "Your room code: " + Netplay::Relay::Code() : "No online server set: your network and Tailscale.");
    if (nav.back) g.open = false;
}

void HostScreen(const Canvas& c, const Nav& nav) {
    Banner(c, "HOST A ROOM");
    int& f = g.focus;
    if (nav.up) f = (f + 2) % 3;
    if (nav.down) f = (f + 1) % 3;
    int playerOption = g.players - 2;
    if (Selector(c, 0, f, nav, 90, "Players", std::to_string(g.players) + " PLAYERS", playerOption, 3)) g.players = playerOption + 2;
    const int whoCount = ServerReady() ? 3 : 1;
    if (!ServerReady()) g.who = 2;
    const char* whoNames[3] = {"ANYONE", "FRIENDS WITH THE CODE", "THIS NETWORK ONLY"};
    int whoOption = ServerReady() ? g.who : 0;
    if (Selector(c, 1, f, nav, 34, "Who can join", whoNames[ServerReady() ? g.who : 2], whoOption, whoCount) && ServerReady()) {
        g.who = whoOption;
    }
    const char* whoHint[3] = {"Listed on the online server: anyone looking for a room can join.",
                              "Not listed: friends join with your room code.",
                              "Players on your own network, or over Tailscale."};
    Body(c, whoHint[ServerReady() ? g.who : 2], 0, -10, 16, kDim, 1, 600);
    if (Button(c, 2, f, nav.confirm, -200, -90, 200, -40, "OPEN ROOM", kPlayer[0])) HostRoom();
    Footer(c, "Everyone in the room plays on their own PC, with their own controller.");
    if (nav.back) g.screen = Screen::Main, g.focus = 0;
}

void CodeScreen(const Canvas& c, const Nav& nav) {
    Banner(c, "JOIN WITH A CODE");
    const bool address = g.code.find('.') != std::string::npos || g.code.size() > 5;
    // Controller entry: five letter slots; up/down changes the letter, left/right moves.
    if (!address) {
        if (g.code.size() < 5) g.code.resize(5, ' ');
        int& cur = g.codeCursor;
        if (nav.left) cur = std::max(0, cur - 1);
        if (nav.right) cur = std::min(5, cur + 1);
        if ((nav.up || nav.down) && cur < 5) {
            const char* at = std::strchr(kCodeLetters, g.code[static_cast<size_t>(cur)]);
            int index = at && g.code[static_cast<size_t>(cur)] != ' ' ? static_cast<int>(at - kCodeLetters) : -1;
            index = (index + (nav.down ? 1 : -1) + 32) % 32;
            g.code[static_cast<size_t>(cur)] = kCodeLetters[index];
        }
        // Typing fills the slots from the cursor.
        std::string typed;
        TypeInto(typed, 64, true);
        for (char ch : typed) {
            if (ch == '.' || ch == ':') {
                std::string trimmed = g.code;
                trimmed.erase(std::remove(trimmed.begin(), trimmed.end(), ' '), trimmed.end());
                g.code = trimmed + ch;
                break;
            }
            if (cur < 5) g.code[static_cast<size_t>(cur++)] = ch;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && cur > 0) g.code[static_cast<size_t>(--cur)] = ' ';
        for (int i = 0; i < 5; ++i) {
            const float x = -150 + i * 75;
            const bool on = cur == i;
            Plate(c, x - 30, 40, x + 30, 120, on ? 0x2B3350FFu : 0x1C2133FFu, 6.0f);
            if (on) c.list->AddRect(c.P(x - 38, 122), c.P(x + 38, 38), Col(kOrange, 0.6f + 0.4f * std::sin(c.t * 6)), 4.0f, 0, 3.0f);
            Text(c, *TitleFont(), std::string(1, g.code[static_cast<size_t>(i)]), x, 80, 48, kSnow, 0xFFE7B0FFu, 1, 0, true);
            if (on) {
                Arrow(c, x, 134, 16, 0, kOrange);
                Arrow(c, x, 26, 16, 1, kOrange);
            }
        }
        const bool full = g.code.find(' ') == std::string::npos;
        int f = cur == 5 ? 0 : -1;
        if (Button(c, 0, f, nav.confirm && cur == 5, -150, -70, 150, -24, "JOIN", kPlayer[1], full) ||
            (nav.confirm && cur < 5 && full)) {
            JoinTyped(g.code);
        }
        if (f == 0) cur = 5;
        Body(c, "Up/down picks letters, or type the code. An address works too (type it).", 0, -110, 16, kDim, 1, 700);
    } else {
        TypeInto(g.code, 60, true);
        Plate(c, -300, 50, 300, 110, 0x2B3350FFu, 6.0f);
        Text(c, *TitleFont(), g.code + ((static_cast<int>(c.t * 2) % 2) ? "_" : ""), 0, 80, 30, kSnow, 0xFFE7B0FFu, 1, 560, true);
        int f = 0;
        if (Button(c, 0, f, nav.confirm, -150, -50, 150, -4, "JOIN", kPlayer[1], !g.code.empty())) JoinTyped(g.code);
        Body(c, "An address: the host's IP or Tailscale name. Backspace to change it.", 0, -90, 16, kDim, 1, 700);
    }
    Footer(c, ServerReady() ? "Your own room code: " + Netplay::Relay::Code() : "Room codes need the online server.");
    if (nav.back) g.screen = Screen::Main, g.focus = 2;
}

void NameScreen(const Canvas& c, const Nav& nav) {
    Banner(c, "YOUR NAME");
    TypeInto(g.name, 20, false);
    Plate(c, -280, 40, 280, 110, 0x2B3350FFu, 6.0f);
    Text(c, *TitleFont(), g.name + ((static_cast<int>(c.t * 2) % 2) ? "_" : ""), 0, 75, 36, kSnow, 0xFFE7B0FFu, 1, 520, true);
    int f = 0;
    if (Button(c, 0, f, nav.confirm, -150, -50, 150, -4, "SAVE", kPlayer[3])) {
        SaveName();
        g.screen = Screen::Main;
        g.focus = 3;
    }
    Body(c, "Type a name with the keyboard. Other players see it in rooms and matches.", 0, -90, 16, kDim, 1, 700);
    Footer(c, "");
    if (nav.back) {
        g.name = Netplay::DeviceName();
        g.screen = Screen::Main;
        g.focus = 3;
    }
}

void BrowseScreen(const Canvas& c, const Nav& nav, const NetplayLobby::Snapshot& lobby) {
    Banner(c, "FIND ROOMS");
    const int count = static_cast<int>(lobby.rooms.size());
    int& f = g.focus;  // 0..count-1 rooms, count = "back"
    if (nav.up) f = std::max(0, f - 1);
    if (nav.down) f = std::min(count, f + 1);
    f = std::min(f, count);
    constexpr int kRows = 5;
    if (f < g.browseFirst) g.browseFirst = f;
    if (f >= g.browseFirst + kRows && f < count) g.browseFirst = f - kRows + 1;
    if (count == 0) {
        Text(c, *TitleFont(), "LOOKING FOR ROOMS", 0, 70, 30, kSnow, 0xFFE7B0FFu, 1, 0, true);
        SmashBall(c, 0, 10, 40, c.t * 2.0f);
        Body(c, ServerReady() ? "Rooms listed online, on your network and over Tailscale show up here."
                              : "Rooms on your network and over Tailscale show up here.",
             0, -40, 16, kDim, 1, 760);
    }
    for (int row = 0; row < kRows && g.browseFirst + row < count; ++row) {
        const int i = g.browseFirst + row;
        const auto& room = lobby.rooms[static_cast<size_t>(i)];
        const float y = 96 - row * 50;
        Netplay::Address address;
        const bool online = Netplay::ParseAddress(room.address, 1, address) && Netplay::Relay::IsRelayAddress(address);
        const uint32_t tint = online ? 0x24407AFFu : room.tailscale ? 0x3A2C66FFu : 0x24563CFFu;
        if (Button(c, i, f, nav.confirm, -420, y - 22, 420, y + 22, "", room.full ? 0x2A2E40FFu : tint, !room.full)) {
            NetplayLobby::Join(room.id);
        }
        Text(c, *TitleFont(), room.name, -395, y, 22, kSnow, 0xFFE7B0FFu, 0, 420, true);
        Body(c, std::to_string(room.players) + " / " + std::to_string(room.maxPlayers) + " players", 140, y, 17, kSnow, 1);
        Body(c, room.full ? "FULL" : online ? "ONLINE" : room.tailscale ? "TAILSCALE" : "YOUR NETWORK", 330, y, 15,
             room.full ? kRed : kGold, 1);
    }
    if (count > kRows) Body(c, std::to_string(count) + " rooms - up/down to see more", 0, -150, 14, kDim, 1);
    if (Button(c, count, f, nav.confirm, -120, -190, 120, -160, "BACK", 0x5A6078FFu, true, 20.0f) || nav.back) {
        g.pendingHost = {};
        NetplayLobby::Stop();
        g.focus = 1;
    }
    Footer(c, "Pick a room and press A to join.");
}

void Slots(const Canvas& c, int count, const std::vector<std::string>& names, int you, const uint32_t* ping, float y) {
    count = std::min(count, 4);
    for (int slot = 0; slot < 4; ++slot) {
        const float x0 = -400 + slot * 202, x1 = x0 + 190;
        const bool open = slot < count;
        const bool used = open && slot < static_cast<int>(names.size()) && !names[static_cast<size_t>(slot)].empty();
        Plate(c, x0, y - 60, x1, y + 60, !open ? 0x141724FFu : used ? kPlayer[slot] : 0x262B40FFu, 8.0f, open ? 1.0f : 0.6f);
        Text(c, *TitleFont(), "P" + std::to_string(slot + 1), x0 + 18, y + 38, 26, open ? kSnow : 0x50556AFFu,
             open ? 0xFFE7B0FFu : 0x50556AFFu, 0, 0, open);
        if (!open) continue;
        if (used) {
            Text(c, *TitleFont(), names[static_cast<size_t>(slot)], (x0 + x1) * 0.5f, y, 22, kSnow, 0xFFF0D0FFu, 1, 160, true);
            if (slot == you) Body(c, "YOU", (x0 + x1) * 0.5f, y - 34, 15, kSnow, 1);
            else if (ping != nullptr && ping[slot] != 0) Body(c, std::to_string(ping[slot]) + " ms", (x0 + x1) * 0.5f, y - 34, 15, kSnow, 1);
        } else {
            Body(c, "Waiting...", (x0 + x1) * 0.5f, y, 17, kDim, 1);
        }
        if (slot == you) c.list->AddRect(c.P(x0 - 6, y + 64), c.P(x1 + 6, y - 64), Col(0xFFFFFFFFu, 0.8f), 6.0f, 0, 3.0f);
    }
}

void RoomScreen(const Canvas& c, const Nav& nav, const NetplayLobby::Snapshot& lobby) {
    const bool hosting = lobby.phase == Phase::Hosting;
    Banner(c, hosting ? "YOUR ROOM" : lobby.roomName);
    std::vector<std::string> names(4);
    for (const auto& m : lobby.members) {
        if (m.slot < 4) names[m.slot] = m.name;
    }
    if (hosting && ServerReady() && g.who != 2) {
        Body(c, "ROOM CODE", -150, 100, 16, kDim, 2);
        Text(c, *TitleFont(), Netplay::Relay::Code(), -130, 100, 34, 0xFFF3B0FFu, kOrange, 0, 0, true);
        Body(c, g.who == 0 ? "Listed online: anyone can join" : "Friends join with this code", 120, 100, 15, kDim, 0, 300);
    }
    Slots(c, std::max<int>(lobby.maxPlayers, static_cast<int>(lobby.members.size())), names, lobby.localSlot, nullptr, 10);
    int& f = g.focus;
    if (hosting) {
        if (nav.left || nav.up) f = 0;
        if (nav.right || nav.down) f = 1;
        const bool ready = lobby.members.size() >= 2;
        if (Button(c, 0, f, nav.confirm, -330, -118, 30, -70, "START MATCH", kPlayer[0], ready)) NetplayLobby::Start();
        if (Button(c, 1, f, nav.confirm, 80, -118, 330, -70, "CLOSE ROOM", 0x5A6078FFu, true, 22.0f) || nav.back) {
            NetplayLobby::Stop();
            g.focus = 0;
        }
        Body(c, ready ? "Everyone's in? Start: every game restarts into the match with your save."
                      : "Waiting for players to join...",
             0, -150, 16, kDim, 1, 760);
    } else {
        f = 0;
        Body(c, "Waiting for the host to start the match...", 0, -60, 20, kSnow, 1);
        if (Button(c, 0, f, nav.confirm, -120, -128, 120, -86, "LEAVE ROOM", 0x5A6078FFu, true, 20.0f) || nav.back) {
            NetplayLobby::Stop();
            g.focus = 0;
        }
    }
    Footer(c, "Each player uses the controller in their own port 1.");
}

void BusyScreen(const Canvas& c, const std::string& title, const std::string& line, const Nav& nav, bool cancel) {
    Banner(c, title);
    SmashBall(c, 0, 40, 70, c.t * 2.5f);
    Body(c, line, 0, -30, 20, kSnow, 1, 700);
    int f = 0;
    if (cancel && (Button(c, 0, f, nav.confirm, -110, -110, 110, -70, "CANCEL", 0x5A6078FFu, true, 20.0f) || nav.back)) {
        NetplayLobby::Stop();
    }
    Footer(c, "");
}

void SessionScreen(const Canvas& c, const Nav& nav) {
    const NetplaySession::Status status = NetplaySession::GetStatus();
    Banner(c, "ONLINE MATCH");
    Slots(c, status.playerCount, NetplayStart::SessionNames(), status.localSlot, status.pingMs, 60);
    std::string line = "You are player " + std::to_string(status.localSlot + 1) + ".";
    uint32_t color = kSnow;
    if (status.lostSlot >= 0) {
        line = "Player " + std::to_string(status.lostSlot + 1) + " stopped answering.";
        color = kRed;
    } else if (status.desyncFrame != 0) {
        line = "The games went out of step (frame " + std::to_string(status.desyncFrame) + "). Leave and start a new room.";
        color = kRed;
    } else if (status.waitingForSlot >= 0 && status.waitedMs > 700) {
        line = "Waiting for player " + std::to_string(status.waitingForSlot + 1) + "...";
        color = kGold;
    }
    Body(c, line, 0, -30, 18, color, 1, 760);
    int f = 0;
    if (Button(c, 0, f, nav.confirm, -170, -120, 170, -74, "LEAVE THE MATCH", 0x5A6078FFu, true, 22.0f)) NetplayStart::EndSession();
    Footer(c, "Leaving restarts the game. The host keeps the match's save.");
    if (nav.back) g.open = false;
}

void DrawPanel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::Begin("##online", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                             ImGuiWindowFlags_NoScrollWithMouse)) {
        Canvas c{ImGui::GetWindowDrawList(), GL::GameView(), static_cast<float>(ImGui::GetTime())};
        // Lay the menu over the whole window, not just the game picture: 456 units across the window's height.
        c.view.centerX = vp->Pos.x + vp->Size.x * 0.5f;
        c.view.centerY = vp->Pos.y + vp->Size.y * 0.5f;
        c.view.scale = std::min(vp->Size.y / 470.0f, vp->Size.x / 960.0f);
        ImGui::SetWindowFocus();
        Backdrop(c);
        const Nav nav = ReadNav();
        const NetplayLobby::Snapshot lobby = NetplayLobby::Get();
        if (lobby.phase != g.lastPhase) {
            g.focus = 0;
            g.lastPhase = lobby.phase;
        }
        if (NetplaySession::Active()) {
            SessionScreen(c, nav);
        } else {
            switch (lobby.phase) {
            case Phase::Browsing: BrowseScreen(c, nav, lobby); break;
            case Phase::Joining: BusyScreen(c, "JOINING", "Asking the host to let you in...", nav, true); break;
            case Phase::Hosting:
            case Phase::InRoom: RoomScreen(c, nav, lobby); break;
            case Phase::Starting:
            case Phase::Restarting:
                BusyScreen(c, "STARTING", lobby.message.empty() ? "Getting everyone ready..." : lobby.message, nav, false);
                break;
            case Phase::Failed:
                if (g.message.empty() && !lobby.message.empty()) g.message = lobby.message;
                NetplayLobby::Stop();
                [[fallthrough]];
            case Phase::Off:
                switch (g.screen) {
                case Screen::Host: HostScreen(c, nav); break;
                case Screen::Code: CodeScreen(c, nav); break;
                case Screen::Name: NameScreen(c, nav); break;
                default: MainScreen(c, nav); break;
                }
                break;
            }
        }
        if (!g.message.empty()) Body(c, g.message, 0, -186, 16, kRed, 1, 800);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace

bool IsOpen() { return g.open; }

void SetOpen(bool open) {
    if (open && !g.open) {
        g.screen = Screen::Main;
        g.focus = 0;
        g.message.clear();
    }
    g.open = open;
}

void Draw() {
    if (g.name.empty()) {
        const std::string saved = RuntimeConfigFile::OnlineName();
        if (!saved.empty()) Netplay::SetDeviceName(saved);
        g.name = Netplay::DeviceName();
    }
    HandleIntent();
    const NetplayLobby::Snapshot lobby = NetplayLobby::Get();
    if (lobby.phase == Phase::Hosting && g.who == 0 && ServerReady()) {
        Netplay::Relay::SetListing(lobby.maxPlayers, static_cast<uint8_t>(lobby.members.size()), Netplay::DeviceName() + "'s room",
                                   Gecko::BuildIdentity());
    } else {
        Netplay::Relay::SetListing(0, 0, {}, {});
    }
    // A room code or address typed in: join its room as soon as the search finds it.
    if (lobby.phase == Phase::Browsing && g.pendingHost.Valid()) {
        for (const auto& room : lobby.rooms) {
            Netplay::Address address;
            if (Netplay::ParseAddress(room.address, 1, address) && address.ip == g.pendingHost.ip) {
                g.pendingHost = {};
                NetplayLobby::Join(room.id);
                break;
            }
        }
    }
    if (lobby.phase == Phase::Browsing && g.pendingHost.Valid() && Netplay::Relay::IsRelayAddress(g.pendingHost) &&
        ImGui::GetTime() - g.pendingSince > 20 && g.message.empty()) {
        g.message = "Can't reach that room yet. Check the code with the host. If it's right, a router between you blocks "
                    "direct connections: Tailscale (free) gets around that.";
    }
    if (lobby.phase != Phase::Browsing && lobby.phase != Phase::Joining) g.pendingHost = {};
    if (g.open) DrawPanel();
}

} // namespace OnlineMenu
