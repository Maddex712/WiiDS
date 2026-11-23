#pragma once
// Extra dynamic lights for the enhanced lighting mode (include/aurora/enhanced.h).
//
// A game-side provider (e.g. ghosts emitting colored light) hands Aurora a set of point
// lights once per game frame. They are written into the GX FIFO as a command, so they stay in
// order with the frame's draws and are part of captured command streams (frame interpolation
// replays them, blended between game frames by `id`). They light world-geometry draws of that
// frame per pixel, added to the game's own lit color channels (so the game's materials and TEV
// decide how a surface responds), only while the enhanced lighting mode and its "extra lights"
// part are on; otherwise they are ignored.
#include <cmath>
#include <cstdint>

namespace aurora::gfx {
struct ExtraLight {
  float pos[3];      // VIEW space (GX eye space, same space as GXLoadLightObjImm positions)
  float color[3];    // linear RGB, may exceed 1
  float radius;      // world units; smooth falloff to 0 at radius
  float intensity;   // multiplier
  uint32_t flags;    // bit0: cast shadows, bit1: affects ghosts/translucent, bit2: specular
  uint32_t id;       // stable per-source id for interpolation pairing (0 = none)
  // 0187 (flag ExtraLightRoomBox): the light only reaches inside this box (its room), fading out
  // over a short margin past it, so a light without a shadow does not shine through walls into the
  // next room. VIEW space: box centre relative to `pos` (3), then the three half-axis vectors
  // (3 x 3: the box's world axes in view space, scaled by its half extents). Relative to `pos`
  // so it follows the light when frame interpolation blends the position.
  float box[12] = {};
};
static_assert(sizeof(ExtraLight) == 88);

// 0023: was 16 (world lights: candles, fires, lamps). 0520: was 64; the world shaders no longer loop
// over every light: a compute pass bins the lights into screen tiles (16 px, three lists per tile by
// the pre-pass depth range) and each pixel loops over its tile's bit mask only (enhanced.cpp
// "Light tiles"). The FIFO command carries the count as a u16 since then.
constexpr uint32_t MaxExtraLights = 256;
constexpr uint32_t ExtraLightCastShadows = 1u << 0;
constexpr uint32_t ExtraLightTranslucent = 1u << 1;
constexpr uint32_t ExtraLightSpecular = 1u << 2;
// 0025: a flame: yellow-white near the source, red-orange toward the edge of its reach.
constexpr uint32_t ExtraLightFire = 1u << 3;
// 0187: limited to `box` (its room).
constexpr uint32_t ExtraLightRoomBox = 1u << 4;
// 0340: a directional light (sun, moon). `pos` is the unit vector TOWARD the light (view space),
// `radius` the shadow distance (view depth the shadow cascades cover), color x intensity is added
// per pixel (N.L, shadowed) like a GX directional light (0 = shadows only). `box` holds its
// parameters instead of a room box: [0..2] the colour a pixel is multiplied by in full shadow (the
// shadow darkens whatever lit the pixel, the game's own lighting included), [3] how much surfaces
// turned away from the light count as shadowed (0..1), [4] the width of that terminator (cosine),
// [5] the light's angular radius (tangent; soft contact-hardening edges, 0 = fixed filter),
// [6] the distance behind the view toward the light within which casters are rendered (world units).
// With ExtraLightCastShadows it takes a shadow slot: four orthographic cascades in its atlas cell.
constexpr uint32_t ExtraLightDirectional = 1u << 5;
// 0480 (point lights without ExtraLightRoomBox): inverse-square falloff from a small source,
// windowed to zero at `radius`; box[0] = the source's core radius as a share of `radius` (0.02..,
// 0 = 0.02). The light is then mostly a local pool (a candle, a lamp, a screen).
constexpr uint32_t ExtraLightPhysical = 1u << 6;
// 0480 (directional lights): the shadow tint darkens only the share of a pixel's light that is not
// the extra (point) lights' (those have their own shadows): a room's window light in a room lit by
// lamps.
constexpr uint32_t ExtraLightSpareExtra = 1u << 7;
// 0520a (point lights without ExtraLightRoomBox): adds a coloured rim on the silhouettes of draws
// with the material hint AURORA_MATERIAL_WORLD_RIM / _SOFT; box[11] = its strength (0 = none).
constexpr uint32_t ExtraLightRim = 1u << 16;
// 0441 (optional, directional lights): a world anchor for the cascades' texel snapping. Aurora gets
// only view-space data, so it snapped the cascade centres in a frame that moves and turns with the
// camera: the shadow map's texel grid slid over the world whenever the camera moved (shadow edges
// crawled and shimmered, and stepped as the light direction changed). With [7..9] = a world-fixed
// reference axis in view space (unit, e.g. the world's east axis) and [10..11] = the light-space
// coordinates of a world-fixed point (dir_light_anchor below, e.g. of the world origin, whose view
// position is the view matrix' translation), the cascades are laid out on a grid fixed in the
// world. [7..9] all zero = the old view-space layout.
// The cascades' light-space axes: `f` the light's travel direction (unit, view space, = -pos),
// `ref` the reference axis (view space): r = ref without its f part, up = f x r.
inline bool dir_light_axes(const float f[3], const float ref[3], float r[3], float up[3]) {
  const float d = f[0] * ref[0] + f[1] * ref[1] + f[2] * ref[2];
  float x[3] = {ref[0] - f[0] * d, ref[1] - f[1] * d, ref[2] - f[2] * d};
  const float l = x[0] * x[0] + x[1] * x[1] + x[2] * x[2];
  if (!(l > 1e-6f)) return false;
  const float k = 1.f / std::sqrt(l);
  for (int i = 0; i < 3; ++i) r[i] = x[i] * k;
  up[0] = f[1] * r[2] - f[2] * r[1], up[1] = f[2] * r[0] - f[0] * r[2], up[2] = f[0] * r[1] - f[1] * r[0];
  return true;
}
// [10..11] for a directional light toward `toLight` (view space) with reference axis `ref`: the
// world-fixed point `anchor` (view space) in the cascades' light-space axes.
inline void dir_light_anchor(const float toLight[3], const float ref[3], const float anchor[3], float out[2]) {
  float f[3] = {-toLight[0], -toLight[1], -toLight[2]};
  const float fl = f[0] * f[0] + f[1] * f[1] + f[2] * f[2];
  if (fl > 1e-12f) {
    const float k = 1.f / std::sqrt(fl);
    for (float& v : f) v *= k;
  }
  float r[3], up[3];
  if (!dir_light_axes(f, ref, r, up)) {
    out[0] = out[1] = 0.f;
    return;
  }
  out[0] = r[0] * anchor[0] + r[1] * anchor[1] + r[2] * anchor[2];
  out[1] = up[0] * anchor[0] + up[1] * anchor[1] + up[2] * anchor[2];
}

// Call once per game frame (guest thread), before the frame's draws; applies to world-geometry
// draws of that frame. Lights beyond MaxExtraLights are dropped (pass the strongest first).
// count 0 clears the set.
void set_extra_lights(const ExtraLight* lights, uint32_t count);
} // namespace aurora::gfx
