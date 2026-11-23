#pragma once
// World effects (0350, include/aurora/world_fx.h): surface mask, reflections, wet ground, snow
// sparkle, haze, particles, colour grade and tilt-shift at the end of the 3D scene.
#include <aurora/world_fx.h>

#include <cstdint>

namespace aurora::gx {
struct PipelineConfig;
struct DrawData;
} // namespace aurora::gx

namespace aurora::gfx {
struct RenderTargetLayout;
} // namespace aurora::gfx

namespace aurora::gfx::world_fx {

// --- FIFO thread ---
// Effects configured on (the configuration is synced at the call).
bool active() noexcept;
// A draw of a classified surface (GX_AURORA_SET_SURFACE kind != 0) is about to be recorded.
void note_draw(const gx::PipelineConfig& config, uint8_t kind, const gx::DrawData& data) noexcept;
// GX_AURORA_WORLD_FX_END.
void scene_end() noexcept;
// 0570: GX_AURORA_SET_SURFACE into (begin) or out of AURORA_SURFACE_CHARACTER.
void character_scope(bool begin, bool part = false, bool mirror = false, bool attach = false) noexcept;
// 0580: the open group's anchor in view space (a mirrored static model: its first draw's vertices)
void group_local_anchor(float x, float y, float z) noexcept;
// 0580: the open group is a mirrored static model still without an anchor
bool wants_anchor() noexcept;
// New frame recording (drops what the last frame noted without a scene end).
void begin_frame() noexcept;

// --- precache (any thread), 0451 ---
// The latest set configuration re-draws surfaces into the mask (reflections / wet ground / rain
// ripples / snow sparkle switched on, whatever the weather is now).
bool precache_mask() noexcept;
// A remembered config is shaped like one note_draw derives for the mask (no blending, colour +
// alpha writes, depth test, destination alpha 255 for water or 128 for ground); such configs are
// also built for mask_target_layout (a game draw of that shape gets one pipeline too many).
bool is_mask_variant(const gx::PipelineConfig& c) noexcept;
const RenderTargetLayout& mask_target_layout() noexcept;

} // namespace aurora::gfx::world_fx
