// Enhanced lighting: configuration, extra lights, per-draw uniforms and the scene-end
// screen-space effects (bloom, volumetric flashlight beam, ambient occlusion).
// See include/aurora/enhanced.h and include/aurora/lights.h.
#include "enhanced.hpp"
#include "beam_profile.hpp"

#include "encoding.hpp"
#include "pipeline_cache.hpp"
#include "resource_cache.hpp"
#include "recording.hpp"
#include "resources.hpp"
#include "tex_copy_conv.hpp"
#include "world_fx.hpp"
#include "../gx/gx.hpp"
#include "../gx/pipeline.hpp"
#include "../internal.hpp"
#include "../webgpu/gpu.hpp"
#include "../webgpu/gpu_prof.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace aurora::gfx::enhanced {
namespace {
Module Log("aurora::enhanced");

// --- configuration (any thread -> FIFO thread) ---
std::mutex g_cfgMutex;
AuroraEnhancedConfig g_pending{};
std::atomic<uint32_t> g_pendingGen{0};
AuroraEnhancedConfig g_cfg{};
uint32_t g_cfgGen = 0;
std::atomic<uint32_t> g_lastLightCount{0};

bool same(const AuroraEnhancedConfig& a, const AuroraEnhancedConfig& b) {
#define EQ(f) (a.f == b.f)
  return EQ(enabled) && EQ(perPixelLighting) && EQ(flashlightQuality) && EQ(flashlightSoftness) && EQ(flashlightSpecular) && EQ(softShadows) &&
         EQ(softShadowQuality) && EQ(softShadowRadius) && EQ(softShadowPenumbra) && EQ(extraLights) && EQ(extraLightSaturation) &&
         EQ(extraLightIntensity) && EQ(volumetric) && EQ(volumetricIntensity) && EQ(volumetricQuality) && EQ(bloom) &&
         EQ(bloomIntensity) && EQ(bloomThreshold) && EQ(bloomRadius) && EQ(bloomFlashlightSuppress) && EQ(bloomScene) && EQ(bloomEmissive) && EQ(bloomLightGlow) && EQ(ssao) &&
         EQ(ssaoIntensity) && EQ(ssaoRadius) &&
         // 0031: the fields added after 0016 (changing only them did not reach the renderer)
         EQ(extraShadows) && EQ(extraShadowQuality) && EQ(extraShadowMode) && EQ(extraShadowLights) &&
         EQ(extraShadowMapSize) && EQ(extraShadowSoftness) && EQ(shadowDarkness) && EQ(shadowAmbientKeep) &&
         EQ(realisticFlashlight) && EQ(beamDensity) && EQ(beamDust) && EQ(lightReceivers) &&
         EQ(grade) && EQ(gradeGain[0]) && EQ(gradeGain[1]) && EQ(gradeGain[2]) && EQ(gradeLift[0]) && EQ(gradeLift[1]) &&
         EQ(gradeLift[2]) && EQ(gradeSaturation) && EQ(gradeContrast) && EQ(gradeKeepLights) && EQ(shafts) && EQ(shaftIntensity) && EQ(shaftSteps) &&
         EQ(extraShadowOffsetFloor);
#undef EQ
}

// --- frame state (FIFO thread) ---
std::array<ExtraLight, MaxExtraLights> g_lights{};
uint32_t g_lightCount = 0;
Range g_lightRange{};
bool g_lightRangeValid = false;

struct Camera {
  bool valid = false;
  float p00 = 1, p11 = 1, p02 = 0, p12 = 0, p03 = 0, p13 = 0, a = 0, b = 0;
  Viewport vp{};
};
Camera g_camera;
struct Flashlight {
  bool valid = false;
  Vec4<float> pos, dir, cosAtt, distAtt, color;
};
Flashlight g_flash;
float g_flashDist2 = 0.f;

// 0196: realistic flashlight. The flashlight's shadow map as the game's receivers see it: taken
// from the first main-view draw doing the shadow compare (TEV GR16 compare of the depth-ramp key
// against the Z16 copy read as IA8), with its two projective texgens turned into view space
// (texgen x post matrix x inverse position matrix), so the scattering pass can test any point in
// the air the same way. In interpolated replays the receivers' matrices are rebuilt from the
// blended light (docs/INTERPOLATION.md), so this follows the presented frame.
struct BeamShadow {
  bool valid = false;
  float map[3][4]{};  // view -> s, t, q of the shadow map
  float ramp[2][4]{}; // view -> s, q of the depth ramp (receiver key = floor(65536 s / q))
  float center[3]{};  // the light the map was rendered from (where all three map rows vanish)
  bool centerValid = false;
  TextureHandle tex;
  uint32_t sr = 0, sg = 0;
};
BeamShadow g_beam;
// World anchor (AURORA_EXTRA_LIGHT_ANCHOR pseudo-lights): view positions of world points.
std::array<std::array<float, 3>, 3> g_anchor{};
bool g_anchorValid = false;
// 0530: the directional lights' focus cascade (AURORA_SHADOW_FOCUS_ID pseudo-light of this frame):
// view-space sphere around a character drawn into face 4 of the light's cell at high resolution.
struct ShadowFocus {
  float pos[3] = {0.f, 0.f, 0.f};
  float radius = 0.f;
  bool valid = false;
};
ShadowFocus g_focus;
// AURORA_ENH_FOCUS=0: no focus cascade (A/B); AURORA_ENH_FOCUS_SOFT=<world units>: its filter
// radius floor (default 0: the light's own penumbra, at least 1.5 texels).
const bool g_focusOff = [] {
  const char* v = std::getenv("AURORA_ENH_FOCUS");
  return v != nullptr && *v == '0';
}();
const float g_focusSoft = [] {
  const char* v = std::getenv("AURORA_ENH_FOCUS_SOFT");
  return v != nullptr ? std::max(float(std::atof(v)), 0.f) : 0.f;
}();
// AURORA_ENH_FOCUS_BIAS=<world units>: floor of the focus cascade's receiver offset (normal /
// toward the light, scaled like the texel-based one).
const float g_focusBias = [] {
  const char* v = std::getenv("AURORA_ENH_FOCUS_BIAS");
  return v != nullptr ? std::max(float(std::atof(v)), 0.f) : 0.3f;
}();
// 0540: AURORA_ENH_XL_SHADOW_FLOOR=<world units>: the extra lights' shadow receiver offset floor
// instead of the app's (AuroraEnhancedConfig::extraShadowOffsetFloor; test aid).
const float g_xlFloorOverride = [] {
  const char* v = std::getenv("AURORA_ENH_XL_SHADOW_FLOOR");
  return v != nullptr ? std::max(float(std::atof(v)), 0.f) : -1.f;
}();
// Flash / flare detection: the flashlight's brightness relative to its recent steady level.
float g_flashLevel = 0.f;
float g_flare = 0.f;
float g_flarePulse = 0.f;              // decaying flare impulse (aurora_enhanced_beam_flare, power-on)
std::atomic<float> g_flareKick{0.f};   // set from any thread
float g_flashOffTime = 0.f;            // seconds the flashlight has been off (scene ends without it)
bool beam_on() noexcept { return g_cfg.enabled && g_cfg.realisticFlashlight != 0; }

// Vibrance: push a light color away from its gray (luma) by `sat`, keeping its brightest channel,
// so every provider (ghost lights, test lights) gets one consistent saturated look.
std::array<float, 3> vivid(const float c[3], float sat) noexcept {
  // Vibrance of the colored lights. The linear boost (each channel pushed away from the luminance by
  // `sat`) gives the contrasty look; where a channel would go below 0 the old code clamped it (a hue
  // shift: orange fire turned red) and 0025 stopped the boost early (saturated lights lost their
  // punch). 0026: take the boosted color's saturation (HSV, capped at full) and rebuild the color
  // with the original hue and brightness, so lights can reach full saturation without a hue shift.
  // Debug: AURORA_VIVID_MODE=0 (clamp, before 0025), 1 (0025 early stop), 2 (default).
  static const int sMode = [] {
    const char* v = std::getenv("AURORA_VIVID_MODE");
    return v ? std::atoi(v) : 2;
  }();
  const float l = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
  const float mx = std::max({c[0], c[1], c[2]});
  const float mn = std::min({c[0], c[1], c[2]});
  std::array<float, 3> o{};
  if (sMode == 2 && mx > 1e-5f && mx - mn > 1e-5f) {
    float bmx = -1e9f, bmn = 1e9f;
    for (int i = 0; i < 3; ++i) {
      const float v = l + (c[i] - l) * sat;
      bmx = std::max(bmx, v);
      bmn = std::min(bmn, v);
    }
    const float s = bmx > 1e-5f ? std::clamp((bmx - bmn) / bmx, 0.f, 1.f) : 1.f;
    for (int i = 0; i < 3; ++i) {
      const float f = (c[i] - mn) / (mx - mn);  // hue: where the channel sits between min and max
      o[i] = mx * (1.f - s) + mx * s * f;
    }
    return o;
  }
  if (sMode == 1 && mn < l && sat > 1.f) {
    sat = std::min(sat, l / (l - mn));
  }
  float omx = 0.f;
  for (int i = 0; i < 3; ++i) {
    o[i] = std::max(l + (c[i] - l) * sat, 0.f);
    omx = std::max(omx, o[i]);
  }
  if (omx > 1e-5f) {
    for (float& v : o) {
      v *= mx / omx;
    }
  }
  return o;
}

// Bloom glow buffer: emissive draws (additive blending: what the game adds as light) recorded
// with a copy of their pipeline that renders into the glow buffer, depth-tested against the
// scene's depth, at scene end.
struct GlowDraw {
  gx::DrawData data;
  PipelineRef lightPipe{}; // 0028: shadow-map caster variant (depth draws; 0 = none)
  Viewport viewport;
  ClipRect scissor;
  uint32_t pass = UINT32_MAX, command = 0; // the recorded draw (merged draws extend it)
  uint32_t vtxCount = 0; // debug
  uint32_t tevStages = 0;
  bool lit = false;
  bool nrm = false; // 0224: depth pre-pass pipeline built for depth_normal_layout (AO normals)
  bool casterOnly = false; // 0470: AURORA_DRAW_CLASS_SHADOW: shadow-map atlas only, not the pre-pass
};
std::vector<GlowDraw> g_glowDraws;

// Extra-light shadows: lights flagged ExtraLightCastShadows get a mask slot (up to 4); the opaque
// world draws are re-rendered depth-only (same mechanism as the glow draws) into a pre-pass
// before the EFB pass they belong to, a ray-march pass writes the RGBA8 mask and it is copied
// into a storage-buffer range that the world shaders of that pass read.
// 0028: up to 8 lights (two RGBA8 planes) and, with extraShadowMode 1, shadow maps: every
// opaque world draw also gets a caster variant (ShaderConfig::lightDepth) that renders it into
// a cube-map atlas from each shadowed light (one instanced draw: instance = light x face), and
// the mask pass looks the camera-depth positions up in the atlas (PCF / PCSS) instead of ray
// marching. The mask lives in its own GPU buffer (group 0 binding 2): nothing is uploaded.
constexpr uint32_t MaxShadowLights = 16; // 0187: was 8 (shadow maps; screen-space stays at 4)
std::array<uint32_t, MaxExtraLights> g_shadowSlot{};
uint32_t g_shadowCount = 0;
uint32_t g_atlasCols = 0, g_atlasRows = 0, g_atlasS = 0; // 0223: shadow-map atlas grid (FIFO thread; rows only grow)
std::vector<GlowDraw> g_depthDraws;
bool g_maskReserved = false; // this frame
size_t g_depthFlushed = 0;   // g_depthDraws already resolved and in a queued mask
uint32_t g_maskW = 0, g_maskH = 0, g_maskPitch = 0; // pitch in u32 words
std::atomic<uint32_t> g_statLights{0}, g_statCasters{0}, g_statSkipped{0};
uint32_t g_shadowClassDraws = 0;              // 0470: caster-only draws recorded this frame (FIFO thread)
size_t g_lastCasterOnly = SIZE_MAX;           // 0470: g_depthDraws index of the last note_draw's caster-only draw
std::atomic<uint32_t> g_statShadowClass{0};   // 0470: ... in the last finished frame
uint32_t g_lightGen = 0;                  // bumped when the lights or the configuration change
uint64_t g_maskKey = ~0ull;               // light gen / atlas / mask layout of the last mask
bool shadow_maps() noexcept;
void assign_shadow_slots() noexcept;
// 0032: ambient occlusion runs forward (mask pass, applied in the opaque world draws);
// AURORA_ENH_AO_POST=1 restores the former scene-end multiply (A/B).
const bool g_aoPost = std::getenv("AURORA_ENH_AO_POST") != nullptr;
// 0452: sun shafts at half the mask resolution (AURORA_ENH_SHAFTS_FULL=1: full, A/B).
bool shafts_half() noexcept {
  static const bool sFull = std::getenv("AURORA_ENH_SHAFTS_FULL") != nullptr;
  return !sFull;
}
// Unlit textured opaque world draws get the enhanced variant too while AO is on (they receive it).
bool ao_receivers() noexcept { return g_cfg.enabled && g_cfg.ssao && !g_aoPost; }
// 0340: unlit opaque world draws get the world class (they receive AO and/or directional shadows).
bool receivers() noexcept { return ao_receivers() || (g_cfg.enabled && g_cfg.lightReceivers); }
bool ao_forward() noexcept { return ao_receivers() && g_cfg.ssaoIntensity > 0.f; }
constexpr auto GlowFormat = wgpu::TextureFormat::RGBA16Float;
constexpr size_t MaxGlowDraws = 4096;
// 0186: the depth pre-pass list was capped at MaxGlowDraws; LM's main view has ~5000 opaque world
// draws, so the last ~900 (Luigi, the other actors, late room parts) were missing from the pre-pass
// and their pixels received the ambient occlusion / light shadows of whatever stood behind them.
constexpr size_t MaxDepthDraws = 32768;

uint32_t g_sceneEnds = 0;
const uint32_t g_drawLogFrame = [] {
  const char* v = std::getenv("AURORA_ENH_DRAWLOG"); // log every draw of the n-th scene (1-based)
  return v != nullptr ? uint32_t(std::atoi(v)) : 0u;
}();
const int g_glowDbg = [] {
  const char* v = std::getenv("AURORA_ENH_GLOWDBG"); // 1: show the glow buffer, 2: one glow draw per frame
  return v != nullptr ? std::atoi(v) : 0;
}();
uint32_t g_segDraws[3] = {};
const bool g_drawLogAll = std::getenv("AURORA_ENH_DRAWLOG_ALL") != nullptr;
const bool g_log = [] {
  const char* v = std::getenv("AURORA_ENH_LOG");
  return v != nullptr && v[0] == '1';
}();

// --- GPU side (render worker) ---
constexpr std::string_view FxShader = R"""(
struct Fx {
  size: vec4f,   // target w, h, 1/w, 1/h
  vp: vec4f,     // render viewport x, y, w, h (target pixels)
  proj: vec4f,   // P00, P11, P02, P12
  projz: vec4f,  // A, B (depth = A + B / z), P03, P13
  dr: vec4f,     // viewport min/max depth, frame, effect mask
  lpos: vec4f,   // flashlight (view space), w = valid
  ldir: vec4f,
  lcos: vec4f,
  ldist: vec4f,
  lcol: vec4f,
  bloom: vec4f,  // intensity, threshold, knee, flashlight suppression
  vol: vec4f,    // intensity, steps, max distance, 0
  ao: vec4f,     // intensity, radius, 0, 0
  pp: vec4f,     // per pass: source texel size xy, radius, weight
  bloom2: vec4f, // scene highlights amount, emissive amount, light glow amount, light count
  glowdepth: vec4f, // glow depth pass: source scale xy (glow -> target pixels), reversed z, bias
  xl: array<vec4f, 36>, // light shadows (0187: 16 lights): see fs_xlshadow / cube_occlusion
  beam: array<vec4f, 16>, // 0196 realistic flashlight (see fs_beam)
};
@group(0) @binding(0) var<uniform> fx: Fx;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var t0: texture_2d<f32>;
@group(0) @binding(3) var t1: texture_2d<f32>;
@group(0) @binding(4) var t2: texture_2d<f32>;
@group(0) @binding(5) var tdepth: texture_2d<f32>;

struct VsOut {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
};
@vertex
fn vs_main(@builtin(vertex_index) vtx: u32) -> VsOut {
  let p = vec2f(f32((vtx << 1u) & 2u), f32(vtx & 2u));
  var out: VsOut;
  out.pos = vec4f(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
  out.uv = p;
  return out;
}

fn ign(p: vec2f) -> f32 { return fract(52.9829189 * fract(dot(p, vec2f(0.06711056, 0.00583715)))); }
fn luma(c: vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }

// View-space position of a target pixel (pixel units) at raw depth d.
fn view_pos(px: vec2f, d: f32) -> vec3f {
  let dn = (d - fx.dr.x) / max(fx.dr.y - fx.dr.x, 1e-6);
  let ndc = vec2f((px.x - fx.vp.x) / fx.vp.z * 2.0 - 1.0, 1.0 - (px.y - fx.vp.y) / fx.vp.w * 2.0);
  // Raw depth (reversed Z as the EFB stores it) = A + B / z_view (A, B from the GX projection).
  let den = dn - fx.projz.x;
  let z = fx.projz.y / select(den, 1e-7, abs(den) < 1e-7);
  let w = -z;
  return vec3f((ndc.x * w - fx.proj.z * z - fx.projz.z) / fx.proj.x, (ndc.y * w - fx.proj.w * z - fx.projz.w) / fx.proj.y, z);
}
fn load_depth(px: vec2f) -> f32 {
  let dims = vec2i(textureDimensions(tdepth));
  return textureLoad(tdepth, clamp(vec2i(px), vec2i(0), dims - vec2i(1)), 0).r;
}
// GX spot light attenuation of the flashlight at a view-space point (no N.L).
fn flash_attn(p: vec3f) -> f32 {
  var l = fx.lpos.xyz - p;
  let dist2 = dot(l, l);
  let dist = sqrt(dist2);
  l = l / max(dist, 1e-4);
  let c = max(0.0, dot(l, fx.ldir.xyz));
  let ca = max(0.0, dot(fx.lcos.xyz, vec3f(1.0, c, c * c)));
  let da = max(dot(fx.ldist.xyz, vec3f(1.0, dist, dist2)), 1e-4);
  return ca / da;
}

// Bloom bright pass: 13-tap downsample of the scene (Karis-weighted against fireflies) with a
// soft-knee threshold; surfaces that are only bright because the flashlight shines on them glow
// less (bloom.w).
fn tap(uv: vec2f) -> vec3f { return textureSampleLevel(t0, samp, uv, 0.0).rgb; }
fn karis(c: vec3f) -> f32 { return 1.0 / (1.0 + luma(c)); }
@fragment
fn fs_bright(in: VsOut) -> @location(0) vec4f {
  let ts = fx.pp.xy;
  let uv = in.uv;
  let a = tap(uv + ts * vec2f(-2.0, -2.0));
  let b = tap(uv + ts * vec2f(0.0, -2.0));
  let c = tap(uv + ts * vec2f(2.0, -2.0));
  let d = tap(uv + ts * vec2f(-1.0, -1.0));
  let e = tap(uv + ts * vec2f(1.0, -1.0));
  let f = tap(uv + ts * vec2f(-2.0, 0.0));
  let g = tap(uv);
  let h = tap(uv + ts * vec2f(2.0, 0.0));
  let i = tap(uv + ts * vec2f(-1.0, 1.0));
  let j = tap(uv + ts * vec2f(1.0, 1.0));
  let k = tap(uv + ts * vec2f(-2.0, 2.0));
  let l = tap(uv + ts * vec2f(0.0, 2.0));
  let m = tap(uv + ts * vec2f(2.0, 2.0));
  let g0 = (d + e + i + j) * 0.25;
  let g1 = (a + b + f + g) * 0.25;
  let g2 = (b + c + g + h) * 0.25;
  let g3 = (f + g + k + l) * 0.25;
  let g4 = (g + h + l + m) * 0.25;
  let w0 = karis(g0) * 0.5;
  let w1 = karis(g1) * 0.125;
  let w2 = karis(g2) * 0.125;
  let w3 = karis(g3) * 0.125;
  let w4 = karis(g4) * 0.125;
  var col = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
  // Soft knee on the brightest channel (saturated light sources keep their color).
  let br = max(col.r, max(col.g, col.b));
  let th = fx.bloom.y;
  let knee = max(fx.bloom.z, 1e-4);
  var soft = clamp(br - th + knee, 0.0, 2.0 * knee);
  soft = soft * soft / (4.0 * knee);
  let contrib = max(soft, br - th) / max(br, 1e-4);
  col = col * max(contrib, 0.0);
  if (fx.lpos.w > 0.5 && fx.bloom.w > 0.0) {
    let px = uv * fx.size.xy;
    let p = view_pos(px, load_depth(px));
    let lit = clamp(flash_attn(p) * luma(fx.lcol.rgb), 0.0, 1.0);
    col = col * (1.0 - fx.bloom.w * lit);
  }
  // Ordinary lit surfaces only glow by the "scene highlights" amount; light sources come from the
  // glow buffer (additive/emissive draws re-rendered depth-tested at this resolution) only.
  // 0187: no halo at extra-light positions any more. A point light is not a visible object: a
  // halo drawn there showed as a floating orb wherever the source itself was hidden or never
  // drawn (hidden ghosts, the light above a Grabbing Ghost, fixtures).
  // The glow buffer is compressed (Reinhard on luma) so large additive areas such as the
  // flashlight beam pointing at the camera glow softly instead of blowing out.
  let gl = textureSampleLevel(t1, samp, uv, 0.0).rgb * fx.bloom2.y;
  col = col * fx.bloom2.x + gl / (1.0 + luma(gl));
  return vec4f(col, 1.0);
}

// Glow buffer depth: the scene depth at the glow buffer's resolution, nudged slightly farther so
// re-rendered emissive surfaces pass the depth test against themselves.
@fragment
fn fs_glowdepth(in: VsOut) -> @builtin(frag_depth) f32 {
  // 0031: the farthest depth of the scene pixels this texel covers (point sampling one of them
  // failed the re-rendered surface on floors seen at grazing angles).
  let p0 = floor(in.pos.xy) * fx.glowdepth.xy;
  let n = vec2u(clamp(ceil(fx.glowdepth.xy), vec2f(1.0), vec2f(4.0)));
  var d = select(0.0, 1.0, fx.glowdepth.z > 0.5); // reversed Z: farther is smaller (min from 1)
  for (var y = 0u; y < n.y; y++) {
    for (var x = 0u; x < n.x; x++) {
      let s = load_depth(p0 + vec2f(f32(x), f32(y)) + vec2f(0.5));
      d = select(max(d, s), min(d, s), fx.glowdepth.z > 0.5);
    }
  }
  if (fx.glowdepth.z > 0.5) {
    return d * (1.0 - fx.glowdepth.w);
  }
  return 1.0 - (1.0 - d) * (1.0 - fx.glowdepth.w);
}

// Extra-light shadow mask (pre-pass depth at mask resolution in tdepth): per shadowed light (up
// to 4, fx.xl[i] = view position + reach), march from the surface toward the light through the
// depth buffer; a stored surface in front of the ray (within a thickness) occludes it.
// pp: steps, target pixels per mask pixel, light count.
fn mask_view_pos(mp: vec2f) -> vec3f {
  let dims = vec2i(textureDimensions(tdepth));
  let d = textureLoad(tdepth, clamp(vec2i(mp), vec2i(0), dims - vec2i(1)), 0).r;
  return view_pos((floor(mp) + vec2f(0.5)) * fx.pp.y, d);
}
// 0210: incremental masks. A second mask in a frame only serves the opaque draws of the later EFB
// pass, which read it where they are (4x4 tent, depth-matched to their own surface): mask texels
// are recomputed only within a tile of where those new draws changed the pre-pass depth (t1 =
// 8x8 tile flags from fs_dirty, fx.ao.z = 8), the rest keeps the frame's previous mask.
@group(0) @binding(9) var tdepthold: texture_2d<f32>;
fn mask_needed(mp: vec2f) -> bool {
  if (fx.ao.z < 0.5) { return true; }
  let td = vec2i(textureDimensions(t1));
  let t = vec2i(mp) / vec2i(8);
  for (var y = -1; y <= 1; y++) {
    for (var x = -1; x <= 1; x++) {
      let c = clamp(t + vec2i(x, y), vec2i(0), td - vec2i(1));
      if (textureLoad(t1, c, 0).r > 0.5) { return true; }
    }
  }
  return false;
}
// One texel per 8x8 mask texels: 1 where the pre-pass depth differs from the previous mask's.
// 2x2 gathers (clamp to edge: texels past the edge repeat the edge, which compares the same).
@group(0) @binding(10) var psamp: sampler;
@fragment
fn fs_dirty(in: VsOut) -> @location(0) vec4f {
  let inv = 1.0 / vec2f(textureDimensions(tdepth));
  let t = floor(in.pos.xy) * 8.0 + vec2f(1.0);
  var diff = false;
  for (var y = 0; y < 4; y++) {
    for (var x = 0; x < 4; x++) {
      let uv = (t + vec2f(f32(x), f32(y)) * 2.0) * inv;
      diff = diff || any(textureGather(0, tdepth, psamp, uv) != textureGather(0, tdepthold, psamp, uv));
    }
  }
  return vec4f(select(0.0, 1.0, diff));
}
@fragment
fn fs_xlshadow(in: VsOut) -> @location(0) vec4f {
  let mp = floor(in.pos.xy);
  let dims = vec2i(textureDimensions(tdepth));
  let d0 = textureLoad(tdepth, vec2i(mp), 0).r;
  if (fx.pp.w > 0.5 && fx.pp.w < 1.5) { return vec4f(fract(d0 * 1000.0), fract(d0 * 100.0), d0 * 10.0, 1.0); } // debug: depth
  if (d0 <= 1e-7) { return vec4f(0.0); } // nothing drawn (cleared to far)
  let p = view_pos((mp + vec2f(0.5)) * fx.pp.y, d0);
  let steps = max(fx.pp.x, 4.0);
  // Per-pixel step offset, the same every frame: a pattern that changes per frame made the shadow
  // edges crawl and pop (and differ between interpolated replays); the receivers blur the mask.
  let jitter = ign(mp);
  var occ = vec4f(0.0);
  if (fx.pp.w > 1.5) {
    return vec4f(-p.z / 3000.0, -fx.xl[0].z / 3000.0, length(fx.xl[0].xyz - p) / 3000.0, fx.pp.z / 4.0);
  }
  for (var s = 0u; s < u32(fx.pp.z); s++) {
    let lp = fx.xl[s];
    let toL = lp.xyz - p;
    let dist = length(toL);
    if (dist >= lp.w || dist < 1.0) { continue; }
    let dir = toL / dist;
    if (fx.pp.w > 1.5) { occ[s] = dist / 1000.0; continue; } // debug: distance to the light
    // Start a little off the surface (self-shadowing), stop short of the light (its own body).
    let t0 = max(3.0, dist * 0.02);
    let len = dist * 0.92 - t0;
    if (len <= 0.0) { continue; }
    var o = 0.0;
    for (var k = 0.0; k < steps; k = k + 1.0) {
      let t = t0 + (k + jitter) / steps * len;
      let q = p + dir * t;
      let w = -q.z;
      if (w <= 1.0) { break; }
      let cx = (fx.proj.x * q.x + fx.proj.z * q.z + fx.projz.z) / w;
      let cy = (fx.proj.y * q.y + fx.proj.w * q.z + fx.projz.w) / w;
      let sp = vec2f(fx.vp.x + (cx * 0.5 + 0.5) * fx.vp.z, fx.vp.y + (0.5 - cy * 0.5) * fx.vp.w) / fx.pp.y;
      if (any(sp < vec2f(0.0)) || any(sp >= vec2f(dims))) { break; }
      let sz = mask_view_pos(sp).z;
      let front = sz - q.z; // > 0: the stored surface is nearer to the camera than the ray point
      // Soft occlusion: ramps in over the first units in front of the ray and out toward the
      // thickness limit, and is weaker for the last stretch before the light (a hard 0/1 test
      // flipped whole regions between frames as the casters moved).
      let bias = 2.0 + t * 0.01;
      let occ1 = smoothstep(bias, bias + 10.0, front) * (1.0 - smoothstep(120.0, 160.0, front));
      o = max(o, occ1 * (1.0 - smoothstep(0.75, 1.0, t / (dist * 0.92))));
      if (o >= 0.999) { break; }
    }
    occ[s] = o;
  }
  return occ;
}

// 0028: shadow maps. The atlas holds a cube map per shadowed light (3x2 faces); xrec has one
// record per (light, face): view -> atlas clip matrix rows, the face's tile in atlas NDC (x0, y0,
// x1, y1) and in atlas pixels.
// fx.xl[s] = view position + reach, fx.xl[16 + s] = near, tan(half fov), face size, 0;
// fx.xl[32] = atlas w, h, filter taps, base filter radius (texels);
// fx.xl[33] = light size (PCSS, world units), reversed z, focus offset floor (0530), extra-light
// offset floor (0540). (0187: 16 lights; was 8/16/17.)
@group(0) @binding(6) var tatlas: texture_depth_2d;
@group(0) @binding(7) var tcmp: sampler_comparison;
@group(0) @binding(8) var<storage, read> xrec: array<f32>;
struct MaskOut {
  @location(0) a: vec4f,
  @location(1) b: vec4f,
  @location(2) c: vec4f, // 0187: lights 8-11
  @location(3) d: vec4f, // 0187: lights 12-15
};
fn xrow(i: u32) -> vec4f { return vec4f(xrec[i], xrec[i + 1u], xrec[i + 2u], xrec[i + 3u]); }
fn vogel(k: u32, n: u32, rot: f32) -> vec2f {
  let r = sqrt((f32(k) + 0.5) / f32(n));
  let th = f32(k) * 2.39996323 + rot;
  return r * vec2f(cos(th), sin(th));
}
// Cube face of a direction (view space): 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z.
fn cube_face(d: vec3f) -> u32 {
  let a = abs(d);
  if (a.x >= a.y && a.x >= a.z) { return select(1u, 0u, d.x >= 0.0); }
  if (a.y >= a.z) { return select(3u, 2u, d.y >= 0.0); }
  return select(5u, 4u, d.z >= 0.0);
}
// Distance along the face axis of a stored depth.
fn atlas_axis(z: f32, near: f32) -> f32 {
  if (fx.xl[33].y > 0.5) { return near / max(z, 1e-7); }
  return near / max(1.0 - z, 1e-7);
}
fn atlas_blocks(z: f32, r: f32) -> bool { return select((z < r), (z > r), (fx.xl[33].y > 0.5)); }
// Surface normal from the camera depth (the flatter of the two neighbours on each axis, so
// silhouettes do not bend it).
// w = 0 where the pixel sits on a one-pixel ledge (both neighbours on an axis jump away, e.g. the
// edge row of a floor in front of a wall): no reliable normal there.
fn mask_normal(mp: vec2f, p: vec3f) -> vec4f {
  let r = mask_view_pos(mp + vec2f(1.0, 0.0)) - p;
  let l = p - mask_view_pos(mp - vec2f(1.0, 0.0));
  let u = mask_view_pos(mp + vec2f(0.0, 1.0)) - p;
  let d = p - mask_view_pos(mp - vec2f(0.0, 1.0));
  let dx = select(l, r, abs(r.z) < abs(l.z));
  let dy = select(d, u, abs(u.z) < abs(d.z));
  let jump = 4.0 + abs(p.z) * 0.02;
  let ok = min(abs(r.z), abs(l.z)) < jump && min(abs(u.z), abs(d.z)) < jump;
  let c = cross(dx, dy);
  if (dot(c, c) < 1e-12) { return vec4f(normalize(-p), 0.0); }
  var n = normalize(c);
  if (dot(n, p) > 0.0) { n = -n; }
  return vec4f(n, select(0.0, 1.0, ok));
}
var<private> dbg: vec4f;
// Atlas pixel position and depth of a view-space point in face record b.
// 0210: the face record's four matrix rows are loaded once per light (m), not per filter tap.
fn cube_proj(m: mat4x4f, w: vec3f, adims: vec2f) -> vec3f {
  let v = vec4f(w, 1.0);
  let c = vec4f(dot(v, m[0]), dot(v, m[1]), dot(v, m[2]), dot(v, m[3]));
  let nd = c.xyz / max(c.w, 1e-4);
  return vec3f((nd.x * 0.5 + 0.5) * adims.x, (0.5 - nd.y * 0.5) * adims.y, nd.z);
}
// 0340: shadow of a directional light (directional_records): the cascade by view depth, a
// receiver-plane PCF in world units on the surface's tangent plane (PCSS with the light's angular
// size), blended into the next cascade over the last 12% of a slice and faded out toward the
// shadow distance. Surfaces turned away from the light are left to the receiving shader's
// terminator (this is only the shadow map's verdict).
// 0530: the cascade data lives in face 5 (face 4 is the focus cascade, c = 4): [0..3] far view
// depths, [4..7] texel sizes, [8..11] focus centre (view) + radius (0 = none), [16..19] depth
// ranges, [20] the light's angular radius, [21] focus texel, [22] focus range, [23] focus filter
// radius floor (world units).
fn dir_cascade(s: u32, c: u32, q0: vec3f, n: vec3f, l: vec3f, ndl: f32, mp: vec2f) -> f32 {
  let b = (s * 6u + c) * 24u;
  let m5 = (s * 6u + 5u) * 24u;
  let focus = c >= 4u;
  let texel = select(xrec[m5 + 4u + min(c, 3u)], xrec[m5 + 21u], focus);
  let range = select(xrec[m5 + 16u + min(c, 3u)], xrec[m5 + 22u], focus);
  let tanSun = xrec[m5 + 20u];
  // 0530: the focus cascade's receiver offset has a floor in world units (fx.xl[33].z): its texels
  // are so small that a low-poly character shadowed itself along its facets near the terminator
  // (a hard polygonal line across the cheek), which the view cascades' larger offset never showed.
  let ob = select(texel, max(texel, fx.xl[33].z), focus);
  let q = q0 + n * ob * (1.0 + 2.0 * (1.0 - clamp(ndl, 0.0, 1.0))) + l * ob * 1.5;
  let fm = mat4x4f(xrow(b), xrow(b + 4u), xrow(b + 8u), xrow(b + 12u));
  let adims = fx.xl[32].xy;
  let tile = xrow(b + 20u);
  let lo = tile.xy + vec2f(1.0);
  let hi = tile.zw - vec2f(1.0);
  let taps = max(u32(fx.xl[32].z), 4u);
  let rot = ign(mp) * 6.2831853;
  let tu = normalize(select(cross(n, vec3f(0.0, 1.0, 0.0)), cross(n, vec3f(1.0, 0.0, 0.0)), abs(n.y) > 0.9));
  let tv = cross(n, tu);
  let floorR = select(0.0, xrec[m5 + 23u], focus);
  var radius = max(fx.xl[32].w * texel, floorR);
  let rev = fx.xl[33].y > 0.5;
  if (tanSun > 0.0) {
    let t0 = cube_proj(fm, q, adims);
    let search = clamp(tanSun * 600.0, 2.0 * texel, 24.0 * texel);
    let ns = max(taps / 2u, 8u);
    var sum = 0.0;
    var cnt = 0.0;
    for (var k = 0u; k < ns; k++) {
      let o = vogel(k, ns, rot + 1.7) * search;
      let t = cube_proj(fm, q + tu * o.x + tv * o.y, adims);
      let z = textureLoad(tatlas, vec2i(clamp(t.xy, lo, hi)), 0);
      if (atlas_blocks(z, t.z)) {
        sum = sum + abs(t.z - z) * range;
        cnt = cnt + 1.0;
      }
    }
    if (cnt < 0.5) { return 0.0; }
    if (cnt > f32(ns) - 0.5) { return 1.0; }
    radius = clamp(radius + tanSun * (sum / cnt), max(texel, floorR), max(24.0 * texel, floorR));
  }
  var lit = 0.0;
  for (var k = 0u; k < taps; k++) {
    let o = vogel(k, taps, rot) * radius;
    let t = cube_proj(fm, q + tu * o.x + tv * o.y, adims);
    lit = lit + textureSampleCompareLevel(tatlas, tcmp, clamp(t.xy, lo, hi) / adims, t.z);
  }
  return 1.0 - lit / f32(taps);
}
fn dir_occlusion(s: u32, p: vec3f, nr: vec4f, mp: vec2f) -> f32 {
  let l = normalize(fx.xl[s].xyz);
  let maxd = fx.xl[s].w;
  let vz = -p.z;
  if (vz >= maxd || vz <= 0.0) { return 0.0; }
  let n = select(l, nr.xyz, nr.w > 0.5);
  let ndl = dot(n, l);
  let mb = (s * 6u + 5u) * 24u; // 0530: cascade data in face 5
  var c = 0u;
  for (; c < 3u; c++) {
    if (vz < xrec[mb + c]) { break; }
  }
  var o = 0.0;
  // 0530: the focus cascade (a small high-resolution map around a character, face 4) inside its
  // sphere, blended into the view cascades over the sphere's outer quarter.
  let fr = xrec[mb + 11u];
  var fw = 0.0;
  if (fr > 0.0) {
    fw = 1.0 - smoothstep(fr * 0.72, fr * 0.95, length(p - vec3f(xrec[mb + 8u], xrec[mb + 9u], xrec[mb + 10u])));
  }
  if (fw < 1.0) {
    o = dir_cascade(s, c, p, n, l, ndl, mp);
    let far = xrec[mb + c];
    let near = select(0.0, xrec[mb + c - 1u], c > 0u);
    let t = (vz - near) / max(far - near, 1e-3);
    if (c < 3u && t > 0.88) {
      o = mix(o, dir_cascade(s, c + 1u, p, n, l, ndl, mp), smoothstep(0.88, 1.0, t));
    }
  }
  if (fw > 0.0) {
    o = mix(o, dir_cascade(s, 4u, p, n, l, ndl, mp), fw);
    c = 4u;
  }
  // Debug (AURORA_ENH_GLOWDBG=9): cascade (red), shadow (green), N.L (blue).
  if (fx.pp.w > 0.5) { dbg = vec4f(f32(c) / 3.0, o, ndl * 0.5 + 0.5, 1.0); }
  // (=10: the light direction left half, the receiver normal right half)
  if (fx.pp.w > 1.5) { dbg = vec4f(select(n, l, mp.x * fx.pp.y < fx.size.x * 0.5) * 0.5 + 0.5, 1.0); }
  return o * (1.0 - smoothstep(maxd * 0.85, maxd, vz));
}
fn cube_occlusion(s: u32, p: vec3f, nr: vec4f, mp: vec2f) -> f32 {
  if (fx.xl[16u + s].w > 0.5) { return dir_occlusion(s, p, nr, mp); }
  let lp = fx.xl[s];
  let lq = fx.xl[16u + s];
  let toL = lp.xyz - p;
  let dist = length(toL);
  if (dist >= lp.w || dist <= lq.x) { return 0.0; }
  let l = toL / dist;
  // Unreliable normal (ledge pixel): offset toward the light instead.
  let n = select(l, nr.xyz, nr.w > 0.5);
  let ndl = dot(n, l);
  if (ndl <= -0.05) { return 0.0; } // facing away: the light's N.L already leaves it dark
  let dn = -toL;
  let axis0 = max(max(abs(dn.x), abs(dn.y)), abs(dn.z));
  // World size of one shadow-map texel at the receiver; offset along the normal (more at grazing
  // angles) and toward the light against acne.
  let texel = 2.0 * axis0 * lq.y / lq.z;
  // 0540: with a floor in world units (fx.xl[33].w): close to the light the texel is a few
  // hundredths of a unit, far less than how much a low-poly surface bends under the filter taps
  // spread on its tangent plane (the PCSS search region), so a character standing next to a
  // shadowed light shadowed itself in facet-shaped blotches that changed with every pose.
  let ob = max(texel, fx.xl[33].w);
  let q = p + n * ob * (0.8 + 1.6 * (1.0 - clamp(ndl, 0.0, 1.0))) + l * ob * 0.6;
  let d = q - lp.xyz;
  let f = cube_face(d);
  let b = (s * 6u + f) * 24u;
  let qv = vec4f(q, 1.0);
  let fm = mat4x4f(xrow(b), xrow(b + 4u), xrow(b + 8u), xrow(b + 12u));
  let clip = vec4f(dot(qv, fm[0]), dot(qv, fm[1]), dot(qv, fm[2]), dot(qv, fm[3]));
  if (clip.w <= 1e-4) { return 0.0; }
  let ndc = clip.xyz / clip.w;
  let adims = fx.xl[32].xy;
  let px = vec2f((ndc.x * 0.5 + 0.5) * adims.x, (0.5 - ndc.y * 0.5) * adims.y);
  let tile = xrow(b + 20u);
  let lo = tile.xy + vec2f(1.0);
  let hi = tile.zw - vec2f(1.0);
  let refz = ndc.z;
  if (fx.pp.w > 0.5) {
    // Debug (AURORA_ENH_GLOWDBG=9): stored vs receiver distance along the face axis, face.
    let zs = textureLoad(tatlas, vec2i(clamp(px, lo, hi)), 0);
    dbg = vec4f(atlas_axis(zs, lq.x) / 1000.0, clip.w / 1000.0, f32(f) / 5.0, 1.0);
  }
  let taps = max(u32(fx.xl[32].z), 4u);
  // Same rotation every frame for a pixel: a per-frame pattern makes the edges crawl.
  let rot = ign(mp) * 6.2831853;
  let axis = clip.w;
  // Filter taps are spread on the receiver's tangent plane in world space and each projected
  // with its own depth (receiver-plane depth): a wide soft filter on a floor seen at a grazing
  // angle from the light does not shadow the floor itself.
  let tu = normalize(select(cross(n, vec3f(0.0, 1.0, 0.0)), cross(n, vec3f(1.0, 0.0, 0.0)), abs(n.y) > 0.9));
  let tv = cross(n, tu);
  var radius = fx.xl[32].w * texel; // world units
  if (fx.xl[33].x > 0.0) {
    // PCSS: average blocker distance in a region the size of the light seen from the receiver,
    // then a penumbra that widens with the receiver's distance behind the blocker.
    let search = clamp(fx.xl[33].x * 1.5, 2.0 * texel, 40.0 * texel);
    let ns = max(taps / 2u, 8u);
    var sum = 0.0;
    var cnt = 0.0;
    for (var k = 0u; k < ns; k++) {
      let o = vogel(k, ns, rot + 1.7) * search;
      let t = cube_proj(fm, q + tu * o.x + tv * o.y, adims);
      let z = textureLoad(tatlas, vec2i(clamp(t.xy, lo, hi)), 0);
      if (atlas_blocks(z, t.z)) {
        sum = sum + atlas_axis(z, lq.x);
        cnt = cnt + 1.0;
      }
    }
    if (cnt < 0.5) { return 0.0; }
    // Every search tap blocked: deep in the umbra, no filtering needed.
    if (cnt > f32(ns) - 0.5) { return 1.0; }
    let ab = sum / cnt;
    let pen = fx.xl[33].x * max(axis - ab, 0.0) / max(ab, 1.0);
    radius = clamp(radius + pen, texel, 24.0 * texel);
  }
  var lit = 0.0;
  for (var k = 0u; k < taps; k++) {
    let o = vogel(k, taps, rot) * radius;
    let t = cube_proj(fm, q + tu * o.x + tv * o.y, adims);
    lit = lit + textureSampleCompareLevel(tatlas, tcmp, clamp(t.xy, lo, hi) / adims, t.z);
  }
  return 1.0 - lit / f32(taps);
}
@fragment
fn fs_xlcube(in: VsOut) -> MaskOut {
  let mp = floor(in.pos.xy);
  if (!mask_needed(mp)) { discard; }
  let d0 = textureLoad(tdepth, vec2i(mp), 0).r;
  var o: MaskOut;
  o.a = vec4f(0.0);
  o.b = vec4f(0.0);
  o.c = vec4f(0.0);
  o.d = vec4f(0.0);
  if (d0 <= 1e-7) { return o; } // nothing drawn (cleared to far)
  let p = view_pos((mp + vec2f(0.5)) * fx.pp.y, d0);
  let n = mask_normal(mp, p);
  let cnt = u32(fx.pp.z);
  for (var s = 0u; s < min(cnt, 4u); s++) { o.a[s] = cube_occlusion(s, p, n, mp); }
  for (var s = 4u; s < min(cnt, 8u); s++) { o.b[s - 4u] = cube_occlusion(s, p, n, mp); }
  for (var s = 8u; s < min(cnt, 12u); s++) { o.c[s - 8u] = cube_occlusion(s, p, n, mp); }
  for (var s = 12u; s < min(cnt, 16u); s++) { o.d[s - 12u] = cube_occlusion(s, p, n, mp); }
  if (fx.pp.w > 0.5) { o.a = dbg; }
  return o;
}

// 0032: ambient occlusion in the mask pass (third plane, slot 8), from the opaque depth pre-pass,
// applied inside the opaque world draws (ubuf.enh_flags bit 16) instead of multiplying the whole
// picture at scene end, which also darkened what is drawn in front of occluded geometry: the
// ghost layer, the flashlight beam, light effects, particles, glass and water.
// 0186: AO samples went through a depth-aware 4x4 blur; 0187: occluders only within the radius.
// 0224: horizon-based ambient occlusion (after GTAO, Jimenez et al. 2016, "Practical Real-Time
// Strategies for Accurate Indirect Occlusion"), replacing 0032's hemisphere samples. Those were
// oriented by the face normal reconstructed from the depth buffer, so every triangle of a round
// low-poly character (Toad's cap, E. Gadd's head, Luigi) got its own flat grey: faceted, blocky
// patches. Now:
// - the normal is the interpolated vertex normal, written by the depth pre-pass into a normal
//   target (t0 here; draws without normals and pixels the pre-pass did not cover fall back to the
//   depth-derived normal);
// - per pixel two view-aligned slices, each searched on both sides for the highest horizon over
//   AO_STEPS depth samples (quadratically spaced out to the projected radius), and the visible
//   arc integrated against the cosine of the projected normal (the GTAO slice integral);
// - an angle bias lowers each horizon a little, so a low-poly convex surface does not occlude
//   itself through the facets next to it;
// - thickness heuristics: occluders count with a smooth 3D distance falloff to the radius, the
//   part of their offset along the view ray counts double (a character standing in front of a
//   wall darkens it less than a wall corner), and a horizon decays toward lower samples after it
//   (objects have a finite thickness: thin rails and legs no longer shadow everything behind);
// - slice rotation and step jitter follow a 4x4 interleaved pattern (32 directions per 4x4 block),
//   which the depth- and normal-aware 4x4 filter below cancels exactly on smooth surfaces. Same
//   pattern every frame (no temporal noise): stable when the camera stops and across interpolated
//   replays.
// Computed at fx.ao.w (1 or 2) mask texels per AO texel; output: visibility (1 = open), view z,
// octahedral normal (the filter's weights). fx.ao: intensity, radius (world units), incremental
// flag, scale.
const AO_STEPS = 6u;
const AO_HALF_PI = 1.5707963;
const AO_BIAS = 0.12;   // radians
const AO_THICK = 0.12;  // horizon decay per lower sample
fn bayer4(p: vec2f) -> f32 {
  var m = array<u32, 16>(0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u);
  let i = (u32(p.y) & 3u) * 4u + (u32(p.x) & 3u);
  return (f32(m[i]) + 0.5) / 16.0;
}
fn oct_enc(n: vec3f) -> vec2f {
  let p = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));
  if (n.z < 0.0) { return (vec2f(1.0) - abs(p.yx)) * select(vec2f(-1.0), vec2f(1.0), p >= vec2f(0.0)); }
  return p;
}
fn oct_dec(e: vec2f) -> vec3f {
  var n = vec3f(e, 1.0 - abs(e.x) - abs(e.y));
  let t = max(-n.z, 0.0);
  n = vec3f(n.x + select(t, -t, n.x >= 0.0), n.y + select(t, -t, n.y >= 0.0), n.z);
  return normalize(n);
}
// View-space position of a target pixel at view z.
fn view_pos_z(px: vec2f, z: f32) -> vec3f {
  let ndc = vec2f((px.x - fx.vp.x) / fx.vp.z * 2.0 - 1.0, 1.0 - (px.y - fx.vp.y) / fx.vp.w * 2.0);
  let w = -z;
  return vec3f((ndc.x * w - fx.proj.z * z - fx.projz.z) / fx.proj.x, (ndc.y * w - fx.proj.w * z - fx.projz.w) / fx.proj.y, z);
}
// The surface normal at a mask texel: the pre-pass normal target's (xyz * 0.5 + 0.5, w = 1 where
// the draw had normals), else the depth-derived one; facing the camera.
fn ao_normal(nt: vec4f, mp: vec2f, p: vec3f) -> vec3f {
  let v = normalize(-p);
  var n: vec3f;
  if (nt.w > 0.5) {
    n = nt.xyz * 2.0 - vec3f(1.0);
    let l = dot(n, n);
    n = select(v, n * inverseSqrt(max(l, 1e-12)), l > 1e-6);
    if (dot(n, v) < -0.25) { n = -n; } // back face of a two-sided surface
    // 0340: art normals (City Folk lights its backdrops and foliage with normals bent far from the
    // geometry, e.g. a vertical tree wall with upward normals): where the vertex normal is more
    // than ~60 degrees off a reliable geometric normal, the geometry's is used (the wall greyed
    // out from its own ground-facing horizon otherwise).
    let g = mask_normal(mp, p);
    if (g.w > 0.5 && dot(n, g.xyz) < 0.5) { n = g.xyz; }
  } else {
    // (w = 0 marks a one-pixel ledge; its one-sided normal is still far better than the view
    // vector, which made the edge rows of rugs and steps a dashed dark line)
    n = mask_normal(mp, p).xyz;
  }
  let nv = dot(n, v);
  if (nv < 0.04) { n = normalize(n + v * (0.04 - nv)); } // silhouettes: just inside the view
  return n;
}
fn ao_needed(mp: vec2f, r: i32) -> bool {
  // 0210 incremental masks, with r tiles around changed depth (mask_needed: 1): the upsample
  // reads denoised texels one AO texel away (needs 2), each denoise pass reads its input two
  // away (second pass 3, first pass 4 for the raw AO).
  if (fx.ao.z < 0.5) { return true; }
  let td = vec2i(textureDimensions(t1));
  let t = vec2i(mp) / vec2i(8);
  for (var y = -r; y <= r; y++) {
    for (var x = -r; x <= r; x++) {
      let c = clamp(t + vec2i(x, y), vec2i(0), td - vec2i(1));
      if (textureLoad(t1, c, 0).r > 0.5) { return true; }
    }
  }
  return false;
}
// The highest horizon (cosine to the view vector) on one side of a slice, starting from the
// tangent plane's (low).
fn ao_horizon(mp: vec2f, p: vec3f, n: vec3f, v: vec3f, dir: vec2f, rpx: f32, jit: f32, low: f32, dims: vec2f) -> f32 {
  let radius = fx.ao.y;
  // Height bias: occluders count from a little above the tangent plane (decals, rug edges, small
  // steps and depth precision make no crease line).
  let hb = n * (radius * 0.1 + abs(p.z) * 0.004);
  var h = low;
  for (var k = 0u; k < AO_STEPS; k++) {
    let t = (f32(k) + jit) / f32(AO_STEPS);
    let off = max(t * t * rpx, f32(k) + 1.0);
    let sp = mp + vec2f(0.5) + dir * off;
    if (any(sp < vec2f(0.0)) || any(sp >= dims)) { break; }
    let sd = textureLoad(tdepth, vec2i(sp), 0).r;
    if (sd <= 1e-7) { continue; } // nothing drawn there (sky / cleared)
    let q = view_pos((floor(sp) + vec2f(0.5)) * fx.pp.y, sd);
    let dl = q - p - hb;
    let len2 = dot(dl, dl);
    let along = dot(dl, v);
    let c = along * inverseSqrt(max(len2, 1e-8));
    // Thickness: the offset toward the camera counts double in the falloff distance.
    let dist = sqrt(len2 + max(along, 0.0) * max(along, 0.0));
    let w = 1.0 - smoothstep(radius * 0.5, radius, dist);
    let shc = mix(low, c, w);
    h = select(mix(h, shc, AO_THICK), shc, shc > h);
  }
  return h;
}
@fragment
fn fs_aomask(in: VsOut) -> @location(0) vec4f {
  let s = max(fx.ao.w, 1.0);
  let ap = floor(in.pos.xy);
  let dims = vec2f(textureDimensions(tdepth));
  let mp = min(ap * s, dims - vec2f(1.0));
  if (!ao_needed(mp, 4)) { discard; }
  let d0 = textureLoad(tdepth, vec2i(mp), 0).r;
  if (d0 <= 1e-7) { return vec4f(1.0, 0.0, 0.0, 0.0); } // nothing drawn (z 0 marks it)
  let p = view_pos((mp + vec2f(0.5)) * fx.pp.y, d0);
  let n = ao_normal(textureLoad(t0, vec2i(mp), 0), mp, p);
  let v = normalize(-p);
  // Projected radius in mask texels (capped: close-ups would sample far across the screen).
  let focal = fx.proj.y * 0.5 * fx.vp.w / fx.pp.y;
  let rpx = min(fx.ao.y * focal / max(-p.z, 1.0), dims.y * 0.15);
  var vis = 1.0;
  if (rpx >= 2.0) {
    let noise = bayer4(ap);
    let m = u32(noise * 16.0);
    let jit = (f32((m * 7u + 5u) & 15u) + 0.5) / 16.0;
    var sum = 0.0;
    for (var sl = 0u; sl < 2u; sl++) {
      let phi = (f32(sl) + noise) * AO_HALF_PI;
      let dir = vec2f(cos(phi), sin(phi)); // mask texels, y down
      let dv = vec3f(dir.x, -dir.y, 0.0);  // the same direction in view space
      let ortho = normalize(dv - v * dot(dv, v));
      let axis = cross(ortho, v);
      let pn = n - axis * dot(n, axis);
      let pl = length(pn);
      if (pl < 1e-4) { continue; }
      let cn = clamp(dot(pn, v) / pl, -1.0, 1.0);
      let na = select(-1.0, 1.0, dot(pn, ortho) >= 0.0) * acos(cn);
      let hp = ao_horizon(mp, p, n, v, dir, rpx, jit, cos(na + AO_HALF_PI), dims);
      let hm = ao_horizon(mp, p, n, v, -dir, rpx, jit, cos(na - AO_HALF_PI), dims);
      // Horizon angles from the view vector (- side negative), within the normal's hemisphere,
      // lowered by the bias.
      let h0 = max(-acos(clamp(hm, -1.0, 1.0)) - AO_BIAS, na - AO_HALF_PI);
      let h1 = min(acos(clamp(hp, -1.0, 1.0)) + AO_BIAS, na + AO_HALF_PI);
      let sn = sin(na);
      let a0 = (cn + 2.0 * h0 * sn - cos(2.0 * h0 - na)) * 0.25;
      let a1 = (cn + 2.0 * h1 * sn - cos(2.0 * h1 - na)) * 0.25;
      sum = sum + pl * (a0 + a1);
    }
    vis = sum * 0.5;
  }
  return vec4f(vis, p.z, oct_enc(n));
}
// 0224: depth- and normal-aware 4x4 denoise of the raw AO (t0) at AO resolution: cancels the
// interleaved pattern on smooth surfaces without bleeding across silhouettes (plane distance
// against this texel's surface) or creases (normal similarity). Same output layout as the raw.
fn ao_weight(n0: vec3f, p0: vec3f, r: vec4f, q: vec3f, tol: f32) -> f32 {
  let pd = abs(dot(n0, q - p0));
  return (1.0 - smoothstep(tol, tol * 3.0, pd)) * smoothstep(0.55, 0.9, dot(n0, oct_dec(r.ba)));
}
// Two passes (the second over the first's output, a 7x7 tent in all): one 4x4 box cancels the
// pattern on flat surfaces but left blotches where an occluder covers only some of the 32
// directions (a wall below the bathtub's rim).
@fragment
fn fs_aodenoise(in: VsOut) -> @location(0) vec4f { return ao_denoise(in, 3, -2); }
@fragment
fn fs_aodenoise2(in: VsOut) -> @location(0) vec4f { return ao_denoise(in, 2, -1); }  // (offset the other way: centred in all)
fn ao_denoise(in: VsOut, need: i32, o0: i32) -> vec4f {
  let s = max(fx.ao.w, 1.0);
  let ap = floor(in.pos.xy);
  let dims = vec2f(textureDimensions(tdepth));
  if (!ao_needed(min(ap * s, dims - vec2f(1.0)), need)) { discard; }
  let adims = vec2i(textureDimensions(t0));
  let r0 = textureLoad(t0, vec2i(ap), 0);
  if (r0.g > -1e-3) { return r0; } // nothing drawn
  let p0 = view_pos_z((min(ap * s, dims - vec2f(1.0)) + vec2f(0.5)) * fx.pp.y, r0.g);
  let n0 = oct_dec(r0.ba);
  let tol = 1.0 + abs(p0.z) * 0.01;
  var sum = 0.0;
  var ws = 0.0;
  for (var y = o0; y < o0 + 4; y++) {
    for (var x = o0; x < o0 + 4; x++) {
      let c = clamp(vec2i(ap) + vec2i(x, y), vec2i(0), adims - vec2i(1));
      let r = textureLoad(t0, c, 0);
      if (r.g > -1e-3) { continue; }
      let q = view_pos_z((min(vec2f(c) * s, dims - vec2f(1.0)) + vec2f(0.5)) * fx.pp.y, r.g);
      let w = ao_weight(n0, p0, r, q, tol);
      sum = sum + r.r * w;
      ws = ws + w;
    }
  }
  return vec4f(select(r0.r, sum / ws, ws > 1e-3), r0.gba);
}
// 0224: upsample of the denoised AO (t0) to the mask: the 2x2 AO texels around the mask texel,
// bilinear weights times the same depth/normal weights against this texel's own surface (t2: the
// normal target). Output: the darkening amount the receivers multiply by (0 = none).
@fragment
fn fs_aoblur4(in: VsOut) -> @location(0) vec4f {
  let mp = floor(in.pos.xy);
  if (!mask_needed(mp)) { discard; }
  let d0 = textureLoad(tdepth, vec2i(mp), 0).r;
  if (d0 <= 1e-7) { return vec4f(0.0); }
  let dims = vec2f(textureDimensions(tdepth));
  let p0 = view_pos((mp + vec2f(0.5)) * fx.pp.y, d0);
  let n0 = ao_normal(textureLoad(t2, vec2i(mp), 0), mp, p0);
  let s = max(fx.ao.w, 1.0);
  let adims = vec2i(textureDimensions(t0));
  // (AO texel c was computed at mask texel c * s)
  let a = mp / s;
  let base = vec2i(floor(a));
  let f = a - floor(a);
  let tol = 1.0 + abs(p0.z) * 0.01;
  var sum = 0.0;
  var ws = 0.0;
  var near = 1.0;
  var nearD = 1e30;
  for (var k = 0; k < 4; k++) {
    let o = vec2i(k & 1, k >> 1u);
    let c = clamp(base + o, vec2i(0), adims - vec2i(1));
    let r = textureLoad(t0, c, 0);
    if (r.g > -1e-3) { continue; }
    let q = view_pos_z((min(vec2f(c) * s, dims - vec2f(1.0)) + vec2f(0.5)) * fx.pp.y, r.g);
    let pd = abs(dot(n0, q - p0));
    if (pd < nearD) {
      nearD = pd;
      near = r.r;
    }
    let bw = select(1.0 - f.x, f.x, o.x == 1) * select(1.0 - f.y, f.y, o.y == 1) + 1e-3;
    let w = bw * ao_weight(n0, p0, r, q, tol);
    sum = sum + r.r * w;
    ws = ws + w;
  }
  let vis = clamp(select(near, sum / ws, ws > 1e-4), 0.0, 1.0);
  // 0450: intensity i darkens as visibility^(2 i): 0 nothing, 0.5 the plain horizon-based
  // occlusion, more is stronger (was ^(1 + 2 i): the slider's lowest value still gave all of it).
  return vec4f(1.0 - pow(vis, fx.ao.x * 2.0), 0.0, 0.0, 0.0);
}

// Downsample (13-tap, as above without threshold).
@fragment
fn fs_down(in: VsOut) -> @location(0) vec4f {
  let ts = fx.pp.xy;
  let uv = in.uv;
  let a = tap(uv + ts * vec2f(-2.0, -2.0));
  let b = tap(uv + ts * vec2f(0.0, -2.0));
  let c = tap(uv + ts * vec2f(2.0, -2.0));
  let d = tap(uv + ts * vec2f(-1.0, -1.0));
  let e = tap(uv + ts * vec2f(1.0, -1.0));
  let f = tap(uv + ts * vec2f(-2.0, 0.0));
  let g = tap(uv);
  let h = tap(uv + ts * vec2f(2.0, 0.0));
  let i = tap(uv + ts * vec2f(-1.0, 1.0));
  let j = tap(uv + ts * vec2f(1.0, 1.0));
  let k = tap(uv + ts * vec2f(-2.0, 2.0));
  let l = tap(uv + ts * vec2f(0.0, 2.0));
  let m = tap(uv + ts * vec2f(2.0, 2.0));
  var col = (d + e + i + j) * 0.125;
  col = col + (a + b + f + g) * 0.03125 + (b + c + g + h) * 0.03125;
  col = col + (f + g + k + l) * 0.03125 + (g + h + l + m) * 0.03125;
  return vec4f(col, 1.0);
}

// Upsample: 9-tap tent of the smaller level, added onto the larger one (blend one/one).
@fragment
fn fs_up(in: VsOut) -> @location(0) vec4f {
  let ts = fx.pp.xy * fx.pp.z;
  let uv = in.uv;
  var col = tap(uv) * 4.0;
  col = col + (tap(uv + vec2f(-ts.x, 0.0)) + tap(uv + vec2f(ts.x, 0.0)) + tap(uv + vec2f(0.0, -ts.y)) + tap(uv + vec2f(0.0, ts.y))) * 2.0;
  col = col + tap(uv - ts) + tap(uv + ts) + tap(uv + vec2f(-ts.x, ts.y)) + tap(uv + vec2f(ts.x, -ts.y));
  return vec4f(col * (fx.pp.w / 16.0), 1.0);
}

fn hash3(p: vec3f) -> f32 { return fract(sin(dot(p, vec3f(127.1, 311.7, 74.7))) * 43758.5453); }
fn vnoise(p: vec3f) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  let a = mix(mix(hash3(i), hash3(i + vec3f(1.0, 0.0, 0.0)), u.x), mix(hash3(i + vec3f(0.0, 1.0, 0.0)), hash3(i + vec3f(1.0, 1.0, 0.0)), u.x), u.y);
  let b = mix(mix(hash3(i + vec3f(0.0, 0.0, 1.0)), hash3(i + vec3f(1.0, 0.0, 1.0)), u.x), mix(hash3(i + vec3f(0.0, 1.0, 1.0)), hash3(i + vec3f(1.0, 1.0, 1.0)), u.x), u.y);
  return mix(a, b, u.z);
}

// Volumetric flashlight beam: in-scattering along the view ray up to the surface, through the
// flashlight's GX spot attenuation.
@fragment
fn fs_vol(in: VsOut) -> @location(0) vec4f {
  if (fx.lpos.w < 0.5) { return vec4f(0.0); }
  let px = in.uv * fx.size.xy;
  let surf = view_pos(px, load_depth(px));
  let dist = min(length(surf), fx.vol.z);
  let dir = surf / max(length(surf), 1e-4);
  let steps = max(fx.vol.y, 4.0);
  let ds = dist / steps;
  var t = ds * ign(in.pos.xy + vec2f(fx.dr.z * 5.588238, fx.dr.z * 3.1));
  var acc = 0.0;
  let r0 = 320.0; // haze falls off with distance from the lens (r0: half-density distance)
  for (var s = 0.0; s < steps; s = s + 1.0) {
    let p = dir * t;
    let lv = fx.lpos.xyz - p;
    let dl2 = dot(lv, lv);
    // Forward-scattering phase (looking toward the lens is brighter).
    let phase = 0.6 + 0.4 * max(dot(-dir, lv * inverseSqrt(max(dl2, 1e-4))), 0.0);
    // Dust: slowly drifting value noise modulates the density (motes in the beam).
    let dq = p * 0.045 + vec3f(0.0, fx.vol.w * 0.35, fx.vol.w * 0.2);
    let dust = 0.4 + 1.2 * vnoise(dq) * vnoise(dq * 2.7 + vec3f(17.0)) * 2.0;
    acc = acc + min(flash_attn(p), 1.0) * phase * dust / (1.0 + dl2 / (r0 * r0));
    t = t + ds;
  }
  // Normalized so a ray along the whole beam gathers about 1 (the falloff integrates to pi * r0).
  let v = acc * ds / (3.14159 * r0) * fx.vol.x * 0.45;
  return vec4f(fx.lcol.rgb * (1.0 - exp(-v)), 1.0);
}

// ---- 0196: realistic flashlight -----------------------------------------------------------
// Single scattering of the flashlight in the room's air, ray-marched along each view ray through
// the part of it inside the beam, every sample tested against the flashlight's own shadow map
// (the game's Z16 copy, read as the receivers read it), so Luigi's hand, furniture, ghosts and
// door frames cut shadow shafts into the haze. fx.beam:
//   0-2  shadow map texgen rows (view space -> s, t, q)      3, 4  depth-ramp texgen rows (s, q)
//   5    shadow valid, swap index of the key's low / high byte, map width
//   6    density (scattering coefficient), dust, steps, march range from the lens
//   7    cone edge cosine (GX cutoff), anchor valid, time (s), flare
//   8-10 world anchor: view positions of a world point and of points 256 units along world X / Y
//   11   reflector rings, lens radius^2, reference distance^2, world size of a pixel at distance 1
//   12   map height, quality, heat-map debug (0220), 0
fn beam_key(t: vec4f, sr: u32, sg: u32) -> f32 {
  let v = round(clamp(t, vec4f(0.0), vec4f(1.0)) * 255.0);
  return v[sg] * 256.0 + v[sr];
}
fn beam_gather(c: u32, uv: vec2f) -> vec4f {
  switch c {
    case 0u: { return textureGather(0, t0, samp, uv); }
    case 1u: { return textureGather(1, t0, samp, uv); }
    case 2u: { return textureGather(2, t0, samp, uv); }
    default: { return textureGather(3, t0, samp, uv); }
  }
}
// Fraction of the flashlight reaching view-space point p (2x2 bilinear PCF on the shadow map).
fn beam_vis(p: vec3f) -> f32 {
  if (fx.beam[5].x < 0.5) { return 1.0; }
  let h = vec4f(p, 1.0);
  let q = dot(fx.beam[2], h);
  if (q <= 1e-4) { return 1.0; }
  let uv = vec2f(dot(fx.beam[0], h), dot(fx.beam[1], h)) / q;
  if (any(uv < vec2f(0.0)) || any(uv > vec2f(1.0))) { return 1.0; }
  let rq = dot(fx.beam[4], h);
  if (abs(rq) < 1e-6) { return 1.0; }
  let recv = min(floor(clamp(dot(fx.beam[3], h) / rq, 0.0, 1.0) * 65536.0), 65535.0);
  let dims = vec2f(fx.beam[5].w, fx.beam[12].x);
  let sr = u32(fx.beam[5].y);
  let sg = u32(fx.beam[5].z);
  let f = uv * dims - vec2f(0.5);
  let b = floor(f);
  let w = f - b;
  // 0210: the 2x2 footprint as two gathers (the key's two bytes) instead of four loads; sampled
  // at the footprint's shared corner, so the hardware picks exactly texels b .. b + 1, and the
  // clamp-to-edge sampler repeats the edge texels as the clamped loads did.
  // Gather order: x (0, 1), y (1, 1), z (1, 0), w (0, 0).
  let guv = (b + vec2f(1.0)) / dims;
  let keys = round(clamp(beam_gather(sg, guv), vec4f(0.0), vec4f(1.0)) * 255.0) * 256.0 +
             round(clamp(beam_gather(sr, guv), vec4f(0.0), vec4f(1.0)) * 255.0);
  let v = select(vec4f(1.0), vec4f(0.0), vec4f(recv - 48.0) > keys);
  return mix(mix(v.w, v.z, w.x), mix(v.x, v.y, w.x), w.y);
}
fn beam_inside(p: vec3f, a: vec3f, cosc: f32) -> bool {
  let v = p - fx.lpos.xyz;
  let d = dot(v, a);
  return d > 0.0 && d >= cosc * length(v);
}
// [t0, t1] of the ray (origin 0, direction dir, up to tmax) inside the cone (apex at the lens,
// axis a, half-angle acos(cosc)); x < 0 when it misses. The cone is convex: one interval at most.
fn beam_interval(dir: vec3f, tmax: f32, a: vec3f, cosc: f32) -> vec2f {
  let co = -fx.lpos.xyz;
  let da = dot(dir, a);
  let ca = dot(co, a);
  let c2 = cosc * cosc;
  let qa = da * da - c2;
  let qb = 2.0 * (da * ca - c2 * dot(dir, co));
  let qc = ca * ca - c2 * dot(co, co);
  var ts = array<f32, 4>(0.0, tmax, tmax, tmax);
  var n = 2u;
  var r0 = -1.0;
  var r1 = -1.0;
  if (abs(qa) > 1e-7) {
    let disc = qb * qb - 4.0 * qa * qc;
    if (disc >= 0.0) {
      let sd = sqrt(disc);
      r0 = (-qb - sd) / (2.0 * qa);
      r1 = (-qb + sd) / (2.0 * qa);
      if (r0 > r1) { let tt = r0; r0 = r1; r1 = tt; }
    }
  } else if (abs(qb) > 1e-7) {
    r0 = -qc / qb;
  }
  if (r0 > 0.0 && r0 < tmax) { ts[1] = r0; ts[2] = tmax; n = 3u; }
  if (r1 > 0.0 && r1 < tmax && r1 > ts[n - 2u]) { ts[n - 1u] = r1; ts[n] = tmax; n = n + 1u; }
  var lo = -1.0;
  var hi = -1.0;
  for (var i = 0u; i + 1u < n; i++) {
    let m = 0.5 * (ts[i] + ts[i + 1u]);
    if (ts[i + 1u] > ts[i] && beam_inside(dir * m, a, cosc)) {
      if (lo < 0.0) { lo = ts[i]; }
      hi = ts[i + 1u];
    }
  }
  return vec2f(lo, hi);
}
// Henyey-Greenstein phase (unnormalised: 1 at 90 degrees for g = 0).
fn beam_hg(c: f32, g: f32) -> f32 {
  let d = 1.0 + g * g - 2.0 * g * c;
  return (1.0 - g * g) / (d * sqrt(d));
}
// World-anchored detail (haze, dust) is sampled relative to the anchor point (fx.beam[8..10]),
// which the port snaps to 1024 units: every pattern repeats every 1024 units, so re-anchoring is
// seamless.
// Integer hash (PCG 3D): cheap and exact on every GPU, unlike sin-based hashes.
fn beam_pcg3(v0: vec3u) -> vec3u {
  var v = v0 * 1664525u + vec3u(1013904223u);
  v.x = v.x + v.y * v.z;
  v.y = v.y + v.z * v.x;
  v.z = v.z + v.x * v.y;
  v = v ^ (v >> vec3u(16u));
  v.x = v.x + v.y * v.z;
  v.y = v.y + v.z * v.x;
  v.z = v.z + v.x * v.y;
  return v;
}
fn beam_hash3p(c: vec3i, period: i32) -> vec3f {
  // 0210: every period is a power of two (8, 16, 32): the positive modulo is a bit mask (the same
  // values for negative cells too, two's complement), without three integer divisions.
  let m = c & vec3i(period - 1);
  return vec3f(beam_pcg3(vec3u(m))) * (1.0 / 4294967296.0);
}
fn beam_hashp(c: vec3i, period: i32) -> f32 { return beam_hash3p(c, period).x; }
// Periodic value noise (lattice `cell` units, `period` cells = 1024 units).
fn beam_noise(w: vec3f, cell: f32, period: i32) -> f32 {
  let p = w / cell;
  let i = vec3i(floor(p));
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  let a = mix(mix(beam_hashp(i, period), beam_hashp(i + vec3i(1, 0, 0), period), u.x),
              mix(beam_hashp(i + vec3i(0, 1, 0), period), beam_hashp(i + vec3i(1, 1, 0), period), u.x), u.y);
  let b = mix(mix(beam_hashp(i + vec3i(0, 0, 1), period), beam_hashp(i + vec3i(1, 0, 1), period), u.x),
              mix(beam_hashp(i + vec3i(0, 1, 1), period), beam_hashp(i + vec3i(1, 1, 1), period), u.x), u.y);
  return mix(a, b, u.z);
}
// Light arriving at p from the flashlight (no phase): profile x inverse square x extinction x shadow.
fn beam_light(p: vec3f, a: vec3f, thc: f32) -> f32 {
  let v = p - fx.lpos.xyz;
  let r = length(v);
  let x = acos(clamp(dot(v, a) / max(r, 1e-4), -1.0, 1.0)) / thc;
  let prof = enh_beam_profile(x, fx.beam[11].x);
  if (prof <= 0.0) { return 0.0; }
  // Inverse square, capped at 4x the reference level: right at the lens the air (and a camera looking
  // down the beam from close by) would otherwise see an unbounded, screen-filling white.
  let e = min((fx.beam[11].z + fx.beam[11].y) / (r * r + fx.beam[11].y), 4.0);
  // Light lost on the way: the room air (a beam fades within a few metres) plus the haze density.
  return prof * e * exp(-r * (0.0007 + fx.beam[6].x * 0.15)) * beam_vis(p);
}
@fragment
fn fs_beam(in: VsOut) -> @location(0) vec4f {
  let px = in.uv * fx.size.xy;
  let d = load_depth(px);
  let empty = d <= fx.dr.x + 1e-7;
  let surf = view_pos(px, select(d, fx.dr.x + 2e-7, empty));
  let slen = length(surf);
  let dir = surf / max(slen, 1e-4);
  let zout = min(select(-surf.z, 60000.0, empty), 60000.0);
  if (fx.lpos.w < 0.5) { return vec4f(0.0, 0.0, zout, 1.0); }
  let a = -normalize(fx.ldir.xyz);
  let thc = acos(clamp(fx.beam[7].x, -1.0, 0.9999));
  let cosSpill = cos(min(thc * 1.45, 1.5));
  let L = fx.lpos.xyz;
  let tmax = min(select(slen, 1e6, empty), length(L) + fx.beam[6].w);
  var iv = beam_interval(dir, tmax, a, cosSpill);
  // Only within the march range of the lens (sphere around it): the inverse square has faded.
  {
    let bL = dot(dir, L);
    let disc = bL * bL - (dot(L, L) - fx.beam[6].w * fx.beam[6].w);
    if (disc <= 0.0) { return vec4f(0.0, 0.0, zout, 1.0); }
    let sd = sqrt(disc);
    iv = vec2f(max(iv.x, bL - sd), min(iv.y, bL + sd));
  }
  if (iv.y <= iv.x) { return vec4f(0.0, 0.0, zout, 1.0); }
  if (iv.x < 0.0 || iv.y <= iv.x) { return vec4f(0.0, 0.0, zout, 1.0); }
  // Scattering: a fixed per-pixel jitter (stable at any frame rate), smoothed by the blur pass.
  let steps = max(fx.beam[6].z, 4.0);
  let dt = (iv.y - iv.x) / steps;
  let jit = ign(floor(in.pos.xy));
  let anchored = fx.beam[7].y > 0.5;
  let time = fx.beam[7].z;
  // World frame of the ray (anchor pseudo-lights): w(t) = wc + wd * t.
  let o = fx.beam[8].xyz;
  let ex = normalize(fx.beam[9].xyz - o);
  var ey = fx.beam[10].xyz - o;
  ey = normalize(ey - ex * dot(ey, ex));
  let ez = cross(ex, ey);
  let wc = vec3f(dot(-o, ex), dot(-o, ey), dot(-o, ez));
  let wd = vec3f(dot(dir, ex), dot(dir, ey), dot(dir, ez));
  // Haze: slowly drifting, world-anchored density, evaluated at 9 points along the lit segment
  // and interpolated (integrated along the ray, finer detail would average out anyway).
  var hz = array<f32, 9>(1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0);
  if (anchored) {
    let drift = vec3f(time * 6.0, time * 2.5, time * -4.0);
    for (var k = 0u; k < 9u; k++) {
      let w = wc + wd * (iv.x + (iv.y - iv.x) * f32(k) / 8.0) + drift;
      hz[k] = 0.4 + 1.2 * (beam_noise(w, 128.0, 8) * 0.7 + beam_noise(w + vec3f(37.0), 64.0, 16) * 0.3);
    }
  }
  var acc = 0.0;
  for (var s = 0.0; s < steps; s = s + 1.0) {
    let u = (s + jit) / steps;
    let t = iv.x + u * (iv.y - iv.x);
    let p = dir * t;
    let li = beam_light(p, a, thc);
    if (li <= 0.0) { continue; }
    let lv = normalize(L - p);
    // Dust and droplets scatter mostly forward (looking into the lens is brighter), plus an
    // isotropic part so the beam stays visible from the side.
    let phase = 0.55 + 0.1 * beam_hg(dot(lv, dir), 0.35);
    let hf = min(u * 8.0, 7.999);
    let hi = u32(hf);
    let haze = mix(hz[hi], hz[hi + 1u], hf - f32(hi));
    acc = acc + li * phase * haze * exp(-fx.beam[6].x * 0.04 * t);
  }
  // 0220: looking into the beam (the view ray within the cone's angle of the way back to the lens)
  // the ray runs lengthwise through the bright part near the lens, so its path through the beam,
  // and the haze it gathers, grows like 1 / sin(angle to the axis): from inside the cone the glow
  // washed the whole picture. Scaled by sin(angle) / sin(cone half angle), floored, so the glow
  // stays bounded and centred on the lens; rays at larger angles (every side and back view of the
  // beam) are unchanged.
  var along = 1.0;
  {
    let c = -dot(dir, a);
    let sc = sin(thc);
    if (c > cos(thc) && sc > 1e-3) {
      let s2 = max(1.0 - c * c, 0.0);
      along = min(sqrt(s2 / (sc * sc) + 0.36), 1.0);
    }
  }
  let scatter = acc * dt * fx.beam[6].x * along;
  // Dust motes: one possible mote per 32-unit world cell (periodic hash), walked with a 3D DDA
  // along the lit part of the ray near the lens; each is a tiny glint lit only by the beam (and
  // its shadows).
  var motes = 0.0;
  if (anchored && fx.beam[6].y > 0.0) {
    let C = 32.0;
    let t0 = iv.x;
    // Only where the beam is bright enough to light dust (within ~900 units of the lens).
    let bL = dot(dir, L);
    let disc = bL * bL - (dot(L, L) - 900.0 * 900.0);
    let t1 = select(-1.0, min(iv.y, bL + sqrt(max(disc, 0.0))), disc > 0.0);
    let w0 = wc + wd * t0;
    var cell = vec3i(floor(w0 / C));
    let stp = vec3i(sign(wd));
    let inv = 1.0 / max(abs(wd), vec3f(1e-6));
    let cellf = vec3f(cell);
    let bnd = select(cellf * C, (cellf + vec3f(1.0)) * C, wd > vec3f(0.0));
    var tmx = abs(bnd - w0) * inv;
    let tdl = C * inv;
    var tc = 0.0;
    let maxIt = u32(fx.beam[12].y);
    let chance = 0.16 * fx.beam[6].y;
    for (var it = 0u; it < maxIt; it++) {
      if (t0 + tc > t1) { break; }
      let h = beam_hash3p(cell, 32);
      if (h.x < chance) {
        let r = beam_hash3p(cell + vec3i(7, 11, 13), 32);
        let ph = r * 6.2831853;
        let drift = vec3f(sin(time * (0.21 + 0.17 * r.x) + ph.x), sin(time * (0.13 + 0.11 * r.y) + ph.y),
                          sin(time * (0.17 + 0.13 * r.z) + ph.z)) * (0.18 * C);
        let mw = (vec3f(cell) + vec3f(0.2) + r * 0.6) * C + drift;
        let mv = o + ex * mw.x + ey * mw.y + ez * mw.z;
        let tm = dot(mv, dir);
        if (tm > 1.0 && tm < tmax) {
          let pr = tm * fx.beam[11].w;
          let sig = max(0.7, pr * 0.55);
          let off = mv - dir * tm;
          let d2 = dot(off, off);
          if (d2 < 9.0 * sig * sig) {
            let li = beam_light(mv, a, thc);
            if (li > 0.0) {
              let lv = normalize(L - mv);
              let tw = 0.3 + 0.7 * (0.5 + 0.5 * sin(time * (1.3 + 2.7 * h.y) + h.z * 18.0));
              motes = motes + li * exp(-0.5 * d2 / (sig * sig)) * (0.49 / (sig * sig)) * tw *
                              (0.25 + 0.75 * beam_hg(dot(lv, dir), 0.6) * 0.15) * exp(-fx.beam[6].x * 0.04 * tm);
            }
          }
        }
      }
      // next cell
      if (tmx.x < tmx.y && tmx.x < tmx.z) { tc = tmx.x; tmx.x = tmx.x + tdl.x; cell.x = cell.x + stp.x; }
      else if (tmx.y < tmx.z) { tc = tmx.y; tmx.y = tmx.y + tdl.y; cell.y = cell.y + stp.y; }
      else { tc = tmx.z; tmx.z = tmx.z + tdl.z; cell.z = cell.z + stp.z; }
    }
    motes = motes * 1.1 * fx.beam[6].y;
  }
  return vec4f(scatter, motes, zout, 1.0);
}
// Depth-aware separable blur of the scattering (r); motes (g) and depth (b) pass through.
@fragment
fn fs_beamblur(in: VsOut) -> @location(0) vec4f {
  let dims = vec2i(textureDimensions(t0));
  let c0 = vec2i(floor(in.pos.xy));
  let ctr = textureLoad(t0, c0, 0);
  let tol = ctr.b * 0.03 + 4.0;
  var sum = 0.0;
  var ws = 0.0;
  for (var k = -4; k <= 4; k++) {
    let c = clamp(c0 + vec2i(fx.pp.xy * f32(k)), vec2i(0), dims - vec2i(1));
    let v = textureLoad(t0, c, 0);
    let w = exp(-f32(k * k) / 8.0) * exp(-abs(v.b - ctr.b) / tol);
    sum = sum + v.r * w;
    ws = ws + w;
  }
  return vec4f(sum / max(ws, 1e-5), ctr.g, ctr.b, 1.0);
}
// The beam at a full-resolution pixel: depth-aware upsample of the beam target (t1), so the
// haze stops cleanly at silhouettes in front of it.
fn beam_upsample(uv: vec2f) -> vec2f {
  let px = uv * fx.size.xy;
  let dd = load_depth(px);
  let z0 = select(-view_pos(px, dd).z, 60000.0, dd <= fx.dr.x + 1e-7);
  let bd = vec2f(textureDimensions(t1));
  let f = uv * bd - vec2f(0.5);
  let b = floor(f);
  let w = f - b;
  var acc = vec2f(0.0);
  var ws = 0.0;
  let tol = z0 * 0.03 + 4.0;
  for (var k = 0u; k < 4u; k++) {
    let o = vec2i(i32(k & 1u), i32(k >> 1u));
    let c = clamp(vec2i(b) + o, vec2i(0), vec2i(bd) - vec2i(1));
    let v = textureLoad(t1, c, 0);
    let bw = select(1.0 - w.x, w.x, o.x == 1) * select(1.0 - w.y, w.y, o.y == 1);
    let wt = bw * exp(-abs(min(v.b, 60000.0) - min(z0, 60000.0)) / tol) + 1e-4 * bw;
    acc = acc + v.rg * wt;
    ws = ws + wt;
  }
  return acc / max(ws, 1e-6);
}

// Ambient occlusion from the depth buffer (normal from depth, 12 hemisphere samples).
@fragment
fn fs_ao(in: VsOut) -> @location(0) vec4f {
  let px = in.uv * fx.size.xy;
  let d = load_depth(px);
  if (d <= fx.dr.x + 1e-7) { return vec4f(1.0); }
  let p = view_pos(px, d);
  let s = max(fx.size.x / 640.0, 1.0);
  let pr = view_pos(px + vec2f(s, 0.0), load_depth(px + vec2f(s, 0.0)));
  let pl = view_pos(px - vec2f(s, 0.0), load_depth(px - vec2f(s, 0.0)));
  let pu = view_pos(px + vec2f(0.0, s), load_depth(px + vec2f(0.0, s)));
  let pd = view_pos(px - vec2f(0.0, s), load_depth(px - vec2f(0.0, s)));
  let dx = select(pr - p, p - pl, abs(pr.z - p.z) > abs(p.z - pl.z));
  let dy = select(pu - p, p - pd, abs(pu.z - p.z) > abs(p.z - pd.z));
  var n = normalize(cross(dx, dy));
  n = select(n, -n, dot(n, p) > 0.0);
  let radius = fx.ao.y;
  let rot = ign(in.pos.xy) * 6.2831853;
  var occ = 0.0;
  for (var k = 0u; k < 12u; k++) {
    let r = sqrt((f32(k) + 0.5) / 12.0);
    let a = f32(k) * 2.39996323 + rot;
    // A point on a disk around the normal, pushed out along it (cosine-ish hemisphere).
    var tg = normalize(cross(n, select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(n.y) > 0.9)));
    let bt = cross(n, tg);
    let h = sqrt(max(1.0 - r * r, 0.0));
    let q = p + (tg * (cos(a) * r) + bt * (sin(a) * r) + n * h) * radius * (0.35 + 0.65 * fract(f32(k) * 0.618 + rot));
    // Project q back to the screen.
    let w = -q.z;
    if (w <= 1.0) { continue; }
    let cx = (fx.proj.x * q.x + fx.proj.z * q.z + fx.projz.z) / w;
    let cy = (fx.proj.y * q.y + fx.proj.w * q.z + fx.projz.w) / w;
    let sp = vec2f(fx.vp.x + (cx * 0.5 + 0.5) * fx.vp.z, fx.vp.y + (0.5 - cy * 0.5) * fx.vp.w);
    let sd = load_depth(sp);
    let sz = view_pos(sp, sd).z;
    // Occluded when the stored surface is in front of the sample (range-checked).
    let diff = sz - q.z;
    // 0031: a bias relative to the radius with a soft ramp: decals and layered meshes a few units
    // above the ground no longer draw dark lines along their edges.
    let bias = max(2.0, radius * 0.06);
    if (diff > bias) {
      occ = occ + smoothstep(bias, bias * 4.0, diff) * smoothstep(0.0, 1.0, radius / max(abs(p.z - sz), 1e-3));
    }
  }
  let ao = 1.0 - occ / 12.0;
  return vec4f(vec3f(pow(clamp(ao, 0.0, 1.0), fx.ao.x * 2.0)), 1.0); // (0450: as the forward mask)
}

// Depth-aware 5x5 blur of the occlusion.
@fragment
fn fs_aoblur(in: VsOut) -> @location(0) vec4f {
  let px = in.uv * fx.size.xy;
  let z0 = view_pos(px, load_depth(px)).z;
  var sum = 0.0;
  var wsum = 0.0;
  for (var y = -2; y <= 2; y++) {
    for (var x = -2; x <= 2; x++) {
      let o = vec2f(f32(x), f32(y)) * fx.pp.xy;
      let uv = in.uv + o;
      let z = view_pos(uv * fx.size.xy, load_depth(uv * fx.size.xy)).z;
      let w = exp(-abs(z - z0) * 0.05);
      sum = sum + textureSampleLevel(t0, samp, uv, 0.0).r * w;
      wsum = wsum + w;
    }
  }
  return vec4f(vec3f(sum / max(wsum, 1e-4)), 1.0);
}

// Composite onto the EFB: rgb added (bloom + beam), alpha = occlusion factor
// (blend: src * 1 + dst * src.a).
@fragment
fn fs_composite(in: VsOut) -> @location(0) vec4f {
  let mask = u32(fx.dr.w);
  if ((mask & 16u) != 0u) { // AURORA_ENH_GLOWDBG=7: the glow pass's scene depth
    let dd = load_depth(in.uv * fx.size.xy);
    return vec4f(fract(dd * 1000.0), fract(dd * 100.0), dd * 10.0, 0.0);
  }
  if ((mask & 8u) != 0u) { // AURORA_ENH_GLOWDBG: the glow buffer instead of the picture
    return vec4f(textureSampleLevel(t0, samp, in.uv, 0.0).rgb, 0.0);
  }
  var add = vec3f(0.0);
  if ((mask & 1u) != 0u) {
    add = add + textureSampleLevel(t0, samp, in.uv, 0.0).rgb * fx.bloom.x;
  }
  if ((mask & 32u) != 0u) {
    // 0196: realistic flashlight: scattering + motes in the flashlight's colour, a little whiter
    // where the beam is dense (the eye sees a bright beam as white), compressed so a beam pointing
    // into the camera saturates smoothly; flare (fx.beam[7].w) brightens it during a flash.
    let b = beam_upsample(in.uv);
    var v = (b.x + b.y) * (1.0 + fx.beam[7].w * 2.5);
    if ((mask & 128u) != 0u) { v = b.y * 20.0; } // AURORA_BEAM_SHOW=2: the dust motes alone, amplified
    let fl = fx.beam[7].w;
    let col = mix(fx.lcol.rgb, vec3f(1.0), clamp(v * 0.25 + fl * 0.4, 0.0, 0.6));
    add = add + col * (1.0 - exp(-v));
    // (No glare sprite at the lens: enhanced lighting never draws a halo or orb at a light's
    // position, which showed as a ball behind Luigi's head when the lens was hidden. The flash
    // brightens the beam itself; the game's own lens sprite glows through the glow buffer.)
    // 0220: AURORA_BEAM_SHOW=3: the scattering amount v as a heat map (red 0..2, + green 2..6, + blue 8..16)
    if (fx.beam[12].z > 0.5) { return vec4f(clamp(vec3f(v / 2.0, v / 4.0 - 0.5, v / 8.0 - 1.0), vec3f(0.0), vec3f(1.0)), 0.0); }
    if ((mask & 64u) != 0u) { return vec4f(col * (1.0 - exp(-v)), 0.0); } // AURORA_BEAM_SHOW: the beam alone
  } else if ((mask & 2u) != 0u || (mask & 256u) != 0u) {
    add = add + textureSampleLevel(t1, samp, in.uv, 0.0).rgb; // (256: 0343 sun shafts)
  }
  var ao = 1.0;
  if ((mask & 4u) != 0u) {
    ao = textureSampleLevel(t2, samp, in.uv, 0.0).r;
  }
  return vec4f(add, ao);
}

// 0342: colour grade (fx.beam[13] gain, [14] lift, [15].x saturation, .y contrast) of the scene
// copy in t0.
@fragment
fn fs_grade(in: VsOut) -> @location(0) vec4f {
  let c0 = textureLoad(t0, vec2i(floor(in.pos.xy)), 0);
  var c = clamp(c0.rgb, vec3f(0.0), vec3f(1.0));
  c = c * fx.beam[13].rgb + fx.beam[14].rgb * (vec3f(1.0) - c);
  c = mix(vec3f(luma(c)), c, fx.beam[15].x);
  c = (c - vec3f(0.5)) * fx.beam[15].y + vec3f(0.5);
  // 0421: light sources keep their colour (bright and saturated: lit windows, lamp glows, neon).
  let s0 = clamp(c0.rgb, vec3f(0.0), vec3f(1.0));
  let hi = max(s0.r, max(s0.g, s0.b));
  let chroma = hi - min(s0.r, min(s0.g, s0.b));
  let keep = fx.beam[15].z * smoothstep(0.45, 0.8, hi) * smoothstep(0.1, 0.3, chroma);
  c = mix(c, s0, keep);
  return vec4f(clamp(c, vec3f(0.0), vec3f(1.0)), c0.a);
}

// 0343: sun shafts. Single scattering of the directional light in slot fx.beam[0].x along each
// view ray (mask resolution) up to the pre-pass surface (or the shadow distance where nothing
// was drawn), every step tested against the light's cascades: the shadows of trees and houses
// cut darker shafts into the lit haze. Henyey-Greenstein forward scattering (brighter looking
// toward the light). Colour = fx.beam[1].rgb.
@fragment
fn fs_shafts(in: VsOut) -> @location(0) vec4f {
  // 0452: computed at half the mask resolution (fx.beam[0].z = 2; the composite samples the shaft
  // texture filtered by uv): 1.0-1.2 ms -> ~0.3 ms at 4K with 32 steps, shafts being soft anyway.
  let sc = max(fx.beam[0].z, 1.0);
  let mp = min(floor(in.pos.xy) * sc, vec2f(textureDimensions(tdepth)) - vec2f(1.0));
  let s = u32(fx.beam[0].x);
  let steps = max(u32(fx.beam[0].y), 4u);
  let maxd = fx.xl[s].w;
  let l = normalize(fx.xl[s].xyz);
  let px = (mp + vec2f(0.5)) * fx.pp.y;
  let d0 = textureLoad(tdepth, vec2i(mp), 0).r;
  let dir = normalize(view_pos(px, 0.5));
  var tend = maxd;
  if (d0 > 1e-7) { tend = min(length(view_pos(px, d0)), maxd); }
  let mb = (s * 6u + 5u) * 24u; // 0530: the cascades' far depths in face 5 [0..3]
  let adims = fx.xl[32].xy;
  let jit = ign(mp);
  var acc = 0.0;
  for (var k = 0u; k < steps; k++) {
    let x = dir * ((f32(k) + jit) / f32(steps) * tend);
    let vz = -x.z;
    var c = 0u;
    for (; c < 3u; c++) {
      if (vz < xrec[mb + c]) { break; }
    }
    let b = (s * 6u + c) * 24u;
    let fm = mat4x4f(xrow(b), xrow(b + 4u), xrow(b + 8u), xrow(b + 12u));
    let pr = cube_proj(fm, x, adims);
    let tile = xrow(b + 20u);
    var vis = 1.0;
    if (all(pr.xy > tile.xy) && all(pr.xy < tile.zw)) {
      vis = textureSampleCompareLevel(tatlas, tcmp, pr.xy / adims, pr.z);
    }
    acc = acc + vis;
  }
  let g = 0.6;
  let cv = dot(dir, l);
  let hg = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cv, 1e-4), 1.5);
  let phase = 0.25 + hg * 0.12;
  let amount = acc / f32(steps) * (tend / maxd) * phase;
  return vec4f(fx.beam[1].rgb * amount, 1.0);
}
)""";

struct FxUniform {
  float size[4];
  float vp[4];
  float proj[4];
  float projz[4];
  float dr[4];
  float lpos[4];
  float ldir[4];
  float lcos[4];
  float ldist[4];
  float lcol[4];
  float bloom[4];
  float vol[4];
  float ao[4];
  float pass[4];
  float bloom2[4];
  float glowdepth[4];
  float xl[36][4];
  float beam[16][4]; // 0196
};
static_assert(sizeof(FxUniform) == 224 + 32 + 576 + 256);

constexpr uint32_t MaxBloomLevels = 6;
constexpr auto HdrFormat = wgpu::TextureFormat::RGBA16Float;

struct Tex {
  wgpu::Texture texture;
  wgpu::TextureView view;
  uint32_t width = 0, height = 0;
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
};

struct Gpu {
  bool initialized = false;
  bool failed = false;
  wgpu::ShaderModule module;
  wgpu::BindGroupLayout bgl;
  wgpu::PipelineLayout layout;
  wgpu::Sampler sampler;
  Tex dummy, dummyDepth;
  wgpu::RenderPipeline bright, down, up, vol, ao, aoblur;
  wgpu::RenderPipeline composite;
  wgpu::TextureFormat compositeFormat = wgpu::TextureFormat::Undefined;
  uint32_t compositeSamples = 0;
  Tex depth;
  std::array<Tex, MaxBloomLevels> bloom;
  Tex volTex, aoTex, aoBlur;
  Tex glow, glowDepth, glowSrcDepth;
  wgpu::RenderPipeline glowDepthPipe;
  Tex maskDepth, maskDepthR, mask;
  wgpu::RenderPipeline maskPipe;
  // 0028 shadow maps
  Tex atlas, mask2, mask3, mask4; // 0187: mask3/4 = lights 8-15
  wgpu::BindGroupLayout cubeBgl;
  wgpu::PipelineLayout cubeLayout;
  wgpu::RenderPipeline cubePipe;
  wgpu::Sampler cmpSampler;
  // 0032 forward ambient occlusion (third mask plane)
  Tex maskAo;
  wgpu::RenderPipeline aoMaskPipe;
  // 0186: raw occlusion before the 4x4 blur (0224: GTAO at AO resolution, RGBA16F)
  Tex maskAoRaw;
  wgpu::RenderPipeline aoBlur4Pipe;
  // 0224: the pre-pass normal target (+ write-masked color placeholder); valid = the pre-pass
  // depth so far was rendered with normals (an incremental mask keeps adding to both)
  Tex maskNormal, maskNormalColor;
  bool maskNormalValid = false;
  Tex maskAoDen; // 0224: denoised AO (AO resolution); the second pass writes back into maskAoRaw
  wgpu::RenderPipeline aoDenoisePipe, aoDenoise2Pipe;
  // 0196 realistic flashlight
  wgpu::RenderPipeline beam, beamBlur;
  Tex beamTex, beamTmp;
  // 0210: incremental masks (fs_dirty): the previous pre-pass depth and the 8x8 tile flags
  Tex maskDepthPrev, dirty;
  wgpu::BindGroupLayout dirtyBgl;
  wgpu::PipelineLayout dirtyLayout;
  wgpu::RenderPipeline dirtyPipe;
  wgpu::Sampler pointSampler;
  // 0342 colour grade, 0343 sun shafts
  Tex gradeSrc, shaft;
  wgpu::RenderPipeline grade, shaftPipe;
  wgpu::TextureFormat gradeFormat = wgpu::TextureFormat::Undefined;
  uint32_t gradeSamples = 0;
};
Gpu g_gpu;
// 0223: the lazily built pipelines and render targets (init_*, the pipelines created in the encode
// functions, ensure_tex) are also built ahead of their first use by prewarm() on a background thread
// during the startup precache hold, so the first scene with enhanced lighting (the foyer) does not
// build them on the render thread. Everything that creates or replaces them takes this lock.
std::recursive_mutex g_initMutex;
// An enhanced pass was encoded: from then on prewarm() leaves the render targets alone (the encode
// functions swap and resize them without the lock).
std::atomic<bool> g_encoded{false};

void ensure_tex(Tex& t, uint32_t w, uint32_t h, wgpu::TextureFormat format, wgpu::TextureUsage usage, const char* label) {
  const std::lock_guard initLock{g_initMutex}; // 0223
  w = std::max(w, 1u);
  h = std::max(h, 1u);
  if (t.texture && t.width == w && t.height == h && t.format == format) {
    return;
  }
  const wgpu::TextureDescriptor desc{
      .label = label,
      .usage = usage,
      .dimension = wgpu::TextureDimension::e2D,
      .size = {w, h, 1},
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  static const bool sLog = std::getenv("AURORA_ENH_TEX_LOG") != nullptr; // debug aid: every (re)creation
  if (sLog) {
    Log.info("texture '{}' {}x{} (was {}x{}) scene {}", label, w, h, t.width, t.height, g_sceneEnds);
  }
  const webgpu::CreateTimer createTimer{label}; // 0223 (AURORA_CREATE_LOG)
  t.texture = webgpu::g_device.CreateTexture(&desc);
  t.view = t.texture.CreateView();
  t.width = w;
  t.height = h;
  t.format = format;
}

wgpu::RenderPipeline make_pipeline(const char* entry, wgpu::TextureFormat format, uint32_t samples,
                                   const wgpu::BlendState* blend, wgpu::ColorWriteMask mask) {
  const wgpu::ColorTargetState target{
      .format = format,
      .blend = blend,
      .writeMask = mask,
  };
  const wgpu::FragmentState fragment{
      .module = g_gpu.module,
      .entryPoint = entry,
      .targetCount = 1,
      .targets = &target,
  };
  const wgpu::RenderPipelineDescriptor desc{
      .label = entry,
      .layout = g_gpu.layout,
      .vertex = {.module = g_gpu.module, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .multisample = {.count = samples},
      .fragment = &fragment,
  };
  const ::aurora::webgpu::CreateTimer createTimer{entry}; // 0223 (AURORA_CREATE_LOG)
  return webgpu::g_device.CreateRenderPipeline(&desc);
}

bool init_gpu() {
  const std::lock_guard initLock{g_initMutex}; // 0223
  if (g_gpu.initialized) {
    return !g_gpu.failed;
  }
  g_gpu.initialized = true;
  // 0196: the beam profile shared with the surface shaders comes first.
  static const std::string code = std::string(BeamProfileWgsl) + std::string(FxShader);
  const wgpu::ShaderSourceWGSL wgsl{{.code = code.c_str()}};
  const wgpu::ShaderModuleDescriptor moduleDesc{.nextInChain = &wgsl, .label = "Enhanced lighting effects"};
  g_gpu.module = webgpu::g_device.CreateShaderModule(&moduleDesc);
  std::array<wgpu::BindGroupLayoutEntry, 6> entries{};
  entries[0] = {.binding = 0,
                .visibility = wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex,
                .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(FxUniform)}};
  entries[1] = {.binding = 1, .visibility = wgpu::ShaderStage::Fragment, .sampler = {.type = wgpu::SamplerBindingType::Filtering}};
  for (uint32_t i = 2; i < 5; ++i) {
    entries[i] = {.binding = i,
                  .visibility = wgpu::ShaderStage::Fragment,
                  .texture = {.sampleType = wgpu::TextureSampleType::Float, .viewDimension = wgpu::TextureViewDimension::e2D}};
  }
  entries[5] = {.binding = 5,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                            .viewDimension = wgpu::TextureViewDimension::e2D}};
  const wgpu::BindGroupLayoutDescriptor bglDesc{.label = "Enhanced FX", .entryCount = entries.size(), .entries = entries.data()};
  g_gpu.bgl = webgpu::g_device.CreateBindGroupLayout(&bglDesc);
  const wgpu::PipelineLayoutDescriptor plDesc{.label = "Enhanced FX", .bindGroupLayoutCount = 1, .bindGroupLayouts = &g_gpu.bgl};
  g_gpu.layout = webgpu::g_device.CreatePipelineLayout(&plDesc);
  const wgpu::SamplerDescriptor sampDesc{
      .label = "Enhanced FX linear",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
  };
  g_gpu.sampler = webgpu::g_device.CreateSampler(&sampDesc);
  ensure_tex(g_gpu.dummy, 1, 1, HdrFormat, wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment,
             "Enhanced dummy");
  ensure_tex(g_gpu.dummyDepth, 1, 1, wgpu::TextureFormat::R32Float,
             wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment, "Enhanced dummy depth");
  constexpr auto all = wgpu::ColorWriteMask::All;
  static const wgpu::BlendState additive{
      .color = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::One, .dstFactor = wgpu::BlendFactor::One},
      .alpha = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::One, .dstFactor = wgpu::BlendFactor::One},
  };
  g_gpu.bright = make_pipeline("fs_bright", HdrFormat, 1, nullptr, all);
  g_gpu.down = make_pipeline("fs_down", HdrFormat, 1, nullptr, all);
  g_gpu.up = make_pipeline("fs_up", HdrFormat, 1, &additive, all);
  g_gpu.vol = make_pipeline("fs_vol", HdrFormat, 1, nullptr, all);
  g_gpu.ao = make_pipeline("fs_ao", HdrFormat, 1, nullptr, all);
  g_gpu.aoblur = make_pipeline("fs_aoblur", HdrFormat, 1, nullptr, all);
  g_gpu.beam = make_pipeline("fs_beam", HdrFormat, 1, nullptr, all);
  g_gpu.beamBlur = make_pipeline("fs_beamblur", HdrFormat, 1, nullptr, all);
  if (!g_gpu.beam || !g_gpu.beamBlur) {
    Log.error("Realistic flashlight pass failed to build");
  }
  if (!g_gpu.bright || !g_gpu.down || !g_gpu.up || !g_gpu.vol || !g_gpu.ao || !g_gpu.aoblur) {
    Log.error("Enhanced lighting effects failed to build; screen-space effects disabled");
    g_gpu.failed = true;
  }
  return !g_gpu.failed;
}

wgpu::BindGroup bind(const Range& uniform, const wgpu::TextureView& a, const wgpu::TextureView& b,
                     const wgpu::TextureView& c, const wgpu::TextureView& depth) {
  const std::array<wgpu::BindGroupEntry, 6> entries{
      wgpu::BindGroupEntry{.binding = 0, .buffer = detail::resources().uniformBuffer, .offset = uniform.offset, .size = sizeof(FxUniform)},
      wgpu::BindGroupEntry{.binding = 1, .sampler = g_gpu.sampler},
      wgpu::BindGroupEntry{.binding = 2, .textureView = a ? a : g_gpu.dummy.view},
      wgpu::BindGroupEntry{.binding = 3, .textureView = b ? b : g_gpu.dummy.view},
      wgpu::BindGroupEntry{.binding = 4, .textureView = c ? c : g_gpu.dummy.view},
      wgpu::BindGroupEntry{.binding = 5, .textureView = depth ? depth : g_gpu.dummyDepth.view},
  };
  const wgpu::BindGroupDescriptor desc{.layout = g_gpu.bgl, .entryCount = entries.size(), .entries = entries.data()};
  return webgpu::g_device.CreateBindGroup(&desc);
}

void run_pass(const wgpu::CommandEncoder& cmd, const char* label, const wgpu::TextureView& target,
              const wgpu::RenderPipeline& pipeline, const wgpu::BindGroup& bg, bool load,
              const wgpu::PassTimestampWrites* timing = nullptr) {
  const wgpu::RenderPassColorAttachment att{
      .view = target,
      .loadOp = load ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
      .storeOp = wgpu::StoreOp::Store,
      .clearValue = {0, 0, 0, 0},
  };
  const wgpu::RenderPassDescriptor desc{
      .label = label, .colorAttachmentCount = 1, .colorAttachments = &att,
      .timestampWrites = timing != nullptr ? timing : webgpu::gpu_prof::pass_writes(label)};
  const auto pass = cmd.BeginRenderPass(&desc);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bg);
  pass.Draw(3);
  pass.End();
}

// Everything the render worker needs for one scene end, captured on the FIFO thread.
// Bloom level 0 (and glow buffer): half the target, at most 720 lines.
std::pair<uint32_t, uint32_t> bloom_base_size(uint32_t width, uint32_t height) noexcept {
  uint32_t w = std::max(width / 2, 1u), h = std::max(height / 2, 1u);
  if (h > 720) {
    w = std::max(w * 720 / h, 1u);
    h = 720;
  }
  return {w, h};
}

// The extra-light shadow / AO mask's size for a render target size (0223: shared with prewarm()).
std::pair<uint32_t, uint32_t> mask_size(uint32_t width, uint32_t height, const AuroraEnhancedConfig& c) noexcept {
  uint32_t w, h;
  if (c.extraShadowMode == 1 && c.extraShadowQuality >= 2) {
    w = std::max(width, 1u);
    h = std::max(height, 1u);
    if (h > 1440) {
      w = std::max(w * 1440 / h, 1u);
      h = 1440;
    }
  } else {
    std::tie(w, h) = bloom_base_size(width, height);
  }
  // 0186: planes for the light shadows (0187: four, 16 lights), ambient occlusion, and the
  // pre-pass depth that the receivers compare their own depth with (MaskDepthPlane).
  while (uint64_t(AURORA_ALIGN(w * 4, 256)) * h * MaskPlanes > MaskBufferSize) {
    w = std::max(w * 7 / 8, 1u);
    h = std::max(h * 7 / 8, 1u);
  }
  return {w, h};
}

// The realistic flashlight's target size (0223: shared with prewarm()). Beam target: 1/2
// resolution up to 540 / 720 lines (medium / high), full resolution up to 1080 lines (ultra).
std::pair<uint32_t, uint32_t> beam_size(uint32_t width, uint32_t height, uint32_t q) noexcept {
  const uint32_t div = 2;
  uint32_t bw = std::max(width / div, 1u), bh = std::max(height / div, 1u);
  const uint32_t cap = q == 1 ? 540u : q == 2 ? 720u : 1080u;
  if (bh > cap) {
    bw = std::max(bw * cap / bh, 1u);
    bh = cap;
  }
  return {bw, bh};
}

struct SceneJob {
  uint32_t mask = 0; // 1 bloom, 2 volumetric, 4 ambient occlusion, 32 realistic flashlight (0196)
  uint32_t bloomLevels = 0;
  bool glowValid = false; // the glow buffer was rendered this frame
  uint32_t volDiv = 2;
  Range base{};                                  // FxUniform for full-size passes
  std::array<Range, MaxBloomLevels * 2 + 2> passes{}; // per bloom pass + ao blur
  // 0196 realistic flashlight: shadow map, target size, blur passes
  TextureHandle beamShadow;
  uint32_t beamW = 0, beamH = 0;
  Range beamBlur[2]{};
  // 0225: the main view's viewport (target pixels) when it does not cover the target (e.g. the
  // game's unused bordered view): the composite stays inside it.
  bool clipped = false;
  uint32_t clip[4]{};
};

// Glow buffer (bloom level 0 size). Emissive draws are re-rendered right after the EFB pass
// they were drawn in (before a later EFB copy can overwrite textures they sample), depth-tested
// against that pass's depth without writing it. The first flush of a frame clears the buffer;
// later ones add to it.
struct GlowFlush {
  std::vector<GlowDraw> draws;
  Range uniform{}; // FxUniform: glowdepth
  bool clear = true;
};
bool g_glowValid = false; // FIFO thread: the glow buffer holds this frame's draws (any flush)

// 0223: the glow buffer's depth pipeline (prewarm() or the first glow flush).
void ensure_glow_depth_pipe() {
  const std::lock_guard initLock{g_initMutex};
  if (g_gpu.glowDepthPipe) {
    return;
  }
  const auto depthFormat = webgpu::g_graphicsConfig.depthFormat;
  const wgpu::DepthStencilState ds{
      .format = depthFormat,
      .depthWriteEnabled = true,
      .depthCompare = wgpu::CompareFunction::Always,
  };
  const wgpu::FragmentState fragment{.module = g_gpu.module, .entryPoint = "fs_glowdepth", .targetCount = 0};
  const wgpu::RenderPipelineDescriptor desc{
      .label = "Enhanced glow depth",
      .layout = g_gpu.layout,
      .vertex = {.module = g_gpu.module, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .depthStencil = &ds,
      .fragment = &fragment,
  };
  const ::aurora::webgpu::CreateTimer createTimer{"pipeline Enhanced glow depth"}; // 0223 (AURORA_CREATE_LOG)
  g_gpu.glowDepthPipe = webgpu::g_device.CreateRenderPipeline(&desc);
}

void encode_glow(const wgpu::CommandEncoder& cmd, const ScenePassTargets& t, const GlowFlush& job) {
  if (!init_gpu()) {
    return;
  }
  g_encoded.store(true); // 0223
  const auto rt = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
  const auto [gw, gh] = bloom_base_size(t.width, t.height);
  ensure_tex(g_gpu.glow, gw, gh, GlowFormat, rt, "Enhanced glow");
  ensure_tex(g_gpu.glowSrcDepth, t.width, t.height, wgpu::TextureFormat::R32Float, rt, "Enhanced glow scene depth");
  tex_copy_conv::snapshot_depth(cmd, t.depthSource, t.msaa, g_gpu.glowSrcDepth.view);
  const auto depthFormat = webgpu::g_graphicsConfig.depthFormat;
  ensure_tex(g_gpu.glowDepth, gw, gh, depthFormat, wgpu::TextureUsage::RenderAttachment, "Enhanced glow depth");
  ensure_glow_depth_pipe();
  {
    const wgpu::RenderPassDepthStencilAttachment da{
        .view = g_gpu.glowDepth.view,
        .depthLoadOp = wgpu::LoadOp::Clear,
        .depthStoreOp = wgpu::StoreOp::Store,
        .depthClearValue = 0.f,
    };
    const wgpu::RenderPassDescriptor desc{.label = "Enhanced glow depth", .depthStencilAttachment = &da,
                                         .timestampWrites = webgpu::gpu_prof::pass_writes("Enhanced glow depth")};
    const auto pass = cmd.BeginRenderPass(&desc);
    pass.SetPipeline(g_gpu.glowDepthPipe);
    pass.SetBindGroup(0, bind(job.uniform, {}, {}, {}, g_gpu.glowSrcDepth.view));
    pass.Draw(3);
    pass.End();
  }
  const wgpu::RenderPassColorAttachment att{.view = g_gpu.glow.view,
                                            .loadOp = job.clear ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load,
                                            .storeOp = wgpu::StoreOp::Store,
                                            .clearValue = {0, 0, 0, 0}};
  const wgpu::RenderPassDepthStencilAttachment da{
      .view = g_gpu.glowDepth.view,
      .depthLoadOp = wgpu::LoadOp::Load,
      .depthStoreOp = wgpu::StoreOp::Discard,
  };
  const wgpu::RenderPassDescriptor desc{
      .label = "Enhanced glow", .colorAttachmentCount = 1, .colorAttachments = &att, .depthStencilAttachment = &da,
      .timestampWrites = webgpu::gpu_prof::pass_writes("Enhanced glow")};
  const auto pass = cmd.BeginRenderPass(&desc);
  reset_bound_pipeline();
  pass.SetBindGroup(0, detail::resources().staticBindGroup);
  pass.SetBindGroup(2, gx::g_emptyTextureBindGroup);
  const float sx = float(gw) / float(std::max(t.width, 1u));
  const float sy = float(gh) / float(std::max(t.height, 1u));
  for (const auto& g : job.draws) {
    const auto& vp = g.viewport;
    const float minDepth = gx::UseReversedZ ? 1.f - vp.zfar : vp.znear;
    const float maxDepth = gx::UseReversedZ ? 1.f - vp.znear : vp.zfar;
    pass.SetViewport(vp.left * sx, vp.top * sy, std::max(vp.width * sx, 1.f), std::max(vp.height * sy, 1.f), minDepth,
                     maxDepth);
    const auto& sc = g.scissor;
    const uint32_t x0 = std::min(uint32_t(std::max(std::floor(float(sc.x) * sx), 0.f)), gw);
    const uint32_t y0 = std::min(uint32_t(std::max(std::floor(float(sc.y) * sy), 0.f)), gh);
    const uint32_t x1 = std::min(uint32_t(std::max(std::ceil(float(sc.x + sc.width) * sx), 0.f)), gw);
    const uint32_t y1 = std::min(uint32_t(std::max(std::ceil(float(sc.y + sc.height) * sy), 0.f)), gh);
    pass.SetScissorRect(x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0);
    gx::render(g.data, pass);
  }
  pass.End();
  reset_bound_pipeline();
}

struct MaskJob {
  std::vector<GlowDraw> draws;
  Range uniform{};
  uint32_t maskOffset = 0, maskW = 0, maskH = 0, pitchBytes = 0;
  float scaleBack = 2.f; // EFB target pixels per mask pixel
  // 0028 shadow maps
  bool cube = false;
  uint32_t lights = 0, atlasW = 0, atlasH = 0;
  Range records{}; // storage: lights x 6 faces x 24 floats
  // Draws [0, firstNew) are already in the depth pre-pass and the atlas from this frame's
  // previous mask (same lights): only the new ones are added (a second EFB pass in a frame
  // would otherwise render every caster twice).
  size_t firstNew = 0;
  // 0032: point = light shadows in this mask (planes 1-2), ao = ambient occlusion (plane 3).
  bool point = true, ao = false;
  // 0210: firstNew > 0 with shadow maps or AO: recompute only around changed pre-pass depth.
  bool incremental = false;
  // 0343: sun shafts from the directional light's cascades (fs_shafts, mask resolution).
  bool shafts = false;
};
// 0343: a sun-shaft texture was written for the frame being recorded (FIFO thread; set by
// flush_shadows, read by scene_end).
bool g_shaftsQueued = false;

// GPU timing of the light-shadow passes (AURORA_ENH_GPU_TIMING=1, needs TimestampQuery): camera
// depth pre-pass, shadow-map atlas, mask pass; averaged and logged every 240 masks.
constexpr uint32_t TimingQueries = 6;
struct TimingSlot {
  wgpu::Buffer resolve, readback;
  std::atomic<int> state{0}; // 0 free, 1 written, 2 mapping, 3 mapped
};
std::array<TimingSlot, 6> g_tslots;
wgpu::QuerySet g_tq;
int g_tcur = -1;
std::array<wgpu::PassTimestampWrites, 3> g_tw{};
double g_tsum[3] = {}, g_tcnt = 0;
bool timing_on() {
  static const bool on = std::getenv("AURORA_ENH_GPU_TIMING") != nullptr &&
                         webgpu::g_device.HasFeature(wgpu::FeatureName::TimestampQuery);
  if (on && !g_tq) {
    const wgpu::QuerySetDescriptor qd{.label = "Enhanced shadow timing", .type = wgpu::QueryType::Timestamp, .count = TimingQueries};
    g_tq = webgpu::g_device.CreateQuerySet(&qd);
    for (auto& t : g_tslots) {
      const wgpu::BufferDescriptor rd{.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc, .size = TimingQueries * 8};
      t.resolve = webgpu::g_device.CreateBuffer(&rd);
      const wgpu::BufferDescriptor bd{.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst, .size = TimingQueries * 8};
      t.readback = webgpu::g_device.CreateBuffer(&bd);
    }
    for (uint32_t i = 0; i < 3; ++i) {
      g_tw[i] = {.querySet = g_tq, .beginningOfPassWriteIndex = i * 2, .endOfPassWriteIndex = i * 2 + 1};
    }
  }
  return on;
}
// Timestamp writes for pass k of the current mask (0 pre-pass, 1 atlas, 2 mask) or null.
const wgpu::PassTimestampWrites* timing_writes(uint32_t k, const char* label) {
  return g_tcur >= 0 ? &g_tw[k] : webgpu::gpu_prof::pass_writes(label);
}
void timing_begin() {
  g_tcur = -1;
  if (!timing_on()) {
    return;
  }
  for (int i = 0; i < int(g_tslots.size()); ++i) {
    if (g_tslots[i].state.load() == 0) {
      g_tcur = i;
      break;
    }
  }
}
void timing_end(const wgpu::CommandEncoder& cmd) {
  if (g_tcur < 0) {
    return;
  }
  auto& t = g_tslots[g_tcur];
  cmd.ResolveQuerySet(g_tq, 0, TimingQueries, t.resolve, 0);
  cmd.CopyBufferToBuffer(t.resolve, 0, t.readback, 0, TimingQueries * 8);
  t.state.store(1);
  g_tcur = -1;
}

bool init_cube() {
  const std::lock_guard initLock{g_initMutex}; // 0223
  if (g_gpu.cubePipe) {
    return true;
  }
  std::array<wgpu::BindGroupLayoutEntry, 6> e{};
  e[0] = {.binding = 0,
          .visibility = wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex,
          .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(FxUniform)}};
  // 0210: tile flags of an incremental mask (mask_needed)
  e[5] = {.binding = 3,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture = {.sampleType = wgpu::TextureSampleType::Float, .viewDimension = wgpu::TextureViewDimension::e2D}};
  e[1] = {.binding = 5,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat, .viewDimension = wgpu::TextureViewDimension::e2D}};
  e[2] = {.binding = 6,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture = {.sampleType = wgpu::TextureSampleType::Depth, .viewDimension = wgpu::TextureViewDimension::e2D}};
  e[3] = {.binding = 7, .visibility = wgpu::ShaderStage::Fragment, .sampler = {.type = wgpu::SamplerBindingType::Comparison}};
  e[4] = {.binding = 8, .visibility = wgpu::ShaderStage::Fragment, .buffer = {.type = wgpu::BufferBindingType::ReadOnlyStorage}};
  const wgpu::BindGroupLayoutDescriptor bglDesc{.label = "Enhanced shadow maps", .entryCount = e.size(), .entries = e.data()};
  g_gpu.cubeBgl = webgpu::g_device.CreateBindGroupLayout(&bglDesc);
  const wgpu::PipelineLayoutDescriptor plDesc{.label = "Enhanced shadow maps", .bindGroupLayoutCount = 1, .bindGroupLayouts = &g_gpu.cubeBgl};
  g_gpu.cubeLayout = webgpu::g_device.CreatePipelineLayout(&plDesc);
  const wgpu::SamplerDescriptor cmpDesc{
      .label = "Enhanced shadow map compare",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
      .compare = gx::UseReversedZ ? wgpu::CompareFunction::GreaterEqual : wgpu::CompareFunction::LessEqual,
  };
  g_gpu.cmpSampler = webgpu::g_device.CreateSampler(&cmpDesc);
  const std::array<wgpu::ColorTargetState, 4> targets{
      wgpu::ColorTargetState{.format = wgpu::TextureFormat::RGBA8Unorm, .writeMask = wgpu::ColorWriteMask::All},
      wgpu::ColorTargetState{.format = wgpu::TextureFormat::RGBA8Unorm, .writeMask = wgpu::ColorWriteMask::All},
      wgpu::ColorTargetState{.format = wgpu::TextureFormat::RGBA8Unorm, .writeMask = wgpu::ColorWriteMask::All},
      wgpu::ColorTargetState{.format = wgpu::TextureFormat::RGBA8Unorm, .writeMask = wgpu::ColorWriteMask::All},
  };
  const wgpu::FragmentState fragment{.module = g_gpu.module, .entryPoint = "fs_xlcube", .targetCount = targets.size(), .targets = targets.data()};
  const wgpu::RenderPipelineDescriptor desc{
      .label = "Enhanced shadow-map mask",
      .layout = g_gpu.cubeLayout,
      .vertex = {.module = g_gpu.module, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .fragment = &fragment,
  };
  const ::aurora::webgpu::CreateTimer createTimer{"pipeline Enhanced shadow-map mask"}; // 0223 (AURORA_CREATE_LOG)
  g_gpu.cubePipe = webgpu::g_device.CreateRenderPipeline(&desc);
  return bool(g_gpu.cubePipe);
}

// Caster draws into the atlas: like gx::render. 0221: a caster whose pipeline is still compiling
// waits for it like every other draw (patch 0013 policy); it was skipped, which drops its shadow
// for that frame (a light whose casters compile for many frames under heavy CPU load flickers
// between shadowed and unshadowed). Skips that remain (creation failed, LM_PIPELINE_WAIT=0) are
// logged by get_pipeline.
void render_casters(const wgpu::RenderPassEncoder& pass, const MaskJob& job, size_t from, uint32_t& drawn,
                    uint32_t& skipped) {
  const auto& resources = detail::resources();
  const uint32_t recordWord = job.records.offset / 4;
  for (size_t i = from; i < job.draws.size(); ++i) {
    const auto& g = job.draws[i];
    if (!g.lightPipe) {
      continue;
    }
    CompiledPipeline pipeline;
    if (!get_pipeline(g.lightPipe, pipeline, "shadow caster")) {
      ++skipped;
      continue;
    }
    auto imm = g.data.immediateData;
    imm._pad = recordWord;
    pass.SetImmediates(0, &imm, sizeof(imm));
    const std::array offsets{g.data.uniformRange.offset};
    pass.SetBindGroup(1, resources.uniformBindGroup, offsets.size(), offsets.data());
    if (g.data.bindGroups.textureBindGroup) {
      pass.SetBindGroup(2, find_bind_group(g.data.bindGroups.textureBindGroup));
    } else {
      pass.SetBindGroup(2, gx::g_emptyTextureBindGroup);
    }
    pass.SetIndexBuffer(resources.indexBuffer, wgpu::IndexFormat::Uint16, g.data.idxRange.offset, g.data.idxRange.size);
    bind_pipeline(pipeline.main, pass);
    const uint32_t instances = job.lights * 6;
    if (g.data.indexCount == 0) {
      pass.Draw(g.data.vtxCount, instances);
    } else {
      pass.DrawIndexed(g.data.indexCount, instances);
    }
    ++drawn;
  }
}

// 0210: tile-flag pass of incremental masks (fs_dirty: two unfilterable depth snapshots).
bool init_dirty() {
  const std::lock_guard initLock{g_initMutex}; // 0223
  if (g_gpu.dirtyPipe) {
    return true;
  }
  std::array<wgpu::BindGroupLayoutEntry, 3> e{};
  for (uint32_t i = 0; i < 2; ++i) {
    e[i] = {.binding = i == 0 ? 5u : 9u,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat, .viewDimension = wgpu::TextureViewDimension::e2D}};
  }
  e[2] = {.binding = 10, .visibility = wgpu::ShaderStage::Fragment, .sampler = {.type = wgpu::SamplerBindingType::NonFiltering}};
  const wgpu::SamplerDescriptor sd{.label = "Enhanced mask tile flags",
                                   .addressModeU = wgpu::AddressMode::ClampToEdge,
                                   .addressModeV = wgpu::AddressMode::ClampToEdge};
  g_gpu.pointSampler = webgpu::g_device.CreateSampler(&sd);
  const wgpu::BindGroupLayoutDescriptor bglDesc{.label = "Enhanced mask tile flags", .entryCount = e.size(), .entries = e.data()};
  g_gpu.dirtyBgl = webgpu::g_device.CreateBindGroupLayout(&bglDesc);
  const wgpu::PipelineLayoutDescriptor plDesc{.label = "Enhanced mask tile flags", .bindGroupLayoutCount = 1, .bindGroupLayouts = &g_gpu.dirtyBgl};
  g_gpu.dirtyLayout = webgpu::g_device.CreatePipelineLayout(&plDesc);
  const wgpu::ColorTargetState target{.format = wgpu::TextureFormat::R8Unorm, .writeMask = wgpu::ColorWriteMask::All};
  const wgpu::FragmentState fragment{.module = g_gpu.module, .entryPoint = "fs_dirty", .targetCount = 1, .targets = &target};
  const wgpu::RenderPipelineDescriptor desc{
      .label = "Enhanced mask tile flags",
      .layout = g_gpu.dirtyLayout,
      .vertex = {.module = g_gpu.module, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .fragment = &fragment,
  };
  const ::aurora::webgpu::CreateTimer createTimer{"pipeline Enhanced mask tile flags"}; // 0223 (AURORA_CREATE_LOG)
  g_gpu.dirtyPipe = webgpu::g_device.CreateRenderPipeline(&desc);
  if (!g_gpu.dirtyPipe) {
    Log.error("Enhanced mask tile-flag pass failed to build");
  }
  return bool(g_gpu.dirtyPipe);
}

// Extra-light shadow mask, before the EFB pass whose world draws read it: depth pre-pass of the
// opaque world draws at mask resolution, ray march, copy into the storage buffer.
// 0223: the mask pass pipeline, with `ao` also the ambient occlusion mask + blur.
void ensure_mask_pipes(bool ao) {
  // 0224: horizon-based AO at AO resolution (RGBA16F: visibility, view z, normal) + two
  // denoise passes before the upsample (fs_aoblur4)
  struct Pipe {
    wgpu::RenderPipeline Gpu::* slot;
    const char* entry;
    wgpu::TextureFormat format;
    bool needed;
  };
  const Pipe pipes[] = {
      {&Gpu::maskPipe, "fs_xlshadow", wgpu::TextureFormat::RGBA8Unorm, true},
      {&Gpu::aoMaskPipe, "fs_aomask", wgpu::TextureFormat::RGBA16Float, ao},
      {&Gpu::aoDenoisePipe, "fs_aodenoise", wgpu::TextureFormat::RGBA16Float, ao},
      {&Gpu::aoDenoise2Pipe, "fs_aodenoise2", wgpu::TextureFormat::RGBA16Float, ao},
      {&Gpu::aoBlur4Pipe, "fs_aoblur4", wgpu::TextureFormat::RGBA8Unorm, ao},
  };
  // 0550: created outside g_initMutex (a pass prewarm of the standby set compiling these cold held
  // it for the whole compile, and the render thread's init_gpu() waited every frame meanwhile);
  // two threads may build the same one, the first stored wins.
  for (const auto& p : pipes) {
    if (!p.needed) {
      continue;
    }
    {
      const std::lock_guard initLock{g_initMutex};
      if (g_gpu.*p.slot) {
        continue;
      }
    }
    wgpu::RenderPipeline made = make_pipeline(p.entry, p.format, 1, nullptr, wgpu::ColorWriteMask::All);
    const std::lock_guard initLock{g_initMutex};
    if (!(g_gpu.*p.slot)) {
      g_gpu.*p.slot = std::move(made);
    }
  }
}

void encode_mask(const wgpu::CommandEncoder& cmd, const MaskJob& job) {
  if (!init_gpu()) {
    return;
  }
  g_encoded.store(true); // 0223
  timing_begin();
  const uint32_t w = job.maskW, h = job.maskH;
  const auto depthFormat = webgpu::g_graphicsConfig.depthFormat;
  const auto* prevDepth = g_gpu.maskDepth.texture.Get();
  ensure_tex(g_gpu.maskDepth, w, h, depthFormat, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
             "Enhanced light-shadow depth");
  size_t fromDepth = g_gpu.maskDepth.texture.Get() == prevDepth ? job.firstNew : 0;
  // 0224: ambient occlusion reads the pre-pass normals; every draw of the job must have been given
  // the normal-target pipeline (AO was on when it was recorded), else none is drawn with normals.
  bool normals = job.ao;
  for (size_t i = 0; i < job.draws.size() && normals; ++i) {
    normals = job.draws[i].nrm || job.draws[i].casterOnly; // 0470: caster-only draws are not in the pre-pass
  }
  if (normals) {
    const auto* prevNormal = g_gpu.maskNormal.texture.Get();
    ensure_tex(g_gpu.maskNormal, w, h, wgpu::TextureFormat::RGBA8Unorm,
               wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding, "Enhanced pre-pass normals");
    ensure_tex(g_gpu.maskNormalColor, w, h, wgpu::TextureFormat::RGBA8Unorm, wgpu::TextureUsage::RenderAttachment,
               "Enhanced pre-pass color placeholder");
    if (g_gpu.maskNormal.texture.Get() != prevNormal || !g_gpu.maskNormalValid) {
      fromDepth = 0; // the normals so far are missing: render the whole pre-pass again
    }
  }
  ensure_tex(g_gpu.maskDepthR, w, h, wgpu::TextureFormat::R32Float,
             wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc,
             "Enhanced light-shadow depth R");
  ensure_tex(g_gpu.mask, w, h, wgpu::TextureFormat::RGBA8Unorm,
             wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::TextureBinding,
             "Enhanced light-shadow mask");
  ensure_mask_pipes(false);
  {
    const wgpu::RenderPassDepthStencilAttachment da{
        .view = g_gpu.maskDepth.view,
        .depthLoadOp = fromDepth > 0 ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
        .depthStoreOp = wgpu::StoreOp::Store,
        .depthClearValue = gx::UseReversedZ ? 0.f : 1.f,
    };
    const std::array<wgpu::RenderPassColorAttachment, 2> ca{
        wgpu::RenderPassColorAttachment{.view = g_gpu.maskNormalColor.view,
                                        .loadOp = wgpu::LoadOp::Clear,
                                        .storeOp = wgpu::StoreOp::Discard,
                                        .clearValue = {0, 0, 0, 0}},
        wgpu::RenderPassColorAttachment{.view = g_gpu.maskNormal.view,
                                        .loadOp = fromDepth > 0 ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
                                        .storeOp = wgpu::StoreOp::Store,
                                        .clearValue = {0, 0, 0, 0}},
    };
    const wgpu::RenderPassDescriptor desc{.label = "Enhanced light-shadow depth",
                                          .colorAttachmentCount = normals ? ca.size() : 0,
                                          .colorAttachments = normals ? ca.data() : nullptr,
                                          .depthStencilAttachment = &da,
                                          .timestampWrites = timing_writes(0, "Enhanced light-shadow depth")};
    const auto pass = cmd.BeginRenderPass(&desc);
    reset_bound_pipeline();
    pass.SetBindGroup(0, detail::resources().staticBindGroup);
    pass.SetBindGroup(2, gx::g_emptyTextureBindGroup);
    // Viewports are in EFB target pixels; the mask is bloom-level-0 sized.
    const float sx = 1.f / std::max(job.scaleBack, 1e-3f), sy = sx;
    for (size_t i = fromDepth; i < job.draws.size(); ++i) {
      const auto& g = job.draws[i];
      if (g.nrm != normals || g.casterOnly) {
        continue; // 0224: built for the other pre-pass layout (AO switched within the frame); 0470: atlas only
      }
      const auto& vp = g.viewport;
      const float minDepth = gx::UseReversedZ ? 1.f - vp.zfar : vp.znear;
      const float maxDepth = gx::UseReversedZ ? 1.f - vp.znear : vp.zfar;
      pass.SetViewport(vp.left * sx, vp.top * sy, std::max(vp.width * sx, 1.f), std::max(vp.height * sy, 1.f), minDepth,
                       maxDepth);
      const auto& sc = g.scissor;
      const uint32_t x0 = std::min(uint32_t(std::max(std::floor(float(sc.x) * sx), 0.f)), w);
      const uint32_t y0 = std::min(uint32_t(std::max(std::floor(float(sc.y) * sy), 0.f)), h);
      const uint32_t x1 = std::min(uint32_t(std::max(std::ceil(float(sc.x + sc.width) * sx), 0.f)), w);
      const uint32_t y1 = std::min(uint32_t(std::max(std::ceil(float(sc.y + sc.height) * sy), 0.f)), h);
      pass.SetScissorRect(x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0);
      gx::render(g.data, pass);
    }
    pass.End();
    reset_bound_pipeline();
    g_gpu.maskNormalValid = normals;
  }
  // 0210: an incremental mask (this frame's previous mask, same lights/layout, only new draws
  // added to the pre-pass) recomputes its planes only around the texels whose pre-pass depth
  // changed (mask_needed); the previous depth snapshot is kept for the comparison.
  const bool incremental = job.incremental && init_dirty();
  bool incrementalValid = false;
  if (incremental) {
    std::swap(g_gpu.maskDepthR, g_gpu.maskDepthPrev);
    incrementalValid = fromDepth > 0 && g_gpu.maskDepthPrev.texture && g_gpu.maskDepthPrev.width == w &&
                       g_gpu.maskDepthPrev.height == h;
    ensure_tex(g_gpu.maskDepthR, w, h, wgpu::TextureFormat::R32Float,
               wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc,
               "Enhanced light-shadow depth R");
  }
  tex_copy_conv::snapshot_depth(cmd, g_gpu.maskDepth.view, 1, g_gpu.maskDepthR.view);
  if (incremental) {
    ensure_tex(g_gpu.dirty, (w + 7) / 8, (h + 7) / 8, wgpu::TextureFormat::R8Unorm,
               wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding, "Enhanced mask tile flags");
    const wgpu::RenderPassColorAttachment att{.view = g_gpu.dirty.view,
                                              .loadOp = wgpu::LoadOp::Clear,
                                              .storeOp = wgpu::StoreOp::Store,
                                              .clearValue = {1, 1, 1, 1}};
    const wgpu::RenderPassDescriptor desc{.label = "Enhanced mask tile flags", .colorAttachmentCount = 1, .colorAttachments = &att,
                                          .timestampWrites = webgpu::gpu_prof::pass_writes("Enhanced mask tile flags")};
    const auto pass = cmd.BeginRenderPass(&desc);
    if (incrementalValid) {
      // Otherwise (the textures were just created) every tile stays flagged: a full mask.
      const std::array<wgpu::BindGroupEntry, 3> entries{
          wgpu::BindGroupEntry{.binding = 5, .textureView = g_gpu.maskDepthR.view},
          wgpu::BindGroupEntry{.binding = 9, .textureView = g_gpu.maskDepthPrev.view},
          wgpu::BindGroupEntry{.binding = 10, .sampler = g_gpu.pointSampler},
      };
      const wgpu::BindGroupDescriptor bgDesc{.layout = g_gpu.dirtyBgl, .entryCount = entries.size(), .entries = entries.data()};
      pass.SetPipeline(g_gpu.dirtyPipe);
      pass.SetBindGroup(0, webgpu::g_device.CreateBindGroup(&bgDesc));
      pass.Draw(3);
    }
    pass.End();
  }
  const wgpu::TextureView dirtyView = incremental ? g_gpu.dirty.view : wgpu::TextureView{};
  const auto maskLoad = incremental ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear;
  const wgpu::Extent3D extent{w, h, 1};
  {
    // 0186: the pre-pass depth goes with the mask: a receiver only takes the mask texels that
    // belong to its own surface (depth match), so nothing in front of a shadowed or occluded
    // surface, and no silhouette pixel, picks up that surface's shadow or occlusion.
    const wgpu::TexelCopyTextureInfo srcD{.texture = g_gpu.maskDepthR.texture};
    const wgpu::TexelCopyBufferInfo dstD{
        .layout = {.offset = job.maskOffset + uint64_t(job.pitchBytes) * h * MaskDepthPlane,
                   .bytesPerRow = job.pitchBytes,
                   .rowsPerImage = h},
        .buffer = detail::resources().maskBuffer,
    };
    const webgpu::gpu_prof::Zone zone{cmd, "Mask copy to buffer"};
    cmd.CopyTextureToBuffer(&srcD, &dstD, &extent);
  }
  if (job.ao) {
    // 0032: ambient occlusion into the third plane. 0224: horizon-based (fs_aomask) at AO
    // resolution (half the mask's above 900 lines), then the depth/normal-aware filter and upsample
    // to the mask (fs_aoblur4).
    const uint32_t aoScale = h > 900 ? 2 : 1;
    const uint32_t aw = (w + aoScale - 1) / aoScale, ah = (h + aoScale - 1) / aoScale;
    ensure_tex(g_gpu.maskAo, w, h, wgpu::TextureFormat::RGBA8Unorm,
               wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::TextureBinding,
               "Enhanced ambient occlusion mask");
    const auto* prevRaw = g_gpu.maskAoRaw.texture.Get();
    ensure_tex(g_gpu.maskAoRaw, aw, ah, wgpu::TextureFormat::RGBA16Float,
               wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding, "Enhanced ambient occlusion raw");
    ensure_mask_pipes(true);
    ensure_tex(g_gpu.maskAoDen, aw, ah, wgpu::TextureFormat::RGBA16Float,
               wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding, "Enhanced ambient occlusion denoised");
    const wgpu::TextureView normalView = normals ? g_gpu.maskNormal.view : wgpu::TextureView{};
    {
      // (texels outside the needed tiles of an incremental mask are never read by the filter)
      const bool keep = incremental && g_gpu.maskAoRaw.texture.Get() == prevRaw;
      const wgpu::RenderPassColorAttachment att{.view = g_gpu.maskAoRaw.view,
                                                .loadOp = keep ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
                                                .storeOp = wgpu::StoreOp::Store,
                                                .clearValue = {1, 0, 0, 0}};
      const wgpu::RenderPassDescriptor desc{.label = "Ambient occlusion mask", .colorAttachmentCount = 1, .colorAttachments = &att,
                                            .timestampWrites = webgpu::gpu_prof::pass_writes("Ambient occlusion mask")};
      const auto pass = cmd.BeginRenderPass(&desc);
      pass.SetPipeline(g_gpu.aoMaskPipe);
      pass.SetBindGroup(0, bind(job.uniform, normalView, dirtyView, {}, g_gpu.maskDepthR.view));
      pass.Draw(3);
      pass.End();
    }
    {
      const wgpu::RenderPassColorAttachment att{.view = g_gpu.maskAoDen.view,
                                                .loadOp = incremental ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
                                                .storeOp = wgpu::StoreOp::Store,
                                                .clearValue = {1, 0, 0, 0}};
      const wgpu::RenderPassDescriptor desc{.label = "Ambient occlusion denoise", .colorAttachmentCount = 1, .colorAttachments = &att,
                                            .timestampWrites = webgpu::gpu_prof::pass_writes("Ambient occlusion denoise")};
      const auto pass = cmd.BeginRenderPass(&desc);
      pass.SetPipeline(g_gpu.aoDenoisePipe);
      pass.SetBindGroup(0, bind(job.uniform, g_gpu.maskAoRaw.view, dirtyView, {}, g_gpu.maskDepthR.view));
      pass.Draw(3);
      pass.End();
    }
    {
      // second denoise pass: maskAoDen -> maskAoRaw (the raw AO is not needed any more)
      const wgpu::RenderPassColorAttachment att{.view = g_gpu.maskAoRaw.view,
                                                .loadOp = incremental ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
                                                .storeOp = wgpu::StoreOp::Store,
                                                .clearValue = {1, 0, 0, 0}};
      const wgpu::RenderPassDescriptor desc{.label = "Ambient occlusion denoise", .colorAttachmentCount = 1, .colorAttachments = &att,
                                            .timestampWrites = webgpu::gpu_prof::pass_writes("Ambient occlusion denoise")};
      const auto pass = cmd.BeginRenderPass(&desc);
      pass.SetPipeline(g_gpu.aoDenoise2Pipe);
      pass.SetBindGroup(0, bind(job.uniform, g_gpu.maskAoDen.view, dirtyView, {}, g_gpu.maskDepthR.view));
      pass.Draw(3);
      pass.End();
    }
    const wgpu::RenderPassColorAttachment att{
        .view = g_gpu.maskAo.view, .loadOp = maskLoad, .storeOp = wgpu::StoreOp::Store, .clearValue = {0, 0, 0, 0}};
    const wgpu::RenderPassDescriptor desc{.label = "Ambient occlusion blur", .colorAttachmentCount = 1, .colorAttachments = &att,
                                          .timestampWrites = webgpu::gpu_prof::pass_writes("Ambient occlusion blur")};
    const auto pass = cmd.BeginRenderPass(&desc);
    pass.SetPipeline(g_gpu.aoBlur4Pipe);
    pass.SetBindGroup(0, bind(job.uniform, g_gpu.maskAoRaw.view, dirtyView, normalView, g_gpu.maskDepthR.view));
    pass.Draw(3);
    pass.End();
    const wgpu::TexelCopyTextureInfo src3{.texture = g_gpu.maskAo.texture};
    const wgpu::TexelCopyBufferInfo dst3{
        .layout = {.offset = job.maskOffset + uint64_t(job.pitchBytes) * h * MaskAoPlane, .bytesPerRow = job.pitchBytes, .rowsPerImage = h},
        .buffer = detail::resources().maskBuffer,
    };
    {
      const webgpu::gpu_prof::Zone zone{cmd, "Mask copy to buffer"};
      cmd.CopyTextureToBuffer(&src3, &dst3, &extent);
    }
  }
  if (!job.point) {
    timing_end(cmd);
    return;
  }
  if (job.cube && init_cube()) {
    // Shadow maps: all casters from every shadowed light into the atlas, then the lookup.
    const auto* prevAtlas = g_gpu.atlas.texture.Get();
    ensure_tex(g_gpu.atlas, job.atlasW, job.atlasH, depthFormat,
               wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding, "Enhanced shadow-map atlas");
    const size_t fromAtlas = g_gpu.atlas.texture.Get() == prevAtlas ? job.firstNew : 0;
    ensure_tex(g_gpu.mask2, w, h, wgpu::TextureFormat::RGBA8Unorm,
               wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::TextureBinding,
               "Enhanced light-shadow mask 2");
    ensure_tex(g_gpu.mask3, w, h, wgpu::TextureFormat::RGBA8Unorm,
               wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::TextureBinding,
               "Enhanced light-shadow mask 3");
    ensure_tex(g_gpu.mask4, w, h, wgpu::TextureFormat::RGBA8Unorm,
               wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::TextureBinding,
               "Enhanced light-shadow mask 4");
    uint32_t drawn = 0, skipped = 0;
    {
      const wgpu::RenderPassDepthStencilAttachment da{
          .view = g_gpu.atlas.view,
          .depthLoadOp = fromAtlas > 0 ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
          .depthStoreOp = wgpu::StoreOp::Store,
          .depthClearValue = gx::UseReversedZ ? 0.f : 1.f,
      };
      const wgpu::RenderPassDescriptor desc{
          .label = "Enhanced shadow-map atlas", .depthStencilAttachment = &da, .timestampWrites = timing_writes(1, "Enhanced shadow-map atlas")};
      const auto pass = cmd.BeginRenderPass(&desc);
      reset_bound_pipeline();
      pass.SetBindGroup(0, detail::resources().staticBindGroup);
      pass.SetViewport(0.f, 0.f, float(job.atlasW), float(job.atlasH), 0.f, 1.f);
      pass.SetScissorRect(0, 0, job.atlasW, job.atlasH);
      render_casters(pass, job, fromAtlas, drawn, skipped);
      pass.End();
      reset_bound_pipeline();
    }
    g_statLights.store(job.lights, std::memory_order_relaxed);
    g_statCasters.store(drawn, std::memory_order_relaxed);
    g_statSkipped.store(skipped, std::memory_order_relaxed);
    const std::array<wgpu::BindGroupEntry, 6> entries{
        wgpu::BindGroupEntry{.binding = 0, .buffer = detail::resources().uniformBuffer, .offset = job.uniform.offset, .size = sizeof(FxUniform)},
        wgpu::BindGroupEntry{.binding = 5, .textureView = g_gpu.maskDepthR.view},
        wgpu::BindGroupEntry{.binding = 6, .textureView = g_gpu.atlas.view},
        wgpu::BindGroupEntry{.binding = 7, .sampler = g_gpu.cmpSampler},
        wgpu::BindGroupEntry{.binding = 8, .buffer = detail::resources().storageBuffer, .offset = job.records.offset, .size = job.records.size},
        wgpu::BindGroupEntry{.binding = 3, .textureView = dirtyView ? dirtyView : g_gpu.dummy.view},
    };
    const wgpu::BindGroupDescriptor bgDesc{.layout = g_gpu.cubeBgl, .entryCount = entries.size(), .entries = entries.data()};
    const auto bg = webgpu::g_device.CreateBindGroup(&bgDesc);
    const std::array<wgpu::RenderPassColorAttachment, 4> att{
        wgpu::RenderPassColorAttachment{.view = g_gpu.mask.view, .loadOp = maskLoad, .storeOp = wgpu::StoreOp::Store, .clearValue = {0, 0, 0, 0}},
        wgpu::RenderPassColorAttachment{.view = g_gpu.mask2.view, .loadOp = maskLoad, .storeOp = wgpu::StoreOp::Store, .clearValue = {0, 0, 0, 0}},
        wgpu::RenderPassColorAttachment{.view = g_gpu.mask3.view, .loadOp = maskLoad, .storeOp = wgpu::StoreOp::Store, .clearValue = {0, 0, 0, 0}},
        wgpu::RenderPassColorAttachment{.view = g_gpu.mask4.view, .loadOp = maskLoad, .storeOp = wgpu::StoreOp::Store, .clearValue = {0, 0, 0, 0}},
    };
    const wgpu::RenderPassDescriptor desc{.label = "Shadow-map mask",
                                          .colorAttachmentCount = att.size(),
                                          .colorAttachments = att.data(),
                                          .timestampWrites = timing_writes(2, "Shadow-map mask")};
    const auto pass = cmd.BeginRenderPass(&desc);
    pass.SetPipeline(g_gpu.cubePipe);
    pass.SetBindGroup(0, bg);
    pass.Draw(3);
    pass.End();
    if (job.shafts) {
      // 0343: sun shafts against the cascades just rendered (same bindings).
      // 0452: half the mask resolution (AURORA_ENH_SHAFTS_FULL=1: the mask resolution, A/B)
      const uint32_t sd = shafts_half() ? 2u : 1u;
      ensure_tex(g_gpu.shaft, (w + sd - 1) / sd, (h + sd - 1) / sd, HdrFormat,
                 wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding, "Enhanced sun shafts");
      {
        const std::lock_guard initLock{g_initMutex};
        if (!g_gpu.shaftPipe) {
          const wgpu::ColorTargetState target{.format = HdrFormat, .writeMask = wgpu::ColorWriteMask::All};
          const wgpu::FragmentState fragment{.module = g_gpu.module, .entryPoint = "fs_shafts", .targetCount = 1, .targets = &target};
          const wgpu::RenderPipelineDescriptor sd{
              .label = "Enhanced sun shafts",
              .layout = g_gpu.cubeLayout,
              .vertex = {.module = g_gpu.module, .entryPoint = "vs_main"},
              .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
              .fragment = &fragment,
          };
          g_gpu.shaftPipe = webgpu::g_device.CreateRenderPipeline(&sd);
        }
      }
      const wgpu::RenderPassColorAttachment sa{
          .view = g_gpu.shaft.view, .loadOp = wgpu::LoadOp::Clear, .storeOp = wgpu::StoreOp::Store, .clearValue = {0, 0, 0, 0}};
      const wgpu::RenderPassDescriptor sdesc{.label = "Sun shafts", .colorAttachmentCount = 1, .colorAttachments = &sa,
                                             .timestampWrites = webgpu::gpu_prof::pass_writes("Sun shafts")};
      const auto sp = cmd.BeginRenderPass(&sdesc);
      sp.SetPipeline(g_gpu.shaftPipe);
      sp.SetBindGroup(0, bg);
      sp.Draw(3);
      sp.End();
    }
    const wgpu::TexelCopyTextureInfo src2{.texture = g_gpu.mask2.texture};
    const wgpu::TexelCopyBufferInfo dst2{
        .layout = {.offset = job.maskOffset + uint64_t(job.pitchBytes) * h, .bytesPerRow = job.pitchBytes, .rowsPerImage = h},
        .buffer = detail::resources().maskBuffer,
    };
    if (job.lights > 4) {
      const webgpu::gpu_prof::Zone zone{cmd, "Mask copy to buffer"};
      cmd.CopyTextureToBuffer(&src2, &dst2, &extent);
    }
    // 0187: lights 8-11 and 12-15 (planes 2 and 3).
    const Tex* more[2] = {&g_gpu.mask3, &g_gpu.mask4};
    for (uint32_t k = 0; k < 2; ++k) {
      if (job.lights <= 8 + 4 * k) {
        break;
      }
      const wgpu::TexelCopyTextureInfo srcK{.texture = more[k]->texture};
      const wgpu::TexelCopyBufferInfo dstK{
          .layout = {.offset = job.maskOffset + uint64_t(job.pitchBytes) * h * (2 + k), .bytesPerRow = job.pitchBytes, .rowsPerImage = h},
          .buffer = detail::resources().maskBuffer,
      };
      const webgpu::gpu_prof::Zone zone{cmd, "Mask copy to buffer"};
      cmd.CopyTextureToBuffer(&srcK, &dstK, &extent);
    }
  } else {
    const wgpu::RenderPassColorAttachment att{
        .view = g_gpu.mask.view, .loadOp = wgpu::LoadOp::Clear, .storeOp = wgpu::StoreOp::Store, .clearValue = {0, 0, 0, 0}};
    const wgpu::RenderPassDescriptor desc{
        .label = "Light-shadow mask", .colorAttachmentCount = 1, .colorAttachments = &att, .timestampWrites = timing_writes(2, "Light-shadow mask")};
    const auto pass = cmd.BeginRenderPass(&desc);
    pass.SetPipeline(g_gpu.maskPipe);
    pass.SetBindGroup(0, bind(job.uniform, {}, {}, {}, g_gpu.maskDepthR.view));
    pass.Draw(3);
    pass.End();
  }
  const wgpu::TexelCopyTextureInfo src{.texture = g_gpu.mask.texture};
  const wgpu::TexelCopyBufferInfo dst{
      .layout = {.offset = job.maskOffset, .bytesPerRow = job.pitchBytes, .rowsPerImage = h},
      .buffer = detail::resources().maskBuffer,
  };
  {
    const webgpu::gpu_prof::Zone zone{cmd, "Mask copy to buffer"};
    cmd.CopyTextureToBuffer(&src, &dst, &extent);
  }
  timing_end(cmd);
}

// 0196: GPU time of the flashlight passes (AURORA_ENH_GPU_TIMING=1): the realistic flashlight
// (scattering + blur) or, with it off, the volumetric beam; averaged and logged every 240 frames.
struct BeamTiming {
  wgpu::QuerySet qs;
  struct Slot {
    wgpu::Buffer resolve, readback;
    std::atomic<int> state{0}; // 0 free, 1 written, 2 mapping, 3 mapped
  };
  std::array<Slot, 6> slots;
  wgpu::PassTimestampWrites first{}, last{};
  int cur = -1;
  double sum = 0, cnt = 0;
} g_beamTiming;
bool beam_timing_begin() {
  auto& b = g_beamTiming;
  b.cur = -1;
  if (!timing_on()) {
    return false;
  }
  if (!b.qs) {
    const wgpu::QuerySetDescriptor qd{.label = "Flashlight timing", .type = wgpu::QueryType::Timestamp, .count = 2};
    b.qs = webgpu::g_device.CreateQuerySet(&qd);
    for (auto& sl : b.slots) {
      const wgpu::BufferDescriptor rd{.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc, .size = 16};
      sl.resolve = webgpu::g_device.CreateBuffer(&rd);
      const wgpu::BufferDescriptor bd{.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst, .size = 16};
      sl.readback = webgpu::g_device.CreateBuffer(&bd);
    }
    b.first = {.querySet = b.qs, .beginningOfPassWriteIndex = 0, .endOfPassWriteIndex = wgpu::kQuerySetIndexUndefined};
    b.last = {.querySet = b.qs, .beginningOfPassWriteIndex = wgpu::kQuerySetIndexUndefined, .endOfPassWriteIndex = 1};
  }
  for (int i = 0; i < int(b.slots.size()); ++i) {
    if (b.slots[i].state.load() == 0) {
      b.cur = i;
      return true;
    }
  }
  return false;
}
void beam_timing_end(const wgpu::CommandEncoder& cmd) {
  auto& b = g_beamTiming;
  if (b.cur < 0) {
    return;
  }
  auto& sl = b.slots[b.cur];
  cmd.ResolveQuerySet(b.qs, 0, 2, sl.resolve, 0);
  cmd.CopyBufferToBuffer(sl.resolve, 0, sl.readback, 0, 16);
  sl.state.store(1);
  b.cur = -1;
}
void beam_timing_after_submit() {
  auto& b = g_beamTiming;
  for (auto& sl : b.slots) {
    if (sl.state.load() == 3) {
      const auto* ts = static_cast<const uint64_t*>(sl.readback.GetConstMappedRange(0, 16));
      if (ts != nullptr && ts[1] > ts[0] && ts[1] - ts[0] < 1000000000ull) {
        b.sum += double(ts[1] - ts[0]) * 1e-6;
        b.cnt += 1;
      }
      sl.readback.Unmap();
      sl.state.store(0);
      if (b.cnt >= 240) {
        Log.info("flashlight GPU (ms per frame): {:.3f} ({})", b.sum / b.cnt,
                 g_cfg.realisticFlashlight != 0 ? "realistic" : "volumetric beam");
        b.sum = 0;
        b.cnt = 0;
      }
    } else if (sl.state.load() == 1) {
      sl.state.store(2);
      sl.readback.MapAsync(wgpu::MapMode::Read, 0, 16, wgpu::CallbackMode::AllowSpontaneous,
                           [&sl](wgpu::MapAsyncStatus status, wgpu::StringView) {
                             sl.state.store(status == wgpu::MapAsyncStatus::Success ? 3 : 0);
                           });
    }
  }
}

// 0223: the composite pipeline for the EFB's color format and sample count.
void ensure_composite(wgpu::TextureFormat format, uint32_t msaa) {
  const std::lock_guard initLock{g_initMutex};
  if (!g_gpu.composite || g_gpu.compositeFormat != format || g_gpu.compositeSamples != msaa) {
    static const wgpu::BlendState blend{
        .color = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::One, .dstFactor = wgpu::BlendFactor::SrcAlpha},
        .alpha = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::Zero, .dstFactor = wgpu::BlendFactor::One},
    };
    g_gpu.composite = make_pipeline("fs_composite", format, msaa, &blend,
                                    wgpu::ColorWriteMask::Red | wgpu::ColorWriteMask::Green | wgpu::ColorWriteMask::Blue);
    g_gpu.compositeFormat = format;
    g_gpu.compositeSamples = msaa;
  }
}

void encode_scene(const wgpu::CommandEncoder& cmd, const ScenePassTargets& t, const SceneJob& job) {
  if (!init_gpu()) {
    return;
  }
  g_encoded.store(true); // 0223
  const auto rt = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
  ensure_tex(g_gpu.depth, t.width, t.height, wgpu::TextureFormat::R32Float, rt, "Enhanced scene depth");
  // 0213: the bloom bright pass samples the single-sample scene color (the EFB's copy source,
  // sampleable) directly; it ran on a full-resolution copy of it, made even with bloom off.
  const wgpu::TextureView sceneView = (job.mask & 1u) != 0 ? t.colorSource.CreateView() : wgpu::TextureView{};
  tex_copy_conv::snapshot_depth(cmd, t.depthSource, t.msaa, g_gpu.depth.view);

  // Bloom chain.
  wgpu::TextureView bloomView;
  if ((job.mask & 1u) != 0 && job.bloomLevels > 0) {
    auto [w, h] = bloom_base_size(t.width, t.height);
    for (uint32_t i = 0; i < job.bloomLevels; ++i) {
      ensure_tex(g_gpu.bloom[i], w, h, HdrFormat, rt, "Enhanced bloom");
      w = std::max(w / 2, 1u);
      h = std::max(h / 2, 1u);
    }
    run_pass(cmd, "Bloom bright", g_gpu.bloom[0].view, g_gpu.bright,
             bind(job.passes[0], sceneView, job.glowValid ? g_gpu.glow.view : wgpu::TextureView{}, {},
                  g_gpu.depth.view),
             false);
    for (uint32_t i = 1; i < job.bloomLevels; ++i) {
      run_pass(cmd, "Bloom down", g_gpu.bloom[i].view, g_gpu.down, bind(job.passes[i], g_gpu.bloom[i - 1].view, {}, {}, {}),
               false);
    }
    for (uint32_t i = job.bloomLevels - 1; i > 0; --i) {
      run_pass(cmd, "Bloom up", g_gpu.bloom[i - 1].view, g_gpu.up,
               bind(job.passes[MaxBloomLevels + i], g_gpu.bloom[i].view, {}, {}, {}), true);
    }
    bloomView = g_gpu.bloom[0].view;
  }
  wgpu::TextureView volView;
  if ((job.mask & 32u) != 0 && g_gpu.beam && g_gpu.beamBlur) {
    // 0196: scattering + motes at reduced resolution, then a depth-aware separable blur.
    ensure_tex(g_gpu.beamTex, job.beamW, job.beamH, HdrFormat, rt, "Realistic flashlight");
    ensure_tex(g_gpu.beamTmp, job.beamW, job.beamH, HdrFormat, rt, "Realistic flashlight blur");
    const wgpu::TextureView shadow = job.beamShadow ? job.beamShadow->sampleTextureView : wgpu::TextureView{};
    const bool timed = beam_timing_begin();
    run_pass(cmd, "Flashlight scattering", g_gpu.beamTex.view, g_gpu.beam, bind(job.base, shadow, {}, {}, g_gpu.depth.view),
             false, timed ? &g_beamTiming.first : nullptr);
    run_pass(cmd, "Flashlight blur H", g_gpu.beamTmp.view, g_gpu.beamBlur, bind(job.beamBlur[0], g_gpu.beamTex.view, {}, {}, {}),
             false);
    run_pass(cmd, "Flashlight blur V", g_gpu.beamTex.view, g_gpu.beamBlur, bind(job.beamBlur[1], g_gpu.beamTmp.view, {}, {}, {}),
             false, timed ? &g_beamTiming.last : nullptr);
    if (timed) {
      beam_timing_end(cmd);
    }
    volView = g_gpu.beamTex.view;
  } else if ((job.mask & 2u) != 0) {
    // The haze is smooth: at most 540 (low) / 720 lines whatever the render resolution.
    uint32_t vw = std::max(t.width / job.volDiv, 1u), vh = std::max(t.height / job.volDiv, 1u);
    const uint32_t cap = job.volDiv >= 4 ? 540u : 720u;
    if (vh > cap) {
      vw = std::max(vw * cap / vh, 1u);
      vh = cap;
    }
    ensure_tex(g_gpu.volTex, vw, vh, HdrFormat, rt, "Enhanced volumetric");
    if (beam_timing_begin()) {
      // Both writes on the one pass: begin and end.
      const wgpu::PassTimestampWrites both{.querySet = g_beamTiming.qs, .beginningOfPassWriteIndex = 0, .endOfPassWriteIndex = 1};
      run_pass(cmd, "Volumetric beam", g_gpu.volTex.view, g_gpu.vol, bind(job.base, {}, {}, {}, g_gpu.depth.view), false, &both);
      beam_timing_end(cmd);
    } else {
      run_pass(cmd, "Volumetric beam", g_gpu.volTex.view, g_gpu.vol, bind(job.base, {}, {}, {}, g_gpu.depth.view), false);
    }
    volView = g_gpu.volTex.view;
  }
  wgpu::TextureView aoView;
  if ((job.mask & 4u) != 0) {
    ensure_tex(g_gpu.aoTex, t.width / 2, t.height / 2, HdrFormat, rt, "Enhanced AO");
    ensure_tex(g_gpu.aoBlur, t.width / 2, t.height / 2, HdrFormat, rt, "Enhanced AO blur");
    run_pass(cmd, "Ambient occlusion", g_gpu.aoTex.view, g_gpu.ao, bind(job.base, {}, {}, {}, g_gpu.depth.view), false);
    run_pass(cmd, "Ambient occlusion blur", g_gpu.aoBlur.view, g_gpu.aoblur,
             bind(job.passes[MaxBloomLevels * 2], g_gpu.aoTex.view, {}, {}, g_gpu.depth.view), false);
    aoView = g_gpu.aoBlur.view;
  }

  if ((job.mask & 256u) != 0 && g_gpu.shaft.view) {
    volView = g_gpu.shaft.view; // 0343: sun shafts (mask resolution), added by the composite
  }
  if ((job.mask & 512u) != 0 && t.colorSource) {
    // 0342: colour grade of the scene so far: a copy of the resolved colour, then a full-screen
    // pass over the EFB (alpha kept).
    ensure_tex(g_gpu.gradeSrc, t.width, t.height, t.colorFormat, wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::TextureBinding,
               "Enhanced grade source");
    const wgpu::TexelCopyTextureInfo src{.texture = t.colorSource};
    const wgpu::TexelCopyTextureInfo dst{.texture = g_gpu.gradeSrc.texture};
    const wgpu::Extent3D ext{t.width, t.height, 1};
    cmd.CopyTextureToTexture(&src, &dst, &ext);
    {
      const std::lock_guard initLock{g_initMutex};
      if (!g_gpu.grade || g_gpu.gradeFormat != t.colorFormat || g_gpu.gradeSamples != t.msaa) {
        g_gpu.grade = make_pipeline("fs_grade", t.colorFormat, t.msaa, nullptr,
                                    wgpu::ColorWriteMask::Red | wgpu::ColorWriteMask::Green | wgpu::ColorWriteMask::Blue);
        g_gpu.gradeFormat = t.colorFormat;
        g_gpu.gradeSamples = t.msaa;
      }
    }
    const wgpu::RenderPassColorAttachment ga{
        .view = t.colorView, .resolveTarget = t.resolveView, .loadOp = wgpu::LoadOp::Load, .storeOp = wgpu::StoreOp::Store};
    const wgpu::RenderPassDescriptor gdesc{.label = "Enhanced colour grade", .colorAttachmentCount = 1, .colorAttachments = &ga,
                                           .timestampWrites = webgpu::gpu_prof::pass_writes("Enhanced colour grade")};
    const auto gp = cmd.BeginRenderPass(&gdesc);
    gp.SetPipeline(g_gpu.grade);
    if (job.clipped && job.clip[0] + job.clip[2] <= t.width && job.clip[1] + job.clip[3] <= t.height) {
      gp.SetScissorRect(job.clip[0], job.clip[1], job.clip[2], job.clip[3]);
    }
    gp.SetBindGroup(0, bind(job.base, g_gpu.gradeSrc.view, {}, {}, {}));
    gp.Draw(3);
    gp.End();
  }

  // Composite onto the EFB.
  ensure_composite(t.colorFormat, t.msaa);
  const wgpu::RenderPassColorAttachment att{
      .view = t.colorView,
      .resolveTarget = t.resolveView,
      .loadOp = wgpu::LoadOp::Load,
      .storeOp = wgpu::StoreOp::Store,
  };
  const wgpu::RenderPassDescriptor desc{.label = "Enhanced composite", .colorAttachmentCount = 1, .colorAttachments = &att,
                                        .timestampWrites = webgpu::gpu_prof::pass_writes("Enhanced composite")};
  const auto pass = cmd.BeginRenderPass(&desc);
  pass.SetPipeline(g_gpu.composite);
  if (job.clipped && job.clip[0] + job.clip[2] <= t.width && job.clip[1] + job.clip[3] <= t.height) {
    pass.SetScissorRect(job.clip[0], job.clip[1], job.clip[2], job.clip[3]);  // 0225
  }
  pass.SetBindGroup(0, bind(job.base,
                            (job.mask & 8u) != 0 ? (g_glowDbg == 11 && g_gpu.maskAo.view ? g_gpu.maskAo.view
                                                    : g_glowDbg >= 8 && g_gpu.mask.view ? g_gpu.mask.view
                                                                                        : g_gpu.glow.view)
                                                 : bloomView,
                            volView, aoView,
                            (job.mask & 16u) != 0   ? g_gpu.glowSrcDepth.view
                            : (job.mask & 32u) != 0 ? g_gpu.depth.view
                                                    : wgpu::TextureView{}));
  pass.Draw(3);
  pass.End();
}

// ---- 0520: light tiles ------------------------------------------------------------------------
// The world shaders looped over every extra light per pixel (64 at most). With up to 256 lights
// (MaxExtraLights) a compute pass bins them first, once per EFB pass that draws with them: per
// screen tile of 2^shift render-target pixels (32, one 8 x 8 workgroup; larger only when the mask
// buffer's free tail is too small; 16 px tiles with 16 x 16 workgroups cost 0.34 ms at 4K), three
// 256-bit masks of the lights whose sphere can reach a surface seen there:
//   list 0: within the depth range of the depth pre-pass in the tile (draws that are in the
//           pre-pass: opaque depth-writing world draws),
//   list 1: in front of the farthest pre-pass surface in the tile (other depth-tested draws),
//   list 2: the tile's frustum sides only (draws without depth test; all three lists when the frame
//           has no pre-pass, i.e. neither light shadows nor ambient occlusion).
// Directional lights are in every list. A pixel visits its list's set bits in index order with the
// full loop's per-light code (enh_xl_one), so a light the test keeps adds exactly what it added
// before and a light it drops could not reach the pixel: the sum is unchanged. The data lives in
// the mask buffer (the fragment stage has no third storage buffer; WebGPU's 128 MiB binding limit
// is that buffer's size) after the mask planes, behind a header word with the stamp of the job that
// wrote it: a draw whose pass never got its job (a pass ended without a flush) sees another stamp
// and runs the full loop. AURORA_ENH_TILES=0: always the full loop (A/B); AURORA_ENH_TILE_MIN=<n>:
// tiles from n lights on (default 4); AURORA_ENH_TILE_SHOW=1: lights visited per pixel as a heat
// tint (blue 1-8, green ~16, red 32+); AURORA_ENH_TILE_LOG=<n>: jobs / lost jobs / sizes every n
// frames.
constexpr uint32_t TileListWords = MaxExtraLights / 32;
constexpr uint32_t TileLists = 3;
constexpr uint32_t TileWords = TileListWords * TileLists;
constexpr uint32_t TileHeaderWords = 64; // 256 bytes: word 0 = stamp
static_assert(TileWords == 24, "the shaders read 24 words per tile, 8 per list");

const bool g_tilesOff = [] {
  const char* v = std::getenv("AURORA_ENH_TILES");
  return v != nullptr && std::atoi(v) == 0;
}();
const uint32_t g_tileMin = [] {
  const char* v = std::getenv("AURORA_ENH_TILE_MIN");
  return v != nullptr ? uint32_t(std::max(std::atoi(v), 1)) : 4u;
}();
const bool g_tileShow = std::getenv("AURORA_ENH_TILE_SHOW") != nullptr;
// AURORA_ENH_TILES_AB=<n>: tiles on and off in turns of n recorded frames (logged), to compare one
// held frame both ways (e.g. a frozen game presenting its last tape).
const uint32_t g_tilesAb = [] {
  const char* v = std::getenv("AURORA_ENH_TILES_AB");
  return v != nullptr ? uint32_t(std::max(std::atoi(v), 0)) : 0u;
}();
uint32_t g_tileRecFrames = 0;
bool g_tilesAbOff = false;
const uint32_t g_tileLog = [] {
  const char* v = std::getenv("AURORA_ENH_TILE_LOG");
  return v != nullptr ? uint32_t(std::max(std::atoi(v), 1)) : 0u;
}();

// The projection (and viewport) a tile job is built for: every draw that reads it must use it.
struct TileCam {
  float p[8]{}; // P00 P11 P02 P12 P03 P13 A B
  Viewport vp{};
  bool operator==(const TileCam& o) const noexcept { return std::memcmp(this, &o, sizeof(TileCam)) == 0; }
};
TileCam tile_cam() noexcept {
  const auto& st = gx::g_gxState;
  const auto& p = st.proj;
  TileCam c;
  const float v[8] = {p.m0.x(), p.m1.y(), p.m0.z(), p.m1.z(), p.m0.w(), p.m1.w(), p.m2.z(), p.m2.w()};
  std::memcpy(c.p, v, sizeof v);
  c.vp = st.renderViewport;
  return c;
}
// FIFO thread: the job of the current EFB pass (made by the first draw that reads tiles).
struct TileState {
  bool valid = false;
  bool fresh = false; // draws read it since the last queued job
  uint32_t pass = UINT32_MAX;
  uint32_t stamp = 0, gen = 0;
  TileCam cam{};
  Range lights{};
  uint32_t count = 0;
  uint32_t base = 0, shift = 4, tx = 0, ty = 0; // header word in the mask buffer, tile grid
};
TileState g_tile;
uint32_t g_tileCounter = 0;
uint32_t g_tileUniformPass = UINT32_MAX; // pass of the last enhanced uniform built with tiles
uint32_t g_tileJobs = 0, g_tileLost = 0, g_tileDepthJobs = 0, g_tileFrames = 0; // AURORA_ENH_TILE_LOG

struct TileUniform {
  float proj[4];  // P00 P11 P02 P12
  float projz[4]; // A B P03 P13
  float vp[4];    // render viewport x y w h (target pixels)
  float dr[4];    // viewport min / max depth (as stored), target pixels per depth texel, depth valid
  uint32_t dims[4]; // tiles x, y, shift, light count
  uint32_t info[4]; // light base (storage-buffer words), header (mask-buffer words), stamp, 0
};
static_assert(sizeof(TileUniform) == 96);
struct TileJob {
  Range uniform{};
  uint32_t tx = 0, ty = 0;
  bool depth = false;
};

constexpr std::string_view TileShader = R"""(
struct TileU {
  proj: vec4f,
  projz: vec4f,
  vp: vec4f,
  dr: vec4f,
  dims: vec4u,
  info: vec4u,
};
@group(0) @binding(0) var<uniform> tu: TileU;
@group(0) @binding(1) var<storage, read> lb: array<u32>;
@group(0) @binding(2) var tdepth: texture_2d<f32>;
@group(0) @binding(3) var<storage, read_write> ob: array<u32>;
var<workgroup> wz: array<atomic<u32>, 2>;
var<workgroup> wbits: array<atomic<u32>, 24>;
// View z of a stored depth (depth = A + B / z, as fx view_pos); 1 (behind) when undefined.
fn tile_view_z(d: f32) -> f32 {
  let dn = (d - tu.dr.x) / max(tu.dr.y - tu.dr.x, 1e-6);
  let den = dn - tu.projz.x;
  if (abs(den) < 1e-12) { return 1.0; }
  return tu.projz.y / den;
}
// The sphere (c, r) is not entirely on the outer side of the plane n.x + d = 0.
fn tile_plane(n: vec3f, d: f32, c: vec3f, r: f32) -> bool {
  let l = length(n);
  return l < 1e-12 || dot(n, c) + d >= -r * l;
}
@compute @workgroup_size(8, 8)
fn cs_tiles(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_id) lid: vec3u,
            @builtin(local_invocation_index) li: u32) {
  if (li == 0u) {
    atomicStore(&wz[0], 0x7F7FFFFFu);
    atomicStore(&wz[1], 0u);
  }
  if (li < 24u) { atomicStore(&wbits[li], 0u); }
  workgroupBarrier();
  let ts = f32(1u << tu.dims.z);
  let p0 = vec2f(wg.xy) * ts;
  let p1 = p0 + vec2f(ts);
  if (tu.dr.w > 0.5) {
    // The pre-pass depth texels under the tile and one more around it (the pre-pass is rendered at
    // the mask's resolution: a surface the EFB pass sees between its texels lies within them).
    let dims = vec2i(textureDimensions(tdepth));
    let s = max(tu.dr.z, 1e-3);
    let d0 = vec2i(floor(p0 / s)) - vec2i(1);
    let d1 = vec2i(ceil(p1 / s)) + vec2i(1);
    for (var y = d0.y + i32(lid.y); y < d1.y; y += 8) {
      for (var x = d0.x + i32(lid.x); x < d1.x; x += 8) {
        let c = clamp(vec2i(x, y), vec2i(0), dims - vec2i(1));
        let b = bitcast<u32>(max(textureLoad(tdepth, c, 0).r, 0.0));
        atomicMin(&wz[0], b);
        atomicMax(&wz[1], b);
      }
    }
  }
  workgroupBarrier();
  // The surfaces' view z range (in front: z < 0; nearest = largest z); unbounded without depth.
  var zn = 1e30;
  var zf = -1e30;
  if (tu.dr.w > 0.5) {
    let za = tile_view_z(bitcast<f32>(atomicLoad(&wz[0])));
    let zb = tile_view_z(bitcast<f32>(atomicLoad(&wz[1])));
    if (za < 0.0 && zb < 0.0 && za > -1e29 && zb > -1e29) {
      zn = max(za, zb);
      zf = min(za, zb);
    }
  }
  // 64 threads, the lights in turns (li, li + 64, ...)
  for (var i = li; i < tu.dims.w; i += 64u) {
    let b = tu.info.x + i * 24u;
    var k0 = true;
    var k1 = true;
    var k2 = true;
    if ((lb[b + 8u] & 32u) == 0u) { // (directional lights reach every tile)
      let c = vec3f(bitcast<f32>(lb[b]), bitcast<f32>(lb[b + 1u]), bitcast<f32>(lb[b + 2u]));
      // the shader's reach (radius at least 1), a little wider for rounding
      let r = max(bitcast<f32>(lb[b + 3u]), 1.0) * 1.002 + 0.5;
      // The tile's NDC rectangle and its four side planes (clip = P x view, w = -z):
      // ndc.x >= x0  <=>  P00 x + (P02 + x0) z + P03 >= 0, and so on.
      let x0 = (p0.x - tu.vp.x) / tu.vp.z * 2.0 - 1.0;
      let x1 = (p1.x - tu.vp.x) / tu.vp.z * 2.0 - 1.0;
      let yt = 1.0 - (p0.y - tu.vp.y) / tu.vp.w * 2.0;
      let yb = 1.0 - (p1.y - tu.vp.y) / tu.vp.w * 2.0;
      let side = c.z - r < 0.0 &&
                 tile_plane(vec3f(tu.proj.x, 0.0, tu.proj.z + x0), tu.projz.z, c, r) &&
                 tile_plane(vec3f(-tu.proj.x, 0.0, -(tu.proj.z + x1)), -tu.projz.z, c, r) &&
                 tile_plane(vec3f(0.0, tu.proj.y, tu.proj.w + yb), tu.projz.w, c, r) &&
                 tile_plane(vec3f(0.0, -tu.proj.y, -(tu.proj.w + yt)), -tu.projz.w, c, r);
      let front = c.z + r >= zf - (4.0 + 0.02 * abs(zf)); // not wholly behind the farthest surface
      let back = c.z - r <= zn + (4.0 + 0.02 * abs(zn));  // not wholly in front of the nearest
      k0 = side && front && back;
      k1 = side && front;
      k2 = side;
    }
    let bit = 1u << (i & 31u);
    let w = i >> 5u;
    if (k0) { atomicOr(&wbits[w], bit); }
    if (k1) { atomicOr(&wbits[8u + w], bit); }
    if (k2) { atomicOr(&wbits[16u + w], bit); }
  }
  workgroupBarrier();
  let tile = wg.y * tu.dims.x + wg.x;
  if (li < 24u) { ob[tu.info.y + 64u + tile * 24u + li] = atomicLoad(&wbits[li]); }
  if (li == 0u && tile == 0u) { ob[tu.info.y] = tu.info.z; }
}
)""";

wgpu::ComputePipeline g_tilePipe;
wgpu::BindGroupLayout g_tileBgl;
bool g_tileInit = false;

bool init_tiles() {
  const std::lock_guard initLock{g_initMutex}; // 0223
  if (g_tileInit) {
    return bool(g_tilePipe);
  }
  g_tileInit = true;
  const std::string code{TileShader};
  const wgpu::ShaderSourceWGSL wgsl{{.code = code.c_str()}};
  const wgpu::ShaderModuleDescriptor moduleDesc{.nextInChain = &wgsl, .label = "Enhanced light tiles"};
  const auto module = webgpu::g_device.CreateShaderModule(&moduleDesc);
  const std::array entries{
      wgpu::BindGroupLayoutEntry{.binding = 0,
                                 .visibility = wgpu::ShaderStage::Compute,
                                 .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(TileUniform)}},
      wgpu::BindGroupLayoutEntry{.binding = 1,
                                 .visibility = wgpu::ShaderStage::Compute,
                                 .buffer = {.type = wgpu::BufferBindingType::ReadOnlyStorage}},
      wgpu::BindGroupLayoutEntry{.binding = 2,
                                 .visibility = wgpu::ShaderStage::Compute,
                                 .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                                             .viewDimension = wgpu::TextureViewDimension::e2D}},
      wgpu::BindGroupLayoutEntry{.binding = 3,
                                 .visibility = wgpu::ShaderStage::Compute,
                                 .buffer = {.type = wgpu::BufferBindingType::Storage}},
  };
  const wgpu::BindGroupLayoutDescriptor bglDesc{.label = "Enhanced light tiles", .entryCount = entries.size(), .entries = entries.data()};
  g_tileBgl = webgpu::g_device.CreateBindGroupLayout(&bglDesc);
  const wgpu::PipelineLayoutDescriptor plDesc{.label = "Enhanced light tiles", .bindGroupLayoutCount = 1, .bindGroupLayouts = &g_tileBgl};
  const wgpu::ComputePipelineDescriptor desc{
      .label = "Enhanced light tiles",
      .layout = webgpu::g_device.CreatePipelineLayout(&plDesc),
      .compute = {.module = module, .entryPoint = "cs_tiles"},
  };
  const ::aurora::webgpu::CreateTimer createTimer{"pipeline enhanced.cpp light tiles"}; // 0223 (AURORA_CREATE_LOG)
  g_tilePipe = webgpu::g_device.CreateComputePipeline(&desc);
  if (!g_tilePipe) {
    Log.error("Light tile pass failed to build; every pixel loops over all extra lights");
  }
  return bool(g_tilePipe);
}

// Render worker: the job queued before its EFB pass (after that pass's mask, whose pre-pass depth
// it reads when `depth`).
void encode_tiles(const wgpu::CommandEncoder& cmd, const TileJob& job) {
  if (!init_gpu() || !init_tiles()) {
    return;
  }
  if (job.depth && !g_gpu.maskDepthR.view) {
    return; // (no stamp written: the pass's draws take the full loop)
  }
  const std::array entries{
      wgpu::BindGroupEntry{.binding = 0, .buffer = detail::resources().uniformBuffer, .offset = job.uniform.offset, .size = sizeof(TileUniform)},
      wgpu::BindGroupEntry{.binding = 1, .buffer = detail::resources().storageBuffer},
      wgpu::BindGroupEntry{.binding = 2, .textureView = job.depth ? g_gpu.maskDepthR.view : g_gpu.dummyDepth.view},
      wgpu::BindGroupEntry{.binding = 3, .buffer = detail::resources().maskBuffer},
  };
  const wgpu::BindGroupDescriptor bgDesc{.label = "Enhanced light tiles", .layout = g_tileBgl, .entryCount = entries.size(), .entries = entries.data()};
  const wgpu::ComputePassDescriptor passDesc{.label = "Enhanced light tiles",
                                             .timestampWrites = webgpu::gpu_prof::pass_writes("Enhanced light tiles")};
  const auto pass = cmd.BeginComputePass(&passDesc);
  pass.SetPipeline(g_tilePipe);
  pass.SetBindGroup(0, webgpu::g_device.CreateBindGroup(&bgDesc));
  pass.DispatchWorkgroups(job.tx, job.ty);
  pass.End();
}

// FIFO thread (draw_uniform): the current pass's tile job for this draw, made by its first draw.
// False: this draw runs the full loop (its camera or the light set differs from the job's, or the
// grid does not fit into the mask buffer).
bool tile_prepare(uint32_t pass) noexcept {
  const TileCam cam = tile_cam();
  if (g_tile.valid && g_tile.pass == pass) {
    return g_tile.gen == g_lightGen && g_tile.count == g_lightCount && g_tile.cam == cam;
  }
  if (g_tile.valid && g_tile.fresh) {
    ++g_tileLost; // the previous pass ended without a flush: its draws took the full loop
  }
  g_tile.valid = false;
  const auto size = get_render_target_size();
  if (size.x == 0 || size.y == 0) {
    return false;
  }
  // After the mask planes (as reserved this frame, or as they would be).
  const auto [mw, mh] = mask_size(size.x, size.y, g_cfg);
  uint64_t used = uint64_t(AURORA_ALIGN(mw * 4, 256u)) * mh * MaskPlanes;
  if (g_maskReserved) {
    used = std::max(used, uint64_t(g_maskPitch) * 4 * g_maskH * MaskPlanes);
  }
  used = AURORA_ALIGN(used, uint64_t(256));
  uint32_t shift = 5, tx = 0, ty = 0;
  for (;; ++shift) {
    if (shift > 7) {
      return false;
    }
    tx = (size.x + (1u << shift) - 1) >> shift;
    ty = (size.y + (1u << shift) - 1) >> shift;
    if (tx <= 0xFFFFu && ty <= 0xFFFFu && used + (uint64_t(TileHeaderWords) + uint64_t(tx) * ty * TileWords) * 4 <= MaskBufferSize) {
      break;
    }
  }
  if (++g_tileCounter == 0) {
    ++g_tileCounter;
  }
  g_tile = {.valid = true,
            .fresh = false,
            .pass = pass,
            .stamp = g_tileCounter,
            .gen = g_lightGen,
            .cam = cam,
            .lights = g_lightRange,
            .count = g_lightCount,
            .base = uint32_t(used / 4),
            .shift = shift,
            .tx = tx,
            .ty = ty};
  return true;
}

// FIFO thread (flush_shadows: before an EFB copy or the scene end): queues the current pass's tile
// job if draws read it since the last one. `depth`: this flush queued a mask job (its pre-pass
// depth holds every opaque world draw of the frame so far).
void flush_tiles(bool depth) noexcept {
  if (!g_tile.valid || !g_tile.fresh) {
    return;
  }
  if (g_tile.pass != next_command_position().first) {
    ++g_tileLost;
    g_tile.valid = false;
    return;
  }
  const auto size = get_render_target_size();
  depth = depth && g_maskReserved && g_maskW > 0;
  TileUniform u{};
  const auto& c = g_tile.cam;
  u.proj[0] = c.p[0];
  u.proj[1] = c.p[1];
  u.proj[2] = c.p[2];
  u.proj[3] = c.p[3];
  u.projz[0] = c.p[6];
  u.projz[1] = c.p[7];
  u.projz[2] = c.p[4];
  u.projz[3] = c.p[5];
  u.vp[0] = c.vp.left;
  u.vp[1] = c.vp.top;
  u.vp[2] = std::max(c.vp.width, 1.f);
  u.vp[3] = std::max(c.vp.height, 1.f);
  u.dr[0] = gx::UseReversedZ ? 1.f - c.vp.zfar : c.vp.znear;
  u.dr[1] = gx::UseReversedZ ? 1.f - c.vp.znear : c.vp.zfar;
  u.dr[2] = depth ? float(size.x) / float(g_maskW) : 1.f;
  u.dr[3] = depth ? 1.f : 0.f;
  u.dims[0] = g_tile.tx;
  u.dims[1] = g_tile.ty;
  u.dims[2] = g_tile.shift;
  u.dims[3] = g_tile.count;
  u.info[0] = uint32_t(g_tile.lights.offset / 4);
  u.info[1] = g_tile.base;
  u.info[2] = g_tile.stamp;
  TileJob job{.uniform = push_uniform(reinterpret_cast<const uint8_t*>(&u), sizeof(u)), .tx = g_tile.tx, .ty = g_tile.ty, .depth = depth};
  std::vector<efb_ram::PassOp> ops;
  ops.emplace_back([job](const wgpu::CommandEncoder& cmd, detail::FramePacket&) { encode_tiles(cmd, job); });
  if (queue_pre_pass_ops(std::move(ops))) {
    g_tile.fresh = false;
    ++g_tileJobs;
    g_tileDepthJobs += depth ? 1 : 0;
  }
}

} // namespace

bool tile_uniform_stale() noexcept {
  return g_tileUniformPass != UINT32_MAX && next_command_position().first != g_tileUniformPass;
}

bool sync_config() noexcept {
  const uint32_t gen = g_pendingGen.load(std::memory_order_acquire);
  if (gen == g_cfgGen) {
    return false;
  }
  std::lock_guard lock{g_cfgMutex};
  const bool masterChanged = g_cfg.enabled != g_pending.enabled;
  const bool aoBefore = receivers();
  g_cfg = g_pending;
  g_cfgGen = gen;
  ++g_lightGen;
  assign_shadow_slots();
  g_lightRangeValid = false;
  gx::g_gxState.dirty |= gx::DirtyUniform;
  // Only the master switch selects shader variants; the parts are uniforms. 0032: and ambient
  // occlusion, which also gives unlit opaque world draws the variant.
  return masterChanged || aoBefore != receivers();
}

const AuroraEnhancedConfig& config() noexcept { return g_cfg; }

uint8_t shader_class(const gx::ShaderConfig& sc, uint8_t drawClass, bool blended, bool additive) noexcept {
  static const bool sNoVariant = std::getenv("AURORA_ENH_NOVARIANT") != nullptr; // debug aid
  if (drawClass == AURORA_DRAW_CLASS_SHADOW) {
    drawClass = AURORA_DRAW_CLASS_WORLD; // 0470: the world draw's own pipeline (never drawn; its caster variant is)
  }
  if (sNoVariant || !g_cfg.enabled || drawClass == AURORA_DRAW_CLASS_OTHER || additive || sc.lineMode != 0 || sc.wireframe ||
      gx::g_gxState.projType != GX_PERSPECTIVE) {
    return 0;
  }
  bool want = false;
  for (const auto& cc : sc.colorChannels) {
    want = want || cc.lightingEnabled;
  }
  // Surfaces have textures; untextured lit draws are effect volumes whose color/alpha *is* the
  // lighting result (e.g. the flashlight's darkness/haze volumes in the nursery): left alone.
  bool textured = false;
  for (u32 i = 0; i < sc.tevStageCount; ++i) {
    const auto& st = sc.tevStages[i];
    textured = textured || st.texMapId != GX_TEXMAP_NULL;
    want = want || (st.alphaOp.op == GX_TEV_COMP_GR16_GT && st.colorPass.b == GX_CC_TEXC);
  }
  // 0032: unlit textured opaque world surfaces (depth-writing, not additive: LM draws most of its
  // solid geometry with SRCALPHA/INVSRCALPHA blending) receive the forward ambient occlusion.
  want = want || (receivers() && drawClass == AURORA_DRAW_CLASS_WORLD && gx::g_gxState.depthUpdate);
  if (!want || !textured) {
    return 0;
  }
  // Only the ghost pass gets the ghost variant (extra lights reach it only with their
  // translucent flag); blended room geometry is ordinary world geometry.
  (void)blended;
  return drawClass == AURORA_DRAW_CLASS_GHOST ? 2 : 1;
}

DrawUniform draw_uniform() noexcept {
  const auto& c = g_cfg;
  DrawUniform u{};
  if (!c.enabled) {
    return u;
  }
  if (c.perPixelLighting) {
    u.flags |= 1u;
  }
  g_tileUniformPass = UINT32_MAX; // 0520
  if (c.extraLights && g_lightCount > 0) {
    if (!g_lightRangeValid) {
      static std::array<uint32_t, MaxExtraLights * 24> words{}; // (FIFO thread; 24 KB)
      for (uint32_t i = 0; i < g_lightCount; ++i) {
        const auto& l = g_lights[i];
        uint32_t* w = &words[i * 24];
        std::memcpy(w + 0, l.pos, 12);
        std::memcpy(w + 3, &l.radius, 4);
        // (0340: directional lights keep their colour: the sky's light, not a coloured lamp)
        const auto vc = (l.flags & ExtraLightDirectional) != 0 ? std::array<float, 3>{l.color[0], l.color[1], l.color[2]}
                                                               : vivid(l.color, std::clamp(c.extraLightSaturation, 0.f, 2.5f));
        std::memcpy(w + 4, vc.data(), 12);
        std::memcpy(w + 7, &l.intensity, 4);
        w[8] = l.flags;
        w[9] = l.id;
        w[10] = g_shadowSlot[i];
        std::memcpy(w + 12, l.box, 48); // 0187: room box (ExtraLightRoomBox)
      }
      g_lightRange = push_storage(reinterpret_cast<const uint8_t*>(words.data()), g_lightCount * 24 * sizeof(uint32_t));
      g_lightRangeValid = true;
    }
    u.flags |= 2u;
    u.lightBase = g_lightRange.offset / 4;
    u.lightCount = g_lightCount;
    // 0520: light tiles for draws of the EFB pass in its main camera.
    const auto& ts = gx::g_gxState;
    if (!g_tilesOff && !g_tilesAbOff && g_lightCount >= g_tileMin && ts.projType == GX_PERSPECTIVE && !is_offscreen()) {
      const uint32_t pass = next_command_position().first;
      g_tileUniformPass = pass;
      if (tile_prepare(pass)) {
        // The list by what the draw's visible surface can be: in the pre-pass (the same test as
        // note_draw's), in front of it, or anywhere.
        const bool pointShadows = g_shadowCount > 0 && c.extraShadows;
        uint32_t list = 2;
        if (ts.depthCompare) {
          list = 1;
          if (ts.drawClass == AURORA_DRAW_CLASS_WORLD && (pointShadows || ao_forward()) && ts.depthUpdate &&
              !(ts.blendMode == GX_BM_BLEND && ts.blendFacDst == GX_BL_ONE) && g_depthDraws.size() + 1 < MaxDepthDraws) {
            list = 0;
          }
        }
        u.tileBase = g_tile.base;
        u.tileStamp = g_tile.stamp;
        u.tileDims = g_tile.tx | g_tile.ty << 16;
        u.tileCfg = g_tile.shift | list << 8 | (g_tileShow ? 1u << 30 : 0u) | 1u << 31;
        g_tile.fresh = true;
      }
    }
  }
  if (c.softShadows) {
    u.flags |= 4u;
    switch (c.softShadowQuality) {
    case 0:
      u.shadowTaps = 8;
      break;
    case 1:
      u.shadowTaps = 16;
      break;
    case 2:
      u.shadowTaps = 16 | 0x10000u;
      break;
    default:
      u.shadowTaps = 32 | 0x10000u;
      break;
    }
  }
  if (c.flashlightQuality) {
    u.flags |= 8u;
    u.flashSoft = std::clamp(c.flashlightSoftness, 0.f, 1.f);
    u.flashSpec = std::clamp(c.flashlightSpecular, 0.f, 2.f);
  }
  if (c.realisticFlashlight != 0) {
    // 0196: the realistic footprint replaces the spot term of bright spot lights (per pixel).
    u.flags |= 8u | 32u;
    u.flashSoft = 0.f;
    u.beamGain = 1.2f * (1.f + 0.6f * g_flare);
    u.beamDref2 = 650.f * 650.f;
    u.beamFloor = 0.25f;
    u.beamRings = c.realisticFlashlight >= 2 ? 1.f : 0.6f;
  }
  // 0032: forward ambient occlusion in opaque (and alpha-tested) world surfaces only: nothing
  // translucent or additive in front of occluded geometry gets darker.
  const auto& gs = gx::g_gxState;
  const bool aoMask = ao_forward();
  // Opaque = writes depth and does not add light (the same draws as the depth pre-pass; most
  // solid geometry is drawn with SRCALPHA/INVSRCALPHA blending and alpha 1).
  if (aoMask && gs.drawClass == AURORA_DRAW_CLASS_WORLD && gs.projType == GX_PERSPECTIVE && gs.depthUpdate &&
      !(gs.blendMode == GX_BM_BLEND && gs.blendFacDst == GX_BL_ONE) && !is_offscreen()) {
    u.flags |= 16u;
    static const bool sAoShow = std::getenv("AURORA_ENH_AO_SHOW") != nullptr; // 0186 debug
    if (sAoShow) {
      u.flags |= 0x30000u;
    }
  }
  if ((c.extraLights && c.extraShadows && g_shadowCount > 0) || aoMask) {
    if (!g_maskReserved) {
      // Half resolution (bloom level 0 size), except shadow maps at high quality: full resolution
      // (sharp contact edges) up to 1440 lines, within the mask buffer (two planes; 0032: a
      // third for ambient occlusion).
      const auto size = get_render_target_size();
      const auto [w, h] = mask_size(size.x, size.y, g_cfg);
      g_maskW = w;
      g_maskH = h;
      g_maskPitch = AURORA_ALIGN(w * 4, 256) / 4;
      g_maskReserved = true;
    }
    u.maskBase = 0;
    u.maskPitch = g_maskPitch;
    u.maskDims = g_maskW | g_maskH << 16;
    const auto size = get_render_target_size();
    u.maskScale = float(g_maskW) / float(std::max(size.x, 1u));
  }
  u.extraScale = c.extraLightIntensity;
  u.shadowRadius = std::max(c.softShadowRadius, 0.f);
  u.pcss = std::max(c.softShadowPenumbra, 0.f);
  u.shadowDark = std::clamp(c.shadowDarkness, 0.f, 1.5f);
  u.shadowKeep = std::clamp(c.shadowAmbientKeep, 0.f, 1.f);
  if (gx::g_gxState.materialHint == AURORA_MATERIAL_CHARACTER) {
    // Characters keep the game's own (vertex) lighting balance: no per-pixel re-evaluation of
    // the GX lights and no flashlight highlight (glossy on cloth and skin); the extra lights
    // still reach them, with a matte wrap-around response (shader: enh_material).
    u.flags &= ~(1u | 8u);
    u.material = 1.f;
  } else if (gx::g_gxState.materialHint == AURORA_MATERIAL_EMISSIVE || gx::g_gxState.materialHint == AURORA_MATERIAL_EMISSIVE_GLOW) {
    // 0420: light-emitting surfaces are not shadowed (shader: enh_material 2) nor occluded.
    u.flags &= ~0x30010u;
    u.material = 2.f;
  } else if (gx::g_gxState.materialHint == AURORA_MATERIAL_WORLD_RIM) {
    u.material = 0.25f; // 0520a: a world surface (every < 0.5 test) that takes the rim light
  } else if (gx::g_gxState.materialHint == AURORA_MATERIAL_WORLD_RIM_SOFT) {
    u.material = 0.125f;
  }
  return u;
}

namespace {
bool shadow_maps() noexcept { return g_cfg.extraShadowMode == 1; }

// Shadow slots go to the first flagged lights (providers pass the strongest first), as many as
// the configuration allows (screen-space: at most 4).
void assign_shadow_slots() noexcept {
  const uint32_t cap = std::min(g_cfg.extraShadowLights, shadow_maps() ? MaxShadowLights : 4u);
  g_shadowCount = 0;
  // 0186: more flagged lights than slots (an interpolated replay blends a shadow fading out of
  // one frame with one fading into the next; a provider over-flagging): the slots go to the
  // strongest shadow weights (flags bits 8..15, 0 = full), not to the first lights in the list,
  // whose order changes from frame to frame (a full shadow lost its slot every other frame).
  std::array<uint32_t, MaxExtraLights> order{};
  uint32_t n = 0;
  for (uint32_t i = 0; i < MaxExtraLights; ++i) {
    g_shadowSlot[i] = UINT32_MAX;
    // 0340: a directional light casts with any intensity (it can be shadows only), and only with
    // shadow maps (the screen-space march needs a position).
    const bool dir = i < g_lightCount && (g_lights[i].flags & ExtraLightDirectional) != 0;
    if (i < g_lightCount && (g_lights[i].flags & ExtraLightCastShadows) != 0 && (g_lights[i].intensity > 0.f || dir) &&
        g_lights[i].radius > 0.f && (!dir || shadow_maps())) {
      order[n++] = i;
    }
  }
  const auto weight = [](const ExtraLight& l) {
    const uint32_t w = (l.flags >> 8) & 0xFFu;
    return ((l.flags & ExtraLightDirectional) != 0 ? 512u : 0u) + (w != 0 ? w : 256u); // directional first
  };
  std::stable_sort(order.begin(), order.begin() + n,
                   [&](uint32_t a, uint32_t b) { return weight(g_lights[a]) > weight(g_lights[b]); });
  for (uint32_t k = 0; k < n && g_shadowCount < cap; ++k) {
    g_shadowSlot[order[k]] = g_shadowCount++;
  }
}
} // namespace

void set_lights(const ExtraLight* in, uint32_t count) noexcept {
  // 0196: world-anchor pseudo-lights are taken out of the set.
  static std::array<ExtraLight, MaxExtraLights> real{}; // (FIFO thread; 0520: 22 KB)
  uint32_t n = 0;
  uint32_t anchors = 0;
  g_focus.valid = false;
  for (uint32_t i = 0; i < std::min(count, MaxExtraLights); ++i) {
    if ((in[i].flags & AURORA_EXTRA_LIGHT_ANCHOR) != 0) {
      if (in[i].id == AURORA_SHADOW_FOCUS_ID) { // 0530
        const auto& p = in[i].pos;
        if (std::isfinite(p[0] + p[1] + p[2] + in[i].radius) && in[i].radius > 0.f) {
          g_focus = {{p[0], p[1], p[2]}, in[i].radius, true};
        }
        continue;
      }
      const uint32_t k = in[i].id - AURORA_BEAM_ANCHOR_ID;
      if (k < 3) {
        g_anchor[k] = {in[i].pos[0], in[i].pos[1], in[i].pos[2]};
        anchors |= 1u << k;
      }
      continue;
    }
    real[n++] = in[i];
  }
  g_anchorValid = anchors == 7u;
  const ExtraLight* lights = real.data();
  count = n;
  if (count == g_lightCount && std::memcmp(lights, g_lights.data(), count * sizeof(ExtraLight)) == 0) {
    return;
  }
  std::memcpy(g_lights.data(), lights, count * sizeof(ExtraLight));
  g_lightCount = count;
  ++g_lightGen;
  assign_shadow_slots();
  g_lightRangeValid = false;
  g_lastLightCount.store(count, std::memory_order_relaxed);
  gx::g_gxState.dirty |= gx::DirtyUniform;
}

RenderTargetLayout glow_layout() noexcept {
  RenderTargetLayout layout{
      .colorAttachmentCount = 1,
      .colorAttachments = {{{
          .semantic = ColorAttachmentSemantic::SceneColor,
          .format = GlowFormat,
      }}},
      .depthStencilFormat = webgpu::g_graphicsConfig.depthFormat,
      .sampleCount = 1,
  };
  detail::finalize_render_target_layout(layout);
  return layout;
}

// 0224: the depth pre-pass while ambient occlusion is on: the depth plus the interpolated view
// normal (gx shader normal attachment, written by depth-writing draws) for the AO pass; color 0 is
// a write-masked placeholder (the gx fragment output keeps location 0 for the color).
RenderTargetLayout depth_normal_layout() noexcept {
  RenderTargetLayout l{
      .colorAttachmentCount = 2,
      .colorAttachments = {{
          {.semantic = ColorAttachmentSemantic::SceneColor, .format = wgpu::TextureFormat::RGBA8Unorm},
          {.semantic = ColorAttachmentSemantic::Normal, .format = wgpu::TextureFormat::RGBA8Unorm},
      }},
      .depthStencilFormat = webgpu::g_graphicsConfig.depthFormat,
      .sampleCount = 1,
  };
  detail::finalize_render_target_layout(l);
  return l;
}

// The depth pre-pass and the light shadow maps (depth only).
RenderTargetLayout depth_layout() noexcept {
  RenderTargetLayout l{
      .colorAttachmentCount = 0,
      .depthStencilFormat = webgpu::g_graphicsConfig.depthFormat,
      .sampleCount = 1,
  };
  detail::finalize_render_target_layout(l);
  return l;
}

// 0214: the render targets a known pipeline config is drawn into besides the scene. The pipeline
// cache records configs without their target, and the precache built every config for the scene
// (and the offscreen) layout only, so the variants enhanced lighting derives for its own passes
// (the depth pre-pass and light shadow-map casters: depthOnly / lightDepth; the glow buffer: the
// additive draws re-drawn without depth and alpha writes) were compiled on first use: 100-600 ms
// frames in play (pipeline compiles hold Dawn's device lock, which also stalls the game thread).
size_t precache_layouts(const gx::PipelineConfig& c, RenderTargetLayout* out, bool& sceneToo) noexcept {
  size_t n = 0;
  sceneToo = true;
  if (c.shaderConfig.depthOnly || c.shaderConfig.lightDepth) {
    static const RenderTargetLayout depth = depth_layout();
    out[n++] = depth;
    sceneToo = false; // only ever drawn into the depth targets
  } else if (c.colorUpdate && !c.alphaUpdate && !c.depthUpdate && c.dstAlpha == UINT32_MAX &&
             c.blendMode == GX_BM_BLEND && c.blendFacDst == GX_BL_ONE) {
    static const RenderTargetLayout glow = glow_layout();
    out[n++] = glow; // (an additive scene draw of the same shape is also built for the scene)
  }
  return n;
}

bool is_emissive(const gx::PipelineConfig& c) noexcept;

// 0217: the parts of the configuration that select pipeline variants (see precache_variants).
uint32_t precache_bits() noexcept {
  AuroraEnhancedConfig c;
  {
    std::lock_guard lock{g_cfgMutex};
    c = g_pending;
  }
  return precache_bits_for(c);
}

// 0550: precache_bits of any configuration (the standby set's).
uint32_t precache_bits_for(const AuroraEnhancedConfig& c) noexcept {
  // 0451: Enhanced world's surface mask (independent of the master switch; a change rebuilds too).
  const uint32_t world = world_fx::precache_mask() ? PrecacheWorldMask : 0u;
  if (!c.enabled) {
    return world;
  }
  const bool aoRecv = c.ssao && !g_aoPost;
  uint32_t bits = PrecacheEnabled;
  if (c.bloom && c.bloomEmissive > 0.f) {
    bits |= PrecacheGlow;
  }
  const bool pointShadows = c.extraLights && c.extraShadows;
  if (pointShadows || (aoRecv && c.ssaoIntensity > 0.f)) {
    bits |= PrecacheDepth;
  }
  if (pointShadows && c.extraShadowMode == 1) {
    bits |= PrecacheCasters;
  }
  if (c.realisticFlashlight != 0) {
    bits |= PrecacheBeam;
  }
  if (aoRecv) {
    bits |= PrecacheAo;
  }
  if (c.lightReceivers) {
    bits |= PrecacheRecv; // 0340
  }
  return bits | world;
}

// 0217: every pipeline config a game draw with config `c` can lead to while enhanced lighting is
// on with `bits` (precache_bits): its shader classes (shader_class: a config recorded with the
// master switch off is also built as world (1) and ghost-pass (2) variant when it could be one),
// the glow re-draw (note_glow_draw), the depth pre-pass copy and the light shadow-map caster
// (note_draw: dc, lc) and the realistic flashlight's alpha-only cone (ac). Draw classes and
// scene state are not known here, so this errs on the side of building a variant too many.
void precache_variants(const gx::PipelineConfig& c, uint32_t bits,
                       const std::function<void(const gx::PipelineConfig&, uint8_t targets)>& emit) noexcept {
  if (c.shaderConfig.depthOnly || c.shaderConfig.lightDepth) {
    if (bits & PrecacheEnabled) {
      emit(c, PrecacheDepthTarget |
                  (c.shaderConfig.depthOnly && !c.shaderConfig.lightDepth && (bits & PrecacheAo) ? PrecacheDepthNormalTarget : 0));
    }
    return;
  }
  const bool additive = c.blendMode == GX_BM_BLEND && c.blendFacDst == GX_BL_ONE;
  uint8_t classes = 0;
  if ((bits & PrecacheEnabled) == 0) {
    classes = 1u << 0;
  } else {
    classes = 1u << c.shaderConfig.enhClass;
    if (c.shaderConfig.enhClass == 0 && !additive && c.shaderConfig.lineMode == 0 && !c.shaderConfig.wireframe) {
      // shader_class's test on the (canonical) config: textured and lit, a GR16 shadow compare, or
      // an AO receiver (depth-writing). A channel lit but unread by the TEV no longer shows in the
      // canonical config; such a draw is still built when it is first drawn (with its variants).
      bool textured = false, want = (bits & (PrecacheAo | PrecacheRecv)) && c.depthUpdate;
      for (const auto& cc : c.shaderConfig.colorChannels) {
        want = want || cc.lightingEnabled;
      }
      for (u32 i = 0; i < c.shaderConfig.tevStageCount && i < gx::MaxTevStages; ++i) {
        const auto& st = c.shaderConfig.tevStages[i];
        textured = textured || st.texMapId != GX_TEXMAP_NULL;
        want = want || (st.alphaOp.op == GX_TEV_COMP_GR16_GT && st.colorPass.b == GX_CC_TEXC);
      }
      if (textured && want) {
        classes |= (1u << 1) | (1u << 2);
      }
    }
  }
  for (uint8_t cls = 0; cls < 3; ++cls) {
    if ((classes & (1u << cls)) == 0) {
      continue;
    }
    gx::PipelineConfig v = c;
    v.shaderConfig.enhClass = cls;
    // (a recorded glow-shaped config may be a glow re-draw: 0214 builds those for the glow target)
    emit(v, PrecacheScene | PrecacheOffscreen |
                ((bits & PrecacheEnabled) && v.colorUpdate && !v.alphaUpdate && !v.depthUpdate &&
                         v.dstAlpha == UINT32_MAX && additive
                     ? PrecacheGlowTarget
                     : 0));
    if ((bits & PrecacheEnabled) == 0) {
      continue;
    }
    if ((bits & PrecacheGlow) && is_emissive(v)) {
      gx::PipelineConfig gc = v;
      if (gc.depthFunc == GX_LESS) {
        gc.depthFunc = GX_LEQUAL;
      } else if (gc.depthFunc == GX_GREATER) {
        gc.depthFunc = GX_GEQUAL;
      }
      gc.depthUpdate = false;
      gc.alphaUpdate = false;
      gc.colorUpdate = true;
      gc.dstAlpha = UINT32_MAX;
      emit(gc, PrecacheGlowTarget); // only drawn into the glow buffer (not offscreen)
    }
    if ((bits & PrecacheDepth) && v.depthCompare && v.depthUpdate && !additive) {
      gx::PipelineConfig dc = v;
      dc.colorUpdate = false;
      dc.alphaUpdate = false;
      dc.dstAlpha = UINT32_MAX;
      dc.shaderConfig.depthOnly = 1;
      // 0224: with ambient occlusion the pre-pass is drawn with the normal target
      emit(dc, PrecacheDepthTarget | ((bits & PrecacheAo) ? PrecacheDepthNormalTarget : 0));
      if ((bits & PrecacheCasters) && v.shaderConfig.lineMode == 0 && v.shaderConfig.zTexOp == 0) {
        gx::PipelineConfig lc = dc;
        lc.shaderConfig.lightDepth = 1;
        lc.depthCompare = true;
        lc.depthUpdate = true;
        lc.depthFunc = GX_LEQUAL;
        lc.cullMode = GX_CULL_NONE;
        lc.polygonOffsetBits = 0;
        lc.polygonOffsetScaleBits = 0;
        emit(lc, PrecacheDepthTarget);
      }
    }
    if ((bits & PrecacheBeam) && additive && v.alphaUpdate) {
      bool cone = false;
      for (u32 i = 0; i < v.shaderConfig.tevStageCount && i < gx::MaxTevStages && !cone; ++i) {
        const auto tc = v.shaderConfig.tevStages[i].texCoordId;
        cone = tc < gx::MaxTexCoord && v.shaderConfig.tcgs[tc].type == GX_TG_SRTG;
      }
      if (cone) {
        gx::PipelineConfig ac = v;
        ac.colorUpdate = false;
        emit(ac, PrecacheScene); // (not in offscreen passes)
      }
    }
  }
}

// Additive draws: what they add to the picture is light (glows, sparkles, lens/beam sprites,
// fire, lightning effects).
bool is_emissive(const gx::PipelineConfig& c) noexcept {
  if (g_glowDbg >= 3 && g_glowDbg != 4) {
    return c.colorUpdate;
  }
  if (!(c.colorUpdate && c.blendMode == GX_BM_BLEND && c.blendFacDst == GX_BL_ONE &&
        (c.blendFacSrc == GX_BL_SRCALPHA || c.blendFacSrc == GX_BL_ONE || c.blendFacSrc == GX_BL_SRCCLR))) {
    return false;
  }
  // The flashlight beam volume (handlight_ef: its alpha is a color-texgen lookup of the spot
  // light) is a big soft haze already; glowing it again washes the picture out when it points at
  // the camera. Its lens sprite and the volumetric beam option cover the beam's glow.
  for (const auto& tcg : c.shaderConfig.tcgs) {
    if (tcg.type == GX_TG_SRTG) {
      return false;
    }
  }
  return true;
}

void note_glow_draw(const gx::PipelineConfig& config, const gx::DrawData& data) noexcept {
  if (g_glowDraws.size() >= MaxGlowDraws) {
    return;
  }
  gx::PipelineConfig gc = config;
  // The game's own depth function, made inclusive: the glow depth is the scene's (nudged
  // farther), so the draw's surface passes against itself.
  if (gc.depthFunc == GX_LESS) {
    gc.depthFunc = GX_LEQUAL;
  } else if (gc.depthFunc == GX_GREATER) {
    gc.depthFunc = GX_GEQUAL;
  }
  if (g_glowDbg == 5) {
    gc.depthCompare = false;
  }
  if (g_glowDbg == 6) {
    gc.depthFunc = GX_GREATER;
  }
  gc.depthUpdate = false;
  gc.alphaUpdate = false;
  gc.colorUpdate = true;
  gc.dstAlpha = UINT32_MAX;
  static const RenderTargetLayout layout = glow_layout();
  GlowDraw g{.data = data,
             .viewport = current_viewport(),
             .scissor = current_scissor(),
             .vtxCount = data.vtxCount,
             .tevStages = config.shaderConfig.tevStageCount,
             .lit = config.shaderConfig.colorChannels[0].lightingEnabled};
  // The variant follows from the draw's own pipeline: cached by its ref (FIFO thread).
  static std::unordered_map<PipelineRef, PipelineRef> sVariants;
  if (const auto it = sVariants.find(data.pipeline); it != sVariants.end()) {
    g.data.pipeline = it->second;
  } else {
    g.data.pipeline = find_pipeline(gc, layout);
    sVariants.emplace(data.pipeline, g.data.pipeline);
  }
  std::tie(g.pass, g.command) = next_command_position();
  g.data.dstAlpha = UINT32_MAX;
  g_glowDraws.push_back(g);
}

// 0196: a 3x4 matrix as 12 floats (row-major, the layout Aurora stores XF matrices in).
using M34 = std::array<float, 12>;
M34 m34(const Mat3x4<float>& m) noexcept {
  M34 r;
  std::memcpy(r.data(), &m, sizeof(r));
  return r;
}
// a x [b; 0 0 0 1]
M34 m34_mul(const M34& a, const M34& b) noexcept {
  M34 r{};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 4; ++j) {
      float v = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] + a[i * 4 + 2] * b[2 * 4 + j];
      if (j == 3) {
        v += a[i * 4 + 3];
      }
      r[i * 4 + j] = v;
    }
  }
  return r;
}
bool m34_inverse(const M34& m, M34& out) noexcept {
  const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
  const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
  const float det = a * A + b * B + c * C;
  if (!(std::abs(det) > 1e-12f) || !std::isfinite(det)) {
    return false;
  }
  const float s = 1.f / det;
  const float r[9] = {A * s, -(b * i - c * h) * s, (b * f - c * e) * s, B * s, (a * i - c * g) * s, -(a * f - c * d) * s,
                      C * s, -(a * h - b * g) * s, (a * e - b * d) * s};
  for (int k = 0; k < 3; ++k) {
    out[k * 4 + 0] = r[k * 3 + 0];
    out[k * 4 + 1] = r[k * 3 + 1];
    out[k * 4 + 2] = r[k * 3 + 2];
    out[k * 4 + 3] = -(r[k * 3 + 0] * m[3] + r[k * 3 + 1] * m[7] + r[k * 3 + 2] * m[11]);
  }
  return true;
}
// View space -> projective texcoord (s, t, q) of a POS texgen of the current draw, or false when
// it depends on per-vertex matrix indices or is not a linear projective texgen.
bool view_texgen(const gx::ShaderConfig& sc, GXTexCoordID coord, M34& out) noexcept {
  const auto& st = gx::g_gxState;
  const auto& tcg = sc.tcgs[coord];
  if (tcg.type != GX_TG_MTX3x4 || tcg.src != GX_TG_POS || tcg.normalize) {
    return false;
  }
  if (sc.attrs[GX_VA_PNMTXIDX].attrType != GX_NONE || sc.attrs[GX_VA_TEX0MTXIDX + coord].attrType != GX_NONE) {
    return false;
  }
  M34 pinv;
  if (!m34_inverse(m34(st.pnMtx[std::min<u32>(st.currentPnMtx, gx::MaxPnMtx - 1)].pos), pinv)) {
    return false;
  }
  M34 t{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  if (tcg.mtx != GX_IDENTITY) {
    const u32 k = u32(tcg.mtx) / 3;
    if (k < gx::MaxPnMtx) {
      t = m34(st.pnMtx[k].pos);
    } else if (k - gx::MaxPnMtx < gx::MaxTexMtx) {
      t = m34(st.texMtxs[k - gx::MaxPnMtx]);
    } else {
      return false;
    }
  }
  if (tcg.postMtx != GX_PTIDENTITY) {
    const u32 k = (u32(tcg.postMtx) - GX_PTTEXMTX0) / 3;
    if (k >= gx::MaxPTTexMtx) {
      return false;
    }
    t = m34_mul(m34(st.ptTexMtxs[k]), t);
  }
  out = m34_mul(t, pinv);
  for (float v : out) {
    if (!std::isfinite(v)) {
      return false;
    }
  }
  return true;
}
u32 swap_idx(GXTevColorChan c) noexcept { return c == GX_CH_RED ? 0 : c == GX_CH_GREEN ? 1 : c == GX_CH_BLUE ? 2 : 3; }
// The first main-view receiver of the game's shadow map this frame (see BeamShadow).
void capture_beam_shadow(const gx::PipelineConfig& config) noexcept {
  const auto& sc = config.shaderConfig;
  for (u32 i = 1; i < sc.tevStageCount; ++i) {
    const auto& stg = sc.tevStages[i];
    if (stg.alphaOp.op != GX_TEV_COMP_GR16_GT || stg.colorPass.b != GX_CC_TEXC || stg.texMapId == GX_TEXMAP_NULL ||
        stg.texCoordId == GX_TEXCOORD_NULL) {
      continue;
    }
    const auto& prev = sc.tevStages[i - 1];
    if (prev.texMapId == GX_TEXMAP_NULL || prev.texCoordId == GX_TEXCOORD_NULL) {
      continue;
    }
    M34 map, ramp;
    if (!view_texgen(sc, stg.texCoordId, map) || !view_texgen(sc, prev.texCoordId, ramp)) {
      continue;
    }
    const auto& tex = gx::g_gxState.textures[stg.texMapId];
    if (!tex || !tex.ref->sampleTextureView) {
      continue;
    }
    BeamShadow b;
    b.valid = true;
    std::memcpy(b.map, map.data(), sizeof(b.map));
    std::memcpy(b.ramp[0], &ramp[0], 16);
    std::memcpy(b.ramp[1], &ramp[8], 16);
    // Centre of projection: A c = -t over the three rows (singular for an orthographic map).
    M34 inv;
    if (m34_inverse(map, inv)) {
      b.center[0] = inv[3], b.center[1] = inv[7], b.center[2] = inv[11];
      b.centerValid = true;
    }
    b.tex = tex.ref;
    const auto& swap = sc.tevSwapTable[stg.tevSwapTex];
    b.sr = swap_idx(swap.red);
    b.sg = swap_idx(swap.green);
    g_beam = std::move(b);
    return;
  }
}

// The beam is welded to the game's own flashlight cone mesh (handlight_ef, the draw that is
// hidden while the realistic beam is on). Its position matrix (model -> view) is the lens: the
// translation is the lens position and -X of the model is the beam axis (the game builds the
// flashlight GX light from the same matrix: same position, direction = -X, measured in the
// nursery). Frame interpolation blends this matrix with Luigi's hand (it is one of his model
// matrices), so the beam stays on the lens at any frame rate. The spot parameters (cone, colour)
// come from the frame's bright spot light at the lens. (The cone mesh itself is lit by a dummy
// light 7 at (0, 0, 10000): its look does not depend on the flashlight light.)
// The first version took "the bright spot light nearest the camera": in the nursery about one
// scene end in five picked a second spot light 216 units above the lens with the same direction,
// in the gold-ghost room whole stretches picked a light 1100+ units away pointing elsewhere (the
// beam started in mid-air at the far wall), and in interpolated replays the light (blended as a
// light) and the hand (blended as a model matrix) differed by up to ~13 units / 2 degrees.
struct Cone {
  bool valid = false;
  M34 m{};
  uint32_t draws = 0;
};
Cone g_cone;
// Bright spot lights seen on this frame's world draws (candidates for the lens).
std::array<Flashlight, 16> g_spots{};
// Where the game's flashlight light is (its shadow map's eye) when the beam follows the mesh.
Vec4<float> g_shadowRef{};
uint32_t g_spotCount = 0;
// Whether the last frame's cone mesh had the flashlight light at its lens. When it did not (the
// lift between floors, scripted scenes: the game lights the scene with other lights and the
// flashlight is only the mesh), the realistic beam is not drawn and the game's mesh stays visible,
// as in the original.
bool g_coneLit = true;
void note_cone(const gx::PipelineConfig& config, uint32_t vtxCount) noexcept {
  const auto& st = gx::g_gxState;
  static const bool sLog = std::getenv("AURORA_BEAM_LOG") != nullptr;
  ++g_cone.draws;
  if (g_cone.valid) {
    return; // its three materials share one matrix
  }
  // Per-vertex matrix indices: the matrix is not the current one (not seen for handlight_ef).
  if (config.shaderConfig.attrs[GX_VA_PNMTXIDX].attrType != GX_NONE) {
    return;
  }
  const M34 m = m34(st.pnMtx[std::min<u32>(st.currentPnMtx, gx::MaxPnMtx - 1)].pos);
  const float ax = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
  if (!(ax > 1e-6f) || !std::isfinite(ax + m[3] + m[7] + m[11])) {
    return;
  }
  g_cone.valid = true;
  g_cone.m = m;
  if (sLog) {
    Log.info("cone: vtx {} lens {} {} {} axis {} {} {}", vtxCount, m[3], m[7], m[11], -m[0] / ax, -m[4] / ax, -m[8] / ax);
  }
}
// The flashlight of this frame for the beam: the cone mesh's lens and axis with the spot
// parameters of the flashlight's spot light (below). Without a cone mesh (cutscene lights): the
// nearest bright spot light.
void resolve_flash() noexcept {
  // A/B: AURORA_BEAM_NOCONE=1 the first version's choice (nearest bright spot light), =2 that on
  // every other scene end (logged, to pair with the port's LM_BEAM_LOG frame numbers).
  static const int sNoCone = [] {
    const char* v = std::getenv("AURORA_BEAM_NOCONE");
    return v != nullptr ? std::max(std::atoi(v), 1) : 0;
  }();
  static const bool sLog = std::getenv("AURORA_BEAM_LOG") != nullptr;
  const bool skip = sNoCone == 1 || (sNoCone == 2 && (g_sceneEnds & 1) != 0);
  g_shadowRef = g_flash.pos;
  if (sNoCone == 2) {
    Log.info("beam: scene {} {}", g_sceneEnds, skip ? "nearest spot light (first version)" : "cone mesh");
  }
  if (skip) {
    g_coneLit = true;
    return;
  }
  if (!g_cone.valid) {
    // 0220: no cone mesh = no flashlight beam this frame (the flashlight is off: Luigi shocked by
    // the foyer's barrier, A pressed, scripted scenes). The game shows no beam then, so neither does
    // the realistic one. The nearest bright spot light used before is some other light: when the
    // barrier shocks Luigi the game turns the flashlight off and lights him with a white, wide spot
    // light (cos 0.766, distance falloff) pointing back at the camera, and the beam drawn from it
    // filled the screen with a flat white wedge emerging from behind the pillar, unshadowed (the
    // game's shadow map is not rendered from it) and boosted by the switch-on flare.
    g_flash.valid = false;
    return;
  }
  const auto& m = g_cone.m;
  const float ax = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
  const Vec4<float> lens{m[3], m[7], m[11], 1.f};
  const Vec4<float> dir{-m[0] / ax, -m[4] / ax, -m[8] / ax, 0.f};
  // The flashlight light: near the lens and pointing about the same way. During some of Luigi's
  // animations (startled, turning on the spot in the gold-ghost room) the game's light trails the
  // hand by up to ~220 units, smoothly; in the lift and scripted scenes the only spot light is a
  // scene light far away pointing elsewhere (no match: the mesh is the flashlight there).
  int best = -1;
  float bestD = 400.f, bestAng = 0.f;
  for (uint32_t i = 0; i < g_spotCount; ++i) {
    const auto& l = g_spots[i];
    const float dx = l.pos.x() - lens.x(), dy = l.pos.y() - lens.y(), dz = l.pos.z() - lens.z();
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float ll = std::sqrt(l.dir.x() * l.dir.x() + l.dir.y() * l.dir.y() + l.dir.z() * l.dir.z());
    const float c = ll > 1e-6f ? (l.dir.x() * dir.x() + l.dir.y() * dir.y() + l.dir.z() * dir.z()) / ll : -1.f;
    if (c > 0.866f && d < bestD) {
      bestD = d;
      bestAng = std::acos(std::clamp(c, -1.f, 1.f)) * 57.2958f;
      best = int(i);
    }
  }
  if (sLog) {
    float nd = -1.f;
    if (g_flash.valid) {
      const float dx = g_flash.pos.x() - lens.x(), dy = g_flash.pos.y() - lens.y(), dz = g_flash.pos.z() - lens.z();
      nd = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    Log.info("beam: scene {} cone lens, {} spot lights, light at the lens {} ({} units, {} deg); nearest-spot choice off the lens by {} units",
             g_sceneEnds, g_spotCount, best >= 0, best >= 0 ? bestD : -1.f, bestAng, nd);
  }
  g_coneLit = best >= 0;
  if (best < 0) {
    g_flash.valid = false; // the flashlight is only the mesh this frame (shown as the game draws it)
    return;
  }
  Flashlight f = g_spots[best];
  g_shadowRef = f.pos; // the game's shadow map is rendered from its light, not from the mesh
  f.pos = lens;
  f.dir = dir;
  g_flash = f;
}

bool note_draw(const gx::PipelineConfig& config, uint8_t drawClass, uint32_t vtxCount, const gx::DrawData& data) noexcept {
  static const bool sHide = std::getenv("AURORA_ENH_HIDE_ADDITIVE") != nullptr;
  if (sHide && drawClass != AURORA_DRAW_CLASS_OTHER && is_emissive(config)) {
    return true;
  }
  const auto& st = gx::g_gxState;
  g_lastCasterOnly = SIZE_MAX;
  if (drawClass == AURORA_DRAW_CLASS_SHADOW) {
    // 0470: shadow caster only. Opaque depth-writing perspective draws go into the shadow-map atlas
    // of this frame's shadowed lights (like the casters of the world draws below); nothing of it is
    // drawn in the EFB pass or recorded for the pre-pass, AO, glow or world effects.
    static const bool sShow = std::getenv("AURORA_ENH_SHADOW_CLASS_SHOW") != nullptr; // debug: draw them normally too
    const bool pointShadows = g_shadowCount > 0 && g_cfg.extraLights && g_cfg.extraShadows;
    if (pointShadows && shadow_maps() && st.projType == GX_PERSPECTIVE && config.depthCompare && config.depthUpdate &&
        !(config.blendMode == GX_BM_BLEND && config.blendFacDst == GX_BL_ONE) && config.shaderConfig.lineMode == 0 &&
        config.shaderConfig.zTexOp == 0 && g_depthDraws.size() < MaxDepthDraws && !is_offscreen()) {
      static std::unordered_map<PipelineRef, PipelineRef> sCasterOnly;
      GlowDraw g{.data = data, .viewport = current_viewport(), .scissor = current_scissor()};
      g.casterOnly = true;
      g.nrm = ao_forward();
      g.data.dstAlpha = UINT32_MAX;
      if (const auto it = sCasterOnly.find(data.pipeline); it != sCasterOnly.end()) {
        g.lightPipe = it->second;
      } else {
        // The same caster variant as a world draw's (below): one pipeline for both.
        gx::PipelineConfig lc = config;
        lc.colorUpdate = false;
        lc.alphaUpdate = false;
        lc.dstAlpha = UINT32_MAX;
        lc.shaderConfig.depthOnly = 1;
        lc.shaderConfig.lightDepth = 1;
        lc.depthCompare = true;
        lc.depthUpdate = true;
        lc.depthFunc = GX_LEQUAL;
        lc.cullMode = GX_CULL_NONE;
        lc.polygonOffsetBits = 0;
        lc.polygonOffsetScaleBits = 0;
        static const RenderTargetLayout layout = depth_layout();
        g.lightPipe = find_pipeline(lc, layout);
        sCasterOnly.emplace(data.pipeline, g.lightPipe);
      }
      // Not recorded in the pass: no command of its own to refresh merged ranges from (the next
      // recorded command is another draw), its ranges are final.
      g.pass = UINT32_MAX;
      g_lastCasterOnly = g_depthDraws.size();
      g_depthDraws.push_back(g);
      ++g_shadowClassDraws;
    }
    return !sShow;
  }
  ++g_segDraws[std::min<uint8_t>(drawClass, 2)];
  if (st.projType != GX_PERSPECTIVE) {
    drawClass = AURORA_DRAW_CLASS_OTHER; // 2D quads inside the 3D view (ghost-layer composite, screen copies)
  }
  // 0480: and the flames the game draws opaque (material hint EMISSIVE_GLOW).
  if (drawClass != AURORA_DRAW_CLASS_OTHER && g_cfg.bloom && g_cfg.bloomEmissive > 0.f &&
      (is_emissive(config) || (st.materialHint == AURORA_MATERIAL_EMISSIVE_GLOW && config.colorUpdate)) && !is_offscreen()) {
    note_glow_draw(config, data);
  }
  const bool pointShadows = g_shadowCount > 0 && g_cfg.extraLights && g_cfg.extraShadows;
  if (drawClass == AURORA_DRAW_CLASS_WORLD && (pointShadows || ao_forward()) && config.depthCompare && config.depthUpdate && !(config.blendMode == GX_BM_BLEND && config.blendFacDst == GX_BL_ONE) &&
      g_depthDraws.size() < MaxDepthDraws && !is_offscreen()) {
    gx::PipelineConfig dc = config;
    dc.colorUpdate = false;
    dc.alphaUpdate = false;
    dc.dstAlpha = UINT32_MAX;
    dc.shaderConfig.depthOnly = 1; // the enhanced shading of the copy would be wasted (depth only)
    static const RenderTargetLayout layout = depth_layout();
    // 0224: with ambient occlusion the pre-pass also writes the interpolated normals.
    static const RenderTargetLayout normalLayout = depth_normal_layout();
    GlowDraw g{.data = data, .viewport = current_viewport(), .scissor = current_scissor()};
    g.nrm = ao_forward();
    static std::unordered_map<PipelineRef, PipelineRef> sVariants, sNormalVariants;
    auto& variants = g.nrm ? sNormalVariants : sVariants;
    if (const auto it = variants.find(data.pipeline); it != variants.end()) {
      g.data.pipeline = it->second;
    } else {
      g.data.pipeline = find_pipeline(dc, g.nrm ? normalLayout : layout);
      variants.emplace(data.pipeline, g.data.pipeline);
    }
    g.data.dstAlpha = UINT32_MAX;
    // 0480: AURORA_DRAW_CLASS_WORLD_NOCAST draws (a room's shell) are in the pre-pass but cast nothing.
    if (pointShadows && shadow_maps() && config.shaderConfig.lineMode == 0 && config.shaderConfig.zTexOp == 0 && !st.noCast) {
      static std::unordered_map<PipelineRef, PipelineRef> sCasters;
      if (const auto it = sCasters.find(data.pipeline); it != sCasters.end()) {
        g.lightPipe = it->second;
      } else {
        gx::PipelineConfig lc = dc;
        lc.shaderConfig.lightDepth = 1;
        lc.depthCompare = true;
        lc.depthUpdate = true;
        lc.depthFunc = GX_LEQUAL;
        lc.cullMode = GX_CULL_NONE; // single-sided walls and cards still block the light
        lc.polygonOffsetBits = 0;
        lc.polygonOffsetScaleBits = 0;
        g.lightPipe = find_pipeline(lc, layout);
        sCasters.emplace(data.pipeline, g.lightPipe);
      }
    }
    std::tie(g.pass, g.command) = next_command_position();
    g_depthDraws.push_back(g);
  }
  if (g_drawLogFrame != 0 && g_sceneEnds + 1 == g_drawLogFrame && (g_drawLogAll || is_emissive(config))) {
    const auto& sc = config.shaderConfig;
    std::string tex;
    for (u32 i = 0; i < sc.tevStageCount; ++i) {
      const auto& t = sc.tevStages[i];
      tex += fmt::format(" [{}:m{} c{} {}{}{}{} a{}{}{}{}]", i, int(t.texMapId), int(t.texCoordId), int(t.colorPass.a),
                         int(t.colorPass.b), int(t.colorPass.c), int(t.colorPass.d), int(t.alphaPass.a),
                         int(t.alphaPass.b), int(t.alphaPass.c), int(t.alphaPass.d));
    }
    Log.info("draw cls {} proj {} vtx {} blend {} {}/{} z {}{}{} col {} lit {}{}{}{} mat {}{} amb {}{} attn {} nrm {} tg {} "
             "kc {}{}",
             drawClass, int(st.projType), vtxCount, int(config.blendMode), int(config.blendFacSrc),
             int(config.blendFacDst), config.depthCompare, config.depthUpdate, int(config.depthFunc), config.colorUpdate,
             sc.colorChannels[0].lightingEnabled, sc.colorChannels[1].lightingEnabled, sc.colorChannels[2].lightingEnabled,
             sc.colorChannels[3].lightingEnabled, int(sc.colorChannels[0].matSrc), int(sc.colorChannels[2].matSrc),
             int(sc.colorChannels[0].ambSrc), int(sc.colorChannels[2].ambSrc), int(sc.colorChannels[0].attnFn),
             int(sc.attrs[GX_VA_NRM].attrType), int(sc.tcgs[0].type), sc.tevStageCount, tex);
  }
  if (drawClass == AURORA_DRAW_CLASS_OTHER || st.projType != GX_PERSPECTIVE) {
    return false;
  }
  // 0196: realistic flashlight: the shadow map's receivers, and the game's cone mesh (additive,
  // alpha from a colour-texgen lookup of the spot light: handlight_ef) is hidden, the scattering
  // pass draws the beam instead (no double cone).
  bool hide = false;
  if (beam_on() && !is_offscreen()) {
    if (!g_beam.valid) {
      capture_beam_shadow(config);
    }
    static const bool sNoHide = std::getenv("AURORA_BEAM_NOHIDE") != nullptr; // debug: keep the game's cone
    bool cone = false;
    if (config.blendMode == GX_BM_BLEND && config.blendFacDst == GX_BL_ONE) {
      for (u32 i = 0; i < config.shaderConfig.tevStageCount && !cone; ++i) {
        const auto tc = config.shaderConfig.tevStages[i].texCoordId;
        cone = tc != GX_TEXCOORD_NULL && config.shaderConfig.tcgs[tc].type == GX_TG_SRTG;
      }
    }
    if (cone) {
      note_cone(config, vtxCount);
    }
    hide = cone && !sNoHide && g_coneLit;
    // The cone also writes the EFB alpha, which the game's later full-screen passes read (without
    // it the beam area takes the room's blue darkening): keep that write, drop only the colour.
    if (hide && config.alphaUpdate) {
      static std::unordered_map<uint64_t, PipelineRef> sAlphaOnly;
      const auto layout = get_render_target_layout();
      const uint64_t key = uint64_t(data.pipeline) * 0x9E3779B97F4A7C15ull ^ uint64_t(layout.key);
      PipelineRef pipe{};
      if (const auto it = sAlphaOnly.find(key); it != sAlphaOnly.end()) {
        pipe = it->second;
      } else {
        gx::PipelineConfig ac = config;
        ac.colorUpdate = false;
        pipe = find_pipeline(ac, layout);
        sAlphaOnly.emplace(key, pipe);
      }
      gx::DrawData d = data;
      d.pipeline = pipe;
      push_draw_command(d);
    }
  }
  const auto& p = st.proj;
  g_camera = {
      .valid = true,
      .p00 = p.m0.x(), .p11 = p.m1.y(), .p02 = p.m0.z(), .p12 = p.m1.z(), .p03 = p.m0.w(), .p13 = p.m1.w(),
      .a = p.m2.z(), .b = p.m2.w(),
      .vp = st.renderViewport,
  };
  // The flashlight: the bright spot light (GX angular attenuation) lighting world geometry that
  // is nearest to the camera (Luigi holds it; room spot lights are farther away).
  const auto& cc = config.shaderConfig.colorChannels[0];
  if (!cc.lightingEnabled || cc.attnFn != GX_AF_SPOT) {
    return hide;
  }
  const auto mask = st.colorChannelState[0].lightMask.to_ulong();
  for (int i = 0; i < 8; ++i) {
    if ((mask & (1u << i)) == 0) {
      continue;
    }
    const auto& l = st.lights[i];
    const bool angular = std::abs(l.cosAtt.y()) + std::abs(l.cosAtt.z()) > 1e-3f;
    const float lum = 0.2126f * l.color.x() + 0.7152f * l.color.y() + 0.0722f * l.color.z();
    if (!angular || lum < 0.2f) {
      continue;
    }
    bool known = false;
    for (uint32_t k = 0; k < g_spotCount && !known; ++k) {
      known = std::abs(g_spots[k].pos.x() - l.pos.x()) + std::abs(g_spots[k].pos.y() - l.pos.y()) +
                  std::abs(g_spots[k].pos.z() - l.pos.z()) < 1e-3f;
    }
    if (!known && g_spotCount < g_spots.size()) {
      g_spots[g_spotCount++] = {.valid = true, .pos = l.pos, .dir = l.dir, .cosAtt = l.cosAtt, .distAtt = l.distAtt, .color = l.color};
    }
    const float d2 = l.pos.x() * l.pos.x() + l.pos.y() * l.pos.y() + l.pos.z() * l.pos.z();
    if (!g_flash.valid || d2 < g_flashDist2) {
      g_flash = {.valid = true, .pos = l.pos, .dir = l.dir, .cosAtt = l.cosAtt, .distAtt = l.distAtt, .color = l.color};
      g_flashDist2 = d2;
    }
  }
  return hide;
}

void flush_glow() noexcept {
  if (g_glowDraws.empty()) {
    return;
  }
  GlowFlush job{.draws = std::move(g_glowDraws), .clear = !g_glowValid};
  g_glowDraws.clear();
  // Take over what was merged into each draw after it was noted.
  for (auto& g : job.draws) {
    if (const auto* d = gx_draw_at(g.pass, g.command)) {
      g.data.vertRange = d->vertRange;
      g.data.idxRange = d->idxRange;
      g.data.vtxCount = d->vtxCount;
      g.data.indexCount = d->indexCount;
      g.data.instanceCount = d->instanceCount;
    }
  }
  if ((g_glowDbg == 2 || g_glowDbg == 4) && !job.draws.empty()) { // one draw per frame, cycling, logged
    static uint32_t sCycle = 0;
    const uint32_t i = sCycle++ % job.draws.size();
    const auto& g = job.draws[i];
    Log.info("glow dbg scene {}: draw {} of {} vtx {} tev {} lit {} vp {} {} {} {} z {} {}", g_sceneEnds, i,
             job.draws.size(), g.vtxCount, g.tevStages, g.lit, g.viewport.left, g.viewport.top, g.viewport.width,
             g.viewport.height, g.viewport.znear, g.viewport.zfar);
    job.draws = {job.draws[i]};
  }
  const auto size = get_render_target_size();
  const auto [w0, h0] = bloom_base_size(size.x, size.y);
  FxUniform u{};
  u.glowdepth[0] = float(size.x) / float(w0);
  u.glowdepth[1] = float(size.y) / float(h0);
  u.glowdepth[2] = gx::UseReversedZ ? 1.f : 0.f;
  u.glowdepth[3] = 2e-4f;
  job.uniform = push_uniform(reinterpret_cast<const uint8_t*>(&u), sizeof(u));
  if (push_scene_op([job = std::move(job)](const wgpu::CommandEncoder& cmd, const ScenePassTargets& t) {
        encode_glow(cmd, t, job);
      })) {
    g_glowValid = true;
  }
}

// 0340: the shadow cascades of a directional light (ExtraLightDirectional: sun, moon) in its
// atlas cell. Faces 0-3 of the cube layout hold four orthographic cascades (view-depth slices,
// each fitted with a bounding sphere so its size does not change with the camera's rotation, the
// centre snapped to whole texels so the shadow edges do not crawl when the camera moves); faces 4
// and 5 clip everything (w = -1) and carry the cascade data for the lookup: face 4 [16..19] the
// slices' far view depths, [20..23] their texel sizes (world units), face 5 [16..19] their depth
// ranges, [20] the light's angular radius (tangent). xa = direction toward the light + shadow
// distance, xb = (0, 0, S, 1) (w = 1 marks a directional slot for fs_xlcube).
static void directional_records(const ExtraLight& l, uint32_t s, uint32_t cols, uint32_t S, float W, float H, float* rec,
                                float (&xa)[4], float (&xb)[4]) noexcept {
  float d[3] = {l.pos[0], l.pos[1], l.pos[2]};
  float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (!(dl > 1e-6f) || !std::isfinite(dl)) {
    d[0] = 0.f, d[1] = 1.f, d[2] = 0.f, dl = 1.f;
  }
  for (float& v : d) v /= dl;
  const float f[3] = {-d[0], -d[1], -d[2]}; // the light's travel direction
  const float ref[3] = {std::fabs(f[1]) < 0.95f ? 0.f : 1.f, std::fabs(f[1]) < 0.95f ? 1.f : 0.f, 0.f};
  float r[3] = {ref[1] * f[2] - ref[2] * f[1], ref[2] * f[0] - ref[0] * f[2], ref[0] * f[1] - ref[1] * f[0]};
  const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
  for (float& v : r) v /= rl;
  float up[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
  // 0441: axes from the application's world-fixed reference axis and the snapping grid anchored
  // at its world point (lights.h dir_light_anchor), so the grid does not move with the camera.
  float anchor[2] = {0.f, 0.f};
  {
    const float wref[3] = {l.box[7], l.box[8], l.box[9]};
    float wr[3], wu[3];
    if ((wref[0] != 0.f || wref[1] != 0.f || wref[2] != 0.f) && std::isfinite(l.box[10]) && std::isfinite(l.box[11]) &&
        dir_light_axes(f, wref, wr, wu)) {
      for (int a = 0; a < 3; ++a) r[a] = wr[a], up[a] = wu[a];
      anchor[0] = l.box[10], anchor[1] = l.box[11];
    }
  }
  const auto dot3 = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
  const float maxDist = std::clamp(l.radius, 100.f, 100000.f);
  const float back = std::clamp(l.box[6] > 0.f ? l.box[6] : maxDist, 100.f, 100000.f);
  const float camNear = std::max(maxDist * 0.004f, 1.f);
  // Practical split scheme (log / uniform blend).
  float split[4];
  for (int c = 0; c < 4; ++c) {
    const float t = float(c + 1) / 4.f;
    const float lg = camNear * std::pow(maxDist / camNear, t), un = camNear + (maxDist - camNear) * t;
    split[c] = lg * 0.7f + un * 0.3f;
  }
  const float p00 = std::fabs(g_camera.p00) > 1e-4f ? g_camera.p00 : 1.f, p11 = std::fabs(g_camera.p11) > 1e-4f ? g_camera.p11 : 1.f;
  const float fS = float(S);
  // One orthographic cascade into face c of the light's cell: the light-space square of half size
  // R around `ctr` (view space), its centre snapped to whole texels of the world-anchored grid
  // (0441), casters from `back` toward the light. Returns its texel size and depth range.
  const auto cascade = [&](uint32_t c, const float* ctr, float R) {
    const float texel = 2.f * R / fS;
    float cr = dot3(r, ctr), cu = dot3(up, ctr);
    cr = anchor[0] + std::floor((cr - anchor[0]) / texel) * texel;
    cu = anchor[1] + std::floor((cu - anchor[1]) / texel) * texel;
    const float cf = dot3(f, ctr);
    const float dmin = cf - R - back, dmax = cf + R, range = dmax - dmin;
    const float tx = float((s % cols) * 3 * S + (c % 3) * S), ty = float((s / cols) * 2 * S + (c / 3) * S);
    const float cx = 2.f / W * (tx + fS * 0.5f) - 1.f, kx = fS / W;
    const float cy = 1.f - 2.f / H * (ty + fS * 0.5f), ky = fS / H;
    float* o = rec + c * 24;
    for (int a = 0; a < 3; ++a) {
      o[0 + a] = kx / R * r[a];
      o[4 + a] = ky / R * up[a];
      o[8 + a] = (gx::UseReversedZ ? -f[a] : f[a]) / range;
      o[12 + a] = 0.f;
    }
    o[3] = cx - kx / R * cr;
    o[7] = cy - ky / R * cu;
    o[11] = gx::UseReversedZ ? 1.f + dmin / range : -dmin / range;
    o[15] = 1.f;
    o[16] = tx / W * 2.f - 1.f;
    o[17] = 1.f - (ty + fS) / H * 2.f;
    o[18] = (tx + fS) / W * 2.f - 1.f;
    o[19] = 1.f - ty / H * 2.f;
    o[20] = tx;
    o[21] = ty;
    o[22] = tx + fS;
    o[23] = ty + fS;
    return std::pair<float, float>{texel, range};
  };
  for (uint32_t c = 0; c < 4; ++c) {
    const float n0 = c == 0 ? camNear : split[c - 1], n1 = split[c];
    float ctr[3] = {0, 0, 0}, pts[8][3];
    for (int k = 0; k < 8; ++k) {
      const float dz = (k & 4) ? n1 : n0;
      const float nx = (k & 1) ? 1.f : -1.f, ny = (k & 2) ? 1.f : -1.f;
      pts[k][0] = dz * (nx + g_camera.p02) / p00;
      pts[k][1] = dz * (ny + g_camera.p12) / p11;
      pts[k][2] = -dz;
      for (int a = 0; a < 3; ++a) ctr[a] += pts[k][a] / 8.f;
    }
    float R = 0.f;
    for (const auto& p : pts) {
      const float dx = p[0] - ctr[0], dy = p[1] - ctr[1], dz = p[2] - ctr[2];
      R = std::max(R, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    R = std::ceil(R * 1.04f / 8.f) * 8.f; // steady size; room for the filter at the edge
    const auto [texel, range] = cascade(c, ctr, R);
    // 0530: the cascade data moved from face 4 to face 5 (face 4 is the focus cascade).
    rec[5 * 24 + 0 + c] = n1;
    rec[5 * 24 + 4 + c] = texel;
    rec[5 * 24 + 16 + c] = range;
  }
  // 0530: face 4 = the focus cascade (a character's sphere, set_lights' AURORA_SHADOW_FOCUS_ID), or
  // clipped; face 5 stays clipped (its first three rows carry data, the fourth gives w = -1).
  std::fill(rec + 4 * 24, rec + 4 * 24 + 16, 0.f);
  rec[4 * 24 + 15] = -1.f; // w < 0: every caster instance of this face is clipped
  rec[5 * 24 + 12] = rec[5 * 24 + 13] = rec[5 * 24 + 14] = 0.f;
  rec[5 * 24 + 15] = -1.f;
  rec[5 * 24 + 8] = rec[5 * 24 + 9] = rec[5 * 24 + 10] = rec[5 * 24 + 11] = 0.f;
  if (g_focus.valid && !g_focusOff) {
    const float* fc = g_focus.pos;
    const float vz = -fc[2];
    const float fR = std::clamp(g_focus.radius, 4.f, 400.f);
    if (vz > 0.f && vz + fR < maxDist * 0.85f) {
      const auto [texel, range] = cascade(4, fc, fR);
      for (int a = 0; a < 3; ++a) rec[5 * 24 + 8 + a] = fc[a];
      rec[5 * 24 + 11] = fR;
      rec[5 * 24 + 21] = texel;
      rec[5 * 24 + 22] = range;
      rec[5 * 24 + 23] = g_focusSoft;
    }
  }
  rec[5 * 24 + 20] = std::clamp(l.box[5], 0.f, 0.2f);
  xa[0] = d[0], xa[1] = d[1], xa[2] = d[2], xa[3] = maxDist;
  xb[0] = 0.f, xb[1] = 0.f, xb[2] = fS, xb[3] = 1.f;
}

// True: a mask job (with its depth pre-pass) was queued for the current pass.
bool flush_mask() noexcept {
  // The mask is computed before every EFB pass that added opaque world draws, from all of the
  // frame's opaque world draws so far (the pass's own included: they are recorded, not yet run).
  if (g_log && g_depthDraws.size() != g_depthFlushed) {
    Log.info("light shadows: {} lights, {} depth draws ({} new), mask reserved {} camera {}", g_shadowCount,
             g_depthDraws.size(), g_depthDraws.size() - g_depthFlushed, g_maskReserved, g_camera.valid);
    for (uint32_t i = 0; i < g_lightCount; ++i) {
      Log.info("  light {} slot {} pos {} {} {} r {} I {}", i, g_shadowSlot[i], g_lights[i].pos[0], g_lights[i].pos[1],
               g_lights[i].pos[2], g_lights[i].radius, g_lights[i].intensity);
    }
  }
  if (g_depthDraws.size() == g_depthFlushed || !g_maskReserved || !g_camera.valid) {
    return false;
  }
  for (size_t i = g_depthFlushed; i < g_depthDraws.size(); ++i) {
    auto& g = g_depthDraws[i];
    if (const auto* d = gx_draw_at(g.pass, g.command)) {
      g.data.vertRange = d->vertRange;
      g.data.idxRange = d->idxRange;
      g.data.vtxCount = d->vtxCount;
      g.data.indexCount = d->indexCount;
      g.data.instanceCount = d->instanceCount;
    }
  }
  const size_t flushedBefore = g_depthFlushed;
  g_depthFlushed = g_depthDraws.size();
  MaskJob job{.draws = g_depthDraws,
              .maskOffset = 0,
              .maskW = g_maskW,
              .maskH = g_maskH,
              .pitchBytes = g_maskPitch * 4};
  const auto size = get_render_target_size();
  FxUniform u{};
  u.size[0] = float(size.x);
  u.size[1] = float(size.y);
  const auto& vp = g_camera.vp;
  u.vp[0] = vp.left;
  u.vp[1] = vp.top;
  u.vp[2] = std::max(vp.width, 1.f);
  u.vp[3] = std::max(vp.height, 1.f);
  u.proj[0] = g_camera.p00;
  u.proj[1] = g_camera.p11;
  u.proj[2] = g_camera.p02;
  u.proj[3] = g_camera.p12;
  u.projz[0] = g_camera.a;
  u.projz[1] = g_camera.b;
  u.projz[2] = g_camera.p03;
  u.projz[3] = g_camera.p13;
  u.dr[0] = gx::UseReversedZ ? 1.f - vp.zfar : vp.znear;
  u.dr[1] = gx::UseReversedZ ? 1.f - vp.znear : vp.zfar;
  static uint32_t sFrame = 0;
  u.dr[2] = float(sFrame++ % 64);
  const uint32_t q = std::min(g_cfg.extraShadowQuality, 2u);
  u.pass[0] = q == 0 ? 8.f : q == 1 ? 16.f : 32.f;
  u.pass[1] = float(size.x) / float(std::max(g_maskW, 1u));
  for (uint32_t i = 0; i < g_lightCount; ++i) {
    const uint32_t s = g_shadowSlot[i];
    if (s >= MaxShadowLights) {
      continue;
    }
    const auto& l = g_lights[i];
    u.xl[s][0] = l.pos[0];
    u.xl[s][1] = l.pos[1];
    u.xl[s][2] = l.pos[2];
    u.xl[s][3] = l.radius;
  }
  u.pass[2] = float(g_shadowCount);
  u.pass[3] = g_glowDbg == 9 ? 1.f : g_glowDbg == 10 ? 2.f : 0.f;
  job.scaleBack = u.pass[1];
  // 0032: light shadows and/or ambient occlusion in this mask.
  job.point = g_shadowCount > 0 && g_cfg.extraLights && g_cfg.extraShadows;
  job.ao = ao_forward();
  u.ao[0] = std::clamp(g_cfg.ssaoIntensity, 0.f, 2.f);
  u.ao[1] = std::max(g_cfg.ssaoRadius, 1.f);
  u.ao[3] = g_maskH > 900 ? 2.f : 1.f; // 0224: mask texels per AO texel (encode_mask: aoScale)
  if (job.point && shadow_maps()) {
    // Atlas: per light 3x2 faces of S texels. 0187: up to 16 lights in a roughly square grid
    // (cols x 3 ~ rows x 2), within the device's texture size limit (was two columns and 8192).
    // 0223: the grid's columns follow the configured number of shadow-casting lights, not this
    // frame's count, and the atlas only grows (rows): it was sized for the frame's count and
    // reallocated (up to 6144x4096 depth) whenever a light gained or lost its shadow slot (5-6
    // times while the foyer's candles fade in on the first arrival). Tiles keep their size S.
    const uint32_t k = g_shadowCount;
    static const bool sOldAtlas = [] { // AURORA_ENH_ATLAS_GROW=0 (A/B): sized for this frame's count
      const char* v = std::getenv("AURORA_ENH_ATLAS_GROW");
      return v != nullptr && std::atoi(v) == 0;
    }();
    if (sOldAtlas) {
      g_atlasRows = 0;
    }
    const uint32_t kc = sOldAtlas ? k : std::max(k, std::min(g_cfg.extraShadowLights, MaxShadowLights));
    const uint32_t cols = std::max(1u, std::min(kc, uint32_t(std::lround(std::sqrt(double(kc) * 2.0 / 3.0))))),
                   rows = (k + cols - 1) / cols;
    static const uint32_t sMaxTex = [] {
      wgpu::Limits lim{};
      webgpu::g_device.GetLimits(&lim);
      return std::clamp(lim.maxTextureDimension2D, 8192u, 16384u);
    }();
    uint32_t S = std::clamp(g_cfg.extraShadowMapSize, 128u, 2048u);
    while (S > 128 && (cols * 3 * S > sMaxTex || rows * 2 * S > sMaxTex)) {
      S /= 2;
    }
    if (cols != g_atlasCols || S != g_atlasS) {
      g_atlasCols = cols;
      g_atlasS = S;
      g_atlasRows = 0;
    }
    g_atlasRows = std::max(g_atlasRows, std::min(std::max(rows, 1u), std::max(sMaxTex / (2 * S), 1u)));
    const float fS = float(S);
    const float W = float(cols * 3 * S), H = float(std::max(g_atlasRows, rows) * 2 * S);
    // Faces see a little more than 90 degrees, so a filter kernel near a face edge stays on the face.
    const float t = 1.0f + 48.0f / fS;
    static constexpr float kBasis[6][3][3] = {
        // forward, right, up (view space)
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    std::vector<float> rec(size_t(k) * 6 * 24, 0.f);
    for (uint32_t i = 0; i < g_lightCount; ++i) {
      const uint32_t s = g_shadowSlot[i];
      if (s >= MaxShadowLights) {
        continue;
      }
      const auto& l = g_lights[i];
      if ((l.flags & ExtraLightDirectional) != 0) {
        directional_records(l, s, cols, S, W, H, &rec[size_t(s) * 6 * 24], u.xl[s], u.xl[16 + s]);
        continue;
      }
      const float near = std::clamp(l.radius * 0.02f, 3.f, 20.f);
      u.xl[16 + s][0] = near;
      u.xl[16 + s][1] = t;
      u.xl[16 + s][2] = fS;
      for (uint32_t f = 0; f < 6; ++f) {
        const auto& B = kBasis[f];
        const auto row = [&](const float* v) {
          return std::array<float, 4>{v[0], v[1], v[2], -(v[0] * l.pos[0] + v[1] * l.pos[1] + v[2] * l.pos[2])};
        };
        const auto ra = row(B[0]), rr = row(B[1]), ru = row(B[2]);
        const float tx = float((s % cols) * 3 * S + (f % 3) * S), ty = float((s / cols) * 2 * S + (f / 3) * S);
        const float cx = 2.f / W * (tx + fS * 0.5f) - 1.f, kx = fS / (W * t);
        const float cy = 1.f - 2.f / H * (ty + fS * 0.5f), ky = fS / (H * t);
        float* r = &rec[(size_t(s) * 6 + f) * 24];
        for (int c = 0; c < 4; ++c) {
          r[0 + c] = cx * ra[c] + kx * rr[c];
          r[4 + c] = cy * ra[c] + ky * ru[c];
          r[8 + c] = gx::UseReversedZ ? 0.f : ra[c];
          r[12 + c] = ra[c];
        }
        if (gx::UseReversedZ) {
          r[11] = near;
        } else {
          r[11] -= near;
        }
        r[16] = tx / W * 2.f - 1.f;          // NDC x0
        r[17] = 1.f - (ty + fS) / H * 2.f;   // NDC y0 (bottom)
        r[18] = (tx + fS) / W * 2.f - 1.f;   // NDC x1
        r[19] = 1.f - ty / H * 2.f;          // NDC y1 (top)
        r[20] = tx;
        r[21] = ty;
        r[22] = tx + fS;
        r[23] = ty + fS;
      }
    }
    job.cube = true;
    job.lights = k;
    // 0343: sun shafts from the first shadowed directional light.
    if (g_cfg.shafts && g_cfg.shaftIntensity > 0.f) {
      for (uint32_t i = 0; i < g_lightCount; ++i) {
        const uint32_t s = g_shadowSlot[i];
        const auto& l = g_lights[i];
        if (s >= MaxShadowLights || (l.flags & ExtraLightDirectional) == 0) {
          continue;
        }
        u.beam[0][0] = float(s);
        u.beam[0][1] = float(std::clamp(g_cfg.shaftSteps, 8u, 48u));
        u.beam[0][2] = shafts_half() ? 2.f : 1.f; // 0452
        for (int c = 0; c < 3; ++c) {
          u.beam[1][c] = l.color[c] * g_cfg.shaftIntensity;
        }
        job.shafts = true;
        g_shaftsQueued = true;
        break;
      }
    }
    const uint64_t key = uint64_t(g_lightGen) << 32 | uint64_t(S) << 16 | uint64_t(k) << 8 | uint64_t(g_maskW & 0xFF);
    job.firstNew = key == g_maskKey ? flushedBefore : 0;
    g_maskKey = key;
    job.atlasW = uint32_t(W);
    job.atlasH = uint32_t(H);
    job.records = push_storage(reinterpret_cast<const uint8_t*>(rec.data()), rec.size() * sizeof(float));
    u.xl[32][0] = W;
    u.xl[32][1] = H;
    u.xl[32][2] = q == 0 ? 8.f : q == 1 ? 12.f : 20.f; // PCF taps (receiver-plane, rotated Vogel disk)
    u.xl[32][3] = 1.5f; // base filter radius in texels (larger maps: sharper)
    u.xl[33][0] = std::max(g_cfg.extraShadowSoftness, 0.f);
    u.xl[33][1] = gx::UseReversedZ ? 1.f : 0.f;
    u.xl[33][2] = g_focusBias; // 0530: the focus cascade's receiver offset floor (world units)
    // 0540: the extra lights' shadow receiver offset floor (world units).
    u.xl[33][3] = g_xlFloorOverride >= 0.f ? g_xlFloorOverride : std::max(g_cfg.extraShadowOffsetFloor, 0.f);
  }
  if (g_log) {
    Log.info("  mask uniform: size {} {} vp {} {} {} {} proj {} {} {} {} projz {} {} {} {} dr {} {} pass {} {} {}", u.size[0],
             u.size[1], u.vp[0], u.vp[1], u.vp[2], u.vp[3], u.proj[0], u.proj[1], u.proj[2], u.proj[3], u.projz[0],
             u.projz[1], u.projz[2], u.projz[3], u.dr[0], u.dr[1], u.pass[0], u.pass[1], u.pass[2]);
  }
  if (!(job.point && shadow_maps())) {
    // 0210: without light shadow maps (ambient occlusion only), a later mask of this frame with the
    // same layout also adds only its new draws to the pre-pass (was: every draw again).
    const uint64_t key = uint64_t(1) << 63 | uint64_t(g_maskW) << 16 | uint64_t(g_maskH);
    job.firstNew = key == g_maskKey && !job.point ? flushedBefore : 0;
    g_maskKey = key;
  }
  static const bool sFull = std::getenv("AURORA_ENH_MASK_FULL") != nullptr; // A/B: every mask complete
  job.incremental = job.firstNew > 0 && (job.ao || job.cube) && !sFull;
  // 0470: a new caster-only draw changes the atlas without changing the pre-pass depth that the
  // incremental mask looks for: recompute the whole mask then.
  for (size_t i = job.firstNew; i < job.draws.size() && job.incremental; ++i) {
    job.incremental = !job.draws[i].casterOnly;
  }
  u.ao[2] = job.incremental ? 8.f : 0.f;
  job.uniform = push_uniform(reinterpret_cast<const uint8_t*>(&u), sizeof(u));
  std::vector<efb_ram::PassOp> ops;
  ops.emplace_back([job = std::move(job)](const wgpu::CommandEncoder& cmd, detail::FramePacket&) {
    encode_mask(cmd, job);
  });
  return queue_pre_pass_ops(std::move(ops));
}

void flush_shadows() noexcept {
  flush_tiles(flush_mask()); // 0520: the light tiles after the mask (they read its pre-pass depth)
}

void after_submit() noexcept {
  beam_timing_after_submit();
  for (auto& t : g_tslots) {
    if (t.state.load() == 3) {
      const auto* ts = static_cast<const uint64_t*>(t.readback.GetConstMappedRange(0, TimingQueries * 8));
      if (ts != nullptr) {
        for (int k = 0; k < 3; ++k) {
          const uint64_t b = ts[k * 2], e = ts[k * 2 + 1];
          g_tsum[k] += e > b && e - b < 1000000000ull ? double(e - b) * 1e-6 : 0.0;
        }
        g_tcnt += 1;
      }
      t.readback.Unmap();
      t.state.store(0);
      if (g_tcnt >= 240) {
        Log.info("light shadows GPU (per mask, ms): depth pre-pass {:.3f}, shadow-map atlas {:.3f}, mask {:.3f}, total {:.3f}",
                 g_tsum[0] / g_tcnt, g_tsum[1] / g_tcnt, g_tsum[2] / g_tcnt, (g_tsum[0] + g_tsum[1] + g_tsum[2]) / g_tcnt);
        g_tsum[0] = g_tsum[1] = g_tsum[2] = 0;
        g_tcnt = 0;
      }
    }
  }
  for (auto& t : g_tslots) {
    if (t.state.load() == 1) {
      t.state.store(2);
      t.readback.MapAsync(wgpu::MapMode::Read, 0, TimingQueries * 8, wgpu::CallbackMode::AllowSpontaneous,
                          [&t](wgpu::MapAsyncStatus status, wgpu::StringView) {
                            t.state.store(status == wgpu::MapAsyncStatus::Success ? 3 : 0);
                          });
    }
  }
}

void before_efb_copy() noexcept {
  if (g_cfg.enabled) {
    flush_shadows();
    flush_glow();
  }
}

void efb_cleared(float fraction, bool color, bool depth) noexcept {
  struct Reset {
    ~Reset() { g_segDraws[0] = g_segDraws[1] = g_segDraws[2] = 0; }
  } reset;
  if (g_log) {
    Log.info("scene {} EFB clear c{} z{} ({:.0f}%) after {}/{}/{} draws, glow {}", g_sceneEnds, color, depth,
             fraction * 100.f, g_segDraws[0], g_segDraws[1], g_segDraws[2], g_glowValid);
  }
  // A new 3D scene: earlier glow is no longer in the picture.
  if (fraction >= 0.5f && color && depth) {
    g_glowDraws.clear();
    g_glowValid = false;
  }
  if (fraction >= 0.5f && depth) {
    g_depthDraws.clear();
    g_depthFlushed = 0;
  }
}

gx::DrawData* last_caster_only_draw() noexcept {
  if (g_lastCasterOnly >= g_depthDraws.size() || !g_depthDraws[g_lastCasterOnly].casterOnly ||
      g_lastCasterOnly < g_depthFlushed) {
    return nullptr; // (a flushed draw is already in a queued mask job: not extended any more)
  }
  return &g_depthDraws[g_lastCasterOnly].data;
}

void begin_frame() noexcept {
  // 0520: pass indices start again; a job never queued for the last pass is counted lost.
  if (g_tile.valid && g_tile.fresh) {
    ++g_tileLost;
  }
  g_tile.valid = false;
  g_tileUniformPass = UINT32_MAX;
  if (g_tilesAb != 0) {
    const bool off = (g_tileRecFrames++ / g_tilesAb) % 2 == 1;
    if (off != g_tilesAbOff) {
      Log.info("light tiles A/B: {} from recorded frame {}", off ? "off (full loop)" : "on", g_tileRecFrames - 1);
    }
    g_tilesAbOff = off;
  }
  if (g_tileLog != 0 && ++g_tileFrames >= g_tileLog) {
    Log.info("light tiles: {} jobs ({} with pre-pass depth), {} lost in {} frames; {} lights, grid {}x{} of {} px", g_tileJobs,
             g_tileDepthJobs, g_tileLost, g_tileFrames, g_tile.count, g_tile.tx, g_tile.ty, 1u << g_tile.shift);
    g_tileJobs = g_tileDepthJobs = g_tileLost = g_tileFrames = 0;
  }
  g_lastCasterOnly = SIZE_MAX;
  g_statShadowClass.store(g_shadowClassDraws, std::memory_order_relaxed);
  g_shadowClassDraws = 0;
  g_glowDraws.clear();
  g_depthDraws.clear();
  g_depthFlushed = 0;
  g_maskReserved = false;
  g_glowValid = false;
  g_lightRangeValid = false;
  g_camera.valid = false;
  g_flash.valid = false;
  g_beam = {};
  g_cone = {};
  g_spotCount = 0;
  g_anchorValid = false;
  g_focus.valid = false; // 0530
  g_shaftsQueued = false;
}

// 0196: realistic flashlight uniforms (fx.beam, see fs_beam) and target size.
void fill_beam(FxUniform& u, SceneJob& job, uint32_t width, uint32_t height) noexcept {
  const auto& c = g_cfg;
  const auto& f = g_flash;
  const uint32_t q = std::clamp(c.realisticFlashlight, 1u, 3u);
  // Cone edge: where the GX angular polynomial a0 + a1 c + a2 c^2 reaches 0.
  const float a0 = f.cosAtt.x(), a1 = f.cosAtt.y(), a2 = f.cosAtt.z();
  float cut = 0.82f;
  if (std::abs(a2) > 1e-6f) {
    const float disc = a1 * a1 - 4.f * a2 * a0;
    if (disc >= 0.f) {
      const float sd = std::sqrt(disc);
      float best = -2.f;
      for (const float r : {(-a1 + sd) / (2.f * a2), (-a1 - sd) / (2.f * a2)}) {
        if (r > -1.f && r < 0.9999f) {
          best = std::max(best, r);
        }
      }
      if (best > -1.f) {
        cut = best;
      }
    }
  } else if (std::abs(a1) > 1e-6f) {
    cut = std::clamp(-a0 / a1, -0.99f, 0.9999f);
  }
  // Shadow map: only when it was rendered from the flashlight itself (in lit rooms and during
  // lightning the game's shadow light is another light).
  bool shadow = g_beam.valid && g_beam.tex;
  float centerDist = -1.f;
  if (shadow) {
    if (!g_beam.centerValid) {
      shadow = false;
    } else {
      const auto& sp = g_shadowRef;
      const float dx = g_beam.center[0] - sp.x(), dy = g_beam.center[1] - sp.y(), dz = g_beam.center[2] - sp.z();
      centerDist = std::sqrt(dx * dx + dy * dy + dz * dz);
      shadow = centerDist < 120.f;
    }
  }
  if (shadow) {
    std::memcpy(u.beam[0], g_beam.map, sizeof(g_beam.map));
    std::memcpy(u.beam[3], g_beam.ramp, sizeof(g_beam.ramp));
    u.beam[5][0] = 1.f;
    u.beam[5][1] = float(g_beam.sr);
    u.beam[5][2] = float(g_beam.sg);
    u.beam[5][3] = float(g_beam.tex->size.width);
    u.beam[12][0] = float(g_beam.tex->size.height);
    job.beamShadow = g_beam.tex;
  }
  static const float sDensity = [] {
    const char* v = std::getenv("AURORA_BEAM_SIGMA"); // tuning aid: scattering coefficient at density 1
    return v != nullptr ? float(std::atof(v)) : 1.4e-3f;
  }();
  u.beam[6][0] = sDensity * std::clamp(c.beamDensity, 0.f, 3.f);
  u.beam[6][1] = std::clamp(c.beamDust, 0.f, 2.f);
  u.beam[6][2] = q == 1 ? 20.f : q == 2 ? 32.f : 48.f;
  u.beam[6][3] = 1500.f;
  u.beam[7][0] = cut;
  u.beam[7][1] = g_anchorValid ? 1.f : 0.f;
  {
    static const auto sStart = std::chrono::steady_clock::now();
    static const bool sFreeze = std::getenv("AURORA_BEAM_FREEZE_TIME") != nullptr; // test aid: deterministic dust/haze
    u.beam[7][2] = sFreeze ? 0.f : std::fmod(std::chrono::duration<float>(std::chrono::steady_clock::now() - sStart).count(), 3600.f);
  }
  // Flare: the flashlight brighter than its recent steady level (the game's flash).
  {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point sPrev = Clock::now();
    const auto now = Clock::now();
    // AURORA_BEAM_FREEZE_TIME=1 also steps the flare by a fixed 1/30 s per scene end, so frame
    // dumps of separate runs agree (wall-clock steps made the beam brightness differ run to run).
    static const bool sFreezeFlare = std::getenv("AURORA_BEAM_FREEZE_TIME") != nullptr;
    const float dt = sFreezeFlare ? 1.f / 30.f : std::clamp(std::chrono::duration<float>(now - sPrev).count(), 0.f, 0.25f);
    sPrev = now;
    const float level = 0.2126f * f.color.x() + 0.7152f * f.color.y() + 0.0722f * f.color.z();
    if (g_flashLevel <= 0.f) {
      g_flashLevel = level;
    }
    const float rel = level / std::max(g_flashLevel, 1e-3f);
    // Impulses: a ghost stunned by the light (port), the light switched on after a while off.
    const float kick = g_flareKick.exchange(0.f);
    if (g_flashOffTime > 0.4f) {
      g_flarePulse = std::max(g_flarePulse, 0.45f);
    }
    g_flashOffTime = 0.f;
    g_flarePulse = std::max(g_flarePulse * std::exp(-dt * 4.5f), std::clamp(kick, 0.f, 1.f));
    const float target = std::max(std::clamp((rel - 1.04f) * 2.5f, 0.f, 1.f), g_flarePulse);
    g_flare += (target - g_flare) * std::min(1.f, dt * (target > g_flare ? 30.f : 6.f));
    // The steady level follows slowly upward (a flash is short) and quickly downward.
    g_flashLevel += (level - g_flashLevel) * std::min(1.f, dt * (level > g_flashLevel ? 0.6f : 4.f));
    u.beam[7][3] = g_flare;
  }
  for (int k = 0; k < 3; ++k) {
    u.beam[8 + k][0] = g_anchor[k][0], u.beam[8 + k][1] = g_anchor[k][1], u.beam[8 + k][2] = g_anchor[k][2];
  }
  u.beam[11][0] = q >= 2 ? 1.f : 0.6f;
  u.beam[11][1] = 40.f * 40.f;
  u.beam[11][2] = 650.f * 650.f;
  const auto [bw, bh] = beam_size(width, height, q);
  job.beamW = bw;
  job.beamH = bh;
  const float vpH = std::max(g_camera.vp.height, 1.f) * float(bh) / float(std::max(height, 1u));
  u.beam[11][3] = 2.f / (std::max(std::abs(g_camera.p11), 1e-3f) * vpH);
  u.beam[12][1] = q == 1 ? 24.f : q == 2 ? 40.f : 64.f;
  static const bool sShowV = [] { const char* v = std::getenv("AURORA_BEAM_SHOW"); return v != nullptr && std::atoi(v) == 3; }();
  u.beam[12][2] = sShowV ? 1.f : 0.f; // 0220: AURORA_BEAM_SHOW=3 heat map of the scattering
  static const bool sBeamLog = std::getenv("AURORA_BEAM_LOG") != nullptr; // every scene end
  if (sBeamLog) {
    Log.info("beam: col {} {} {} {} cos {} {} {} dist {} {} {} cut {} flare {} level {} shadow {}", f.color.x(), f.color.y(),
             f.color.z(), f.color.w(), a0, a1, a2, f.distAtt.x(), f.distAtt.y(), f.distAtt.z(), cut, g_flare, g_flashLevel,
             shadow);
    Log.info("beam: light pos {} {} {} dir {} {} {} cone draws {}", f.pos.x(), f.pos.y(), f.pos.z(), f.dir.x(), f.dir.y(), f.dir.z(), g_cone.draws);
  }
  if (g_log) {
    static uint32_t sLogged = 0;
    if (sLogged++ % 120 == 0) {
      Log.info("beam: q {} cut {} shadow {} (centre {}, {}x{}) anchor {} flare {} target {}x{}", q, cut, shadow, centerDist,
               shadow ? g_beam.tex->size.width : 0, shadow ? g_beam.tex->size.height : 0, g_anchorValid, g_flare, bw, bh);
    }
  }
}

void scene_end() noexcept {
  const auto& c = g_cfg;
  ++g_sceneEnds;
  flush_shadows();
  flush_glow();
  struct Reset {
    ~Reset() { g_segDraws[0] = g_segDraws[1] = g_segDraws[2] = 0; }
  } reset;
  if (g_log) {
    Log.info("scene end {} after {}/{}/{} draws: {} glow draws, camera {}", g_sceneEnds, g_segDraws[0], g_segDraws[1],
             g_segDraws[2], g_glowDraws.size(), g_camera.valid);
  }
  if (!c.enabled || !g_camera.valid) {
    return;
  }
  SceneJob job;
  if (c.bloom && c.bloomIntensity > 0.f) {
    job.mask |= 1u;
    job.bloomLevels = MaxBloomLevels;
  }
  // 0220: resolved before anything reads g_flash: a frame without the game's cone mesh has no
  // flashlight (no beam of either kind, and it counts as off for the switch-on flare).
  if (beam_on()) {
    resolve_flash();
  }
  if (c.volumetric && c.volumetricIntensity > 0.f && g_flash.valid) {
    job.mask |= 2u;
  }
  if (c.ssao && c.ssaoIntensity > 0.f && g_aoPost) { // 0032: otherwise forward (mask pass)
    job.mask |= 4u;
  }
  // 0196: the realistic flashlight replaces the volumetric beam.
  if (beam_on() && !g_flash.valid) {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point sOff = Clock::now();
    const auto now = Clock::now();
    static const bool sFreezeOff = std::getenv("AURORA_BEAM_FREEZE_TIME") != nullptr; // see fill_beam
    const float dt = sFreezeOff ? 1.f / 30.f : std::clamp(std::chrono::duration<float>(now - sOff).count(), 0.f, 0.25f);
    sOff = now;
    g_flashOffTime += dt;
  }
  static const bool sNoPass = std::getenv("AURORA_BEAM_NOPASS") != nullptr; // debug: no scattering pass
  const bool realistic = beam_on() && g_flash.valid && !sNoPass;
  if (realistic) {
    job.mask = (job.mask & ~2u) | 32u;
    static const bool sShow = std::getenv("AURORA_BEAM_SHOW") != nullptr; // debug: the beam alone
    if (sShow) {
      job.mask |= std::atoi(std::getenv("AURORA_BEAM_SHOW")) == 2 ? 192u : 64u;
    }
  }
  // 0343: sun shafts (the mask pass of this frame wrote them) where no flashlight beam uses t1.
  if (g_shaftsQueued && (job.mask & (2u | 32u)) == 0) {
    job.mask |= 256u;
  }
  // 0342: colour grade.
  const bool gradeOn = c.grade && (c.gradeGain[0] != 1.f || c.gradeGain[1] != 1.f || c.gradeGain[2] != 1.f ||
                                   c.gradeLift[0] != 0.f || c.gradeLift[1] != 0.f || c.gradeLift[2] != 0.f ||
                                   c.gradeSaturation != 1.f || c.gradeContrast != 1.f);
  if (gradeOn) {
    job.mask |= 512u;
  }
  if (job.mask == 0) {
    return;
  }
  const auto size = get_render_target_size();
  static uint32_t sFrame = 0;
  ++sFrame;
  FxUniform u{};
  u.size[0] = float(size.x);
  u.size[1] = float(size.y);
  u.size[2] = 1.f / float(std::max(size.x, 1u));
  u.size[3] = 1.f / float(std::max(size.y, 1u));
  const auto& vp = g_camera.vp;
  u.vp[0] = vp.left;
  u.vp[1] = vp.top;
  u.vp[2] = std::max(vp.width, 1.f);
  u.vp[3] = std::max(vp.height, 1.f);
  {
    // 0225: bloom, the beam and AO are full-screen passes; a main view smaller than the target
    // (the game's unused bordered view) keeps them inside its rectangle. A full-screen view is
    // unchanged (no scissor).
    const float l = std::clamp(vp.left, 0.f, float(size.x)), t = std::clamp(vp.top, 0.f, float(size.y));
    const float r = std::clamp(vp.left + vp.width, l, float(size.x)), b = std::clamp(vp.top + vp.height, t, float(size.y));
    job.clipped = l > 0.5f || t > 0.5f || r < float(size.x) - 0.5f || b < float(size.y) - 0.5f;
    if (job.clipped) {
      job.clip[0] = uint32_t(std::floor(l));
      job.clip[1] = uint32_t(std::floor(t));
      job.clip[2] = uint32_t(std::ceil(r)) - job.clip[0];
      job.clip[3] = uint32_t(std::ceil(b)) - job.clip[1];
    }
  }
  u.proj[0] = g_camera.p00;
  u.proj[1] = g_camera.p11;
  u.proj[2] = g_camera.p02;
  u.proj[3] = g_camera.p12;
  u.projz[0] = g_camera.a;
  u.projz[1] = g_camera.b;
  u.projz[2] = g_camera.p03;
  u.projz[3] = g_camera.p13;
  u.dr[0] = gx::UseReversedZ ? 1.f - vp.zfar : vp.znear;
  u.dr[1] = gx::UseReversedZ ? 1.f - vp.znear : vp.zfar;
  u.dr[2] = float(sFrame % 64);
  job.glowValid = g_glowValid;
  if (g_glowDbg != 0 && (job.mask & 1u) != 0) {
    job.mask |= g_glowDbg == 7 ? 24u : 8u;
  }
  u.dr[3] = float(job.mask);
  if (g_flash.valid) {
    const auto& f = g_flash;
    u.lpos[0] = f.pos.x(), u.lpos[1] = f.pos.y(), u.lpos[2] = f.pos.z(), u.lpos[3] = 1.f;
    u.ldir[0] = f.dir.x(), u.ldir[1] = f.dir.y(), u.ldir[2] = f.dir.z();
    u.lcos[0] = f.cosAtt.x(), u.lcos[1] = f.cosAtt.y(), u.lcos[2] = f.cosAtt.z();
    u.ldist[0] = f.distAtt.x(), u.ldist[1] = f.distAtt.y(), u.ldist[2] = f.distAtt.z();
    u.lcol[0] = f.color.x(), u.lcol[1] = f.color.y(), u.lcol[2] = f.color.z(), u.lcol[3] = f.color.w();
  }
  u.bloom[0] = c.bloomIntensity;
  u.bloom[1] = std::clamp(c.bloomThreshold, 0.f, 2.f);
  u.bloom[2] = std::max(0.5f * u.bloom[1], 0.05f);
  u.bloom[3] = std::clamp(c.bloomFlashlightSuppress, 0.f, 1.f);
  u.bloom2[0] = std::max(c.bloomScene, 0.f);
  u.bloom2[1] = std::max(c.bloomEmissive, 0.f);
  // 0187: the light-glow halos are gone (bloomLightGlow is ignored): extra lights only light
  // surfaces, they never draw anything of their own.
  u.bloom2[2] = 0.f;
  u.bloom2[3] = 0.f;
  {
    const auto [w0, h0] = bloom_base_size(size.x, size.y);
    u.glowdepth[0] = float(size.x) / float(w0);
    u.glowdepth[1] = float(size.y) / float(h0);
  }
  u.glowdepth[2] = gx::UseReversedZ ? 1.f : 0.f;
  u.glowdepth[3] = 2e-4f;
  const uint32_t q = std::min(c.volumetricQuality, 2u);
  u.vol[0] = c.volumetricIntensity;
  u.vol[1] = q == 0 ? 16.f : q == 1 ? 24.f : 48.f;
  u.vol[2] = 3000.f;
  {
    static const auto sStart = std::chrono::steady_clock::now();
    u.vol[3] = std::fmod(std::chrono::duration<float>(std::chrono::steady_clock::now() - sStart).count(), 1000.f);
  }
  job.volDiv = q == 0 ? 4 : 2;
  u.ao[0] = c.ssaoIntensity;
  u.ao[1] = std::max(c.ssaoRadius, 1.f);
  if (realistic) {
    fill_beam(u, job, size.x, size.y);
  }
  // 0342: grade parameters (beam[13..15]: unused by the realistic flashlight).
  for (int k = 0; k < 3; ++k) {
    u.beam[13][k] = gradeOn ? c.gradeGain[k] : 1.f;
    u.beam[14][k] = gradeOn ? c.gradeLift[k] : 0.f;
  }
  u.beam[15][0] = gradeOn ? c.gradeSaturation : 1.f;
  u.beam[15][1] = gradeOn ? c.gradeContrast : 1.f;
  u.beam[15][2] = gradeOn ? std::clamp(c.gradeKeepLights, 0.f, 1.f) : 0.f; // 0421
  job.base = push_uniform(reinterpret_cast<const uint8_t*>(&u), sizeof(u));
  if (realistic) {
    FxUniform pu = u;
    pu.pass[0] = 1.f, pu.pass[1] = 0.f;
    job.beamBlur[0] = push_uniform(reinterpret_cast<const uint8_t*>(&pu), sizeof(pu));
    pu.pass[0] = 0.f, pu.pass[1] = 1.f;
    job.beamBlur[1] = push_uniform(reinterpret_cast<const uint8_t*>(&pu), sizeof(pu));
  }

  // Per-pass uniforms: source texel sizes of the bloom chain and the upsample radius.
  if ((job.mask & 1u) != 0) {
    auto [w, h] = bloom_base_size(size.x, size.y);
    std::array<std::pair<uint32_t, uint32_t>, MaxBloomLevels> dims{};
    for (uint32_t i = 0; i < job.bloomLevels; ++i) {
      dims[i] = {w, h};
      w = std::max(w / 2, 1u);
      h = std::max(h / 2, 1u);
    }
    FxUniform pu = u;
    pu.pass[0] = 1.f / float(size.x);
    pu.pass[1] = 1.f / float(size.y);
    job.passes[0] = push_uniform(reinterpret_cast<const uint8_t*>(&pu), sizeof(pu));
    for (uint32_t i = 1; i < job.bloomLevels; ++i) {
      pu.pass[0] = 1.f / float(dims[i - 1].first);
      pu.pass[1] = 1.f / float(dims[i - 1].second);
      job.passes[i] = push_uniform(reinterpret_cast<const uint8_t*>(&pu), sizeof(pu));
    }
    const float radius = std::clamp(c.bloomRadius, 0.25f, 2.f);
    for (uint32_t i = job.bloomLevels - 1; i > 0; --i) {
      pu.pass[0] = 1.f / float(dims[i].first);
      pu.pass[1] = 1.f / float(dims[i].second);
      pu.pass[2] = radius;
      // Wider levels weigh less with a small radius, more with a large one.
      pu.pass[3] = std::pow(0.7f, float(i)) * (0.6f + 0.4f * radius);
      job.passes[MaxBloomLevels + i] = push_uniform(reinterpret_cast<const uint8_t*>(&pu), sizeof(pu));
    }
  }
  if ((job.mask & 4u) != 0) {
    FxUniform pu = u;
    pu.pass[0] = 1.f / float(std::max(size.x / 2, 1u));
    pu.pass[1] = 1.f / float(std::max(size.y / 2, 1u));
    job.passes[MaxBloomLevels * 2] = push_uniform(reinterpret_cast<const uint8_t*>(&pu), sizeof(pu));
  }
  if (g_log) {
    static uint32_t sLogged = 0;
    if (sLogged++ % 120 == 0) {
      Log.info("scene end: mask {} target {}x{} vp {} {} {} {} proj {} {} {} {} A {} B {} flash {} pos {} {} {} dir {} {} {} "
               "cos {} {} {} dist {} {} {} col {} {} {}",
               job.mask, size.x, size.y, vp.left, vp.top, vp.width, vp.height, u.proj[0], u.proj[1], u.proj[2],
               u.proj[3], u.projz[0], u.projz[1], g_flash.valid, u.lpos[0], u.lpos[1], u.lpos[2], u.ldir[0],
               u.ldir[1], u.ldir[2], u.lcos[0], u.lcos[1], u.lcos[2], u.ldist[0], u.ldist[1], u.ldist[2], u.lcol[0],
               u.lcol[1], u.lcol[2]);
    }
  }
  push_scene_op([job](const wgpu::CommandEncoder& cmd, const ScenePassTargets& t) { encode_scene(cmd, t, job); });
}

// 0223: prewarm (see g_initMutex). The first scene with enhanced lighting built all of this on the
// render thread: the effect module's ten passes, the shadow-map mask, the tile flags and glow depth
// pipelines (1.5 s on the user's first foyer arrival with a fresh shader cache, 50-70 ms warm) and
// some 20 render targets; the GX shaders were precached but these are not GX pipelines.
namespace {
std::mutex g_prewarmMutex; // g_prewarmThread
std::thread g_prewarmThread;

struct PrewarmJob {
  AuroraEnhancedConfig cfg{};
  uint32_t width = 0, height = 0; // EFB render target
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  uint32_t msaa = 1;
  bool targets = true; // 0550: false for the standby set (pipelines only, no memory for targets)
};
std::thread g_standbyPrewarmThread; // 0550 (under g_prewarmMutex)
std::atomic<bool> g_standbyPrewarmRunning{false};

void run_prewarm(const PrewarmJob& j) {
  const auto start = std::chrono::steady_clock::now();
  if (!init_gpu()) {
    return;
  }
  const auto& c = j.cfg;
  const bool ao = c.ssao && !g_aoPost && c.ssaoIntensity > 0.f;
  const bool shadows = c.extraLights && c.extraShadows;
  ensure_composite(j.format, j.msaa);
  ensure_glow_depth_pipe();
  if (c.extraLights && !g_tilesOff) {
    init_tiles(); // 0520
  }
  if (shadows || ao) {
    ensure_mask_pipes(ao);
    init_dirty();
  }
  if (shadows && c.extraShadowMode == 1) {
    init_cube();
  }
  // Render targets at the current size, unless the passes already run (they resize and swap
  // them without the lock; a later resize recreates them anyway).
  uint32_t textures = 0;
  if (j.targets && !g_encoded.load() && j.width > 0 && j.height > 0) {
    const std::lock_guard initLock{g_initMutex};
    const auto rt = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    const auto depthFormat = webgpu::g_graphicsConfig.depthFormat;
    const auto make = [&](Tex& t, uint32_t w, uint32_t h, wgpu::TextureFormat f, wgpu::TextureUsage u, const char* label) {
      if (!t.texture) {
        ensure_tex(t, w, h, f, u, label);
        ++textures;
      }
    };
    make(g_gpu.depth, j.width, j.height, wgpu::TextureFormat::R32Float, rt, "Enhanced scene depth");
    const auto [gw, gh] = bloom_base_size(j.width, j.height);
    if (c.bloom && c.bloomEmissive > 0.f) {
      make(g_gpu.glow, gw, gh, GlowFormat, rt, "Enhanced glow");
      make(g_gpu.glowSrcDepth, j.width, j.height, wgpu::TextureFormat::R32Float, rt, "Enhanced glow scene depth");
      make(g_gpu.glowDepth, gw, gh, depthFormat, wgpu::TextureUsage::RenderAttachment, "Enhanced glow depth");
    }
    if (c.bloom && c.bloomIntensity > 0.f) {
      uint32_t w = gw, h = gh;
      for (uint32_t i = 0; i < MaxBloomLevels; ++i) {
        make(g_gpu.bloom[i], w, h, HdrFormat, rt, "Enhanced bloom");
        w = std::max(w / 2, 1u);
        h = std::max(h / 2, 1u);
      }
    }
    if (c.realisticFlashlight != 0) {
      const auto [bw, bh] = beam_size(j.width, j.height, std::clamp(c.realisticFlashlight, 1u, 3u));
      make(g_gpu.beamTex, bw, bh, HdrFormat, rt, "Realistic flashlight");
      make(g_gpu.beamTmp, bw, bh, HdrFormat, rt, "Realistic flashlight blur");
    }
    if (shadows || ao) {
      const auto [w, h] = mask_size(j.width, j.height, c);
      const auto maskUse = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::TextureBinding;
      make(g_gpu.maskDepth, w, h, depthFormat, rt, "Enhanced light-shadow depth");
      make(g_gpu.maskDepthR, w, h, wgpu::TextureFormat::R32Float, rt | wgpu::TextureUsage::CopySrc, "Enhanced light-shadow depth R");
      make(g_gpu.mask, w, h, wgpu::TextureFormat::RGBA8Unorm, maskUse, "Enhanced light-shadow mask");
      // the incremental mask swaps the depth snapshot with its previous one (encode_mask)
      make(g_gpu.maskDepthPrev, w, h, wgpu::TextureFormat::R32Float, rt | wgpu::TextureUsage::CopySrc, "Enhanced light-shadow depth R");
      make(g_gpu.dirty, (w + 7) / 8, (h + 7) / 8, wgpu::TextureFormat::R8Unorm, rt, "Enhanced mask tile flags");
      if (shadows && c.extraShadowMode == 1) {
        make(g_gpu.mask2, w, h, wgpu::TextureFormat::RGBA8Unorm, maskUse, "Enhanced light-shadow mask 2");
        make(g_gpu.mask3, w, h, wgpu::TextureFormat::RGBA8Unorm, maskUse, "Enhanced light-shadow mask 3");
        make(g_gpu.mask4, w, h, wgpu::TextureFormat::RGBA8Unorm, maskUse, "Enhanced light-shadow mask 4");
      }
      if (ao) {
        // 0224: AO at half the mask's resolution above 900 lines, the pre-pass normal target
        const uint32_t s2 = h > 900 ? 2 : 1, aw = (w + s2 - 1) / s2, ah = (h + s2 - 1) / s2;
        make(g_gpu.maskAo, w, h, wgpu::TextureFormat::RGBA8Unorm, maskUse, "Enhanced ambient occlusion mask");
        make(g_gpu.maskAoRaw, aw, ah, wgpu::TextureFormat::RGBA16Float, rt, "Enhanced ambient occlusion raw");
        make(g_gpu.maskAoDen, aw, ah, wgpu::TextureFormat::RGBA16Float, rt, "Enhanced ambient occlusion denoised");
        make(g_gpu.maskNormal, w, h, wgpu::TextureFormat::RGBA8Unorm, rt, "Enhanced pre-pass normals");
        make(g_gpu.maskNormalColor, w, h, wgpu::TextureFormat::RGBA8Unorm, wgpu::TextureUsage::RenderAttachment,
             "Enhanced pre-pass color placeholder");
      }
    }
  }
  Log.info("prewarm{}: enhanced lighting passes ready in {:.1f} ms ({} render targets at {}x{})",
           j.targets ? "" : " (standby, pipelines only)",
           std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), textures,
           j.width, j.height);
}
} // namespace

bool start_prewarm(std::function<void()> done) noexcept {
  PrewarmJob j;
  {
    std::lock_guard lock{g_cfgMutex};
    j.cfg = g_pending;
  }
  // AURORA_ENH_PREWARM=0 (A/B): build them on first use again.
  static const bool sOff = [] {
    const char* v = std::getenv("AURORA_ENH_PREWARM");
    return v != nullptr && std::atoi(v) == 0;
  }();
  if (!j.cfg.enabled || sOff || !webgpu::g_device) {
    return false;
  }
  j.width = webgpu::g_frameBuffer.size.width;
  j.height = webgpu::g_frameBuffer.size.height;
  j.format = webgpu::g_graphicsConfig.surfaceConfiguration.format;
  j.msaa = std::max(webgpu::g_graphicsConfig.msaaSamples, 1u);
  std::lock_guard lock{g_prewarmMutex};
  if (g_prewarmThread.joinable()) {
    g_prewarmThread.join();
  }
  g_prewarmThread = std::thread([j, done = std::move(done)] {
    run_prewarm(j);
    done();
  });
  return true;
}

bool start_standby_prewarm(const AuroraEnhancedConfig& c, std::function<void()> done) noexcept {
  static const bool sOff = [] {
    const char* v = std::getenv("AURORA_ENH_PREWARM");
    return v != nullptr && std::atoi(v) == 0;
  }();
  if (!c.enabled || sOff || !webgpu::g_device) {
    return false;
  }
  PrewarmJob j;
  j.cfg = c;
  j.format = webgpu::g_graphicsConfig.surfaceConfiguration.format;
  j.msaa = std::max(webgpu::g_graphicsConfig.msaaSamples, 1u);
  j.targets = false;
  std::lock_guard lock{g_prewarmMutex};
  if (g_standbyPrewarmRunning.load()) {
    return false; // (one at a time; the caller's thread never waits for one)
  }
  if (g_standbyPrewarmThread.joinable()) {
    g_standbyPrewarmThread.join(); // finished
  }
  // Its own thread: the passes' init functions take g_initMutex, so it runs alongside a real
  // prewarm safely, and the render thread never joins it.
  g_standbyPrewarmRunning = true;
  g_standbyPrewarmThread = std::thread([j, done = std::move(done)] {
    run_prewarm(j);
    done();
    g_standbyPrewarmRunning = false;
  });
  return true;
}

void stop_prewarm() noexcept {
  std::lock_guard lock{g_prewarmMutex};
  if (g_prewarmThread.joinable()) {
    g_prewarmThread.join();
  }
  if (g_standbyPrewarmThread.joinable()) {
    g_standbyPrewarmThread.join();
  }
}

} // namespace aurora::gfx::enhanced

extern "C" {
void aurora_enhanced_set_config(const AuroraEnhancedConfig* config) {
  using namespace aurora::gfx::enhanced;
  if (config == nullptr) {
    return;
  }
  std::lock_guard lock{g_cfgMutex};
  if (same(*config, g_pending)) {
    return;
  }
  g_pending = *config;
  g_pendingGen.fetch_add(1, std::memory_order_release);
}

void aurora_enhanced_beam_flare(float strength) {
  using namespace aurora::gfx::enhanced;
  float cur = g_flareKick.load();
  while (strength > cur && !g_flareKick.compare_exchange_weak(cur, strength)) {
  }
}

void aurora_enhanced_shadow_stats(uint32_t* lights, uint32_t* casters, uint32_t* skipped) {
  using namespace aurora::gfx::enhanced;
  if (lights != nullptr) {
    *lights = g_statLights.load(std::memory_order_relaxed);
  }
  if (casters != nullptr) {
    *casters = g_statCasters.load(std::memory_order_relaxed);
  }
  if (skipped != nullptr) {
    *skipped = g_statSkipped.load(std::memory_order_relaxed);
  }
}

uint32_t aurora_enhanced_shadow_class_draws(void) {
  return aurora::gfx::enhanced::g_statShadowClass.load(std::memory_order_relaxed);
}

uint32_t aurora_enhanced_last_light_count(void) {
  return aurora::gfx::enhanced::g_lastLightCount.load(std::memory_order_relaxed);
}
}
