// The renderer's entry points a game's own sources may call, for the benchmark, which has
// no renderer: a game installs its eye hook from a static initializer and turns its own
// eye on from a vr::GameHooks hook, and the benchmark compiles those sources too, since a
// patch in the game's tables may call into them. Nothing here is ever reached there -- the
// benchmark presents no frame -- so each is a no-op. Only the hook-installation API
// belongs here; a game source that needs more of the renderer than this needs a hook.
#include "gx/render_gl.h"

namespace gx {
void render_set_eye_hook(EyeHook) {}
void render_set_first_person(bool) {}
bool render_first_person() { return false; }
void render_set_frame_hook(FrameHook) {}
void render_set_world_pitch_hook(WorldPitchHook) {}
void render_set_shader_keys(const void*, size_t) {}
uint32_t present_count() { return 0; }
}  // namespace gx
