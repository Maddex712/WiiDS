#include "gx_cheat_menu.h"
#include "gx_internal.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

extern "C" int port_cheat_god_mode;
extern "C" int port_cheat_movement_speed;
extern "C" int port_cheat_jump_height;
extern "C" int port_cheat_infinite_lives;
extern "C" int port_cheat_infinite_fludd;
extern "C" int port_cheat_coin_multiplier;

namespace {

const char* const kOnOffNames[] = {"OFF", "ON"};
const int kOnOffValues[] = {0, 1};
const char* const kMovementNames[] = {"OFF", "2x", "4x"};
const int kMovementValues[] = {1, 2, 4};
const char* const kJumpNames[] = {"OFF", "2x", "4x", "6x"};
const int kJumpValues[] = {1, 2, 4, 6};
const char* const kCoinNames[] = {"OFF", "2x", "5x", "10x"};
const int kCoinValues[] = {1, 2, 5, 10};

struct Option {
    const char* section;  // the heading above the row when it starts a section
    const char* label;
    int* value;
    const char* const* names;
    const int* values;
    int count;
};

const Option kOptions[] = {
    {"PLAYER", "God Mode", &port_cheat_god_mode, kOnOffNames, kOnOffValues, 2},
    {nullptr, "Infinite Lives", &port_cheat_infinite_lives, kOnOffNames, kOnOffValues, 2},
    {nullptr, "Infinite FLUDD", &port_cheat_infinite_fludd, kOnOffNames, kOnOffValues, 2},
    {"MOVEMENT", "Movement Speed", &port_cheat_movement_speed, kMovementNames, kMovementValues, 3},
    {nullptr, "Jump Height", &port_cheat_jump_height, kJumpNames, kJumpValues, 4},
    {"ITEMS", "Coin Multiplier", &port_cheat_coin_multiplier, kCoinNames, kCoinValues, 4},
};
constexpr int kOptionCount = int(sizeof kOptions / sizeof kOptions[0]);

// Layout units, except kCapH: stb_easy_font's capitals are 7 font pixels high.
const int kCapH = 7;
const int kPad = 6, kTitleH = 17, kSectionH = 14, kRowH = 15, kFooterH = 18;
const int kBoxW = 50, kArrowW = 10, kColumnGap = 16;
const int kMargin = 16;  // from the window's top and right edges
// Scales the panel's geometry; the glyphs keep their integer scale.
constexpr float kNativeMenuSizeScale = 1.20f;

const uint8_t kPanelBg[4] = {15, 15, 15, 240};
const uint8_t kLine[4] = {80, 80, 92, 240};
const uint8_t kTitleBg[4] = {41, 74, 122, 255};
const uint8_t kText[4] = {255, 255, 255, 255};
const uint8_t kTextDim[4] = {128, 128, 128, 255};
const uint8_t kArrowDim[4] = {80, 80, 92, 255};
const uint8_t kRowSelected[4] = {34, 60, 94, 240};
const uint8_t kAccent[4] = {66, 150, 250, 255};
const uint8_t kBox[4] = {29, 47, 73, 240};
const uint8_t kBoxSelected[4] = {52, 96, 150, 240};
const uint8_t kBoxOn[4] = {41, 98, 180, 255};

bool s_visible = false;
int s_selected = 0;
bool s_heldByMenu[SDL_NUM_SCANCODES];

// GL thread: the bitmap last uploaded and what it shows
typedef std::array<int, 3 + kOptionCount> Shown;
std::vector<uint8_t> s_pixels;
Shown s_shown;
bool s_shownValid = false;

int indexOf(const Option& o) {
    for (int i = 0; i < o.count; i++)
        if (o.values[i] == *o.value) return i;
    return 0;
}

void step(const Option& o, int delta, bool wrap) {
    const int i = indexOf(o) + delta;
    *o.value = o.values[wrap ? (i + o.count) % o.count : std::clamp(i, 0, o.count - 1)];
}

// The top of option i's row; i == kOptionCount gives the end of the last row.
int rowTop(int i) {
    int y = kTitleH + 3;
    for (int k = 0; k < i; k++) y += (kOptions[k].section ? kSectionH : 0) + kRowH;
    return y + (i < kOptionCount && kOptions[i].section ? kSectionH : 0);
}

struct Rect {
    int x0, y0, x1, y1;
    bool contains(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

// All of the menu's rectangles and text positions, in window pixels from the panel's top-left.
struct Layout {
    int glyph;         // window pixels per font pixel
    float unit;        // window pixels per layout unit
    int logicalWidth;  // layout units
    int width, height;
    int x, y;  // the panel's top-left in the window

    int at(float units) const { return int(std::lround(units * unit)); }
    int boxX() const { return logicalWidth - kPad - kBoxW; }
    int textY(const Rect& band) const { return band.y0 + (band.y1 - band.y0 - kCapH * glyph) / 2; }
    Rect rect(float x0, float y0, float x1, float y1) const { return {at(x0), at(y0), at(x1), at(y1)}; }
    Rect title() const { return {0, 0, width, at(kTitleH)}; }
    Rect closeBox() const { return {at(logicalWidth - kTitleH), 0, width, at(kTitleH)}; }
    Rect sectionBand(int i) const { return {0, at(rowTop(i) - kSectionH), width, at(rowTop(i))}; }
    Rect row(int i) const { return {0, at(rowTop(i)), width, at(rowTop(i) + kRowH)}; }
    Rect box(int i) const { return rect(boxX(), rowTop(i) + 1, boxX() + kBoxW, rowTop(i) + kRowH - 1); }
    Rect arrowLeft(int i) const { return rect(boxX(), rowTop(i), boxX() + kArrowW, rowTop(i) + kRowH); }
    Rect arrowRight(int i) const {
        return rect(boxX() + kBoxW - kArrowW, rowTop(i), boxX() + kBoxW, rowTop(i) + kRowH);
    }
};

Layout placedAt(int glyph, int logicalWidth, int logicalHeight, int winW) {
    Layout l;
    l.glyph = glyph;
    l.unit = glyph * kNativeMenuSizeScale;
    l.logicalWidth = logicalWidth;
    l.width = l.at(logicalWidth);
    l.height = l.at(logicalHeight);
    l.x = winW - l.width - l.at(kMargin);
    l.y = l.at(kMargin);
    return l;
}

Layout layoutFor(int winW, int winH) {
    int labelW = 0;
    for (const Option& o : kOptions) labelW = std::max(labelW, gx::overlayTextWidth(o.label));
    const int logicalWidth = kPad + labelW + kColumnGap + kBoxW + kPad;
    const int logicalHeight = rowTop(kOptionCount) + kFooterH;
    int glyph = std::max(2, (winH + 360) / 720);  // a font pixel is about a 720th of the height, at least 2 pixels
    Layout l = placedAt(glyph, logicalWidth, logicalHeight, winW);
    while (glyph > 1 && (l.x < 0 || l.y + l.height > winH))
        l = placedAt(--glyph, logicalWidth, logicalHeight, winW);
    return l;
}

struct Hit {
    int row = -1;
    int arrow = 0;  // -1 or 1 on a choice's < or >
    bool close = false;
};

Hit hitTest(const Layout& l, int px, int py) {
    Hit hit;
    const int x = px - l.x, y = py - l.y;
    if (x < 0 || y < 0 || x >= l.width || y >= l.height) return hit;
    if (l.title().contains(x, y)) {
        hit.close = l.closeBox().contains(x, y);
        return hit;
    }
    for (int i = 0; i < kOptionCount; i++) {
        if (!l.row(i).contains(x, y)) continue;
        hit.row = i;
        if (kOptions[i].count > 2 && l.arrowLeft(i).contains(x, y)) hit.arrow = -1;
        if (kOptions[i].count > 2 && l.arrowRight(i).contains(x, y)) hit.arrow = 1;
    }
    return hit;
}

void rasterize(const Layout& l) {
    const int w = l.width, h = l.height, g = l.glyph;
    s_pixels.resize(size_t(w) * h * 4);
    auto fill = [&](const Rect& r, const uint8_t c[4]) { gx::overlayFillRect(s_pixels, w, h, r.x0, r.y0, r.x1, r.y1, c); };
    auto text = [&](int x, int y, const char* str, const uint8_t c[4]) { gx::overlayText(s_pixels, w, h, x, y, str, c, g); };
    auto textWidth = [&](const char* str) { return gx::overlayTextWidth(str) * g; };
    fill(Rect{0, 0, w, h}, kPanelBg);
    const Rect title = l.title();
    fill(title, kTitleBg);
    text(l.at(kPad), l.textY(title), "Cheats", kText);
    text(l.at(l.logicalWidth - kPad - 5), l.textY(title), "X", kText);
    for (int i = 0; i < kOptionCount; i++) {
        const Option& o = kOptions[i];
        if (o.section) {
            const Rect band = l.sectionBand(i);
            const int lineY = (band.y0 + band.y1) / 2 - g / 2;
            const int headingX = l.at(kPad + 8);
            const int headingEnd = headingX + textWidth(o.section);
            fill(Rect{l.at(kPad), lineY, headingX - l.at(3), lineY + g}, kLine);
            text(headingX, l.textY(band), o.section, kText);
            fill(Rect{headingEnd + l.at(2), lineY, w - l.at(kPad), lineY + g}, kLine);
        }
        const Rect r = l.row(i);
        const bool selected = i == s_selected;
        if (selected) {
            fill(r, kRowSelected);
            fill(Rect{0, r.y0, 2 * g, r.y1}, kAccent);
        }
        text(l.at(kPad), l.textY(r), o.label, kText);
        const int index = indexOf(o);
        const Rect box = l.box(i);
        fill(box, index ? kBoxOn : selected ? kBoxSelected : kBox);
        const char* name = o.names[index];
        text(box.x0 + (box.x1 - box.x0 - textWidth(name)) / 2, l.textY(r), name, index ? kText : kTextDim);
        if (o.count > 2) {
            text(l.at(l.boxX() + 3), l.textY(r), "<", index > 0 ? kText : kArrowDim);
            text(l.at(l.boxX() + kBoxW - 6), l.textY(r), ">", index < o.count - 1 ? kText : kArrowDim);
        }
    }
    const int footer = rowTop(kOptionCount) + 3;
    text(l.at(kPad), l.at(footer + 4), "F9 Close Menu", kTextDim);
    // Hairlines stay one or two window pixels thick at any scale.
    const int t = std::max(1, g / 2);
    const int footerY = l.at(footer);
    fill(Rect{0, footerY, w, footerY + t}, kLine);
    fill(Rect{0, 0, w, t}, kLine);
    fill(Rect{0, h - t, w, h}, kLine);
    fill(Rect{0, 0, t, h}, kLine);
    fill(Rect{w - t, 0, w, h}, kLine);
}

Shown shownState(int winW, int winH) {
    Shown state{};
    state[0] = winW;
    state[1] = winH;
    state[2] = s_selected;
    for (int i = 0; i < kOptionCount; i++) state[3 + i] = *kOptions[i].value;
    return state;
}

bool isArrow(SDL_Scancode key) {
    return key == SDL_SCANCODE_UP || key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_RIGHT;
}

// False for keys that are not the menu's.
bool menuKey(SDL_Scancode key) {
    const Option& o = kOptions[s_selected];
    switch (key) {
    case SDL_SCANCODE_UP: s_selected = (s_selected + kOptionCount - 1) % kOptionCount; return true;
    case SDL_SCANCODE_DOWN: s_selected = (s_selected + 1) % kOptionCount; return true;
    case SDL_SCANCODE_LEFT: step(o, -1, false); return true;
    case SDL_SCANCODE_RIGHT: step(o, 1, false); return true;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_SPACE: step(o, 1, true); return true;
    case SDL_SCANCODE_ESCAPE: s_visible = false; return true;
    default: return false;
    }
}

bool onKey(const SDL_KeyboardEvent& ev) {
    const SDL_Scancode key = ev.keysym.scancode;
    if (unsigned(key) >= SDL_NUM_SCANCODES) return false;
    if (ev.type == SDL_KEYUP) {
        s_heldByMenu[key] = false;
        return false;
    }
    if (ev.repeat) {
        if (!s_heldByMenu[key]) return false;
        if (s_visible && isArrow(key)) menuKey(key);
        return true;  // still the menu's after it closed: a repeating Escape would quit the game
    }
    if (!s_visible || !menuKey(key)) return false;
    s_heldByMenu[key] = true;
    return true;
}

bool onMouse(const SDL_Event& ev) {
    if (!s_visible) return false;
    const bool motion = ev.type == SDL_MOUSEMOTION;
    int winW = 0, winH = 0, drawW = 0, drawH = 0;
    if (SDL_Window* window = SDL_GetWindowFromID(motion ? ev.motion.windowID : ev.button.windowID)) {
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_GL_GetDrawableSize(window, &drawW, &drawH);
    }
    if (winW <= 0 || winH <= 0) return true;
    // Events are in window coordinates, the panel in drawable pixels: they differ at high DPI.
    const int x = (motion ? ev.motion.x : ev.button.x) * drawW / winW;
    const int y = (motion ? ev.motion.y : ev.button.y) * drawH / winH;
    const Hit hit = hitTest(layoutFor(drawW, drawH), x, y);
    if (hit.row >= 0) s_selected = hit.row;
    if (ev.type != SDL_MOUSEBUTTONDOWN) return true;
    const bool left = ev.button.button == SDL_BUTTON_LEFT;
    if (!left && ev.button.button != SDL_BUTTON_RIGHT) return true;
    if (hit.close && left) s_visible = false;
    else if (hit.row >= 0 && hit.arrow) step(kOptions[hit.row], hit.arrow, false);
    else if (hit.row >= 0) step(kOptions[hit.row], left ? 1 : -1, true);
    return true;
}

}  // namespace

namespace gx {

bool cheatMenuVisible() { return s_visible; }

void cheatMenuToggle() { s_visible = !s_visible; }

bool cheatMenuHandleEvent(const SDL_Event& ev) {
    switch (ev.type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        return onKey(ev.key);
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        return onMouse(ev);
    default:
        return false;
    }
}

void cheatMenuRender(int winW, int winH) {
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
    drawOverlayPanel(OVERLAY_CHEAT_MENU, changed ? s_pixels.data() : nullptr, l.width, l.height, l.x, l.y, 1, winW, winH);
}

}  // namespace gx
