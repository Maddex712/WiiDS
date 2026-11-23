// World effects (0350): see include/aurora/world_fx.h.
//
// At GX_AURORA_WORLD_FX_END the EFB pass is sealed (push_scene_op) and, on the frame encoder:
//   1. the scene depth is snapshot into an R32F texture;
//   2. the water / ground draws noted this frame are re-rendered into a surface mask (RGBA8: their
//      shaded colour without blending, the surface kind as the destination alpha) over a depth
//      target filled from that snapshot; water draws write their depth there, so the mask depth is
//      the water surface's (the scene depth under translucent water is the pond bed);
//   3. "surfaces": scene colour + depth + mask -> work (RGBA16F): water reflections (screen-space
//      march through the scene depth, sky fallback with the moon), rain ripples, wet ground with
//      puddles, snow sparkle, distance haze;
//   4. particles (instanced quads, procedural positions in a world-space box around the focus
//      point, depth-tested against the scene depth) blended into work;
//   5. "final": colour grade (+ tilt-shift blur in two separable passes) from work into the EFB.
// Effect time is host time at the scene end and the camera comes from the application XF memory
// (blended by frame interpolation like any matrix), so everything moves smoothly at any frame rate.
#include "world_fx.hpp"

#include "encoding.hpp"
#include "pipeline_cache.hpp"
#include "recording.hpp"
#include "resources.hpp"
#include "tex_copy_conv.hpp"
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
#include <tuple>
#include <unordered_map>
#include <vector>

namespace aurora::gfx::world_fx {
namespace {
Module Log("aurora::world_fx");

// --- configuration (any thread -> FIFO thread) ---
std::mutex g_cfgMutex;
AuroraWorldFxConfig g_pending{};
std::atomic<uint32_t> g_pendingGen{0};
AuroraWorldFxConfig g_cfg{};
uint32_t g_cfgGen = 0;

std::atomic<uint32_t> g_statDraws{0}, g_statEnds{0}, g_statPasses{0};

void sync() noexcept {
  const uint32_t gen = g_pendingGen.load(std::memory_order_acquire);
  if (gen == g_cfgGen) {
    return;
  }
  std::lock_guard lock{g_cfgMutex};
  g_cfg = g_pending;
  g_cfgGen = gen;
}

bool wants_mask(const AuroraWorldFxConfig& c) noexcept {
  return c.reflections || (c.wetGround && c.wetness > 0.f) || (c.rainRipples && c.rain > 0.f) ||
         (c.snowSparkle && c.sparkle > 0.f);
}

// --- per frame (FIFO thread) ---
struct SurfaceDraw {
  gx::DrawData data;
  Viewport viewport;
  ClipRect scissor;
  uint32_t pass = 0, command = 0;
};
constexpr size_t MaxSurfaceDraws = 2048;
std::vector<SurfaceDraw> g_draws;
struct Camera {
  bool valid = false;
  float p00 = 1, p11 = 1, p02 = 0, p12 = 0, p03 = 0, p13 = 0, a = 0, b = 0;
  Viewport vp{};
};
Camera g_camera;
// 0570: planar reflections of characters. A character (AURORA_SURFACE_CHARACTER) is a run of
// recorded commands in one pass, drawn again at the scene end with its projection mirrored through
// the water under it. The anchor (a point of the character in view space, from its first draw's
// position matrix) and the projection rows (as the GX uniform holds them) come from its first draw.
struct CharGroup {
  uint32_t pass = 0, begin = 0, end = UINT32_MAX;
  float anchor[3]{};
  float proj[16]{};
  bool anchored = false;
  bool part = false; // 0580: AURORA_SURFACE_CHARACTER_PART (uses the nearest character's plane)
  bool attach = false; // 0590: AURORA_SURFACE_CHARACTER_ATTACH (a part that is never mirrored alone)
  bool mirror = false;   // 0580: AURORA_SURFACE_MIRROR (a static model; parts never attach to it)
  bool hasLocal = false; // 0580: anchor given in view space (a mirrored static model: its first draw's vertices)
  float local[3]{};
};
constexpr size_t MaxCharGroups = 16; // planes (water lookups)
// 0590: groups noted per frame (characters, parts and attachments: a player alone is up to six:
// body, head, Mii opaque / translucent halves, hat, held tool), more than planes: only whole
// characters and bridges take one
constexpr size_t MaxCharEntries = 64;
constexpr size_t MaxMirrorDraws = 1024;
std::vector<CharGroup> g_chars;
struct MirrorDraw {
  gx::DrawData data;  // with its uniform copy (projection mirrored on the GPU)
  Range original{};   // the draw's own uniform block (the unmirrored copy: character mask)
  uint32_t group = 0;
};
struct PlaneUniform {
  float anchors[MaxCharGroups][4]; // view-space anchor, w = 1
  float rows[MaxCharGroups][4][4]; // projection rows
  float info[4];                   // group count
};
static_assert(sizeof(PlaneUniform) % 16 == 0);
bool planar_on() noexcept {
  static const bool sOff = [] {
    const char* v = std::getenv("AURORA_WORLD_FX_PLANAR");
    return v && v[0] == '0';
  }();
  return !sOff;
}
bool g_cameraFromSurface = false;

constexpr wgpu::TextureFormat MaskFormat = wgpu::TextureFormat::RGBA8Unorm;
constexpr wgpu::TextureFormat WorkFormat = wgpu::TextureFormat::RGBA16Float;

RenderTargetLayout mask_layout() noexcept {
  RenderTargetLayout layout{
      .colorAttachmentCount = 1,
      .colorAttachments = {{{
          .semantic = ColorAttachmentSemantic::SceneColor,
          .format = MaskFormat,
      }}},
      .depthStencilFormat = webgpu::g_graphicsConfig.depthFormat,
      .sampleCount = 1,
  };
  detail::finalize_render_target_layout(layout);
  return layout;
}

// --- GPU (render worker) ---
struct Tex {
  wgpu::Texture texture;
  wgpu::TextureView view;
  uint32_t width = 0, height = 0;
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  uint32_t samples = 1;
};

struct Gpu {
  bool initialized = false, failed = false;
  wgpu::ShaderModule module;
  wgpu::BindGroupLayout bgl;
  wgpu::PipelineLayout layout;
  wgpu::Sampler sampler;
  wgpu::RenderPipeline maskDepth, surfaces, particles, tiltH;
  wgpu::RenderPipeline finalPipe;
  wgpu::TextureFormat finalFormat = wgpu::TextureFormat::Undefined;
  uint32_t finalSamples = 0;
  Tex depth, mask, maskDepthTex, work, tmp, dummy, dummyDepth, dummyZ;
  // 0570: planar character reflections
  wgpu::BindGroupLayout planeBgl;
  wgpu::PipelineLayout planeLayout;
  wgpu::RenderPipeline planePipe;
  wgpu::Buffer planeBuf;
  Tex planeTex, reflColor, reflResolve, reflNormal, reflDepth, reflDepthR;
};
Gpu g_gpu;
std::mutex g_initMutex;

// Uniform block shared by every pass (vec4 slots; see the WGSL struct).
struct FxUniform {
  float size[4];    // target w, h, 1/w, 1/h
  float vp[4];      // main view viewport (target pixels)
  float proj[4];    // p00 p11 p02 p12
  float projz[4];   // A B p03 p13 (raw depth = A + B / z_view)
  float dr[4];      // depth range lo, hi, reversed Z, time (s)
  float view[12];   // world -> view (3 rows)
  float focus[4];   // view-space focus point, valid
  float refl[4];    // reflections strength, march steps, ripple distortion, wetness
  float p1[4];      // rain, sparkle, haze density, haze start
  float hazeCol[4]; // rgb, tilt strength
  float zenith[4];  // rgb, moon
  float horizon[4]; // rgb, tilt band
  float moonDir[4]; // xyz (world), grade
  float ambient[4]; // rgb, grade strength
  float part[4];    // particle kind, count, world scale, mask valid
  float wind[4];    // wind x, z, enables (bit 0 reflections, 1 wet, 2 ripples, 3 sparkle, 4 haze), focus y (px)
  float pass[4];    // per pass: blur direction x, y; final: tilt on, reserved
  float fv[4];      // 0440: view-space focus point from AURORA_WORLD_FX_XF_FOCUS, w = 1 when given
};
static_assert(sizeof(FxUniform) % 16 == 0);

const char* const FxShader = R"(
struct FxU {
  size: vec4f, vp: vec4f, proj: vec4f, projz: vec4f, dr: vec4f,
  view0: vec4f, view1: vec4f, view2: vec4f,
  focus: vec4f, refl: vec4f, p1: vec4f, hazeCol: vec4f, zenith: vec4f, horizon: vec4f,
  moonDir: vec4f, ambient: vec4f, part: vec4f, wind: vec4f, pp: vec4f, fv: vec4f,
};
@group(0) @binding(0) var<uniform> u: FxU;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var tcolor: texture_2d<f32>;
@group(0) @binding(3) var tmask: texture_2d<f32>;
@group(0) @binding(4) var twork: texture_2d<f32>;
@group(0) @binding(5) var tdepth: texture_2d<f32>;
@group(0) @binding(6) var tmdepth: texture_depth_2d;
// 0570: the planar reflection of the characters (its depth; its colour is twork in the surfaces
// pass) and the plane pass's groups (anchor + projection rows per character).
@group(0) @binding(7) var trefld: texture_2d<f32>;
struct PlaneU { anchors: array<vec4f, 16>, rows: array<vec4f, 64>, info: vec4f };
@group(0) @binding(8) var<uniform> pg: PlaneU;

struct VsOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f };
@vertex fn vs_full(@builtin(vertex_index) i: u32) -> VsOut {
  let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
  var o: VsOut;
  o.pos = vec4f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
  o.uv = uv;
  return o;
}

fn hash12(p: vec2f) -> f32 {
  var p3 = fract(vec3f(p.x, p.y, p.x) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
fn hash22(p: vec2f) -> vec2f {
  var p3 = fract(vec3f(p.x, p.y, p.x) * vec3f(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.xx + p3.yz) * p3.zy);
}
fn hash31(n: f32) -> vec3f {
  var p3 = fract(vec3f(n) * vec3f(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.xxy + p3.yzz) * p3.zyx);
}
fn vnoise(p: vec2f) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let w = f * f * (3.0 - 2.0 * f);
  let a = hash12(i);
  let b = hash12(i + vec2f(1.0, 0.0));
  let c = hash12(i + vec2f(0.0, 1.0));
  let d = hash12(i + vec2f(1.0, 1.0));
  return mix(mix(a, b, w.x), mix(c, d, w.x), w.y);
}
fn fbm(p: vec2f) -> f32 {
  return vnoise(p) * 0.55 + vnoise(p * 2.13 + vec2f(5.2, 1.3)) * 0.3 + vnoise(p * 4.37 + vec2f(1.7, 9.2)) * 0.15;
}
fn luma(c: vec3f) -> f32 { return dot(c, vec3f(0.299, 0.587, 0.114)); }

// --- camera ---
fn view_pos(px: vec2f, d: f32) -> vec3f {
  let dn = (d - u.dr.x) / max(u.dr.y - u.dr.x, 1e-6);
  let ndc = vec2f((px.x - u.vp.x) / u.vp.z * 2.0 - 1.0, 1.0 - (px.y - u.vp.y) / u.vp.w * 2.0);
  let den = dn - u.projz.x;
  let z = u.projz.y / select(den, 1e-7, abs(den) < 1e-7);
  let w = -z;
  return vec3f((ndc.x * w - u.proj.z * z - u.projz.z) / u.proj.x, (ndc.y * w - u.proj.w * z - u.projz.w) / u.proj.y, z);
}
// View-space point -> (target px x, y, raw depth).
fn project(p: vec3f) -> vec3f {
  let w = max(-p.z, 1e-4);
  let nx = (u.proj.x * p.x + u.proj.z * p.z + u.projz.z) / w;
  let ny = (u.proj.y * p.y + u.proj.w * p.z + u.projz.w) / w;
  let dn = u.projz.x + u.projz.y / min(p.z, -1e-4);
  return vec3f(u.vp.x + (nx * 0.5 + 0.5) * u.vp.z, u.vp.y + (0.5 - ny * 0.5) * u.vp.w, u.dr.x + dn * (u.dr.y - u.dr.x));
}
fn load_depth(px: vec2f) -> f32 {
  let dims = vec2i(textureDimensions(tdepth));
  return textureLoad(tdepth, clamp(vec2i(px), vec2i(0), dims - vec2i(1)), 0).r;
}
fn load_mdepth(px: vec2f) -> f32 {
  let dims = vec2i(textureDimensions(tmdepth));
  return textureLoad(tmdepth, clamp(vec2i(px), vec2i(0), dims - vec2i(1)), 0);
}
fn view_rot(w: vec3f) -> vec3f { return vec3f(dot(u.view0.xyz, w), dot(u.view1.xyz, w), dot(u.view2.xyz, w)); }
// view -> world direction (transpose of the rotation)
fn world_dir(v: vec3f) -> vec3f { return u.view0.xyz * v.x + u.view1.xyz * v.y + u.view2.xyz * v.z; }
fn world_pos(v: vec3f) -> vec3f {
  let t = vec3f(u.view0.w, u.view1.w, u.view2.w);
  return world_dir(v - t);
}
fn up_view() -> vec3f { return normalize(view_rot(vec3f(0.0, 1.0, 0.0))); }
fn view_ok() -> bool { return dot(u.view0.xyz, u.view0.xyz) > 0.25; }
// The point the effects centre on: the scene under the view's centre column, a little below the
// middle (where the player stands in a third-person view); far / empty -> 1500 units along that ray.
fn focus_v() -> vec3f {
  if (u.fv.w > 0.5) { return u.fv.xyz; }
  let px = vec2f(u.vp.x + u.vp.z * 0.5, u.vp.y + u.vp.w * 0.56);
  let p = view_pos(px, load_depth(px));
  let dir = normalize(view_pos(px, mix(u.dr.x, u.dr.y, 0.5)));
  if (!(p.z < -1.0) || p.z < -8000.0) { return dir * 1500.0; }
  return p;
}
// "One metre" for the effects: an eighth of the view height at the focus distance.
// 0442: with the application's focus point (0440) and a world scale in the configuration, "a metre"
// is that fixed scale: from the focus distance it changed whenever the camera rose or sank (slopes,
// steps between levels), and the particle field, laid out in that unit around a world position,
// slid and zoomed as a whole (falling leaves "freaking out" on slopes).
fn wscale(f: vec3f) -> f32 {
  if (u.fv.w > 0.5 && u.part.z > 1.5) { return u.part.z; }
  return max(2.0 * (-f.z) / max(abs(u.proj.y), 1e-3) / 8.0, 1e-3);
}

// Normal of the surface at px from a depth source (0 scene, 1 mask), the side with the smaller step.
fn depth_normal(px: vec2f, src: i32) -> vec3f {
  var dc: f32; var dl: f32; var dr: f32; var du: f32; var dd: f32;
  if (src == 0) {
    dc = load_depth(px); dl = load_depth(px - vec2f(1.0, 0.0)); dr = load_depth(px + vec2f(1.0, 0.0));
    du = load_depth(px - vec2f(0.0, 1.0)); dd = load_depth(px + vec2f(0.0, 1.0));
  } else {
    dc = load_mdepth(px); dl = load_mdepth(px - vec2f(1.0, 0.0)); dr = load_mdepth(px + vec2f(1.0, 0.0));
    du = load_mdepth(px - vec2f(0.0, 1.0)); dd = load_mdepth(px + vec2f(0.0, 1.0));
  }
  let c = view_pos(px, dc);
  let l = view_pos(px - vec2f(1.0, 0.0), dl);
  let r = view_pos(px + vec2f(1.0, 0.0), dr);
  let t = view_pos(px - vec2f(0.0, 1.0), du);
  let b = view_pos(px + vec2f(0.0, 1.0), dd);
  let dx = select(c - l, r - c, abs(r.z - c.z) < abs(c.z - l.z));
  let dy = select(c - t, b - c, abs(b.z - c.z) < abs(c.z - t.z));
  var n = cross(dy, dx);
  let ln = length(n);
  if (ln < 1e-8) { return vec3f(0.0, 0.0, 1.0); }
  n = n / ln;
  if (dot(n, -c) < 0.0) { n = -n; }
  return n;
}

// --- sky ---
fn sky_color(rv: vec3f, clouds: bool) -> vec3f {
  let rw = normalize(world_dir(rv));
  let h = clamp(rw.y, -0.2, 1.0);
  var c = mix(u.horizon.xyz, u.zenith.xyz, smoothstep(-0.05, 0.65, h));
  // soft clouds (cloud cover in focus.x), drifting slowly
  if (clouds && u.focus.x > 0.0) {
    let cp = rw.xz / max(rw.y + 0.15, 0.1) * 1.6 + vec2f(u.dr.w * 0.004, u.dr.w * 0.0015);
    let cl = smoothstep(0.45, 0.8, fbm(cp) * (0.6 + 0.6 * u.focus.x));
    let cc = mix(vec3f(luma(u.horizon.xyz)) * 1.15 + 0.05, u.horizon.xyz, 0.3);
    c = mix(c, cc, cl * u.focus.x * smoothstep(-0.05, 0.25, h));
  }
  if (u.zenith.w > 0.0) {
    let md = normalize(u.moonDir.xyz);
    let m = max(dot(rw, md), 0.0);
    c += vec3f(1.0, 0.97, 0.85) * (pow(m, 900.0) * 1.6 + pow(m, 60.0) * 0.18) * u.zenith.w;
  }
  return c;
}

// (0570) distance along the ray of the last ssr() hit (1e9: none) and its scene point (view space)
var<private> g_ssrT: f32 = 1e9;
var<private> g_ssrHit: vec3f = vec3f(0.0);
var<private> g_ssrHitPx: vec2f = vec2f(0.0);
var<private> g_ssrCol: vec3f = vec3f(0.0); // (0580) the last hit's scene colour
// Screen-space reflection of view ray `r` from `p`: rgb, a = how much of it is a scene hit.
fn ssr(p: vec3f, r: vec3f, steps: i32, jitter: f32) -> vec4f {
  let dist = max(-p.z, 1.0);
  let maxLen = dist * 1.6;
  // (0580) twice as many, half as long steps as 0570 (the same reach): a thin thing seen edge-on
  // (the side of the bridge deck) fell between two steps for a band of rays, whose edge was the
  // stair-stepped outline of the mirrored deck
  let n = steps * 2;
  var stepLen = maxLen / f32(steps) * 0.5;
  var t = stepLen * (0.3 + 0.7 * jitter);
  var prevT = 0.0;
  var prevDiff = -1.0;
  // (0580) the closest pass in front of a surface above the water (a near miss)
  var nearGap = 1e9;
  var nearThick = 1.0;
  var nearPx = vec2f(0.0);
  var nearT = 0.0;
  g_ssrT = 1e9;
  for (var i = 0; i < n; i++) {
    let q = p + r * t;
    if (q.z > -1.0) { break; }
    let s = project(q);
    if (s.x < u.vp.x || s.y < u.vp.y || s.x >= u.vp.x + u.vp.z || s.y >= u.vp.y + u.vp.w) { break; }
    let sp = view_pos(s.xy, load_depth(s.xy));
    let diff = sp.z - q.z;
    let thick = max(stepLen * 1.5, 0.04 * dist);
    // (0580) The ray went behind the scene between the last step and this one. 0570 took that as
    // a hit when this step lay within `thick` behind the surface, which made every outline a
    // per-pixel yes / no that flipped with the step positions (the stair-stepped edge of the
    // mirrored bridge deck, jagged fragments along a reflection's edges). Now every crossing is
    // refined, and the refined point decides: on a surface it lies on it (a small residual); a
    // ray passing behind an object's outline ends up far behind it, so it marches on (to what
    // lies behind, or the sky). Near the outline the residual grows smoothly, and the hit fades
    // with it: a clean, soft outline instead of steps.
    if (diff > 0.0 && (prevDiff <= 0.0 || diff < thick)) {
      var lo = prevT;
      var hi = t;
      for (var k = 0; k < 6; k++) {
        let mid = 0.5 * (lo + hi);
        let qm = p + r * mid;
        let sm = project(qm);
        let spm = view_pos(sm.xy, load_depth(sm.xy));
        if (spm.z - qm.z > 0.0) { hi = mid; } else { lo = mid; }
      }
      let qh = p + r * hi;
      let sh = project(qh);
      let hp = view_pos(sh.xy, load_depth(sh.xy));
      let res = max(hp.z - qh.z, 0.0);
      // (the depth buffer holds one depth per pixel: on a surface seen at a grazing angle the
      // next pixel is far deeper, so the residual of a real hit is up to that step; without this
      // slack those hits failed in pixel-sized stairs along the mirrored bridge's side)
      let gx = abs(view_pos(sh.xy + vec2f(1.0, 0.0), load_depth(sh.xy + vec2f(1.0, 0.0))).z - hp.z);
      let gy = abs(view_pos(sh.xy + vec2f(0.0, 1.0), load_depth(sh.xy + vec2f(0.0, 1.0))).z - hp.z);
      let tol = thick + 2.0 * min(max(gx, gy), thick * 4.0);
      // (0391) a reflection only shows what is above the reflecting plane. Under translucent
      // water the scene depth is the bed (the sea floor, the terraces' sides below the water
      // line), so rays from the surface "hit" those underwater faces: the striped terrace sides
      // came back as long vertical bands. Hits below the ray's origin plane: march on.
      if (res < tol && dot(hp - p, up_view()) >= -0.01 * dist) {
        g_ssrT = hi;
        g_ssrHit = hp;
        g_ssrHitPx = sh.xy;
        let uv = sh.xy * u.size.zw;
        let col = textureSampleLevel(tcolor, samp, uv, 0.0).rgb;
        // fade near the picture edges, with distance along the ray, and by how sure the hit is
        let e = min(min(sh.x - u.vp.x, u.vp.x + u.vp.z - sh.x), min(sh.y - u.vp.y, u.vp.y + u.vp.w - sh.y));
        let edge = smoothstep(0.0, 0.08 * u.vp.w, e);
        let along = 1.0 - smoothstep(0.6, 1.0, hi / maxLen);
        let sure = 1.0 - smoothstep(0.2 * tol, tol, res);
        g_ssrCol = col;
        return vec4f(col, edge * along * sure);
      }
    }
    if (diff <= 0.0 && -diff < nearGap && dot(sp - p, up_view()) > 0.01 * dist) {
      nearGap = -diff;
      nearThick = thick;
      nearPx = s.xy;
      nearT = t;
    }
    prevDiff = diff;
    prevT = t;
    t += stepLen;
    stepLen *= 1.0296;
  }
  // (0580) No hit: a ray that passed just in front of something (the outline of the mirrored deck,
  // the cliff along a pond's waterline) shows a little of it, fading with the gap, so outlines
  // fade out softly instead of ending in ragged yes / no pieces. Not a hit for the planar
  // characters (g_ssrT stays unset); one on a character is dropped there like a hit.
  // Only at an outline (the depth jumps next to the pixel passed): a ray skimming a smooth surface
  // (the ground beyond a pond) would otherwise smear it over the sky's reflection.
  var fringe = 1.0 - smoothstep(0.0, 1.5 * nearThick, nearGap);
  if (fringe > 0.0) {
    let nz = view_pos(nearPx, load_depth(nearPx)).z;
    var jump = 0.0;
    for (var o = 0; o < 4; o++) {
      let d = vec2f(select(-2.0, 2.0, (o & 1) == 1) * select(1.0, 0.0, o >= 2), select(-2.0, 2.0, (o & 1) == 1) * select(0.0, 1.0, o >= 2));
      jump = max(jump, abs(view_pos(nearPx + d, load_depth(nearPx + d)).z - nz));
    }
    fringe *= smoothstep(nearThick, 4.0 * nearThick, jump);
  }
  if (fringe > 0.0) {
    g_ssrHitPx = nearPx;
    let e = min(min(nearPx.x - u.vp.x, u.vp.x + u.vp.z - nearPx.x), min(nearPx.y - u.vp.y, u.vp.y + u.vp.w - nearPx.y));
    let edge = smoothstep(0.0, 0.08 * u.vp.w, e);
    let along = 1.0 - smoothstep(0.6, 1.0, nearT / maxLen);
    let col = textureSampleLevel(tcolor, samp, nearPx * u.size.zw, 0.0).rgb;
    return vec4f(col, 0.6 * fringe * edge * along);
  }
  return vec4f(0.0);
}
fn ssr_why(p: vec3f, r: vec3f, steps: i32) -> vec4f {
  let dist = max(-p.z, 1.0);
  let maxLen = dist * 1.6;
  var stepLen = maxLen / f32(steps);
  var t = stepLen * 0.65;
  var minDiff = 1e9;
  for (var i = 0; i < steps; i++) {
    let q = p + r * t;
    if (q.z > -1.0) { return vec4f(0.25, f32(i) / f32(steps), 0.0, 1.0); }
    let s = project(q);
    if (s.x < u.vp.x || s.y < u.vp.y || s.x >= u.vp.x + u.vp.z || s.y >= u.vp.y + u.vp.w) {
      return vec4f(0.5, f32(i) / f32(steps), clamp(minDiff / 100.0 * 0.5 + 0.5, 0.0, 1.0), 1.0);
    }
    let sp = view_pos(s.xy, load_depth(s.xy));
    let diff = sp.z - q.z;
    if (abs(diff) < abs(minDiff)) { minDiff = diff; }
    let thick = max(stepLen * 1.5, 0.04 * dist);
    if (diff > 0.0 && diff < thick) { return vec4f(1.0, f32(i) / f32(steps), 0.0, 1.0); }
    t += stepLen;
    stepLen *= 1.06;
  }
  return vec4f(0.75, 1.0, clamp(minDiff / 100.0 * 0.5 + 0.5, 0.0, 1.0), 1.0);
}
// rgb, a = scene hit share (0570)
fn reflection(p: vec3f, n: vec3f, steps: i32, jitter: f32, march: bool) -> vec4f {
  let v = normalize(p);
  let r = reflect(v, n);
  let sky = sky_color(r, true);
  if (!march) { return vec4f(sky, 0.0); }
  let h = ssr(p + n * (0.002 * max(-p.z, 1.0)), r, steps, jitter);
  return vec4f(mix(sky, h.rgb, h.a), h.a);
}
fn schlick(cosT: f32, f0: f32) -> f32 { return f0 + (1.0 - f0) * pow(1.0 - clamp(cosT, 0.0, 1.0), 5.0); }

// Rain ripples: perturbation of a flat normal (world xz) by expanding rings, two layers.
fn ripples(wp: vec2f, t: f32, rate: f32, cell: f32) -> vec2f {
  var g = vec2f(0.0);
  for (var layer = 0; layer < 2; layer++) {
    let sc = cell * select(1.0, 0.73, layer == 1);
    let q = wp / sc + vec2f(f32(layer) * 17.3, f32(layer) * 5.1);
    let base = floor(q);
    for (var oy = -1; oy <= 1; oy++) {
      for (var ox = -1; ox <= 1; ox++) {
        let id = base + vec2f(f32(ox), f32(oy));
        let h = hash22(id);
        let ph = fract(t * rate * (0.7 + 0.6 * h.x) + h.y);
        let ctr = id + 0.2 + 0.6 * hash22(id + 3.7);
        let d = q - ctr;
        let dl = length(d);
        let rad = ph * 1.1;
        let x = (dl - rad) * 9.0;
        let ring = sin(x * 3.14159) * exp(-x * x) * (1.0 - ph) * (1.0 - ph);
        g += select(vec2f(0.0), d / max(dl, 1e-4) * ring, dl < 1.4);
      }
    }
  }
  return g;
}

fn tangent_bend(n: vec3f, gw: vec2f) -> vec3f {
  // gw is a gradient in the world xz plane: tilt the normal by it (in view space)
  let tx = view_rot(vec3f(1.0, 0.0, 0.0));
  let tz = view_rot(vec3f(0.0, 0.0, 1.0));
  return normalize(n + tx * gw.x + tz * gw.y);
}

// (0570) Plane pass: one RGBA32F texel per projection row of each character (x = 4 group + row):
// the water height under the character from the water surface's depth around its anchor on the
// screen (rivers, ponds and the sea lie at different heights), and its projection rows mirrored
// through that plane (and flipped vertically on the screen, which restores the triangle winding
// the mirror reversed, so the draws keep their own pipelines). No water near it: a row set that
// clips every vertex (w = -1).
@fragment fn fs_plane(in: VsOut) -> @location(0) vec4f {
  let x = u32(in.pos.x);
  let g = x / 4u;
  let ri = x % 4u;
  let none = select(vec4f(0.0), vec4f(0.0, 0.0, 0.0, -1.0), ri == 3u);
  if (g >= u32(pg.info.x)) { return none; }
  let a = pg.anchors[g].xyz;
  if (a.z > -1.0) { return none; }
  let up = up_view();
  let dist = -a.z;
  let s = project(a).xy;
  let rad = 0.14 * u.vp.w;
  var hs = 0.0;
  var wsum = 0.0;
  // (0580: down to 3.6 x rad below the anchor, was 1.6: a player standing back from the edge of a
  // cliff above the water has the water further down the screen, and the head's own window found
  // none while the body's did)
  for (var j = 0; j < 24; j++) {
    for (var i = 0; i < 12; i++) {
      let q = s + vec2f(mix(-rad, rad, f32(i) / 11.0), mix(-0.4 * rad, 3.6 * rad, f32(j) / 23.0));
      if (q.x < u.vp.x || q.y < u.vp.y || q.x >= u.vp.x + u.vp.z || q.y >= u.vp.y + u.vp.w) { continue; }
      if (textureLoad(tmask, vec2i(q), 0).a < 0.75) { continue; }
      let pw = view_pos(q, load_mdepth(q));
      let d = pw - a;
      let hd = length(d - dot(d, up) * up) / (0.08 * dist);
      let w = 1.0 / (1.0 + hd * hd);
      hs += dot(pw, up) * w;
      wsum += w;
    }
  }
  if (wsum < 1e-3) { return none; }
  let h = hs / wsum;
  let above = dot(a, up) - h;
  if (above < -0.02 * dist || above > 0.4 * dist) { return none; }
  let row = pg.rows[g * 4u + ri];
  let dn = dot(row.xyz, up);
  let m = vec4f(row.xyz - 2.0 * dn * up, row.w + 2.0 * h * dn);
  return select(m, -m, ri == 1u);
}
fn refl_depth(q: vec2f) -> f32 {
  let dims = vec2i(textureDimensions(trefld));
  return textureLoad(trefld, clamp(vec2i(q), vec2i(0), dims - vec2i(1)), 0).r;
}
// (0570) The characters as the camera sees them (the unmirrored copy, half resolution, below the
// planar image in the same target): a character is visible at scene pixel q (or one mask texel
// beside it: the half-size copy loses thin gaps, e.g. between the legs).
fn char_at(q: vec2f) -> bool {
  let zs = view_pos(q, load_depth(q)).z;
  let tol = 0.02 * max(-zs, 1.0);
  let c = q * 0.5 + vec2f(0.0, u.size.y);
  for (var k = 0; k < 5; k++) {
    let o = select(select(select(select(vec2f(0.0), vec2f(1.0, 0.0), k == 1), vec2f(-1.0, 0.0), k == 2), vec2f(0.0, 1.0), k == 3),
                   vec2f(0.0, -1.0), k == 4);
    let du = refl_depth(c + o);
    if (refl_covered(du) && abs(view_pos(q, du).z - zs) < tol) { return true; }
  }
  return false;
}
fn refl_covered(d: f32) -> bool {
  if (u.dr.z > 0.5) { return d > 0.00001; }
  return d < 0.99999;
}

@fragment fn fs_maskdepth(in: VsOut) -> @builtin(frag_depth) f32 {
  let d = load_depth(floor(in.pos.xy) + vec2f(0.5));
  if (u.dr.z > 0.5) { return d * (1.0 - 2e-5); }
  return 1.0 - (1.0 - d) * (1.0 - 2e-5);
}

@fragment fn fs_surfaces(in: VsOut) -> @location(0) vec4f {
  let px = floor(in.pos.xy) + vec2f(0.5);
  let uv = px * u.size.zw;
  var col = textureSampleLevel(tcolor, samp, uv, 0.0).rgb;
  let en = u32(u.wind.z);
  let d = load_depth(px);
  let p = view_pos(px, d);
  let t = u.dr.w;
  let fv = focus_v();
  let ws = wscale(fv);
  let jitter = 0.5;
  let steps = i32(u.refl.y);
  if (u.pp.y > 9.5 && u.pp.y < 10.5 && u.pp.w > 0.5) { // debug view 10: the planar character reflection as drawn
    let tq = vec2f(px.x, 2.0 * u.vp.y + u.vp.w - px.y);
    return vec4f(select(col * 0.3, textureSampleLevel(twork, samp, tq / vec2f(textureDimensions(twork)), 0.0).rgb, refl_covered(refl_depth(tq))), 1.0);
  }
  if (u.part.w > 0.5 && view_ok()) {
    let m = textureLoad(tmask, vec2i(px), 0);
    let water = m.a > 0.75;
    let ground = m.a > 0.3 && m.a < 0.75;
    if (water && ((en & 1u) != 0u || (en & 4u) != 0u)) {
      let pw = view_pos(px, load_mdepth(px));
      var n = up_view();
      let up = up_view();
      // the water plane: mostly its depth normal, held towards up (flat water)
      if ((en & 1u) != 0u) {
        // the game's own water pattern bends the reflection (luminance gradient of the mask)
        let o = max(u.size.y * 0.004, 1.5);
        let lx = luma(textureSampleLevel(tmask, samp, (px + vec2f(o, 0.0)) * u.size.zw, 0.0).rgb) -
                 luma(textureSampleLevel(tmask, samp, (px - vec2f(o, 0.0)) * u.size.zw, 0.0).rgb);
        let ly = luma(textureSampleLevel(tmask, samp, (px + vec2f(0.0, o)) * u.size.zw, 0.0).rgb) -
                 luma(textureSampleLevel(tmask, samp, (px - vec2f(0.0, o)) * u.size.zw, 0.0).rgb);
        n = tangent_bend(n, vec2f(lx, ly) * 0.35 * u.refl.z);
      }
      let wpos = world_pos(pw);
      if ((en & 4u) != 0u && u.p1.x > 0.0) {
        n = tangent_bend(n, ripples(wpos.xz, t, 0.55 + 0.9 * u.p1.x, ws * 0.55) * 0.35 * min(u.p1.x * 1.5, 1.0));
      }
      if ((en & 1u) != 0u) {
        let rh = reflection(pw, n, steps, jitter, true);
        var refl = rh.rgb;
        // (0570) planar reflection of the characters: where one is mirrored over this water pixel
        // it replaces the screen-space march (which can only put the camera's view of a character
        // back upside down, in pieces), unless the march hit the world in front of it. The water's
        // own bend (pattern, ripples) moves it like the march's rays.
        if (u.pp.w > 0.5) {
          let fy = 2.0 * u.vp.y + u.vp.w;
          // A march hit on a character (its scene pixel shows the character: the unmirrored copy)
          // is dropped for the sky: the camera's view of a character, put back upside down in
          // pieces (the face apart from the head, a cap of hair, slivers at its outline). The
          // planar character below takes its place where it lies.
          var ssrWorld = rh.a > 0.0;
          if (ssrWorld && char_at(g_ssrHitPx)) {
            refl = sky_color(reflect(normalize(pw), n), true);
            ssrWorld = false;
          }
          let d0 = refl_depth(vec2f(px.x, fy - px.y));
          if (refl_covered(d0)) {
            let mp = view_pos(px, d0);
            let pathLen = max(length(mp) - length(pw), 0.0);
            let vd = normalize(pw);
            let dl = (reflect(vd, n) - reflect(vd, up)) * pathLen;
            let sp = project(mp + dl - 2.0 * dot(dl, up) * up).xy;
            // (0580) the bend grows with the path below the surface: a player on a cliff top is
            // mirrored far below it, and the water pattern's bend moved the planar picture by up to
            // tens of pixels, smearing the Mii face's thin lines (eyes, glasses, mouth) into
            // streaks while the plain head around them looked clean. Limit it to a ripple's worth.
            let tq0 = vec2f(px.x, fy - px.y);
            let off = vec2f(sp.x, fy - sp.y) - tq0;
            let cap = 0.002 * u.vp.w;
            let tq = tq0 + off * min(1.0, cap / max(length(off), 1e-4));
            var world = ssrWorld && g_ssrT < pathLen * 0.9;
            let ssrFront = world;
            if (!world) {
              // The march may step over a thin thing between the water and the character (the deck
              // of a bridge the player stands on): a short march toward the character's own point
              // finds it and shows that instead of the character (it would hide it in a mirror).
              let pc = mp - 2.0 * dot(mp - pw, up) * up;
              let dist = max(-pw.z, 1.0);
              for (var k = 1; k < 13; k++) {
                let qk = mix(pw, pc, f32(k) / 13.0);
                if (qk.z > -1.0) { break; }
                let sk = project(qk).xy;
                if (sk.x < u.vp.x || sk.y < u.vp.y || sk.x >= u.vp.x + u.vp.z || sk.y >= u.vp.y + u.vp.w) { break; }
                let spk = view_pos(sk, load_depth(sk));
                let dk = spk.z - qk.z;
                // (0580) ...or the ground the character stands on: a player on a cliff top above the
                // water, back from the edge, has the path to its feet run through the cliff, often
                // further behind the visible cliff top / face than the thin-occluder slack; a point
                // behind a surface that also lies below it is inside the ground (the mirrored cliff
                // hides the mirrored feet, and only the head shows, further out).
                let behind = dk > 0.0 && (dk < 0.04 * dist || (dk < 0.3 * dist && dot(qk - spk, up) < 0.0));
                if (behind && dot(spk - pw, up) > 0.01 * dist && !char_at(sk)) {
                  refl = textureSampleLevel(tcolor, samp, sk * u.size.zw, 0.0).rgb;
                  world = true;
                  break;
                }
              }
            }
            if (refl_covered(refl_depth(tq))) {
              let pcol = textureSampleLevel(twork, samp, tq / vec2f(textureDimensions(twork)), 0.0).rgb;
              if (!world) {
                refl = pcol;
              } else if (ssrFront) {
                // (0580) a march hit in front fades by how sure it is (its outline): over the
                // character, not over the sky
                refl = mix(pcol, g_ssrCol, rh.a);
              }
            }
          }
          if (u.pp.y > 8.5 && u.pp.y < 9.5) { // debug view 9: planar here, march hit dropped as a character, march hit
            return vec4f(select(0.0, 1.0, refl_covered(d0)), select(0.0, 1.0, rh.a > 0.0 && !ssrWorld), select(0.0, 1.0, rh.a > 0.0), 1.0);
          }
        }
        let v = normalize(-pw);
        let f = schlick(dot(v, n), 0.04);
        // (0391) shallow water: the game draws the sea / river over its bed with a low alpha near
        // the shore (the sand shows through); the mask keeps only the water's colour, so weight
        // the reflection by how far the bed lies behind the surface along the view axis (mask
        // depth = surface, scene depth = bed): full from 1.5 "metres", a trace at the waterline.
        let bedDepth = max(pw.z - p.z, 0.0);
        let deep = smoothstep(0.0, 1.5 * ws, bedDepth);
        // (0580) ...or by how blue the water pixel is: a pond drawn over a bed right behind its
        // surface is still opaque blue water, while the sea's surf over the sand shows the sand
        // (blue share of the scene colour: surf 0.34-0.45, pond 0.59-0.73, open sea 0.54-0.61, from
        // noon to night at the beach and the pond by the player's house). Blue shallow water is
        // lifted to 60 % (0570's planar character there): at full strength the sky washed the
        // ponds out.
        let bs = col.b / max(col.r + col.g + col.b, 0.02);
        let water = max(deep, 0.6 * smoothstep(0.44, 0.56, bs));
        // One ramp for the world and the planar characters (0570 kept a character at 60 % at the
        // waterline, so it lay over the sand-coloured surf at the beach while the bank beside it
        // faded): faint over the surf, full over deep or opaque water, no line where it changes.
        let k = u.refl.x * (0.35 + 0.65 * f) * mix(0.1, 1.0, water);
        if (u.pp.y > 10.5 && u.pp.y < 11.5) { // debug view 11: blue share, depth ramp, water weight
          return vec4f(bs, deep, water, 1.0);
        }
        if (u.pp.y > 4.5) { // debug view 5: scene hit share of the reflection, its weight, normal tilt
          if (u.pp.y > 7.5) { // 8: why the march ends (r: 0.25 behind camera, 0.5 off screen, 0.75 no hit, 1 hit; g: step; b: closest miss)
            return ssr_why(pw + n * (0.002 * max(-pw.z, 1.0)), reflect(-v, n), steps);
          }
          let h = ssr(pw + n * (0.002 * max(-pw.z, 1.0)), reflect(-v, n), steps, jitter);
          return vec4f(h.a, k, 1.0 - dot(n, up_view()), 1.0);
        }
        // the reflection replaces part of the water's own colour, keeping its tint
        let tint = m.rgb / max(luma(m.rgb), 0.05);
        let r2 = mix(refl, refl * clamp(tint, vec3f(0.0), vec3f(2.0)), 0.35);
        col = mix(col, r2, clamp(k, 0.0, 0.85));
      } else {
        // ripples without reflections: a light sheen on the rings
        let v = normalize(-pw);
        col += sky_color(reflect(-v, n), false) * 0.12 * (1.0 - dot(n, up));
      }
    } else if (ground) {
      let wet = select(0.0, u.refl.w, (en & 2u) != 0u);
      let wpos = world_pos(p);
      if (u.pp.y > 3.5) { return vec4f(1.0, wet, fract(wpos.x / (ws * 4.0)), 1.0); } // debug view 4: ground branch
      if (wet > 0.001) {
        let green = clamp((col.g - max(col.r, col.b)) * 5.0, 0.0, 1.0);
        let dirt = 1.0 - green;
        // puddles: world-anchored noise, a few distinct pools with crisp edges
        let nz = fbm(wpos.xz / (ws * 1.8) + vec2f(3.1, 7.7));
        let thr = 1.0 - wet * 0.36;
        let puddle = smoothstep(thr, thr + 0.025, nz) * mix(0.25, 1.0, dirt) * smoothstep(0.2, 0.6, wet);
        let dk = wet * mix(0.5, 1.0, dirt);
        let base = mix(col, pow(col, vec3f(1.25)) * 0.86, dk);
        let up = up_view();
        var n = normalize(mix(depth_normal(px, 0), up, 0.6));
        let flatness = dot(n, up);
        var pn = up;
        if ((en & 4u) != 0u && u.p1.x > 0.0) {
          pn = tangent_bend(up, ripples(wpos.xz, t, 0.55 + 0.9 * u.p1.x, ws * 0.45) * 0.3 * min(u.p1.x * 1.5, 1.0));
        }
        let v = normalize(-p);
        let pud = puddle * smoothstep(0.85, 0.97, flatness);
        var refl = vec3f(0.0);
        if (pud > 0.03) {
          refl = reflection(p, pn, max(steps / 2, 8), jitter, (en & 1u) != 0u).rgb;
        } else {
          refl = sky_color(reflect(-v, n), false);
        }
        let f = schlick(dot(v, select(n, pn, pud > 0.03)), 0.03);
        let k = pud * (0.35 + 0.65 * f) * 0.85 + (1.0 - pud) * wet * f * 0.5;
        col = mix(base, refl, clamp(k, 0.0, 0.9));
      }
      let sp = u.p1.y;
      if ((en & 8u) != 0u && sp > 0.001) {
        let l = luma(col);
        let sat = max(max(col.r, col.g), col.b) - min(min(col.r, col.g), col.b);
        let snow = smoothstep(0.55, 0.75, l) * (1.0 - smoothstep(0.08, 0.2, sat));
        if (snow > 0.0) {
          let pxw = max(-p.z, 1.0) * 2.0 / (abs(u.proj.y) * u.vp.w); // world size of a pixel
          let cell = max(pxw * 3.0, ws * 0.06);
          let q = wpos.xz / cell;
          let id = floor(q);
          let h = hash12(id);
          let ctr = id + 0.25 + 0.5 * hash22(id + 1.7);
          let dpx = length(q - ctr) * cell / pxw;
          let vdir = normalize(world_dir(normalize(p)));
          let vq = floor(vdir.xz * 9.0 + vec2f(h * 13.0));
          let tw = hash12(id * 0.37 + vq + floor(t * 1.5 + h * 7.0) * 0.11);
          let on = step(0.94, h) * step(0.5, tw);
          let glint = on * (smoothstep(1.6, 0.0, dpx) + 0.35 * smoothstep(3.5, 0.0, dpx));
          let tintSel = hash12(id + 9.1);
          let gc = select(select(vec3f(1.0, 1.0, 1.0), vec3f(0.75, 0.92, 1.0), tintSel > 0.6), vec3f(1.0, 0.93, 0.72), tintSel > 0.85);
          col = col * (1.0 - 0.07 * snow * sp) + gc * glint * snow * sp * 1.4;
        }
      }
    }
  }
  if ((en & 16u) != 0u && u.p1.z > 0.0) {
    let fd = max(length(fv), 1.0);
    let dist = length(p);
    let start = fd * u.p1.w;
    var f = u.p1.z * (1.0 - exp(-max(dist - start, 0.0) / (fd * 2.2)));
    // the sky dome keeps most of its colour
    let skyish = 1.0 - smoothstep(fd * 6.0, fd * 12.0, dist);
    f *= mix(0.4, 1.0, skyish);
    col = mix(col, u.hazeCol.xyz, clamp(f, 0.0, 0.8));
  }
  return vec4f(col, 1.0);
}

// --- particles ---
struct PartOut {
  @builtin(position) pos: vec4f,
  @location(0) local: vec2f,
  @location(1) color: vec4f,
  @location(2) vz: f32,
};
@vertex fn vs_particles(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> PartOut {
  var o: PartOut;
  let kind = u32(u.part.x);
  let fvv = focus_v();
  let ws = wscale(fvv);
  let t = u.dr.w;
  let h = hash31(f32(ii) * 1.618 + 0.37);
  let h2 = hash31(f32(ii) * 2.718 + 4.1);
  // world-space box around the focus point (wrapped, so particles stay around the player)
  let fw = world_pos(fvv);
  // 0443: the box's top from the configuration (pp.z, metres above the focus; 0 = 19)
  let tall = u.pp.z > 0.5;
  let box = vec3f(46.0, select(22.0, u.pp.z + 3.0, tall), 46.0) * ws;
  var fall = 0.55;
  var sway = 0.6;
  var size = 0.16;
  if (kind == 2u) { fall = 0.42; sway = 0.8; size = 0.11; }
  if (kind == 3u) { fall = 0.75; sway = 0.35; size = 0.05 + 0.05 * h2.z; }
  fall *= (0.75 + 0.5 * h2.x);
  let ph = h2.y * 6.2832;
  var pos = vec3f(h.x, 0.0, h.z) * box;
  pos.x += u.wind.x * ws * t + sin(t * (0.6 + h.y) + ph) * sway * ws;
  pos.z += u.wind.y * ws * t + cos(t * (0.5 + h.x * 0.7) + ph) * sway * ws * 0.7;
  let rel = pos - fw;
  let wrapped = (fract(rel.xz / box.xz + 0.5) - 0.5) * box.xz;
  let yf = fract(h.y - t * fall * ws / box.y);  // 0 bottom .. 1 top of the box
  let y = fw.y - 3.0 * ws + yf * box.y;
  let wpos = vec3f(fw.x + wrapped.x, y, fw.z + wrapped.y);
  // view space
  let t3 = vec3f(u.view0.w, u.view1.w, u.view2.w);
  let pv = view_rot(wpos) + t3;
  let corner = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = corner[vi];
  let spin = t * (0.8 + 1.6 * h2.z) * select(1.0, -1.0, h.y > 0.5) + ph;
  var lc = c;
  var shade = 1.0;
  if (kind != 3u) {
    // tumbling: the quad turns about its own axis (foreshortened), lit by its facing
    let flip = cos(t * (1.1 + h2.x * 1.5) + ph * 1.7);
    lc.x *= mix(0.25, 1.0, abs(flip));
    shade = 0.75 + 0.25 * flip;
  }
  let cs = cos(spin);
  let sn = sin(spin);
  let r = vec2f(lc.x * cs - lc.y * sn, lc.x * sn + lc.y * cs);
  let q = pv + vec3f(r * size * ws, 0.0);
  let s = project(q);
  let ndc = vec2f((s.x - u.vp.x) / u.vp.z * 2.0 - 1.0, 1.0 - (s.y - u.vp.y) / u.vp.w * 2.0);
  // target-relative clip position
  let tx = s.x * u.size.z * 2.0 - 1.0;
  let ty = 1.0 - s.y * u.size.w * 2.0;
  o.pos = vec4f(tx, ty, 0.5, 1.0);
  if (q.z > -2.0 * ws) { o.pos = vec4f(2.0, 2.0, 0.5, 1.0); }
  o.local = c;
  var colr = vec3f(1.0);
  if (kind == 1u) {
    let pick = h2.z;
    colr = select(select(select(vec3f(0.92, 0.45, 0.12), vec3f(0.95, 0.72, 0.18), pick > 0.35), vec3f(0.75, 0.22, 0.10), pick > 0.7),
                  vec3f(0.55, 0.36, 0.17), pick > 0.9);
  } else if (kind == 2u) {
    colr = mix(vec3f(1.0, 0.80, 0.86), vec3f(0.98, 0.66, 0.76), h2.z);
  } else {
    colr = vec3f(1.0, 1.0, 1.0);
  }
  var edgeFade = 1.0 - smoothstep(0.38, 0.5, max(abs(wrapped.x) / box.x, abs(wrapped.y) / box.z));
  // 0443: a tall box can be seen to its top (a camera looking over the horizon): particles fade in
  // as they leave it instead of popping in where the box wraps
  if (tall) { edgeFade *= 1.0 - smoothstep(0.88, 1.0, yf); }
  o.color = vec4f(colr * shade * u.ambient.xyz, edgeFade * select(0.95, 0.85, kind == 3u));
  o.vz = q.z;
  return o;
}
@fragment fn fs_particles(in: PartOut) -> @location(0) vec4f {
  let kind = u32(u.part.x);
  let l = in.local;
  var a = 0.0;
  if (kind == 1u) {
    // leaf: pointed ellipse with a darker middle vein
    let e = l.x * l.x / 0.36 + l.y * l.y;
    a = 1.0 - smoothstep(0.75, 1.0, e + abs(l.y) * 0.15 * l.x * l.x);
  } else if (kind == 2u) {
    // petal: rounded with a notch at the tip
    let e = l.x * l.x / 0.5 + (l.y + 0.1) * (l.y + 0.1) / 0.85;
    let notch = smoothstep(0.18, 0.0, length(l - vec2f(0.0, 0.95)));
    a = (1.0 - smoothstep(0.7, 1.0, e)) * (1.0 - notch);
  } else {
    a = 1.0 - smoothstep(0.2, 1.0, length(l));
  }
  // depth test against the scene (soft)
  let sz = view_pos(in.pos.xy, load_depth(in.pos.xy)).z;
  let ws = wscale(focus_v());
  let vis = clamp((in.vz - sz) / (0.4 * ws), 0.0, 1.0);
  a *= in.color.a * vis;
  var c = in.color.rgb;
  if (kind == 1u) { c *= 1.0 - 0.25 * smoothstep(0.08, 0.0, abs(l.x)); }
  if (a < 0.004) { discard; }
  return vec4f(c * a, a);
}

// --- grade + tilt-shift ---
fn grade(cin: vec3f, mode: u32, s: f32) -> vec3f {
  var c = cin;
  if (mode == 1u) {
    // "New Leaf warmth": warm white balance, lifted shadows, a little more colour, soft contrast
    c = c * vec3f(1.05, 1.0, 0.9) + vec3f(0.025, 0.02, 0.01);
    let l = luma(c);
    c = mix(vec3f(l), c, 1.12);
    c = (c - 0.5) * 1.04 + 0.5;
    c = mix(c, c * vec3f(1.03, 1.0, 0.95), smoothstep(0.5, 1.0, l));
  } else if (mode == 2u) {
    // soft film: lower contrast, lifted blacks, muted colour, cool shadows / warm highlights
    let l = luma(c);
    c = mix(vec3f(l), c, 0.82);
    c = clamp(c, vec3f(0.0), vec3f(1.0));
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.25); // gentle S-curve...
    c = c * 0.88 + 0.055;                       // ...in a lower contrast range (lifted blacks, soft whites)
    c = mix(c * vec3f(0.97, 1.0, 1.04), c * vec3f(1.04, 1.0, 0.95), smoothstep(0.2, 0.8, l));
  } else if (mode == 3u) {
    // vivid
    let l = luma(c);
    let sat = max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);
    c = mix(vec3f(l), c, 1.2 + 0.15 * (1.0 - sat));
    c = (c - 0.5) * 1.06 + 0.5;
  }
  return mix(cin, clamp(c, vec3f(0.0), vec3f(1.0)), s);
}
fn tilt_radius(y: f32) -> f32 {
  let fy = select((u.vp.y + 0.56 * u.vp.w) * u.size.w, u.wind.w / u.size.y, u.wind.w > 0.0);
  let dy = abs(y * u.size.w - fy);
  return u.hazeCol.w * 0.009 * u.size.y * smoothstep(u.horizon.w, u.horizon.w + 0.28, dy);
}
fn blur(px: vec2f, dir: vec2f) -> vec3f {
  let r = tilt_radius(px.y);
  let c0 = textureSampleLevel(twork, samp, px * u.size.zw, 0.0).rgb;
  if (r < 0.3) { return c0; }
  var acc = c0 * 0.2;
  var wsum = 0.2;
  for (var i = 1; i <= 6; i++) {
    let o = dir * (f32(i) * r / 3.0);
    let w = exp(-f32(i * i) / 12.0);
    acc += (textureSampleLevel(twork, samp, (px + o) * u.size.zw, 0.0).rgb + textureSampleLevel(twork, samp, (px - o) * u.size.zw, 0.0).rgb) * w;
    wsum += 2.0 * w;
  }
  return acc / wsum;
}
@fragment fn fs_tilt_h(in: VsOut) -> @location(0) vec4f {
  return vec4f(blur(in.pos.xy, vec2f(1.0, 0.0)), 1.0);
}
@fragment fn fs_final(in: VsOut) -> @location(0) vec4f {
  let px = in.pos.xy;
  if (u.pp.y > 5.5 && u.pp.y < 7.5) { // 6: focus z, scale, this pixel's z; 7: raw depth (x1000, 1-d x1000), mask depth x1000
    let d = load_depth(px);
    if (u.pp.y > 6.5) { return vec4f(d * 1000.0, (1.0 - d) * 1000.0, textureLoad(tmask, vec2i(px), 0).a, 1.0); }
    let f = focus_v();
    return vec4f(-f.z / 4000.0, wscale(f) / 200.0, -view_pos(px, d).z / 4000.0, 1.0);
  }
  if (u.pp.y > 0.5 && u.pp.y < 3.5) { // debug views (AURORA_WORLD_FX_SHOW): 1 mask, 2 focus distance / scale
    if (u.pp.y > 2.5) { // 3: mask alpha, wetness, enables bit 1, view ok
      return vec4f(textureLoad(tmask, vec2i(px), 0).a, u.refl.w, select(0.0, 1.0, (u32(u.wind.z) & 2u) != 0u), 1.0);
    }
    if (u.pp.y < 1.5) { return vec4f(textureLoad(tmask, vec2i(px), 0).rgb * 0.5 + vec3f(textureLoad(tmask, vec2i(px), 0).a * 0.5), 1.0); }
    let f = focus_v();
    let d = abs(view_pos(px, load_depth(px)).z) / max(-f.z, 1.0);
    return vec4f(fract(d * 4.0), wscale(f) / 200.0, select(0.0, 1.0, abs(px.y - (u.vp.y + 0.56 * u.vp.w)) < 2.0), 1.0);
  }
  var c: vec3f;
  if (u.pp.x > 0.5) {
    c = blur(px, vec2f(0.0, 1.0));
    // diorama look: a touch more colour and contrast
    let l = luma(c);
    c = mix(vec3f(l), c, 1.0 + 0.12 * u.hazeCol.w);
    c = (c - 0.5) * (1.0 + 0.05 * u.hazeCol.w) + 0.5;
  } else {
    c = textureSampleLevel(twork, samp, px * u.size.zw, 0.0).rgb;
  }
  c = grade(c, u32(u.moonDir.w), u.ambient.w);
  return vec4f(clamp(c, vec3f(0.0), vec3f(1.0)), 1.0);
}
)";

void ensure_tex(Tex& t, uint32_t w, uint32_t h, wgpu::TextureFormat format, wgpu::TextureUsage usage, const char* label,
                uint32_t samples = 1) {
  w = std::max(w, 1u);
  h = std::max(h, 1u);
  if (t.texture && t.width == w && t.height == h && t.format == format && t.samples == samples) {
    return;
  }
  const wgpu::TextureDescriptor desc{
      .label = label,
      .usage = usage,
      .dimension = wgpu::TextureDimension::e2D,
      .size = {w, h, 1},
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = samples,
  };
  const webgpu::CreateTimer createTimer{label};
  t.texture = webgpu::g_device.CreateTexture(&desc);
  t.view = t.texture.CreateView();
  t.width = w;
  t.height = h;
  t.format = format;
  t.samples = samples;
}

wgpu::RenderPipeline make_pipeline(const char* vs, const char* fs, wgpu::TextureFormat format, uint32_t samples,
                                   const wgpu::BlendState* blend) {
  const wgpu::ColorTargetState target{.format = format, .blend = blend, .writeMask = wgpu::ColorWriteMask::All};
  const wgpu::FragmentState fragment{.module = g_gpu.module, .entryPoint = fs, .targetCount = 1, .targets = &target};
  const wgpu::RenderPipelineDescriptor desc{
      .label = fs,
      .layout = g_gpu.layout,
      .vertex = {.module = g_gpu.module, .entryPoint = vs},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .multisample = {.count = samples},
      .fragment = &fragment,
  };
  const webgpu::CreateTimer createTimer{fs};
  return webgpu::g_device.CreateRenderPipeline(&desc);
}

bool init_gpu() {
  const std::lock_guard initLock{g_initMutex};
  if (g_gpu.initialized) {
    return !g_gpu.failed;
  }
  g_gpu.initialized = true;
  const wgpu::ShaderSourceWGSL wgsl{{.code = FxShader}};
  const wgpu::ShaderModuleDescriptor moduleDesc{.nextInChain = &wgsl, .label = "World effects"};
  g_gpu.module = webgpu::g_device.CreateShaderModule(&moduleDesc);
  std::array<wgpu::BindGroupLayoutEntry, 8> entries{};
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
                .visibility = wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex,
                .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                            .viewDimension = wgpu::TextureViewDimension::e2D}};
  entries[6] = {.binding = 6,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture = {.sampleType = wgpu::TextureSampleType::Depth, .viewDimension = wgpu::TextureViewDimension::e2D}};
  entries[7] = {.binding = 7,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                            .viewDimension = wgpu::TextureViewDimension::e2D}};
  const wgpu::BindGroupLayoutDescriptor bglDesc{.label = "World FX", .entryCount = entries.size(), .entries = entries.data()};
  g_gpu.bgl = webgpu::g_device.CreateBindGroupLayout(&bglDesc);
  const wgpu::PipelineLayoutDescriptor plDesc{.label = "World FX", .bindGroupLayoutCount = 1, .bindGroupLayouts = &g_gpu.bgl};
  g_gpu.layout = webgpu::g_device.CreatePipelineLayout(&plDesc);
  const wgpu::SamplerDescriptor sampDesc{
      .label = "World FX linear",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
  };
  g_gpu.sampler = webgpu::g_device.CreateSampler(&sampDesc);
  const auto sampled = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
  ensure_tex(g_gpu.dummy, 1, 1, MaskFormat, sampled, "World FX dummy");
  ensure_tex(g_gpu.dummyDepth, 1, 1, wgpu::TextureFormat::R32Float, sampled, "World FX dummy depth");
  ensure_tex(g_gpu.dummyZ, 1, 1, webgpu::g_graphicsConfig.depthFormat, sampled, "World FX dummy Z");
  {
    const wgpu::DepthStencilState ds{
        .format = webgpu::g_graphicsConfig.depthFormat,
        .depthWriteEnabled = true,
        .depthCompare = wgpu::CompareFunction::Always,
    };
    const wgpu::FragmentState fragment{.module = g_gpu.module, .entryPoint = "fs_maskdepth", .targetCount = 0};
    const wgpu::RenderPipelineDescriptor desc{
        .label = "World FX mask depth",
        .layout = g_gpu.layout,
        .vertex = {.module = g_gpu.module, .entryPoint = "vs_full"},
        .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
        .depthStencil = &ds,
        .fragment = &fragment,
    };
    g_gpu.maskDepth = webgpu::g_device.CreateRenderPipeline(&desc);
  }
  static const wgpu::BlendState premul{
      .color = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::One,
                .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha},
      .alpha = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::Zero, .dstFactor = wgpu::BlendFactor::One},
  };
  g_gpu.surfaces = make_pipeline("vs_full", "fs_surfaces", WorkFormat, 1, nullptr);
  {
    // 0570: the plane pass (FxU, mask, mask depth, the groups' uniform)
    std::array<wgpu::BindGroupLayoutEntry, 4> pe{};
    pe[0] = entries[0];
    pe[1] = entries[3];
    pe[2] = entries[6];
    pe[3] = {.binding = 8,
             .visibility = wgpu::ShaderStage::Fragment,
             .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(PlaneUniform)}};
    const wgpu::BindGroupLayoutDescriptor pd{.label = "World FX plane", .entryCount = pe.size(), .entries = pe.data()};
    g_gpu.planeBgl = webgpu::g_device.CreateBindGroupLayout(&pd);
    const wgpu::PipelineLayoutDescriptor pl{.label = "World FX plane", .bindGroupLayoutCount = 1, .bindGroupLayouts = &g_gpu.planeBgl};
    g_gpu.planeLayout = webgpu::g_device.CreatePipelineLayout(&pl);
    const wgpu::ColorTargetState target{.format = wgpu::TextureFormat::RGBA32Float, .writeMask = wgpu::ColorWriteMask::All};
    const wgpu::FragmentState fragment{.module = g_gpu.module, .entryPoint = "fs_plane", .targetCount = 1, .targets = &target};
    const wgpu::RenderPipelineDescriptor desc{
        .label = "World FX plane",
        .layout = g_gpu.planeLayout,
        .vertex = {.module = g_gpu.module, .entryPoint = "vs_full"},
        .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
        .fragment = &fragment,
    };
    const webgpu::CreateTimer createTimer{"World FX plane"};
    g_gpu.planePipe = webgpu::g_device.CreateRenderPipeline(&desc);
    const wgpu::BufferDescriptor bd{.label = "World FX planes",
                                    .usage = wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst,
                                    .size = MaxCharGroups * 64};
    g_gpu.planeBuf = webgpu::g_device.CreateBuffer(&bd);
  }
  g_gpu.particles = make_pipeline("vs_particles", "fs_particles", WorkFormat, 1, &premul);
  g_gpu.tiltH = make_pipeline("vs_full", "fs_tilt_h", WorkFormat, 1, nullptr);
  if (!g_gpu.module || !g_gpu.maskDepth || !g_gpu.surfaces || !g_gpu.particles || !g_gpu.tiltH || !g_gpu.planePipe) {
    Log.error("World effects failed to build; disabled");
    g_gpu.failed = true;
  }
  return !g_gpu.failed;
}

wgpu::BindGroup bind(const Range& uniform, const wgpu::TextureView& color, const wgpu::TextureView& mask,
                     const wgpu::TextureView& work, const wgpu::TextureView& depth, const wgpu::TextureView& mdepth,
                     const wgpu::TextureView& refld = {}) {
  const std::array<wgpu::BindGroupEntry, 8> entries{
      wgpu::BindGroupEntry{.binding = 0, .buffer = detail::resources().uniformBuffer, .offset = uniform.offset, .size = sizeof(FxUniform)},
      wgpu::BindGroupEntry{.binding = 1, .sampler = g_gpu.sampler},
      wgpu::BindGroupEntry{.binding = 2, .textureView = color ? color : g_gpu.dummy.view},
      wgpu::BindGroupEntry{.binding = 3, .textureView = mask ? mask : g_gpu.dummy.view},
      wgpu::BindGroupEntry{.binding = 4, .textureView = work ? work : g_gpu.dummy.view},
      wgpu::BindGroupEntry{.binding = 5, .textureView = depth ? depth : g_gpu.dummyDepth.view},
      wgpu::BindGroupEntry{.binding = 6, .textureView = mdepth ? mdepth : g_gpu.dummyZ.view},
      wgpu::BindGroupEntry{.binding = 7, .textureView = refld ? refld : g_gpu.dummyDepth.view},
  };
  const wgpu::BindGroupDescriptor desc{.layout = g_gpu.bgl, .entryCount = entries.size(), .entries = entries.data()};
  return webgpu::g_device.CreateBindGroup(&desc);
}

struct SceneJob {
  std::vector<SurfaceDraw> draws;
  std::vector<MirrorDraw> mirror; // 0570
  Range planeU{};
  // 0580: AURORA_WORLD_FX_PLANAR_LOG (the plane pass's result read back and logged)
  PlaneUniform planeCpu{};
  uint32_t planeGroups = 0, charGroups = 0;
  std::array<int, MaxCharEntries> partOf{}, planeUsed{}, drawsOf{}, cmdsOf{}; // (0590: mirrored draws / recorded commands per group)
  float up[3]{};
  uint32_t frameNo = 0;
  RenderTargetLayout layout{};
  Range base{}, tiltH{}, finalU{};
  bool mask = false, surfaces = false, tilt = false;
  uint32_t particles = 0; // instance count
};

void run_full(const wgpu::CommandEncoder& cmd, const char* label, const wgpu::TextureView& target,
              const wgpu::RenderPipeline& pipe, const wgpu::BindGroup& bg) {
  const wgpu::RenderPassColorAttachment att{
      .view = target, .loadOp = wgpu::LoadOp::Clear, .storeOp = wgpu::StoreOp::Store, .clearValue = {0, 0, 0, 1}};
  const wgpu::RenderPassDescriptor desc{.label = label, .colorAttachmentCount = 1, .colorAttachments = &att,
                                        .timestampWrites = webgpu::gpu_prof::pass_writes(label)};
  const auto pass = cmd.BeginRenderPass(&desc);
  pass.SetPipeline(pipe);
  pass.SetBindGroup(0, bg);
  pass.Draw(3);
  pass.End();
  g_statPasses.fetch_add(1, std::memory_order_relaxed);
}

// 0580: AURORA_WORLD_FX_PLANAR_LOG=1: each frame's plane pass result read back (a ring of map
// buffers: copied in one frame's commands, mapped two frames later, logged when the map is done; the
// map callback only sets a flag) and logged per character group: mirrored or not, the plane height
// and the anchor's height above it, so a group left out in some frames is caught.
namespace planelog {
constexpr size_t Slots = 6;
struct Slot {
  wgpu::Buffer buf;
  std::atomic<int> state{0}; // 0 free, 1 copied, 2 map requested, 3 mapped, 4 map failed
  uint32_t age = 0, frame = 0, groups = 0, chars = 0;
  PlaneUniform pu{};
  std::array<int, MaxCharEntries> partOf{}, planeUsed{}, drawsOf{}, cmdsOf{}; // (0590: mirrored draws / recorded commands per group)
  float up[3]{};
};
std::array<Slot, Slots> g_slots;
uint32_t g_frame = 0;
bool on() noexcept {
  static const bool v = [] {
    const char* e = std::getenv("AURORA_WORLD_FX_PLANAR_LOG");
    return e != nullptr && *e != '0';
  }();
  return v;
}
void report(Slot& s) noexcept {
  const auto* rows = static_cast<const float*>(s.buf.GetConstMappedRange(0, MaxCharGroups * 64));
  std::string line = fmt::format("[planar] frame {} groups {}:", s.frame, s.chars);
  for (uint32_t gi = 0; gi < s.chars && gi < MaxCharEntries; ++gi) {
    const int pl = s.planeUsed[gi];
    line += fmt::format(" | g{} d{}/{}{}", gi, s.drawsOf[gi], s.cmdsOf[gi], s.partOf[gi] >= 0 ? fmt::format(" part of g{}", s.partOf[gi]) : s.partOf[gi] == -2 ? std::string(" part (alone)") : s.partOf[gi] == -3 ? std::string(" attachment (alone)") : std::string{});
    if (pl < 0 || rows == nullptr) {
      line += " not drawn";
      continue;
    }
    const float* r3 = rows + size_t(pl) * 16 + 12; // mirrored projection row 3
    const float* m3 = &s.pu.rows[pl][3][0];
    if (r3[3] < -0.5f && r3[0] == 0.f && r3[1] == 0.f && r3[2] == 0.f) {
      line += fmt::format(" plane {} none", pl);
      continue;
    }
    // row3' = row3 M with M the reflection about n.p = h: row3'.w = m3.w + 2 h (m3.xyz . n)
    const float dn = m3[0] * s.up[0] + m3[1] * s.up[1] + m3[2] * s.up[2];
    const float h = std::abs(dn) > 1e-6f ? (r3[3] - m3[3]) / (2.f * dn) : 0.f;
    const float* a = s.pu.anchors[pl];
    const float above = a[0] * s.up[0] + a[1] * s.up[1] + a[2] * s.up[2] - h;
    line += fmt::format(" plane {} h {:.1f} anchor {:.1f} above", pl, h, above);
  }
  Log.info("{}", line);
}
// before a frame's commands: report finished maps, request maps of slots copied >= 2 frames ago
void poll() noexcept {
  ++g_frame;
  for (auto& s : g_slots) {
    const int st = s.state.load();
    if (st == 3) {
      report(s);
      s.buf.Unmap();
      s.state = 0;
    } else if (st == 4) {
      s.state = 0;
    } else if (st == 1 && g_frame - s.age >= 2) {
      s.state = 2;
      s.buf.MapAsync(wgpu::MapMode::Read, 0, MaxCharGroups * 64, wgpu::CallbackMode::AllowSpontaneous,
                     [&s](wgpu::MapAsyncStatus status, wgpu::StringView) {
                       s.state = status == wgpu::MapAsyncStatus::Success ? 3 : 4; // no Dawn call here
                     });
    }
  }
}
void copy(const wgpu::CommandEncoder& cmd, const SceneJob& job, const wgpu::Buffer& planes) noexcept {
  for (auto& s : g_slots) {
    if (s.state.load() != 0) {
      continue;
    }
    if (!s.buf) {
      const wgpu::BufferDescriptor bd{.label = "World FX plane log",
                                      .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
                                      .size = MaxCharGroups * 64};
      s.buf = webgpu::g_device.CreateBuffer(&bd);
    }
    cmd.CopyBufferToBuffer(planes, 0, s.buf, 0, MaxCharGroups * 64);
    s.age = g_frame, s.frame = job.frameNo, s.groups = job.planeGroups, s.chars = job.charGroups;
    s.pu = job.planeCpu, s.partOf = job.partOf, s.planeUsed = job.planeUsed, s.drawsOf = job.drawsOf, s.cmdsOf = job.cmdsOf;
    std::memcpy(s.up, job.up, sizeof(s.up));
    s.state = 1;
    return;
  }
}
} // namespace planelog

void encode(const wgpu::CommandEncoder& cmd, const ScenePassTargets& t, const SceneJob& job) {
  if (planelog::on()) {
    planelog::poll();
  }
  if (!init_gpu() || !t.colorSource) {
    return;
  }
  const auto rt = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
  ensure_tex(g_gpu.depth, t.width, t.height, wgpu::TextureFormat::R32Float, rt, "World FX scene depth");
  tex_copy_conv::snapshot_depth(cmd, t.depthSource, t.msaa, g_gpu.depth.view);
  const wgpu::TextureView sceneView = t.colorSource.CreateView();

  // 2. surface mask
  wgpu::TextureView maskView, maskDepthView;
  if (job.mask) {
    ensure_tex(g_gpu.mask, t.width, t.height, MaskFormat, rt, "World FX surface mask");
    ensure_tex(g_gpu.maskDepthTex, t.width, t.height, webgpu::g_graphicsConfig.depthFormat, rt, "World FX surface depth");
    {
      const wgpu::RenderPassDepthStencilAttachment da{
          .view = g_gpu.maskDepthTex.view,
          .depthLoadOp = wgpu::LoadOp::Clear,
          .depthStoreOp = wgpu::StoreOp::Store,
          .depthClearValue = 0.f,
      };
      const wgpu::RenderPassDescriptor desc{.label = "World FX mask depth", .depthStencilAttachment = &da,
                                            .timestampWrites = webgpu::gpu_prof::pass_writes("World FX mask depth")};
      const auto pass = cmd.BeginRenderPass(&desc);
      pass.SetPipeline(g_gpu.maskDepth);
      pass.SetBindGroup(0, bind(job.base, {}, {}, {}, g_gpu.depth.view, {}));
      pass.Draw(3);
      pass.End();
    }
    const wgpu::RenderPassColorAttachment att{
        .view = g_gpu.mask.view, .loadOp = wgpu::LoadOp::Clear, .storeOp = wgpu::StoreOp::Store, .clearValue = {0, 0, 0, 0}};
    const wgpu::RenderPassDepthStencilAttachment da{
        .view = g_gpu.maskDepthTex.view,
        .depthLoadOp = wgpu::LoadOp::Load,
        .depthStoreOp = wgpu::StoreOp::Store,
    };
    const wgpu::RenderPassDescriptor desc{.label = "World FX surface mask", .colorAttachmentCount = 1, .colorAttachments = &att,
                                          .depthStencilAttachment = &da,
                                          .timestampWrites = webgpu::gpu_prof::pass_writes("World FX surface mask")};
    const auto pass = cmd.BeginRenderPass(&desc);
    reset_bound_pipeline();
    pass.SetBindGroup(0, detail::resources().staticBindGroup);
    pass.SetBindGroup(2, gx::g_emptyTextureBindGroup);
    for (const auto& g : job.draws) {
      const auto& vp = g.viewport;
      const float minDepth = gx::UseReversedZ ? 1.f - vp.zfar : vp.znear;
      const float maxDepth = gx::UseReversedZ ? 1.f - vp.znear : vp.zfar;
      pass.SetViewport(vp.left, vp.top, std::max(vp.width, 1.f), std::max(vp.height, 1.f), minDepth, maxDepth);
      const auto& sc = g.scissor;
      const uint32_t x0 = uint32_t(std::clamp(sc.x, 0, int32_t(t.width)));
      const uint32_t y0 = uint32_t(std::clamp(sc.y, 0, int32_t(t.height)));
      const uint32_t x1 = uint32_t(std::clamp(sc.x + sc.width, 0, int32_t(t.width)));
      const uint32_t y1 = uint32_t(std::clamp(sc.y + sc.height, 0, int32_t(t.height)));
      pass.SetScissorRect(x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0);
      gx::render(g.data, pass);
    }
    pass.End();
    reset_bound_pipeline();
    g_statPasses.fetch_add(2, std::memory_order_relaxed);
    maskView = g_gpu.mask.view;
    maskDepthView = g_gpu.maskDepthTex.view;
  }

  // 2b. 0570: planar reflection of the characters: the plane under each one (plane pass), its
  // mirrored projection copied into its draws' uniform copies, then those draws again into a
  // target shaped like the EFB (same layout: the pipelines the scene used).
  wgpu::TextureView reflColorView, reflDepthView;
  if (job.mask && !job.mirror.empty()) {
    const auto& lay = job.layout;
    const uint32_t samples = std::max(lay.sampleCount, 1u);
    {
      ensure_tex(g_gpu.planeTex, MaxCharGroups * 4, 1, wgpu::TextureFormat::RGBA32Float,
                 wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc, "World FX planes");
      const wgpu::RenderPassColorAttachment att{
          .view = g_gpu.planeTex.view, .loadOp = wgpu::LoadOp::Clear, .storeOp = wgpu::StoreOp::Store, .clearValue = {0, 0, 0, 0}};
      const wgpu::RenderPassDescriptor desc{.label = "World FX plane", .colorAttachmentCount = 1, .colorAttachments = &att,
                                            .timestampWrites = webgpu::gpu_prof::pass_writes("World FX plane")};
      const std::array<wgpu::BindGroupEntry, 4> pe{
          wgpu::BindGroupEntry{.binding = 0, .buffer = detail::resources().uniformBuffer, .offset = job.base.offset, .size = sizeof(FxUniform)},
          wgpu::BindGroupEntry{.binding = 3, .textureView = maskView},
          wgpu::BindGroupEntry{.binding = 6, .textureView = maskDepthView},
          wgpu::BindGroupEntry{.binding = 8, .buffer = detail::resources().uniformBuffer, .offset = job.planeU.offset, .size = sizeof(PlaneUniform)},
      };
      const wgpu::BindGroupDescriptor bgd{.layout = g_gpu.planeBgl, .entryCount = pe.size(), .entries = pe.data()};
      const auto bg = webgpu::g_device.CreateBindGroup(&bgd);
      const auto pass = cmd.BeginRenderPass(&desc);
      pass.SetPipeline(g_gpu.planePipe);
      pass.SetBindGroup(0, bg);
      pass.Draw(3);
      pass.End();
      const wgpu::TexelCopyTextureInfo src{.texture = g_gpu.planeTex.texture};
      const wgpu::TexelCopyBufferInfo dst{.layout = {.offset = 0, .bytesPerRow = MaxCharGroups * 64, .rowsPerImage = 1},
                                          .buffer = g_gpu.planeBuf};
      const wgpu::Extent3D ext{MaxCharGroups * 4, 1, 1};
      cmd.CopyTextureToBuffer(&src, &dst, &ext);
      for (const auto& m : job.mirror) {
        cmd.CopyBufferToBuffer(g_gpu.planeBuf, uint64_t(m.group) * 64, detail::resources().uniformBuffer, m.data.uniformRange.offset + 32, 64);
      }
      if (planelog::on()) {
        planelog::copy(cmd, job, g_gpu.planeBuf);
      }
    }
    const auto ra = wgpu::TextureUsage::RenderAttachment;
    const auto rs = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    const auto colorFormat = lay.colorAttachments[0].format;
    // the planar image (target size) with the characters' unmirrored copy below it at half size
    const uint32_t reflH = t.height + (t.height + 1) / 2;
    ensure_tex(g_gpu.reflColor, t.width, reflH, colorFormat, samples > 1 ? ra : rs, "World FX planar colour", samples);
    if (samples > 1) {
      ensure_tex(g_gpu.reflResolve, t.width, reflH, colorFormat, rs, "World FX planar colour (resolved)");
    }
    const bool normal = lay.colorAttachmentCount > 1;
    if (normal) {
      ensure_tex(g_gpu.reflNormal, t.width, reflH, lay.colorAttachments[1].format, ra, "World FX planar normals", samples);
    }
    ensure_tex(g_gpu.reflDepth, t.width, reflH, lay.depthStencilFormat, rs, "World FX planar depth", samples);
    ensure_tex(g_gpu.reflDepthR, t.width, reflH, wgpu::TextureFormat::R32Float, rs, "World FX planar depth snapshot");
    std::array<wgpu::RenderPassColorAttachment, 2> atts{};
    atts[0] = {.view = g_gpu.reflColor.view,
               .resolveTarget = samples > 1 ? g_gpu.reflResolve.view : wgpu::TextureView{},
               .loadOp = wgpu::LoadOp::Clear,
               .storeOp = samples > 1 ? wgpu::StoreOp::Discard : wgpu::StoreOp::Store,
               .clearValue = {0, 0, 0, 0}};
    if (normal) {
      atts[1] = {.view = g_gpu.reflNormal.view, .loadOp = wgpu::LoadOp::Clear, .storeOp = wgpu::StoreOp::Discard, .clearValue = {0, 0, 0, 0}};
    }
    const wgpu::RenderPassDepthStencilAttachment da{
        .view = g_gpu.reflDepth.view,
        .depthLoadOp = wgpu::LoadOp::Clear,
        .depthStoreOp = wgpu::StoreOp::Store,
        .depthClearValue = gx::UseReversedZ ? 0.f : 1.f,
    };
    const wgpu::RenderPassDescriptor desc{.label = "World FX planar characters",
                                          .colorAttachmentCount = normal ? 2u : 1u,
                                          .colorAttachments = atts.data(),
                                          .depthStencilAttachment = &da,
                                          .timestampWrites = webgpu::gpu_prof::pass_writes("World FX planar characters")};
    const auto pass = cmd.BeginRenderPass(&desc);
    reset_bound_pipeline();
    pass.SetBindGroup(0, detail::resources().staticBindGroup);
    pass.SetBindGroup(2, gx::g_emptyTextureBindGroup);
    const auto& vp = g_camera.vp;
    const float minDepth = gx::UseReversedZ ? 1.f - vp.zfar : vp.znear;
    const float maxDepth = gx::UseReversedZ ? 1.f - vp.znear : vp.zfar;
    pass.SetViewport(vp.left, vp.top, std::max(vp.width, 1.f), std::max(vp.height, 1.f), minDepth, maxDepth);
    const uint32_t x0 = uint32_t(std::clamp(std::floor(vp.left), 0.f, float(t.width)));
    const uint32_t y0 = uint32_t(std::clamp(std::floor(vp.top), 0.f, float(t.height)));
    const uint32_t x1 = uint32_t(std::clamp(std::ceil(vp.left + vp.width), 0.f, float(t.width)));
    const uint32_t y1 = uint32_t(std::clamp(std::ceil(vp.top + vp.height), 0.f, float(t.height)));
    pass.SetScissorRect(x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0);
    for (const auto& m : job.mirror) {
      gx::render(m.data, pass);
    }
    // the unmirrored copy (character mask for the march's hits): their own uniforms, half size
    pass.SetViewport(vp.left * 0.5f, float(t.height) + vp.top * 0.5f, std::max(vp.width * 0.5f, 1.f), std::max(vp.height * 0.5f, 1.f),
                     minDepth, maxDepth);
    pass.SetScissorRect(0, t.height, t.width, reflH - t.height);
    for (const auto& m : job.mirror) {
      gx::DrawData d = m.data;
      d.uniformRange = m.original;
      gx::render(d, pass);
    }
    pass.End();
    reset_bound_pipeline();
    tex_copy_conv::snapshot_depth(cmd, g_gpu.reflDepth.view, samples, g_gpu.reflDepthR.view);
    reflColorView = samples > 1 ? g_gpu.reflResolve.view : g_gpu.reflColor.view;
    reflDepthView = g_gpu.reflDepthR.view;
    g_statPasses.fetch_add(2, std::memory_order_relaxed);
  }

  // 3. surfaces + haze -> work
  ensure_tex(g_gpu.work, t.width, t.height, WorkFormat, rt, "World FX work");
  run_full(cmd, "World FX surfaces", g_gpu.work.view, g_gpu.surfaces,
           bind(job.base, sceneView, maskView, reflColorView, g_gpu.depth.view, maskDepthView, reflDepthView));

  // 4. particles
  if (job.particles > 0) {
    const wgpu::RenderPassColorAttachment att{
        .view = g_gpu.work.view, .loadOp = wgpu::LoadOp::Load, .storeOp = wgpu::StoreOp::Store};
    const wgpu::RenderPassDescriptor desc{.label = "World FX particles", .colorAttachmentCount = 1, .colorAttachments = &att,
                                          .timestampWrites = webgpu::gpu_prof::pass_writes("World FX particles")};
    const auto pass = cmd.BeginRenderPass(&desc);
    pass.SetPipeline(g_gpu.particles);
    pass.SetBindGroup(0, bind(job.base, {}, {}, {}, g_gpu.depth.view, {}));
    pass.SetScissorRect(uint32_t(std::clamp(g_camera.vp.left, 0.f, float(t.width))), 0, t.width, t.height);
    pass.Draw(6, job.particles);
    pass.End();
    g_statPasses.fetch_add(1, std::memory_order_relaxed);
  }

  // 5. tilt-shift (horizontal half) and the final pass into the EFB
  wgpu::TextureView finalSrc = g_gpu.work.view;
  if (job.tilt) {
    ensure_tex(g_gpu.tmp, t.width, t.height, WorkFormat, rt, "World FX tilt-shift");
    run_full(cmd, "World FX tilt-shift", g_gpu.tmp.view, g_gpu.tiltH, bind(job.tiltH, {}, {}, g_gpu.work.view, {}, {}));
    finalSrc = g_gpu.tmp.view;
  }
  {
    const std::lock_guard initLock{g_initMutex};
    if (!g_gpu.finalPipe || g_gpu.finalFormat != t.colorFormat || g_gpu.finalSamples != t.msaa) {
      g_gpu.finalPipe = make_pipeline("vs_full", "fs_final", t.colorFormat, t.msaa, nullptr);
      g_gpu.finalFormat = t.colorFormat;
      g_gpu.finalSamples = t.msaa;
    }
  }
  const wgpu::RenderPassColorAttachment att{
      .view = t.colorView, .resolveTarget = t.resolveView, .loadOp = wgpu::LoadOp::Load, .storeOp = wgpu::StoreOp::Store};
  const wgpu::RenderPassDescriptor desc{.label = "World FX composite", .colorAttachmentCount = 1, .colorAttachments = &att,
                                        .timestampWrites = webgpu::gpu_prof::pass_writes("World FX composite")};
  const auto pass = cmd.BeginRenderPass(&desc);
  pass.SetPipeline(g_gpu.finalPipe);
  // only the main view's rectangle (a smaller view keeps what is around it)
  const auto& vp = g_camera.vp;
  const uint32_t x0 = uint32_t(std::clamp(std::floor(vp.left), 0.f, float(t.width)));
  const uint32_t y0 = uint32_t(std::clamp(std::floor(vp.top), 0.f, float(t.height)));
  const uint32_t x1 = uint32_t(std::clamp(std::ceil(vp.left + vp.width), 0.f, float(t.width)));
  const uint32_t y1 = uint32_t(std::clamp(std::ceil(vp.top + vp.height), 0.f, float(t.height)));
  pass.SetScissorRect(x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0);
  pass.SetBindGroup(0, bind(job.finalU, {}, maskView, finalSrc, g_gpu.depth.view, {}));
  pass.Draw(3);
  pass.End();
  g_statPasses.fetch_add(1, std::memory_order_relaxed);
}

float time_now() noexcept {
  static const auto sStart = std::chrono::steady_clock::now();
  return std::fmod(std::chrono::duration<float>(std::chrono::steady_clock::now() - sStart).count(), 3600.f);
}

} // namespace

bool active() noexcept {
  sync();
  return g_cfg.enabled;
}

// 0451: the surface-mask re-draws (note_draw) use configs derived from the game's water / ground
// draws, drawn into the mask target. find_pipeline remembers them like any config, but the
// precache built them only for the scene targets, so every launch missed each one at its first
// water / ground view (Rover's bus 15, the town 7, ...: 1-3.5 ms each from the shader cache, 0.1-0.5 s
// cold). Now the rebuild also builds every remembered mask-shaped config for the mask target.
bool precache_mask() noexcept {
  // Sticky for the session: the application switches the outdoor effects off indoors (wet ground
  // etc.), which must not rebuild the pipeline set at every door.
  static std::atomic<bool> sOn{false};
  if (sOn.load(std::memory_order_relaxed)) {
    return true;
  }
  std::lock_guard lock{g_cfgMutex};
  const auto& c = g_pending;
  const bool on = c.enabled && (c.reflections || c.wetGround || c.rainRipples || c.snowSparkle);
  if (on) {
    sOn.store(true, std::memory_order_relaxed);
  }
  return on;
}

bool is_mask_variant(const gx::PipelineConfig& c) noexcept {
  if (c.shaderConfig.depthOnly || c.shaderConfig.lightDepth || c.blendMode != GX_BM_NONE || !c.colorUpdate ||
      !c.alphaUpdate || !c.depthCompare || c.depthFunc == GX_LESS || c.depthFunc == GX_GREATER) {
    return false;
  }
  // (depth writes not tested: the canonical config may drop them)
  return c.dstAlpha == 255u || c.dstAlpha == 128u;
}

const RenderTargetLayout& mask_target_layout() noexcept {
  static const RenderTargetLayout layout = mask_layout();
  return layout;
}

void note_draw(const gx::PipelineConfig& config, uint8_t kind, const gx::DrawData& data) noexcept {
  const auto& st = gx::g_gxState;
  if (st.projType != GX_PERSPECTIVE) {
    return;
  }
  const bool mirrorFlag = (kind & AURORA_SURFACE_MIRROR) != 0;
  kind &= uint8_t(~AURORA_SURFACE_MIRROR);
  if (kind == AURORA_SURFACE_CHARACTER || kind == AURORA_SURFACE_CHARACTER_PART || kind == AURORA_SURFACE_CHARACTER_ATTACH ||
      mirrorFlag) { // 0570: the character's anchor and projection (0590: attachments too)
    if (!g_chars.empty() && !g_chars.back().anchored && g_chars.back().end == UINT32_MAX) {
      // the draw's current position matrix, else (skinned shapes index theirs per vertex) the first
      // loaded one that places something in front of the camera
      const float* m = reinterpret_cast<const float*>(&st.pnMtx[st.currentPnMtx % gx::MaxPnMtx].pos);
      const auto placed = [](const float* x) { return std::isfinite(x[3]) && std::isfinite(x[7]) && std::isfinite(x[11]) && x[11] < -1.f; };
      auto& g0 = g_chars.back();
      // (0580) a mirrored static model: the view-space centre of its first draw's vertices (a town
      // acre's matrices place the acre's corner and bend it along the curved world)
      const float* la = g0.local;
      const bool localOk = g0.hasLocal && std::isfinite(la[0]) && std::isfinite(la[1]) && std::isfinite(la[2]) && la[2] < -1.f;
      for (uint32_t k = 0; k < gx::MaxPnMtx && !localOk && !placed(m); ++k) {
        m = reinterpret_cast<const float*>(&st.pnMtx[k].pos);
      }
      if (localOk || placed(m)) {
        auto& g = g_chars.back();
        if (localOk) {
          g.anchor[0] = la[0], g.anchor[1] = la[1], g.anchor[2] = la[2];
        } else {
          g.anchor[0] = m[3], g.anchor[1] = m[7], g.anchor[2] = m[11];
        }
        auto pr = st.proj; // as fill_uniform writes it
        if constexpr (gx::UseReversedZ) {
          pr.m2 = pr.m2 * Vec4<float>{-1.f, -1.f, -1.f, -1.f};
        } else {
          pr.m2 = pr.m2 + pr.m3;
        }
        static_assert(sizeof(pr) == sizeof(g.proj));
        std::memcpy(g.proj, &pr, sizeof(g.proj));
        g.anchored = true;
      }
    }
  }
  // The camera: the projection of the surface draws (the world proper; the sky dome and other
  // world draws may use their own depth range), any world draw until the first surface draw.
  const bool surface = kind == AURORA_SURFACE_WATER || kind == AURORA_SURFACE_GROUND;
  if (g_cameraFromSurface && !surface) {
    return;
  }
  g_cameraFromSurface = g_cameraFromSurface || surface;
  const auto& p = st.proj;
  g_camera = {
      .valid = true,
      .p00 = p.m0.x(), .p11 = p.m1.y(), .p02 = p.m0.z(), .p12 = p.m1.z(), .p03 = p.m0.w(), .p13 = p.m1.w(),
      .a = p.m2.z(), .b = p.m2.w(),
      .vp = st.renderViewport,
  };
  if (kind != AURORA_SURFACE_WATER && kind != AURORA_SURFACE_GROUND) {
    return;
  }
  static const bool sMtxLog = std::getenv("AURORA_WORLD_FX_MTX_LOG") != nullptr; // debug: a surface draw's matrix
  if (sMtxLog) {
    static uint32_t sN = 0;
    if (sN++ % 400 == 0) {
      const float* m = reinterpret_cast<const float*>(&st.pnMtx[st.currentPnMtx % gx::MaxPnMtx].pos);
      Log.info("surface draw kind {} pnmtx {}: {} {} {} {} / {} {} {} {} / {} {} {} {}", kind, st.currentPnMtx, m[0], m[1], m[2], m[3],
               m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]);
    }
  }
  if (!wants_mask(g_cfg) || g_draws.size() >= MaxSurfaceDraws || !config.colorUpdate) {
    return;
  }
  gx::PipelineConfig gc = config;
  if (gc.depthFunc == GX_LESS) {
    gc.depthFunc = GX_LEQUAL;
  } else if (gc.depthFunc == GX_GREATER) {
    gc.depthFunc = GX_GEQUAL;
  }
  gc.depthCompare = true;
  gc.depthUpdate = kind == AURORA_SURFACE_WATER;
  gc.blendMode = GX_BM_NONE;
  gc.colorUpdate = true;
  gc.alphaUpdate = true;
  const uint32_t a = kind == AURORA_SURFACE_WATER ? 255u : 128u;
  gc.dstAlpha = a;
  static const RenderTargetLayout layout = mask_layout();
  SurfaceDraw s{.data = data, .viewport = current_viewport(), .scissor = current_scissor()};
  static std::unordered_map<uint64_t, PipelineRef> sVariants;
  const uint64_t key = uint64_t(data.pipeline) * 0x9E3779B97F4A7C15ull ^ a;
  if (const auto it = sVariants.find(key); it != sVariants.end()) {
    s.data.pipeline = it->second;
  } else {
    s.data.pipeline = find_pipeline(gc, layout);
    sVariants.emplace(key, s.data.pipeline);
  }
  s.data.dstAlpha = a;
  std::tie(s.pass, s.command) = next_command_position();
  g_draws.push_back(s);
}

// 0570: GX_AURORA_SET_SURFACE into / out of AURORA_SURFACE_CHARACTER.
bool wants_anchor() noexcept {
  return !g_chars.empty() && g_chars.back().end == UINT32_MAX && g_chars.back().mirror && !g_chars.back().anchored &&
         !g_chars.back().hasLocal;
}

void group_local_anchor(float x, float y, float z) noexcept {
  sync();
  if (!g_chars.empty() && g_chars.back().end == UINT32_MAX && !g_chars.back().anchored) {
    auto& g = g_chars.back();
    g.local[0] = x, g.local[1] = y, g.local[2] = z;
    g.hasLocal = true;
  }
}

void character_scope(bool begin, bool part, bool mirror, bool attach) noexcept {
  sync();
  if (!g_cfg.enabled || !g_cfg.reflections || !planar_on()) {
    return;
  }
  const auto [pass, command] = next_command_position();
  if (begin) {
    if (g_chars.size() < MaxCharEntries) {
      g_chars.push_back({.pass = pass, .begin = command, .part = part, .attach = attach, .mirror = mirror});
    }
  } else if (!g_chars.empty() && g_chars.back().end == UINT32_MAX) {
    if (pass == g_chars.back().pass) {
      g_chars.back().end = command;
    } else {
      g_chars.pop_back(); // (the pass changed inside the character: not drawn again)
    }
  }
}

void begin_frame() noexcept {
  g_chars.clear();
  g_draws.clear();
  g_camera.valid = false;
  g_cameraFromSurface = false;
}

void scene_end() noexcept {
  sync();
  const auto& c = g_cfg;
  g_statEnds.fetch_add(1, std::memory_order_relaxed);
  struct Reset {
    ~Reset() {
      g_chars.clear();
      g_draws.clear();
      g_camera.valid = false;
  g_cameraFromSurface = false;
    }
  } reset;
  if (!c.enabled || !g_camera.valid) {
    return;
  }
  const auto& st = gx::g_gxState;
  SceneJob job;
  job.draws = std::move(g_draws);
  for (auto& g : job.draws) { // take over what was merged into each draw after it was noted
    if (const auto* d = gx_draw_at(g.pass, g.command)) {
      g.data.vertRange = d->vertRange;
      g.data.idxRange = d->idxRange;
      g.data.vtxCount = d->vtxCount;
      g.data.indexCount = d->indexCount;
      g.data.instanceCount = d->instanceCount;
    }
  }
  g_statDraws.store(uint32_t(job.draws.size()), std::memory_order_relaxed);
  job.mask = !job.draws.empty() && wants_mask(c);
  const auto size = get_render_target_size();
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
  u.dr[2] = gx::UseReversedZ ? 1.f : 0.f;
  u.dr[3] = c.freezeTime ? 100.f : time_now();
  const float* xv = st.appXf.data() + (AURORA_WORLD_FX_XF_VIEW - AURORA_APP_XF_BASE);
  std::memcpy(u.view, xv, sizeof(u.view));
  const bool viewValid = xv[0] * xv[0] + xv[1] * xv[1] + xv[2] * xv[2] > 0.25f;
  u.focus[3] = viewValid ? 1.f : 0.f;
  {
    // 0440: the application's focus point (its matrix' translation), when given and in front.
    const float* xfo = st.appXf.data() + (AURORA_WORLD_FX_XF_FOCUS - AURORA_APP_XF_BASE);
    const bool given = xfo[0] != 0.f || xfo[1] != 0.f || xfo[2] != 0.f || xfo[3] != 0.f || xfo[7] != 0.f || xfo[11] != 0.f;
    if (given && std::isfinite(xfo[3]) && std::isfinite(xfo[7]) && std::isfinite(xfo[11]) && xfo[11] < -1.f) {
      u.fv[0] = xfo[3], u.fv[1] = xfo[7], u.fv[2] = xfo[11], u.fv[3] = 1.f;
    }
  }
  u.focus[0] = std::clamp(c.clouds, 0.f, 1.f);
  const uint32_t q = std::min(c.reflectionQuality, 2u);
  u.refl[0] = std::clamp(c.reflectionStrength, 0.f, 1.5f);
  u.refl[1] = q == 0 ? 16.f : q == 1 ? 24.f : 40.f;
  u.refl[2] = std::clamp(c.rippleDistortion, 0.f, 2.f);
  u.refl[3] = std::clamp(c.wetness, 0.f, 1.f);
  u.p1[0] = std::clamp(c.rain, 0.f, 1.f);
  u.p1[1] = std::clamp(c.sparkle, 0.f, 1.f);
  u.p1[2] = c.haze ? std::clamp(c.hazeDensity, 0.f, 1.f) : 0.f;
  u.p1[3] = std::max(c.hazeStart, 0.f);
  for (int i = 0; i < 3; ++i) {
    u.hazeCol[i] = c.hazeColor[i];
    u.zenith[i] = c.skyZenith[i];
    u.horizon[i] = c.skyHorizon[i];
    u.moonDir[i] = c.moonDir[i];
    u.ambient[i] = c.ambient[i];
  }
  u.hazeCol[3] = c.tiltShift ? std::clamp(c.tiltStrength, 0.f, 1.f) : 0.f;
  u.zenith[3] = std::clamp(c.moon, 0.f, 1.f);
  u.horizon[3] = std::clamp(c.tiltBand, 0.02f, 0.6f);
  u.moonDir[3] = float(std::min(c.grade, 3u));
  u.ambient[3] = std::clamp(c.gradeStrength, 0.f, 1.f);
  const float ws = c.worldScale > 0.f ? c.worldScale : 1.f;
  const uint32_t kind = viewValid ? std::min(c.particles, 3u) : 0u;
  const float baseCount = kind == AURORA_WORLD_FX_PARTICLES_SNOW ? 900.f : kind == AURORA_WORLD_FX_PARTICLES_PETALS ? 500.f : 350.f;
  // 0443: a taller box (particleTop) gets proportionally more particles: the same density
  const float top = c.particleTop > 0.5f ? std::clamp(c.particleTop, 1.f, 200.f) : 0.f;
  const float heightScale = top > 0.f ? (top + 3.f) / 22.f : 1.f;
  job.particles = kind != 0 ? uint32_t(std::clamp(c.particleDensity, 0.f, 2.f) * baseCount * heightScale) : 0u;
  u.pass[2] = top;
  u.part[0] = float(kind);
  u.part[1] = float(job.particles);
  u.part[2] = ws;
  u.part[3] = job.mask ? 1.f : 0.f;
  u.wind[0] = c.wind[0];
  u.wind[1] = c.wind[1];
  uint32_t en = 0;
  en |= c.reflections ? 1u : 0u;
  en |= c.wetGround && c.wetness > 0.f ? 2u : 0u;
  en |= c.rainRipples && c.rain > 0.f ? 4u : 0u;
  en |= c.snowSparkle && c.sparkle > 0.f ? 8u : 0u;
  en |= c.haze && c.hazeDensity > 0.f ? 16u : 0u;
  u.wind[2] = float(en);
  // the focus row (tilt-shift) in target pixels
  u.wind[3] = 0.f;
  job.tilt = c.tiltShift && c.tiltStrength > 0.f;
  static const int sShowAll = [] {
    const char* v = std::getenv("AURORA_WORLD_FX_SHOW");
    return v ? std::atoi(v) : 0;
  }();
  u.pass[1] = float(sShowAll);
  if (c.reflections && job.mask && planar_on() && !g_chars.empty()) { // 0570
    PlaneUniform pu{};
    uint32_t groups = 0;
    const auto [curPass, curCommand] = next_command_position();
    std::array<int, MaxCharEntries> planeOf{}; // g_chars index -> plane (-1: none)
    planeOf.fill(-1);
    static const bool sOwnPlanes = [] { // A/B: AURORA_WORLD_FX_PART_PLANES=own (a plane per part, as 0570)
      const char* v = std::getenv("AURORA_WORLD_FX_PART_PLANES");
      return v && std::strcmp(v, "own") == 0;
    }();
    // 0580: a part (a player's head model, the worn Mii head) belongs to the nearest whole
    // character (anchors within 64 units; a head may be drawn after other objects, the Mii head in
    // the translucent pass) and uses its plane: the water under the character, not under the
    // part's own point, so the figure is mirrored as a whole or not at all. Characters first.
    std::array<int, MaxCharEntries> parentOf{};
    parentOf.fill(-1);
    for (size_t gi = 0; gi < g_chars.size(); ++gi) {
      const auto& g = g_chars[gi];
      if (!g.part || !g.anchored || sOwnPlanes) {
        continue;
      }
      float best = 64.f * 64.f;
      for (size_t ci = 0; ci < g_chars.size(); ++ci) {
        const auto& o = g_chars[ci];
        if (o.part || o.mirror || !o.anchored) {
          continue;
        }
        const float dx = o.anchor[0] - g.anchor[0], dy = o.anchor[1] - g.anchor[1], dz = o.anchor[2] - g.anchor[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < best) {
          best = d2, parentOf[gi] = int(ci);
        }
      }
    }
    std::array<size_t, MaxCharEntries> order{};
    size_t orderN = 0;
    for (int partPass = 0; partPass < 2; ++partPass) {
      for (size_t gi = 0; gi < g_chars.size(); ++gi) {
        if ((parentOf[gi] >= 0) == (partPass == 1)) {
          order[orderN++] = gi;
        }
      }
    }
    for (size_t oi = 0; oi < orderN; ++oi) {
      const size_t gi = order[oi];
      const auto& g = g_chars[gi];
      // (a part whose character drew nothing mirrored gets its own plane)
      const int shared = parentOf[gi] >= 0 ? planeOf[size_t(parentOf[gi])] : -1;
      // 0590: an attachment only with its character's plane (alone it stays with the march)
      if (g.attach && !sOwnPlanes && shared < 0) {
        continue;
      }
      if ((!g.anchored && shared < 0) || (shared < 0 && groups >= MaxCharGroups)) {
        continue;
      }
      const uint32_t plane = shared >= 0 ? uint32_t(shared) : groups;
      const uint32_t end = g.end != UINT32_MAX ? g.end : (g.pass == curPass ? curCommand : g.begin);
      bool any = false;
      job.cmdsOf[gi] = int(end - g.begin);
      for (uint32_t i = g.begin; i < end && job.mirror.size() < MaxMirrorDraws; ++i) {
        const auto* d = gx_draw_at(g.pass, i);
        // (triangles: the projection sits at byte 32 of the GX uniform block; lines / points and
        // wireframe put more in front of it)
        if (d == nullptr || d->uniformRange.size < 96 || (d->vtxCount == 0 && d->indexCount == 0)) {
          continue;
        }
        const uint8_t* src = uniform_bytes(d->uniformRange);
        if (src == nullptr) {
          continue;
        }
        MirrorDraw m{.data = *d, .original = d->uniformRange, .group = plane};
        m.data.uniformRange = push_uniform(src, d->uniformRange.size);
        job.mirror.push_back(m);
        ++job.drawsOf[gi];
        any = true;
      }
      if (!any || shared >= 0) {
        continue;
      }
      planeOf[gi] = int(groups);
      pu.anchors[groups][0] = g.anchor[0], pu.anchors[groups][1] = g.anchor[1], pu.anchors[groups][2] = g.anchor[2];
      pu.anchors[groups][3] = 1.f;
      std::memcpy(pu.rows[groups], g.proj, sizeof(g.proj));
      ++groups;
    }
    if (!job.mirror.empty()) {
      pu.info[0] = float(groups);
      job.planeU = push_uniform(reinterpret_cast<const uint8_t*>(&pu), sizeof(pu));
      job.planeCpu = pu;
      job.planeGroups = groups;
      for (size_t gi = 0; gi < g_chars.size(); ++gi) {
        job.partOf[gi] = parentOf[gi] >= 0 ? parentOf[gi] : (g_chars[gi].attach && !sOwnPlanes ? -3 : g_chars[gi].part ? -2 : -1);
        job.planeUsed[gi] = planeOf[gi] >= 0 ? planeOf[gi] : (parentOf[gi] >= 0 ? planeOf[size_t(parentOf[gi])] : -1);
      }
      job.charGroups = uint32_t(g_chars.size());
      static uint32_t sFrameNo = 0;
      job.frameNo = ++sFrameNo;
      job.up[0] = u.view[1]; // world up (+y) in view space: (view0.y, view1.y, view2.y)
      job.up[1] = u.view[5];
      job.up[2] = u.view[9];
      job.layout = scene_render_target_layout();
      u.pass[3] = 1.f;
    }
  }
  job.base = push_uniform(reinterpret_cast<const uint8_t*>(&u), sizeof(u));
  if (job.tilt) {
    FxUniform pu = u;
    pu.pass[0] = 1.f;
    job.tiltH = push_uniform(reinterpret_cast<const uint8_t*>(&pu), sizeof(pu));
  }
  FxUniform fu = u;
  fu.pass[0] = job.tilt ? 1.f : 0.f;
  static const int sShow = [] {
    const char* v = std::getenv("AURORA_WORLD_FX_SHOW");
    return v ? std::atoi(v) : 0;
  }();
  fu.pass[1] = float(sShow);
  job.finalU = push_uniform(reinterpret_cast<const uint8_t*>(&fu), sizeof(fu));
  static const bool sLog = std::getenv("AURORA_WORLD_FX_LOG") != nullptr;
  if (sLog) {
    static uint32_t sLogged = 0;
    if (sLogged++ % 120 == 0) {
      Log.info("camera p00 {} p11 {} p02 {} p12 {} A {} B {} dr {} {} en {} wet {}", u.proj[0], u.proj[1], u.proj[2], u.proj[3],
               u.projz[0], u.projz[1], u.dr[0], u.dr[1], u.wind[2], u.refl[3]);
      Log.info("planar characters: {} groups noted, {} draws mirrored", g_chars.size(), job.mirror.size());
      for (const auto& g : g_chars) {
        uint32_t gxDraws = 0;
        for (uint32_t i = g.begin; i < g.end && i < g.begin + 4096; ++i) {
          gxDraws += gx_draw_at(g.pass, i) != nullptr ? 1u : 0u;
        }
        Log.info("  group pass {} commands {}..{} anchored {} anchor {} {} {}, {} GX draws", g.pass, g.begin, g.end, g.anchored,
                 g.anchor[0], g.anchor[1], g.anchor[2], gxDraws);
      }
      Log.info("scene end: {} surface draws, mask {}, particles {}, tilt {}, vp {} {} {} {}, view {} {} {} / {} {} {} / {} {} {}, "
               "focus {} {} {} (row {})",
               job.draws.size(), job.mask, job.particles, job.tilt, vp.left, vp.top, vp.width, vp.height, xv[0], xv[1], xv[2],
               xv[4], xv[5], xv[6], xv[8], xv[9], xv[10], 0.f, 0.f, 0.f, u.wind[3]);
    }
  }
  push_scene_op([job = std::move(job)](const wgpu::CommandEncoder& cmd, const ScenePassTargets& t) { encode(cmd, t, job); });
}

} // namespace aurora::gfx::world_fx

extern "C" {
void aurora_world_fx_set_config(const AuroraWorldFxConfig* config) {
  using namespace aurora::gfx::world_fx;
  if (config == nullptr) {
    return;
  }
  std::lock_guard lock{g_cfgMutex};
  if (std::memcmp(config, &g_pending, sizeof(*config)) == 0) {
    return;
  }
  g_pending = *config;
  g_pendingGen.fetch_add(1, std::memory_order_release);
}

void aurora_world_fx_stats(uint32_t* surfaceDraws, uint32_t* sceneEnds, uint32_t* passes) {
  using namespace aurora::gfx::world_fx;
  if (surfaceDraws != nullptr) {
    *surfaceDraws = g_statDraws.load(std::memory_order_relaxed);
  }
  if (sceneEnds != nullptr) {
    *sceneEnds = g_statEnds.load(std::memory_order_relaxed);
  }
  if (passes != nullptr) {
    *passes = g_statPasses.load(std::memory_order_relaxed);
  }
}
}
