#include "det_clock.h"
#include "display_settings.h"

#include "memory.h"
#include "runtime_log.h"

#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <mutex>

namespace DisplaySettings {
namespace {

// NSMBW (SMNE01 rev 1) EGG::Screen statics. sTVModeInfo holds one record per TV mode,
// {u16 width, u16 height, f32 scaleX = fbWidth / width, f32 scaleY = efbHeight / height}, written
// by EGG::Screen::Initialize from the game's own sizes; record 1 is 16:9 (832x456 canvas units).
// EGG::Screen::SetTVMode copies the current record into the root screen's canvas and flags it for
// rebuild, and NSMBW registers no TV-mode change callback, so doing those writes from here is what
// SetTVMode would do. 2D layouts (m2d), the HOME menu and depth of field read the record directly.
constexpr uint32_t kTVModeInfo = 0x803504E0u;  // EGG::Screen::sTVModeInfo
constexpr uint32_t kTVModeRecordSize = 0x0Cu;
constexpr uint32_t kTVModeWide = 1u;
constexpr uint32_t kTVMode = 0x8042AF08u;      // EGG::Screen::sTVMode
constexpr uint32_t kRootScreen = 0x8042AF0Cu;  // EGG::Screen::spRoot
constexpr uint32_t kFbWidth = 0x8042AF38u;     // EGG::StateGX::s_widthFb (u16, upper half of the word)
constexpr uint32_t kCanvasWidth = 0x08u;       // EGG::Screen: canvas width (f32)
constexpr uint32_t kScreenFlags = 0x34u;       // EGG::Screen: flags (u16), bit 0 = rebuild
// NSMBW's own copy of the 16:9 size {f32 width, f32 height}, taken from the record by d_screen.cpp's
// static initializer before EGG::Screen::Initialize runs. dScreen::GetScreenSize (the game's camera
// and screen-space maths) reads it, so it has to widen with the record.
constexpr uint32_t kGameScreenSize169 = 0x8042A058u;  // dScreen::l_ScreenSize16x9
// 608/832 (f32) in .sdata2, read by d2d::Multi_c::__ct (0x80006D48) and m2d::Simple_c::__ct
// (0x8016395C): the x scale of 16:9 layouts in the 4:3 (608-unit) 2D projection.
constexpr uint32_t kLayoutSqueezeMulti = 0x8042B084u;
constexpr uint32_t kLayoutSqueezeSimple = 0x8042DC48u;
constexpr uint32_t kLayoutBaseWidth = 608u;  // sTVModeInfo[0] (4:3) width: the 2D projection's
// The levels' view, in world units: mVideo::create (0x800E8AB0) copies 408 x 224 from .sdata2 into
// mVideo::l_rayout*, and dBg_c (camera scroll and limits, DispScaleCalc -> dBgParameter_c's visible
// area), dBgGm_c and dActor_c::screenCullCheck read the floats. Widening the width widens the level
// camera itself, so tiles, backgrounds and actors are drawn, spawned and culled across the whole
// screen instead of a 16:9 view being stretched over it.
constexpr uint32_t kStageViewWidthConst = 0x8042CD78u;  // f32 408 (.sdata2, read by mVideo::create)
constexpr uint32_t kStageViewWidthInt = 0x8042A0E4u;    // mVideo::l_rayoutWidth (s32)
constexpr uint32_t kStageViewWidth = 0x8042A0ECu;       // mVideo::l_rayoutWidthF
constexpr uint32_t kStageViewHeight = 0x8042A0F0u;      // mVideo::l_rayoutHeightF
constexpr uint32_t kStageViewAspect = 0x8042A0F4u;      // mVideo::l_rayoutAspect
constexpr float kStageViewBaseWidth = 408.0f;
// bgTex_c::drawBuffer (0x80082740) draws 28 / zoom + 2 columns of tiles (.sdata2 f32, read by it
// alone), a fixed 480 units that fall short of a wider view: grow it in step.
constexpr uint32_t kTileColumns = 0x8042BEC4u;
constexpr float kTileColumnsBase = 28.0f;

constexpr float kLinesPerScale = 480.0f;
constexpr float kRatio16x9 = 16.0f / 9.0f;

std::mutex g_mutex;
Settings g_current;
Settings g_pending;
bool g_hasPending = false;
bool g_bootWidescreen = true;
bool g_rendererDirty = true;
uint32_t g_surfaceWidth = 0;
uint32_t g_surfaceHeight = 0;
uint16_t g_gameWideWidth = 0;  // the game's own 16:9 canvas width, captured before any widening

// The game booted in 4:3: neither widescreen mode exists until the next start.
int32_t EffectiveAspect(int32_t aspect) {
    return g_bootWidescreen ? aspect : kAspect4x3;
}

bool SurfaceWiderThan169() {
    return g_surfaceWidth != 0 && g_surfaceHeight != 0 &&
           static_cast<float>(g_surfaceWidth) / static_cast<float>(g_surfaceHeight) > kRatio16x9;
}

// Render scale 0 ("match screen resolution"): exactly as many lines as the picture is tall on the
// surface, so it renders 1:1 (a 2560x1080 surface in Fill renders 2560x1080, with the game's view
// widened to match), not the window size with the EFB's workspace rows squeezed in.
float RenderLines(float renderScale, int32_t aspect) {
    if (renderScale > 0.0f || g_surfaceWidth == 0 || g_surfaceHeight == 0) {
        return LinesForScale(renderScale);
    }
    const float w = static_cast<float>(g_surfaceWidth), h = static_cast<float>(g_surfaceHeight);
    const bool fillsSurface = aspect == kAspectFill && SurfaceWiderThan169();
    const float pictureAspect = aspect == kAspect4x3 ? 4.0f / 3.0f : kRatio16x9;
    return fillsSurface ? h : std::min(h, w / pictureAspect);
}

void ApplyRenderer(const Settings& settings) {
    const int32_t aspect = EffectiveAspect(settings.aspect);
    if (aspect == kAspect4x3) {
        AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);
        VILockAspectRatio(4, 3);
    } else if (aspect == kAspect16x9 || !SurfaceWiderThan169()) {
        // 16:9, or Fill on a surface no wider than 16:9 (the picture is never squashed).
        VIUnlockAspectRatio();
        AuroraSetViewportPolicy(AURORA_VIEWPORT_16_9);
    } else {
        // The framebuffer takes the surface's aspect; the game draws the wider view into it.
        VIUnlockAspectRatio();
        AuroraSetViewportPolicy(AURORA_VIEWPORT_STRETCH);
    }
    VISetFrameBufferLines(RenderLines(settings.renderScale, aspect));
    RT_LOGF(RT_TAG_VI, "display: aspect %d%s, render %.0f lines (%.2gx), surface %ux%u\n", aspect,
            aspect != settings.aspect ? " (game booted in 4:3)" : "", RenderLines(settings.renderScale, aspect),
            settings.renderScale, g_surfaceWidth, g_surfaceHeight);
}

// Fill: widen the game's 16:9 canvas to the surface (Hor+), keeping the game's own pixel aspect
// (its 832 units stand for 16:9). Otherwise keep, or put back, the game's own width.
void ApplyGameCanvas(int32_t aspect) {
    // 3D: cameras set up for 16:9 are widened to the screen (aurora's projection), the same view
    // height with more to the sides, instead of being stretched across it.
    const bool widened = aspect == kAspectFill && SurfaceWiderThan169();
    AuroraSetWidePerspectiveAspect(
        widened ? static_cast<float>(g_surfaceWidth) / static_cast<float>(g_surfaceHeight) : 0.0f);
    uint32_t root = 0;
    if (!Memory::TryRead32(kRootScreen, root) || root == 0) {
        return;  // EGG::Screen::Initialize has not run yet
    }
    const uint32_t record = kTVModeInfo + kTVModeWide * kTVModeRecordSize;
    uint32_t size = 0;
    if (!Memory::TryRead32(record, size)) {
        return;
    }
    const auto width = static_cast<uint16_t>(size >> 16);
    if (g_gameWideWidth == 0) {
        g_gameWideWidth = width;
    }
    uint16_t target = g_gameWideWidth;
    if (aspect == kAspectFill && SurfaceWiderThan169()) {
        const float surfaceAspect = static_cast<float>(g_surfaceWidth) / static_cast<float>(g_surfaceHeight);
        target = static_cast<uint16_t>(std::lround(g_gameWideWidth * surfaceAspect / kRatio16x9));
    }
    if (target == width) {
        return;
    }
    uint32_t fbWord = 0;
    Memory::TryRead32(kFbWidth, fbWord);
    const auto fbWidth = static_cast<float>(fbWord >> 16);
    Memory::Write16(record, target);
    if (fbWidth > 0.0f) {
        Memory::WriteFloat32(record + 4u, fbWidth / static_cast<float>(target));
    }
    Memory::WriteFloat32(kGameScreenSize169, static_cast<float>(target));
    // The 2D layer: d2d::Multi_c and m2d::Simple_c draw their 832-unit 16:9 layouts in a 608-unit
    // projection and squeeze x by 608/832 (read from these two constants when a layout is built), so
    // on a wider canvas they stretched. 608/width keeps their pixels square on any width.
    for (const uint32_t squeeze : {kLayoutSqueezeMulti, kLayoutSqueezeSimple}) {
        Memory::WriteFloat32(squeeze, static_cast<float>(kLayoutBaseWidth) / static_cast<float>(target));
    }
    // The level camera: as wide as the canvas, in the same proportion as the game's own 408 to 832.
    const float stageWidth = kStageViewBaseWidth * static_cast<float>(target) / static_cast<float>(g_gameWideWidth);
    Memory::WriteFloat32(kStageViewWidthConst, stageWidth);
    Memory::WriteFloat32(kTileColumns, std::ceil(kTileColumnsBase * stageWidth / kStageViewBaseWidth));
    const float stageHeight = Memory::ReadFloat32(kStageViewHeight);
    if (stageHeight > 0.0f) {  // else mVideo::create has yet to run, and takes the constant
        Memory::WriteFloat32(kStageViewWidth, stageWidth);
        Memory::Write32(kStageViewWidthInt, static_cast<uint32_t>(std::lround(stageWidth)));
        Memory::WriteFloat32(kStageViewAspect, stageWidth / stageHeight);
    }
    uint32_t mode = 0;
    Memory::TryRead32(kTVMode, mode);
    if (mode == kTVModeWide) {
        uint32_t flagsWord = 0;
        Memory::TryRead32(root + kScreenFlags, flagsWord);
        Memory::WriteFloat32(root + kCanvasWidth, static_cast<float>(target));
        Memory::Write16(root + kScreenFlags, static_cast<uint16_t>((flagsWord >> 16) | 1u));
    }
    RT_LOGF(RT_TAG_VI, "display: 16:9 canvas %u -> %u units wide, fb %.0f px (surface %ux%u)\n", width,
            target, fbWidth, g_surfaceWidth, g_surfaceHeight);
}

} // namespace

float LinesForScale(float renderScale) {
    return renderScale > 0.0f ? kLinesPerScale * renderScale : 0.0f;
}

// Deterministic mode (network play) keeps every device on the same 16:9 canvas: how wide the game
// draws decides which enemies it wakes, so Fill's screen-dependent width would desync the session.
Settings Lockstep(Settings settings) {
    if (DetClock::Enabled()) {
        settings.aspect = kAspect16x9;
    }
    return settings;
}

void Initialize(const Settings& requested) {
    const Settings settings = Lockstep(requested);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current = settings;
    g_bootWidescreen = settings.aspect != kAspect4x3;
    g_rendererDirty = true;
}

void Request(const Settings& settings) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending = Lockstep(settings);
    g_hasPending = true;
}

// NSMBW_DUMP_SCREENS=<frame>: at that frame, log every EGG::Screen in guest memory (found by the
// root screen's vtable) with its canvas size (finding the 2D layer's own screen).
void DumpScreens() {
    static const long frameToDump = [] {
        const char* value = std::getenv("NSMBW_DUMP_SCREENS");
        return value != nullptr ? std::atol(value) : 0L;
    }();
    static long frame = 0;
    if (frameToDump <= 0 || ++frame != frameToDump) {
        return;
    }
    uint32_t root = 0, vtable = 0;
    if (!Memory::TryRead32(kRootScreen, root) || root == 0) {
        return;
    }
    std::string words;
    for (uint32_t i = 0; i < 0x60; i += 4) {
        uint32_t word = 0;
        Memory::TryRead32(root + i, word);
        char text[16];
        std::snprintf(text, sizeof(text), "%08X ", word);
        words += text;
    }
    RT_LOGF(RT_TAG_VI, "screens: root %08X: %s\n", root, words.c_str());
    for (uint32_t a : {0x8042AF18u, 0x8042AF1Cu, 0x8042AF20u, 0x8042AF24u, 0x803504E0u, 0x803504E4u, 0x803504E8u,
                       0x803504ECu, 0x803504F0u, 0x803504F4u}) {
        uint32_t word = 0;
        Memory::TryRead32(a, word);
        float f;
        std::memcpy(&f, &word, 4);
        RT_LOGF(RT_TAG_VI, "screens: [%08X] = %08X (%g)\n", a, word, f);
    }
    // The vtable: the first word that points into the game's data sections.
    uint32_t vtableOffset = 0;
    for (uint32_t i = 0; i < 0x60 && vtable == 0; i += 4) {
        uint32_t word = 0;
        if (Memory::TryRead32(root + i, word) && word >= 0x80300000u && word < 0x80440000u) {
            vtable = word;
            vtableOffset = i;
        }
    }
    RT_LOGF(RT_TAG_VI, "screens: vtable %08X at +%X\n", vtable, vtableOffset);
    if (vtable == 0) {
        return;
    }
    for (const auto [begin, end] : {std::pair{0x80000000u, 0x81800000u}, std::pair{0x90000000u, 0x94000000u}}) {
        for (uint32_t a = begin; a < end; a += 4) {
            uint32_t word = 0;
            if (!Memory::TryRead32(a, word) || word != vtable || a < vtableOffset) {
                continue;
            }
            const uint32_t object = a - vtableOffset;
            uint32_t w = 0, h = 0, flags = 0;
            Memory::TryRead32(object + kCanvasWidth, w);
            Memory::TryRead32(object + kCanvasWidth + 4, h);
            Memory::TryRead32(object + kScreenFlags, flags);
            float fw, fh;
            std::memcpy(&fw, &w, 4);
            std::memcpy(&fh, &h, 4);
            RT_LOGF(RT_TAG_VI, "screens: object %08X canvas %.1f x %.1f flags %04X\n", object, fw, fh, flags >> 16);
        }
    }
}

void OnFrameBoundary(uint32_t surfaceWidth, uint32_t surfaceHeight) {
    DumpScreens();
    Settings settings;
    bool renderer = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_hasPending) {
            g_current = g_pending;
            g_hasPending = false;
            g_rendererDirty = true;
        }
        if (surfaceWidth != 0 && surfaceHeight != 0 &&
            (surfaceWidth != g_surfaceWidth || surfaceHeight != g_surfaceHeight)) {
            const bool wasWide = SurfaceWiderThan169();
            g_surfaceWidth = surfaceWidth;
            g_surfaceHeight = surfaceHeight;
            // Fill switches between stretching and the 16:9 frame as the surface crosses 16:9, and
            // a "match screen" render follows every size.
            if (SurfaceWiderThan169() != wasWide || g_current.renderScale <= 0.0f) {
                g_rendererDirty = true;
            }
        }
        settings = g_current;
        renderer = g_rendererDirty;
        g_rendererDirty = false;
    }
    if (renderer) {
        ApplyRenderer(settings);
    }
    ApplyGameCanvas(EffectiveAspect(settings.aspect));
}

Settings Current() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_current;
}

Settings Latest() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_hasPending ? g_pending : g_current;
}

bool GameWidescreen() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_bootWidescreen;
}

bool SurfaceWide() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return SurfaceWiderThan169();
}

} // namespace DisplaySettings
