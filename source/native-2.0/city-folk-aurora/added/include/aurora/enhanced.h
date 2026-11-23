#ifndef AURORA_ENHANCED_H
#define AURORA_ENHANCED_H

// Enhanced lighting (optional, off by default): effects layered on top of the accurate GX
// rendering of *world geometry* draws. With `enabled` false nothing here changes a pixel:
// shader configs, passes and outputs are exactly those of the plain renderer.
//
// The application classifies draws with GXSetDrawClassAurora (FIFO command, so it is ordered
// with the draws and part of captured/replayed command streams):
//   AURORA_DRAW_CLASS_OTHER  HUD/2D, shadow-map passes, sub-views, anything not to enhance
//   AURORA_DRAW_CLASS_WORLD  rooms, furniture, characters (opaque or blended)
//   AURORA_DRAW_CLASS_GHOST  ghosts (lit by extra lights only when the light allows it)
// and marks the end of the 3D scene once per frame with GXSceneEndAurora (before the HUD), where
// the screen-space parts (bloom, volumetric flashlight beam, ambient occlusion) are applied to
// the EFB. Extra point lights: include/aurora/lights.h.

#ifdef __cplusplus
#include <cstdint>
extern "C" {
#else
#include <stdbool.h>
#include <stdint.h>
#endif

#define AURORA_DRAW_CLASS_OTHER 0
#define AURORA_DRAW_CLASS_WORLD 1
#define AURORA_DRAW_CLASS_GHOST 2 // translucent world draws (ghosts): extra lights only with their translucent flag
// 0470: a shadow caster only (an object outside the view whose shadow reaches into it): rendered
// into the light shadow maps (opaque, depth-writing draws) and nowhere else: not the EFB pass, the
// depth / normal pre-pass, ambient occlusion, the glow buffer, world-effect masks or EFB copies.
// Draws of this class are always dropped from the main pass, also with shadow maps off.
#define AURORA_DRAW_CLASS_SHADOW 3
// 0480: a world draw that receives light and shadows like AURORA_DRAW_CLASS_WORLD (same pipelines)
// but casts no shadow into the light shadow maps: the shell of a room (floor, walls) around its
// lights, which would otherwise block a window light coming over a wall.
#define AURORA_DRAW_CLASS_WORLD_NOCAST 4

typedef struct {
  bool enabled; // master switch

  // Per-pixel evaluation of the game's GX lights (same formulas as the GX vertex lighting).
  bool perPixelLighting;

  // Flashlight / spot lights: per-pixel spot cone (even without perPixelLighting) with a
  // smoothed penumbra; softness 0 = the GX falloff curve as is, 1 = fully smoothed edge.
  bool flashlightQuality;
  float flashlightSoftness;
  float flashlightSpecular; // 0..2: glossy highlight of the flashlight on surfaces (per pixel)

  // Soft shadows for the game's shadow-map compare (TEV GR16 compare against the depth map):
  // quality 0 = PCF 8 taps, 1 = PCF 16, 2 = PCSS (contact hardening) 16+16, 3 = PCSS 32+32.
  bool softShadows;
  uint32_t softShadowQuality;
  float softShadowRadius;  // filter radius in 640x480 shadow-map texels (scaled with the map)
  float softShadowPenumbra; // PCSS: penumbra growth with blocker distance
  // How the game's shadow darkens a pixel (shader variants only). The game lerps a shadowed pixel
  // toward its shadow color by the shadow strength, whatever lights the pixel. With these, only
  // the share of the light that the shadow represents is removed: colored extra lights always
  // keep lighting the shadow, `shadowAmbientKeep` of the ambient light is kept too (shadows pick
  // up the surrounding color instead of going black), and `shadowDarkness` scales the result.
  // 1 / 0 = the game's darkening (when no extra lights reach the pixel).
  float shadowDarkness;    // 0..1.5
  float shadowAmbientKeep; // 0..1

  // Extra point lights (aurora/lights.h), per pixel on world geometry.
  bool extraLights;
  float extraLightIntensity;
  float extraLightSaturation; // 0..2.5: color vibrance of every extra light (1 = as submitted)
  // Shadows of the extra lights flagged ExtraLightCastShadows (up to 4 per frame): a depth
  // pre-pass of the opaque world draws and a screen-space ray march toward each light.
  bool extraShadows;
  uint32_t extraShadowQuality; // 0 low, 1 medium, 2 high: ray-march steps 8/16/32, shadow-map filter taps 8/16/32
  // 0028: how the extra-light shadows are made. 0 = screen-space ray march through the camera
  // depth (cheap; blockers outside the picture cast nothing; up to 4 lights). 1 = shadow maps:
  // the frame's opaque world draws are rendered from each shadowed light into a cube map (six
  // faces in one atlas, all faces and lights in one instanced draw per world draw), so blockers
  // the camera does not see still cast shadows; PCF/PCSS soft edges.
  uint32_t extraShadowMode;
  uint32_t extraShadowLights;  // shadow-casting lights per frame, 0..8 (the first flagged lights; screen-space: up to 4)
  uint32_t extraShadowMapSize; // cube face size in texels (shadow maps): 256..2048
  float extraShadowSoftness;   // light source size in world units for contact-hardening penumbrae (0 = hard edges)

  // Volumetric flashlight beam (scene end, uses depth).
  bool volumetric;
  float volumetricIntensity;
  uint32_t volumetricQuality; // 0 low (16 steps, 1/4 res), 1 medium (24, 1/2), 2 high (48, 1/2)

  // Bloom / glow of bright light sources (scene end, HUD excluded).
  bool bloom;
  float bloomIntensity;
  float bloomThreshold; // 0..1 luminance where the glow starts
  float bloomRadius;    // 0.25..2: spread
  float bloomFlashlightSuppress; // 0..1: less glow from surfaces that are only bright from the flashlight
  float bloomScene;     // 0..1: glow from bright scene pixels (lit walls/objects) above the threshold
  float bloomEmissive;  // 0..2: glow from light-emitting draws (additive effects: glows, fire, beams, sparks)
  float bloomLightGlow; // unused since 0187 (extra lights never draw a halo of their own)

  // Screen-space ambient occlusion (scene end).
  bool ssao;
  float ssaoIntensity;
  float ssaoRadius; // world units

  // 0196: realistic flashlight (replaces the volumetric beam above while on): light scattering
  // ray-marched through the cone against the flashlight's own shadow map (shadow shafts), a
  // physically shaped footprint (hot centre, reflector rings, soft edge, inverse-square falloff),
  // dust motes lit only inside the beam, a flare when the light flashes; the game's cone mesh is
  // hidden while it is on. 0 off, 1 medium, 2 high, 3 ultra (steps / resolution / filtering).
  uint32_t realisticFlashlight;
  float beamDensity; // 0..3: how hazy the air is (brightness and extinction of the shafts)
  float beamDust;    // 0..2: dust motes drifting in the beam (0 = none)

  // 0340: unlit opaque world draws also get the enhanced variant (as with ambient occlusion), so
  // they receive directional-light shadows (ExtraLightDirectional, include/aurora/lights.h).
  bool lightReceivers;

  // 0342: colour grade of the 3D scene at its end (GXSceneEndAurora, before the HUD), e.g. a
  // time-of-day look: c = c * gain + lift * (1 - c), then saturation around the luma and contrast
  // around mid grey. gain 1 / lift 0 / saturation 1 / contrast 1 = unchanged (and no pass).
  bool grade;
  float gradeGain[3];
  float gradeLift[3];
  float gradeSaturation;
  float gradeContrast;
  // 0421: 0..1, how much bright, coloured pixels (lit windows, lamps, signs: the scene's light
  // sources) keep their colour through the grade (e.g. a cool night look leaves warm windows warm).
  float gradeKeepLights;

  // 0343: sun shafts: light of the first shadow-casting directional light scattered in the air,
  // ray-marched against its shadow cascades (tree and house shadows cut shafts into the haze),
  // added at scene end. Colour = the light's colour x shaftIntensity.
  bool shafts;
  float shaftIntensity;
  uint32_t shaftSteps; // 8..48

  // 0540: floor, in world units, of the receiver offset of the extra lights' shadow maps (the
  // normal / toward-the-light offset is otherwise one cube texel at the receiver, scaled up at
  // grazing angles). Near a light the texels get tiny (0.03 units at 15 units from a light with
  // 1024 faces) while the receiver's tangent-plane filter taps still span the light's PCSS search
  // region: on a low-poly character next to a shadowed point light (a player at a lit doorway) the
  // taps dipped under its own neighbouring facets and blotches of self-shadow jumped with every
  // pose. 0 = texel-based only (as before 0540). Pick about 1% of a character's height.
  float extraShadowOffsetFloor;
} AuroraEnhancedConfig;

// 0196: an ExtraLight with this flag is no light: three of them (ids BeamAnchorId + 0..2) carry
// the view-space positions of a world point and of two points 256 units along world +X and +Y, so
// effects can anchor detail (dust, haze) in the world; frame interpolation blends them like lights.
#define AURORA_EXTRA_LIGHT_ANCHOR 0x40000000u
#define AURORA_BEAM_ANCHOR_ID 0xBEA0A000u
// 0530: an anchor pseudo-light with this id is the shadow focus of the frame: `pos` = the view-space
// centre and `radius` the radius of a sphere around a character (keep the radius steady). Shadowed
// directional lights (ExtraLightDirectional) render a fifth, small orthographic cascade around it
// into the free face 4 of their atlas cell (same texel count as the other cascades, so a 40-unit
// sphere gets ~20x finer texels than a view cascade), used by receivers inside the sphere and
// blended into the view cascades over its outer quarter: sharp, stable self-shadows on the
// character (a hat brim on the face) that do not depend on the view cascades' coarse texels.
// Frame interpolation blends it like a light (by id).
#define AURORA_SHADOW_FOCUS_ID 0xBEA0A100u

// Replaces the configuration (any thread; takes effect at the next draw / scene end).
void aurora_enhanced_set_config(const AuroraEnhancedConfig* config);

// Draw class for the following draws (AURORA_DRAW_CLASS_*). GX FIFO command.
void GXSetDrawClassAurora(uint8_t drawClass);
// Material of the following draws (GX_AURORA_SET_MATERIAL_HINT): AURORA_MATERIAL_WORLD or
// AURORA_MATERIAL_CHARACTER. Only uniforms change (no pipeline variants).
#define AURORA_MATERIAL_WORLD 0
#define AURORA_MATERIAL_CHARACTER 1
// 0420: a light-emitting surface (a lit window, a glowing sign, a lamp head): no directional
// (sun / moon) shadow or terminator and no ambient occlusion darkens it; extra lights still add.
#define AURORA_MATERIAL_EMISSIVE 2
// 0480: an emissive surface that is also a bloom source of its own (a candle's or a fire's flame):
// as AURORA_MATERIAL_EMISSIVE, and drawn into the bloom's glow buffer like additive draws.
#define AURORA_MATERIAL_EMISSIVE_GLOW 3
// 0520a: a world surface (shaded exactly like AURORA_MATERIAL_WORLD) that takes the coloured rim of
// lights flagged ExtraLightRim: characters (WORLD_RIM) and, at about a third, furniture (_SOFT).
#define AURORA_MATERIAL_WORLD_RIM 4
#define AURORA_MATERIAL_WORLD_RIM_SOFT 5
void GXSetMaterialHintAurora(uint8_t hint);
// End of the 3D scene of this frame (before the HUD). GX FIFO command; no effect when the
// enhanced mode or all of its screen-space parts are off, or inside an offscreen pass.
void GXSceneEndAurora(void);

// 0196: the realistic flashlight flares (brighter, whiter beam and pool) by `strength` (0..1),
// decaying over ~0.4 s; e.g. when the light stuns a ghost. Any thread.
void aurora_enhanced_beam_flare(float strength);

// Statistics for the overlay / tests: number of extra lights in the last frame, GPU-side
// scene-end passes recorded in the last frame.
uint32_t aurora_enhanced_last_light_count(void);
// 0028: shadow-map statistics of the last mask (shadowed lights, caster draws rendered into the
// atlas, caster draws skipped because their pipeline was still compiling).
void aurora_enhanced_shadow_stats(uint32_t* lights, uint32_t* casters, uint32_t* skipped);
// 0470: AURORA_DRAW_CLASS_SHADOW draws recorded as shadow-map casters in the last recorded frame.
uint32_t aurora_enhanced_shadow_class_draws(void);

#ifdef __cplusplus
}
#endif

#endif
