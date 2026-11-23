#pragma once
// Enhanced lighting (include/aurora/enhanced.h, include/aurora/lights.h): configuration, the
// frame's extra lights, per-draw uniforms and the scene-end screen-space effects.
#include <aurora/enhanced.h>
#include <aurora/lights.h>

#include "types.hpp"

#include <functional>

namespace aurora::gx {
struct PipelineConfig;
struct ShaderConfig;
struct DrawData;
} // namespace aurora::gx

namespace aurora::gfx::enhanced {

struct DrawUniform {
  uint32_t flags = 0; // bit0 per-pixel lighting, bit1 extra lights, bit2 soft shadows, bit3 flashlight quality
  uint32_t lightBase = 0; // u32 index into the storage buffer
  uint32_t lightCount = 0;
  uint32_t shadowTaps = 0; // taps | 0x10000 for PCSS
  float extraScale = 1.f;
  float shadowRadius = 1.f;
  float flashSoft = 0.f;
  float pcss = 1.f;
  uint32_t maskBase = 0;  // extra-light shadow mask: u32 index into the storage buffer
  uint32_t maskPitch = 0; // words per row
  uint32_t maskDims = 0;  // width | height << 16 (0: no mask this frame)
  float maskScale = 0.f;  // mask pixels per target pixel
  float flashSpec = 0.f;  // flashlight specular strength
  float shadowDark = 1.f;  // AuroraEnhancedConfig::shadowDarkness
  float shadowKeep = 0.f;  // AuroraEnhancedConfig::shadowAmbientKeep
  float material = 0.f;  // 1 = character (GX_AURORA_SET_MATERIAL_HINT)
  // 0196: realistic flashlight footprint (flags bit 5)
  float beamGain = 1.f;
  float beamDref2 = 0.f;
  float beamFloor = 0.f;
  float beamRings = 0.f;
  // 0520: light tiles (enhanced.cpp "Light tiles"; 0 = the full light loop)
  uint32_t tileBase = 0;  // u32 index of the tile header in the mask buffer
  uint32_t tileStamp = 0; // the header must hold it (else the full loop)
  uint32_t tileDims = 0;  // tiles x | y << 16
  uint32_t tileCfg = 0;   // shift | list << 8 | 1 << 31
};
static_assert(sizeof(DrawUniform) == 96);

// --- FIFO thread ---
// Takes a newly set configuration; true when it changed (pipelines must be re-derived).
bool sync_config() noexcept;
const AuroraEnhancedConfig& config() noexcept;
// Shader variant for a draw (ShaderConfig::enhClass) given the current draw class and blend mode.
// Additive draws (light effects) are left as they are: their look is built on the GX vertex
// lighting (e.g. the flashlight beam's cone).
uint8_t shader_class(const gx::ShaderConfig& sc, uint8_t drawClass, bool blended, bool additive) noexcept;
// Per-draw uniform block (pushes this frame's extra lights into the storage buffer on first use).
DrawUniform draw_uniform() noexcept;
// 0520: the cached uniform of an enhanced draw refers to the light tiles of another render pass
// (a new EFB pass began since it was built): it must be built again.
bool tile_uniform_stale() noexcept;
// GX_AURORA_SET_EXTRA_LIGHTS.
void set_lights(const ExtraLight* lights, uint32_t count) noexcept;
// A draw is about to be recorded while the mode is on (world class: camera projection /
// flashlight capture).
// True: drop the draw (debug aid AURORA_ENH_HIDE_ADDITIVE).
bool note_draw(const gx::PipelineConfig& config, uint8_t drawClass, uint32_t vtxCount, const gx::DrawData& data) noexcept;
// 0470: the caster-only draw (AURORA_DRAW_CLASS_SHADOW) the last note_draw recorded, or null: the
// next draw with unchanged GX state is merged into it (like draw_prim merges recorded draws).
gx::DrawData* last_caster_only_draw() noexcept;
// An EFB copy cleared `fraction` of the EFB color: emissive draws recorded before a large clear
// are no longer in the picture.
void efb_cleared(float fraction, bool color, bool depth) noexcept;
// An EFB copy is about to be recorded (renders the pending glow draws while their textures are
// still the ones they were drawn with).
void before_efb_copy() noexcept;
// GX_AURORA_SCENE_END.
void scene_end() noexcept;
// A new frame starts recording.
void begin_frame() noexcept;
// 0028: a frame's command buffer was submitted (GPU timing readback, AURORA_ENH_GPU_TIMING=1).
void after_submit() noexcept;

// --- pipeline precache (any thread) ---
// 0223: builds the scene-end passes' pipelines and (before the first enhanced pass) render
// targets of the latest set configuration on a background thread, then calls `done` there. False
// (nothing started, `done` not called) when enhanced lighting is off. stop_prewarm joins it.
bool start_prewarm(std::function<void()> done) noexcept;
void stop_prewarm() noexcept;
// 0214: the enhanced passes' render targets a known config is drawn into (written to `out`, at most
// 2; returns the count); `sceneToo` false when the config is never drawn into the scene.
size_t precache_layouts(const gx::PipelineConfig& c, RenderTargetLayout* out, bool& sceneToo) noexcept;
// 0217: the configuration parts that select pipeline variants (the latest set configuration) and
// the configs a draw with config `c` leads to under them (itself included; emitted, not deduped).
enum : uint32_t {
  PrecacheEnabled = 1u << 0, // master switch: enhanced shader classes
  PrecacheGlow = 1u << 1,    // bloom from emissive draws: glow re-draws
  PrecacheDepth = 1u << 2,   // depth pre-pass (point-light shadows, forward AO)
  PrecacheCasters = 1u << 3, // light shadow maps (shadow-map mode)
  PrecacheBeam = 1u << 4,    // realistic flashlight (alpha-only cone)
  PrecacheAo = 1u << 5,      // ambient occlusion receivers (unlit opaque world draws get the world class)
  PrecacheRecv = 1u << 6,    // 0340: directional-shadow receivers (unlit opaque world draws get the world class)
  PrecacheWorldMask = 1u << 7, // 0451: Enhanced world's surface-mask re-draws (world_fx, its mask target)
};
// Render targets a variant is built for (emit's second argument).
enum : uint8_t {
  PrecacheScene = 1u << 0,     // the EFB (scene layout)
  PrecacheOffscreen = 1u << 1, // offscreen EFB passes (single sample, one attachment)
  PrecacheGlowTarget = 1u << 2,
  PrecacheDepthTarget = 1u << 3,
  PrecacheDepthNormalTarget = 1u << 4, // 0224: the depth pre-pass with AO normals (depth_normal_layout)
};
// 0224: render target of the depth pre-pass while ambient occlusion is on.
RenderTargetLayout depth_normal_layout() noexcept;
uint32_t precache_bits() noexcept;
// 0550: the bits a configuration `c` would select (Enhanced world's mask bit as currently set).
uint32_t precache_bits_for(const AuroraEnhancedConfig& c) noexcept;
// 0550: builds the scene-end passes' pipelines (no render targets) for configuration `c` on a
// background thread, then calls `done` there; false (nothing started) when `c` is off. For the
// standby set (pipeline_cache.cpp): a later switch to `c` finds them built. stop_prewarm joins it.
bool start_standby_prewarm(const AuroraEnhancedConfig& c, std::function<void()> done) noexcept;
void precache_variants(const gx::PipelineConfig& c, uint32_t bits,
                       const std::function<void(const gx::PipelineConfig&, uint8_t targets)>& emit) noexcept;

} // namespace aurora::gfx::enhanced
