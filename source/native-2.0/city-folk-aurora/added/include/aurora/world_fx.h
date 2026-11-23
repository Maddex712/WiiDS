#ifndef AURORA_WORLD_FX_H
#define AURORA_WORLD_FX_H

// World effects (optional, off by default; 0350): screen-space effects layered on the finished 3D
// scene of a frame, before the HUD: reflections on water surfaces, wet ground and rain ripples,
// sparkle on sunlit snow, distance haze, particles drawn on top of the world (falling leaves,
// petals, snowflakes), a colour grade and a tilt-shift blur. With `enabled` false (or without
// GXWorldFxEndAurora in the stream) nothing changes a pixel.
//
// The application:
//  - classifies the draws of its main 3D view with GXSetSurfaceAurora (FIFO command, so it is part
//    of captured / replayed streams): AURORA_SURFACE_WATER and _GROUND draws are re-rendered into
//    a surface mask at the scene end (their shaded colour, the kind in alpha, the water surface's
//    depth); every surface kind other than NONE also makes the draw's projection the camera of the
//    effects;
//  - loads its camera into the application XF memory (GXLoadAppXfAurora, XF 0x0700-0x07FF, plain
//    XF loads that frame interpolation can blend like matrices): AURORA_WORLD_FX_XF_VIEW = a
//    world -> view matrix (3x4, row-major) of a world frame whose +y is up (a game with another up
//    axis rotates it in); the point the effects centre on (tilt-shift row, particle volume, haze
//    start) and their scale ("a metre" = 1/8 of the view height there) come from the scene depth
//    under the view's centre column at 56 % of its height;
//  - marks the end of the 3D scene with GXWorldFxEndAurora (after the world, before the HUD).
// Everything time- and weather-dependent is in the configuration (set from any thread).

#ifdef __cplusplus
#include <cstdint>
extern "C" {
#else
#include <stdbool.h>
#include <stdint.h>
#endif

#define AURORA_SURFACE_NONE 0
#define AURORA_SURFACE_WATER 1  // rivers, ponds, the sea: reflections, rain ripples
#define AURORA_SURFACE_GROUND 2 // walkable ground: wet sheen and puddles in rain, snow sparkle
#define AURORA_SURFACE_WORLD 3  // any other draw of the main 3D view (camera capture only)
// 0570: a character (player, villager, NPC): drawn a second time at the scene end, mirrored
// through the water under it, into a planar reflection that replaces the screen-space march over
// the water where it lies. One character per run of the kind: switch to another kind (WORLD)
// between two characters drawn back to back.
#define AURORA_SURFACE_CHARACTER 4
// 0580: a part of the character before it (its own head model, a worn Mii head, a held item drawn as
// its own object): drawn again like a character, but mirrored through that character's water plane
// (found from the character's own anchor), so the whole figure is mirrored or none of it.
#define AURORA_SURFACE_CHARACTER_PART 5
// 0590: a thing attached to or carried by a character, drawn as its own object (a hat, glasses, a
// held tool with its line, a caught fish held up): mirrored through the plane of the nearest whole
// character (anchors within 64 units) like a part, and left alone (no planar copy: the
// screen-space march as for the world) when no character is near it, e.g. the same model flying
// or lying about on its own.
#define AURORA_SURFACE_CHARACTER_ATTACH 6
// 0580: OR'ed onto a kind (WORLD / GROUND): a big static model by the water (a bridge) drawn again
// mirrored like a character, keeping its own kind; one run of flagged draws = one group (one plane).
// Screen-space reflection only has the camera's view of it (the deck top and near side): the mirrored
// bridge came out as a fragment with a ragged edge. The group's anchor (for the water lookup) is the
// view-space centre of its first draw's vertices, each through its own position matrix.
#define AURORA_SURFACE_MIRROR 0x80

#define AURORA_WORLD_FX_XF_VIEW 0x0700u
// 0440 (optional): the point the effects centre on, as a 3x4 model -> view matrix whose
// translation is that point in view space (e.g. the camera's look-at target: the player). Loaded
// like a model matrix, frame interpolation blends it with the objects. While it is all zero the
// point comes from the scene depth (below), which jumps whenever another object passes under that
// pixel (falling leaves and ripple cells jumped with the depth under the view's centre).
#define AURORA_WORLD_FX_XF_FOCUS 0x070Cu
#define AURORA_APP_XF_BASE 0x0700u
#define AURORA_APP_XF_WORDS 256u

#define AURORA_WORLD_FX_GRADE_ORIGINAL 0
#define AURORA_WORLD_FX_GRADE_WARM 1  // "New Leaf warmth"
#define AURORA_WORLD_FX_GRADE_FILM 2  // soft film
#define AURORA_WORLD_FX_GRADE_VIVID 3

#define AURORA_WORLD_FX_PARTICLES_NONE 0
#define AURORA_WORLD_FX_PARTICLES_LEAVES 1
#define AURORA_WORLD_FX_PARTICLES_PETALS 2
#define AURORA_WORLD_FX_PARTICLES_SNOW 3

typedef struct {
  bool enabled; // master switch

  // Water reflections (screen-space ray march against the scene depth, sky fallback).
  bool reflections;
  float reflectionStrength;   // 0..1.5
  uint32_t reflectionQuality; // 0 low (16 steps), 1 medium (24), 2 high (40)
  float rippleDistortion;     // 0..2: how much the game's own water pattern bends the reflection

  // Wet ground and rain ripples (the application animates the amounts with the weather).
  bool wetGround;
  float wetness;      // 0..1: how wet the ground is now (puddles, darkening, sheen)
  bool rainRipples;
  float rain;         // 0..1: rain intensity now (ripple rate on water and puddles)

  // Sparkle of sunlit snow on ground surfaces.
  bool snowSparkle;
  float sparkle;      // 0..1: snow cover x daylight (0 = none)

  // Distance haze (depth-based): density 0 = none.
  bool haze;
  float hazeDensity;  // 0..1
  float hazeColor[3]; // linear 0..1
  float hazeStart;    // view-space distance where it starts, as a multiple of the focus distance

  // Sky used where a reflection leaves the picture.
  float skyZenith[3];
  float skyHorizon[3];
  float moonDir[3];   // world-space direction to the moon (normalized)
  float moon;         // 0..1 glint of the moon in reflections (night)
  float clouds;       // 0..1 cloud cover of the reflected sky
  float ambient[3];   // light level of the particles (0..1 per channel)

  // Tilt-shift ("diorama"): blur above and below the focus row.
  bool tiltShift;
  float tiltStrength; // 0..1
  float tiltBand;     // 0.05..0.6: half height of the sharp band, fraction of the view height

  // Colour grade of the 3D scene (AURORA_WORLD_FX_GRADE_*).
  uint32_t grade;
  float gradeStrength; // 0..1

  // Particles drawn on top of the world (AURORA_WORLD_FX_PARTICLES_*).
  uint32_t particles;
  float particleDensity; // 0..2
  float wind[2];         // horizontal drift (effect metres per second, world x / z)
  float worldScale;      // 0442: world units per "metre" while AURORA_WORLD_FX_XF_FOCUS is given (> 1.5;
                         // else, and without a focus, the scale comes from the focus distance)

  bool freezeTime;       // test aid: effect time fixed (deterministic shots)

  // 0443: height of the particle volume's top above the focus point (effect metres; 0 = 19, the
  // original box). A taller volume keeps the density near the ground (the count grows with the
  // height) and fades its particles in at the top, so a camera that looks up over the horizon sees
  // them against the sky without a band where they pop in.
  float particleTop;
} AuroraWorldFxConfig;

// Replaces the configuration (any thread; used from the next scene end).
void aurora_world_fx_set_config(const AuroraWorldFxConfig* config);

// Surface kind of the following draws (AURORA_SURFACE_*). GX FIFO command.
void GXSetSurfaceAurora(uint8_t kind);
// End of the 3D scene of this frame (before the HUD): the world effects are applied here. GX FIFO
// command; nothing happens while the effects are off, inside an offscreen pass, or without a camera.
void GXWorldFxEndAurora(void);
// Plain XF load of `count` floats into the application XF memory at `addr`
// (AURORA_APP_XF_BASE .. + AURORA_APP_XF_WORDS). Readable by host effects at the scene end.
void GXLoadAppXfAurora(uint32_t addr, const float* values, uint32_t count);

// Statistics of the last scene end (overlay / tests): surface draws re-rendered, scene ends seen,
// GPU passes recorded.
void aurora_world_fx_stats(uint32_t* surfaceDraws, uint32_t* sceneEnds, uint32_t* passes);

#ifdef __cplusplus
}
#endif

#endif
