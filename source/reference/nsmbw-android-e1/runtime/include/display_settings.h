#pragma once

#include <cstdint>

// Display settings for New Super Mario Bros. Wii, shared by the desktop settings overlay and the
// Android menu and modelled on KartPad's: an aspect ratio and a render resolution.
//
// Aspect ratio:
//   Original 4:3 - the game in its 4:3 mode, pillarboxed.
//   16:9         - the game's own widescreen mode, with bars on screens of another shape.
//   Fill         - widescreen widened to the window or screen: the game's 16:9 EGG::Screen canvas
//                  is stretched to the surface's aspect, so it draws a genuinely wider view (Hor+)
//                  instead of a stretched image.
// Render resolution: 480 lines times the scale (1x = 480p, 2x = 960p, ...) in every aspect; the
// width follows the aspect, so Fill at 1x on a 20:9 phone renders about 1070x480.
//
// The game reads its aspect (SCGetAspectRatio) once at boot, so 4:3 versus a widescreen mode takes
// effect on the next start; switching between 16:9 and Fill, and the resolution, apply at once.
namespace DisplaySettings {

enum Aspect : int32_t { kAspect4x3 = 0, kAspect16x9 = 1, kAspectFill = 2 };

struct Settings {
    int32_t aspect = kAspectFill;
    float renderScale = 1.0f;  // render height = 480 * renderScale; 0 = the window's own size
};

// Startup, from Config.toml. Fixes the aspect the game boots in.
void Initialize(const Settings& settings);
// From any thread (the Android menu through JNI, the settings overlay). Takes effect at the next
// frame boundary.
void Request(const Settings& settings);
// At every frame boundary on the game thread (after aurora_begin_frame): applies a pending request
// to the renderer and keeps the game's widened canvas in step with the surface.
void OnFrameBoundary(uint32_t surfaceWidth, uint32_t surfaceHeight);

Settings Current();
// Current() with any request not yet applied: what the next frame will use.
Settings Latest();
// What SCGetAspectRatio reports for the whole run.
bool GameWidescreen();
// The window (or screen) is wider than 16:9, so Fill shows more of the game than 16:9 does.
bool SurfaceWide();
// Render height for a scale (480 lines per 1x).
float LinesForScale(float renderScale);

} // namespace DisplaySettings
