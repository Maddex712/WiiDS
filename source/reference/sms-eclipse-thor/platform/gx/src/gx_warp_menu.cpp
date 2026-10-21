#include "gx_warp_menu.h"
#include "gx_internal.h"

#include "port_include/port_warp.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

// Layout units, except kCapH: stb_easy_font's capitals are 7 font pixels high.
const int kCapH = 7;
const int kPad = 6, kTitleH = 17, kRowH = 14, kFooterH = 18, kNoteH = 14;
const int kRows = 14;  // rows shown at once; the list scrolls
const int kColumnGap = 16, kMinWidth = 200, kMaxWidth = 340;
const float kSizeScale = 1.20f;  // as the cheat menu's

const uint8_t kPanelBg[4] = {15, 15, 15, 240};
const uint8_t kLine[4] = {80, 80, 92, 240};
const uint8_t kTitleBg[4] = {41, 74, 122, 255};
const uint8_t kText[4] = {255, 255, 255, 255};
const uint8_t kTextDim[4] = {128, 128, 128, 255};
const uint8_t kNote[4] = {240, 200, 90, 255};
const uint8_t kRowSelected[4] = {34, 60, 94, 240};
const uint8_t kAccent[4] = {66, 150, 250, 255};

struct World {
    std::string name;
    int first, count;  // its episodes in s_entries
};

// The game's list, copied once it is built (it does not change afterwards).
std::vector<PortWarpEntry> s_entries;
std::vector<World> s_worlds;

bool s_visible = false;
int s_level = 0;           // 0: the worlds, 1: the episodes of s_world
int s_world = 0, s_episode = 0;
int s_scroll = 0;          // the first row shown
bool s_notReady = false;   // a warp was picked while none could start
bool s_heldKey[SDL_NUM_SCANCODES];
bool s_heldButton[SDL_CONTROLLER_BUTTON_MAX];

// A held d-pad direction or stick steps again after a pause, as a key repeats.
int s_padDir = 0, s_stickDir = 0;
uint32_t s_repeatAt = 0;
const uint32_t kRepeatDelay = 350, kRepeatRate = 70;

// GL thread: the bitmap last uploaded and what it shows
typedef std::array<int, 9> Shown;
std::vector<uint8_t> s_pixels;
Shown s_shown;
bool s_shownValid = false;

void loadList() {
    if (!s_entries.empty()) return;
    const int n = port_warp_count();
    if (n <= 0) return;
    s_entries.resize(n);
    for (int i = 0; i < n; i++) port_warp_entry(i, &s_entries[i]);
    s_worlds.clear();
    for (int i = 0; i < n; i++) {
        if (s_worlds.empty() || s_worlds.back().name != s_entries[i].world)
            s_worlds.push_back(World{s_entries[i].world, i, 0});
        s_worlds.back().count++;
    }
}

int rowCount() {
    if (s_worlds.empty()) return 1;  // "Loading..."
    return s_level == 0 ? int(s_worlds.size()) : s_worlds[s_world].count;
}

int& selected() { return s_level == 0 ? s_world : s_episode; }

void keepInView() {
    const int n = rowCount();
    int& sel = selected();
    sel = std::clamp(sel, 0, n - 1);
    if (sel < s_scroll) s_scroll = sel;
    if (sel >= s_scroll + kRows) s_scroll = sel - kRows + 1;
    s_scroll = std::clamp(s_scroll, 0, std::max(0, n - kRows));
}

void move(int delta, bool wrap) {
    if (s_worlds.empty()) return;
    const int n = rowCount();
    int& sel = selected();
    if (wrap) sel = ((sel + delta) % n + n) % n;
    else sel = std::clamp(sel + delta, 0, n - 1);
    s_notReady = false;
    keepInView();
}

void open() {
    s_visible = true;
    s_notReady = false;
    port_warp_want_list();
    loadList();
}

void close() {
    s_visible = false;
    s_padDir = s_stickDir = 0;
}

void enterLevel(int level) {
    s_level = level;
    s_scroll = 0;
    s_notReady = false;
    if (level == 1) s_episode = 0;
    keepInView();
}

void activate() {
    if (s_worlds.empty()) return;
    if (s_level == 0) {
        enterLevel(1);
        return;
    }
    if (!port_warp_ready()) {
        s_notReady = true;
        return;
    }
    const PortWarpEntry& e = s_entries[s_worlds[s_world].first + s_episode];
    port_warp_request(e.area, e.episode);
    close();
}

void back() {
    if (s_level == 1) {
        enterLevel(0);
        return;
    }
    close();
}

// str cut to fit maxPx font pixels, with "..." where it was cut.
std::string fit(const char* str, int maxPx) {
    std::string s = str;
    if (gx::overlayTextWidth(s.c_str()) <= maxPx) return s;
    while (!s.empty() && gx::overlayTextWidth((s + "...").c_str()) > maxPx) s.pop_back();
    return s + "...";
}

struct Rect {
    int x0, y0, x1, y1;
    bool contains(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

int rowTop(int i) { return kTitleH + 3 + i * kRowH; }

// The panel's rectangles, in window pixels from its top-left.
struct Layout {
    int glyph;         // window pixels per font pixel
    float unit;        // window pixels per layout unit
    int logicalWidth;  // layout units
    int width, height;
    int x, y;  // the panel's top-left in the window

    int at(float units) const { return int(std::lround(units * unit)); }
    int textY(const Rect& band) const { return band.y0 + (band.y1 - band.y0 - kCapH * glyph) / 2; }
    Rect title() const { return {0, 0, width, at(kTitleH)}; }
    Rect closeBox() const { return {at(logicalWidth - kTitleH), 0, width, at(kTitleH)}; }
    Rect row(int i) const { return {0, at(rowTop(i)), width, at(rowTop(i) + kRowH)}; }
    int footerTop() const { return rowTop(kRows) + 3; }
};

int logicalHeight() { return rowTop(kRows) + 3 + kFooterH + (s_notReady || !port_warp_ready() ? kNoteH : 0); }

// As wide as the longest name and its column need, within kMinWidth..kMaxWidth.
int contentWidth() {
    int w = 0;
    for (const World& world : s_worlds) w = std::max(w, gx::overlayTextWidth(world.name.c_str()) + 30);
    for (const PortWarpEntry& e : s_entries)
        w = std::max(w, gx::overlayTextWidth(e.name) + kColumnGap + gx::overlayTextWidth(e.scene));
    return std::clamp(kPad + w + kPad, kMinWidth, kMaxWidth);
}

Layout placedAt(int glyph, int logicalWidth, int winW, int winH) {
    Layout l;
    l.glyph = glyph;
    l.unit = glyph * kSizeScale;
    l.logicalWidth = logicalWidth;
    l.width = l.at(logicalWidth);
    l.height = l.at(logicalHeight());
    l.x = (winW - l.width) / 2;
    l.y = std::max(0, (winH - l.height) / 3);
    return l;
}

Layout layoutFor(int winW, int winH) {
    const int logicalWidth = contentWidth();
    int glyph = std::max(2, (winH + 360) / 720);  // a font pixel is about a 720th of the height
    Layout l = placedAt(glyph, logicalWidth, winW, winH);
    while (glyph > 1 && (l.x < 0 || l.height > winH)) l = placedAt(--glyph, logicalWidth, winW, winH);
    return l;
}

void rasterize(const Layout& l) {
    const int w = l.width, h = l.height, g = l.glyph;
    s_pixels.resize(size_t(w) * h * 4);
    auto fill = [&](const Rect& r, const uint8_t c[4]) { gx::overlayFillRect(s_pixels, w, h, r.x0, r.y0, r.x1, r.y1, c); };
    auto text = [&](int x, int y, const std::string& str, const uint8_t c[4]) {
        gx::overlayText(s_pixels, w, h, x, y, str.c_str(), c, g);
    };
    auto textWidth = [&](const std::string& str) { return gx::overlayTextWidth(str.c_str()) * g; };
    // Font pixels a row's text may take, of the panel's width
    const int innerPx = int((l.logicalWidth - 2 * kPad) * kSizeScale);
    fill(Rect{0, 0, w, h}, kPanelBg);
    const Rect title = l.title();
    fill(title, kTitleBg);
    const std::string heading = s_level == 1 && !s_worlds.empty() ? s_worlds[s_world].name : "Warp";
    text(l.at(kPad), l.textY(title), fit(heading.c_str(), innerPx - 20), kText);
    text(l.at(l.logicalWidth - kPad - 5), l.textY(title), "X", kText);
    const int n = rowCount();
    for (int i = 0; i < kRows && s_scroll + i < n; i++) {
        const int index = s_scroll + i;
        const Rect r = l.row(i);
        if (s_worlds.empty()) {
            text(l.at(kPad), l.textY(r), "Loading...", kTextDim);
            break;
        }
        const bool sel = index == selected();
        if (sel) {
            fill(r, kRowSelected);
            fill(Rect{0, r.y0, 2 * g, r.y1}, kAccent);
        }
        std::string label, right;
        if (s_level == 0) {
            label = s_worlds[index].name;
            right = ">";
        } else {
            const PortWarpEntry& e = s_entries[s_worlds[s_world].first + index];
            label = e.name;
            right = e.scene;
        }
        const int rightPx = gx::overlayTextWidth(right.c_str());
        text(l.at(kPad), l.textY(r), fit(label.c_str(), innerPx - rightPx - kColumnGap), kText);
        text(w - l.at(kPad) - textWidth(right), l.textY(r), right, kTextDim);
    }
    const int footerY = l.at(l.footerTop());
    // Where the selection is in a list longer than the rows shown
    std::string position;
    if (n > kRows && !s_worlds.empty()) position = std::to_string(selected() + 1) + "/" + std::to_string(n);
    if (!position.empty()) text(w - l.at(kPad) - textWidth(position), footerY + l.at(4), position, kTextDim);
    const char* hint = s_level == 0 ? "A/Enter Open   B/Esc Close" : "A/Enter Warp   B/Esc Back";
    text(l.at(kPad), footerY + l.at(4), fit(hint, innerPx - gx::overlayTextWidth(position.c_str()) - kColumnGap),
         kTextDim);
    if (s_notReady || !port_warp_ready())
        text(l.at(kPad), footerY + l.at(kFooterH + 2), fit("Warps start in a stage, while it plays", innerPx),
             s_notReady ? kNote : kTextDim);
    // Hairlines stay one or two window pixels thick at any scale.
    const int t = std::max(1, g / 2);
    fill(Rect{0, footerY, w, footerY + t}, kLine);
    fill(Rect{0, 0, w, t}, kLine);
    fill(Rect{0, h - t, w, h}, kLine);
    fill(Rect{0, 0, t, h}, kLine);
    fill(Rect{w - t, 0, w, h}, kLine);
}

Shown shownState(int winW, int winH) {
    return Shown{winW, winH, s_level, s_world, s_episode, s_scroll, int(s_entries.size()), s_notReady,
                 port_warp_ready()};
}

// Up and down step and repeat; the rest act once. False for keys that are not the menu's.
bool menuKey(SDL_Scancode key, bool repeat) {
    switch (key) {
    case SDL_SCANCODE_UP: move(-1, !repeat); return true;
    case SDL_SCANCODE_DOWN: move(1, !repeat); return true;
    case SDL_SCANCODE_PAGEUP: move(-kRows, false); return true;
    case SDL_SCANCODE_PAGEDOWN: move(kRows, false); return true;
    case SDL_SCANCODE_HOME: move(-rowCount(), false); return true;
    case SDL_SCANCODE_END: move(rowCount(), false); return true;
    default: break;
    }
    if (repeat) return s_heldKey[key];
    switch (key) {
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_SPACE:
    case SDL_SCANCODE_RIGHT: activate(); return true;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_BACKSPACE:
    case SDL_SCANCODE_ESCAPE: back(); return true;
    default: return false;
    }
}

bool onKey(const SDL_KeyboardEvent& ev) {
    const SDL_Scancode key = ev.keysym.scancode;
    if (unsigned(key) >= SDL_NUM_SCANCODES) return false;
    if (ev.type == SDL_KEYUP) {
        const bool held = s_heldKey[key];
        s_heldKey[key] = false;
        return held;
    }
    if (ev.repeat) {
        if (!s_heldKey[key]) return false;
        if (s_visible) menuKey(key, true);
        return true;  // still the menu's after it closed: a repeating Escape would quit the game
    }
    if (!s_visible || !menuKey(key, false)) return false;
    s_heldKey[key] = true;
    return true;
}

// A first step on a held direction; warpMenuTick repeats it.
void stepHeld(int dir, bool wrap) {
    move(dir, wrap);
    s_repeatAt = SDL_GetTicks() + kRepeatDelay;
}

// Every button pressed while the menu is open is the menu's, and so is its
// release: closing the menu with B must not also press B in the game.
bool onButton(const SDL_ControllerButtonEvent& ev) {
    const int b = ev.button;
    if (b < 0 || b >= SDL_CONTROLLER_BUTTON_MAX) return false;
    if (ev.type == SDL_CONTROLLERBUTTONUP) {
        const bool held = s_heldButton[b];
        s_heldButton[b] = false;
        if ((b == SDL_CONTROLLER_BUTTON_DPAD_UP && s_padDir < 0) || (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN && s_padDir > 0))
            s_padDir = 0;
        return held;
    }
    if (!s_visible) return false;
    s_heldButton[b] = true;
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        s_padDir = -1;
        stepHeld(-1, true);
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        s_padDir = 1;
        stepHeld(1, true);
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: move(-kRows, false); break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: move(kRows, false); break;
    case SDL_CONTROLLER_BUTTON_A:
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: activate(); break;
    case SDL_CONTROLLER_BUTTON_B:
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: back(); break;
    case SDL_CONTROLLER_BUTTON_START: close(); break;
    default: break;
    }
    return true;
}

// The left stick steps through the list too. Its motion still reaches the
// game's pad, which reads nothing while the menu is open.
void onAxis(const SDL_ControllerAxisEvent& ev) {
    if (!s_visible || ev.axis != SDL_CONTROLLER_AXIS_LEFTY) return;
    const int dir = ev.value < -16000 ? -1 : ev.value > 16000 ? 1 : 0;
    if (dir == s_stickDir) return;
    s_stickDir = dir;
    if (dir) stepHeld(dir, true);
}

bool onMouse(const SDL_Event& ev) {
    if (!s_visible) return false;
    const bool motion = ev.type == SDL_MOUSEMOTION;
    const bool wheel = ev.type == SDL_MOUSEWHEEL;
    const Uint32 windowID = motion ? ev.motion.windowID : wheel ? ev.wheel.windowID : ev.button.windowID;
    int winW = 0, winH = 0, drawW = 0, drawH = 0;
    if (SDL_Window* window = SDL_GetWindowFromID(windowID)) {
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_GL_GetDrawableSize(window, &drawW, &drawH);
    }
    if (winW <= 0 || winH <= 0) return true;
    if (wheel) {
        move(ev.wheel.y > 0 ? -3 : ev.wheel.y < 0 ? 3 : 0, false);
        return true;
    }
    // Events are in window coordinates, the panel in drawable pixels: they differ at high DPI.
    const int px = (motion ? ev.motion.x : ev.button.x) * drawW / winW;
    const int py = (motion ? ev.motion.y : ev.button.y) * drawH / winH;
    const Layout l = layoutFor(drawW, drawH);
    const int x = px - l.x, y = py - l.y;
    const bool inside = x >= 0 && y >= 0 && x < l.width && y < l.height;
    int row = -1;
    for (int i = 0; inside && !s_worlds.empty() && i < kRows && s_scroll + i < rowCount(); i++)
        if (l.row(i).contains(x, y)) row = s_scroll + i;
    if (row >= 0 && (motion || ev.type == SDL_MOUSEBUTTONDOWN) && row != selected()) {
        selected() = row;
        s_notReady = false;
    }
    if (ev.type != SDL_MOUSEBUTTONDOWN) return true;
    if (ev.button.button == SDL_BUTTON_RIGHT) back();
    else if (ev.button.button == SDL_BUTTON_LEFT) {
        if (inside && l.closeBox().contains(x, y)) close();
        else if (row >= 0) activate();
    }
    return true;
}

}  // namespace

namespace gx {

bool warpMenuVisible() { return s_visible; }

void warpMenuToggle() {
    if (s_visible) close();
    else open();
}

bool warpMenuHandleEvent(const SDL_Event& ev) {
    switch (ev.type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        return onKey(ev.key);
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        return onButton(ev.cbutton);
    case SDL_CONTROLLERAXISMOTION:
        onAxis(ev.caxis);
        return false;
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
        return onMouse(ev);
    default:
        return false;
    }
}

void warpMenuTick() {
    if (!s_visible) return;
    if (s_entries.empty()) {
        loadList();
        if (!s_entries.empty()) keepInView();
    }
    const int dir = s_padDir ? s_padDir : s_stickDir;
    if (dir && int32_t(SDL_GetTicks() - s_repeatAt) >= 0) {
        move(dir, false);
        s_repeatAt = SDL_GetTicks() + kRepeatRate;
    }
}

void warpMenuRender(int winW, int winH) {
    if (!s_visible || winW <= 0 || winH <= 0) {
        s_shownValid = false;
        return;
    }
    const Layout l = layoutFor(winW, winH);
    const Shown state = shownState(winW, winH);
    const bool changed = !s_shownValid || state != s_shown;
    if (changed) {
        rasterize(l);
        s_shown = state;
        s_shownValid = true;
    }
    drawOverlayPanel(OVERLAY_WARP_MENU, changed ? s_pixels.data() : nullptr, l.width, l.height, l.x, l.y, 1, winW, winH);
}

}  // namespace gx
