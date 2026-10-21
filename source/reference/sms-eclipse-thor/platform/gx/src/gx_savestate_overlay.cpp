// Save states' share of the window (src/port_include/port_savestate.h): the
// line that says a state was saved or loaded, drawn with the debug overlay's
// renderer, and the picture of the last frame a new state keeps. The state
// menu itself is the game's (src/savestate_menu.cpp).
#include "gx_savestate_overlay.h"
#include "gx_internal.h"

#include "port_include/port_savestate.h"
#include "sms_gx/gx_pc.h"

#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

namespace {

const int kCapH = 7;  // stb_easy_font's capitals are 7 font pixels high
const uint8_t kPanelBg[4] = {15, 15, 15, 240};
const uint8_t kText[4] = {255, 255, 255, 255};
const uint8_t kAccent[4] = {66, 150, 250, 255};

// The port's side (platform/savestate); none in the GX tests.
const PortSaveStateOverlayApi* s_api;

std::vector<uint8_t> s_toastPixels;
std::string s_toastShown;
int s_toastW = 0, s_toastH = 0, s_toastGlyph = 0;

// The message ("State saved") in a small panel low in the window, between the
// HUD's corners.
void renderToast(int winW, int winH) {
    char message[128];
    if (!s_api || s_api->message(message, sizeof message) <= 0) {
        s_toastShown.clear();
        return;
    }
    const int glyph = std::max(2, (winH + 360) / 720);
    const bool changed = message != s_toastShown || glyph != s_toastGlyph;
    if (changed) {
        s_toastShown = message;
        s_toastGlyph = glyph;
        const int pad = 6 * glyph;
        s_toastW = gx::overlayTextWidth(message) * glyph + 2 * pad;
        s_toastH = kCapH * glyph + 2 * pad;
        s_toastPixels.assign(size_t(s_toastW) * s_toastH * 4, 0);
        gx::overlayFillRect(s_toastPixels, s_toastW, s_toastH, 0, 0, s_toastW, s_toastH, kPanelBg);
        gx::overlayFillRect(s_toastPixels, s_toastW, s_toastH, 0, 0, 2 * glyph, s_toastH, kAccent);
        gx::overlayText(s_toastPixels, s_toastW, s_toastH, pad, pad, message, kText, glyph);
    }
    gx::drawOverlayPanel(gx::OVERLAY_STATE_TOAST, changed ? s_toastPixels.data() : nullptr, s_toastW, s_toastH,
                         (winW - s_toastW) / 2, winH - s_toastH - winH / 10, 1, winW, winH);
}

}  // namespace

extern "C" void GXPC_SetSaveStateApi(const PortSaveStateOverlayApi* api) { s_api = api; }

// A picture of the last frame shown for a new state: at most 320 pixels wide,
// as a PNG (platform/savestate). The caller frees it.
extern "C" uint8_t* GXPC_SaveStateThumbnail(size_t* size) {
    *size = 0;
    int w = 0, h = 0;
    if (!GXPC_ReadXFB(nullptr, nullptr, &w, &h) || w <= 0 || h <= 0) return nullptr;
    std::vector<uint8_t> full(size_t(w) * h * 4);
    if (!GXPC_ReadXFB(nullptr, full.data(), &w, &h)) return nullptr;
    const int tw = std::min(w, 320), th = int(int64_t(h) * tw / w);
    if (th <= 0) return nullptr;
    std::vector<uint8_t> small(size_t(tw) * th * 4);
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            // the average of the pixels it covers
            const int x0 = x * w / tw, x1 = std::max(x0 + 1, (x + 1) * w / tw);
            const int y0 = y * h / th, y1 = std::max(y0 + 1, (y + 1) * h / th);
            unsigned sum[3] = {0, 0, 0}, n = 0;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1; sx++, n++)
                    for (int c = 0; c < 3; c++) sum[c] += full[(size_t(sy) * w + sx) * 4 + c];
            uint8_t* o = &small[(size_t(y) * tw + x) * 4];
            for (int c = 0; c < 3; c++) o[c] = uint8_t(sum[c] / n);
            o[3] = 255;
        }
    int len = 0;
    unsigned char* png = stbi_write_png_to_mem(small.data(), tw * 4, tw, th, 4, &len);
    if (!png) return nullptr;
    uint8_t* out = (uint8_t*)malloc(size_t(len));
    if (out) {
        memcpy(out, png, size_t(len));
        *size = size_t(len);
    }
    STBIW_FREE(png);
    return out;
}

namespace gx {

void savestateOverlayRender(int winW, int winH) {
    if (winW > 0 && winH > 0) renderToast(winW, winH);
}

}  // namespace gx
