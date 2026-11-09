#pragma once
#include <vector>
#include "render.h"

namespace gx {
// Upper bound on the EFB's supersampling, so that a mistyped digit in vr.txt asks for a
// framebuffer that is merely large rather than one the device cannot allocate at all. 8
// is 5120x4224, past anything either view has a use for.
constexpr int kMaxInternalScale = 8;

// Where compiled shaders are remembered between runs, so that the next run builds them
// all before the game starts instead of at the moment each is first needed. Set before
// render_init; nullptr (the default) builds every shader on first use.
void render_set_shader_cache(const char* path);
void render_init(int internal_scale);

// Building a large cache takes a while -- after a change to the shaders every program is
// compiled again, 6,000 of them in 25 s on a Quest 3 -- and a frontend that must keep
// answering its platform meanwhile (an Android app is killed as not responding after a
// few seconds) builds it in steps instead. Called before render_init, this makes
// render_init only read the file; render_shader_cache_step then builds programs for up to
// `budget_ms` per call and returns true once all are built (or there is no cache), with
// how many are built so far and how many there are. Without it render_init builds them
// all at once, as before.
void render_set_shader_cache_deferred(bool on);
bool render_shader_cache_step(double budget_ms, int* done, int* total);

// Programs the cache does not hold built in the background, where the GL can
// (KHR_parallel_shader_compile), rather than in the frame that first needs one: a draw
// whose program is not built yet is left out of the frame, and drawn once it is. Through
// ANGLE on Metal a program takes about 90 ms to build, so on a headset each new one was a
// visible freeze; this trades it for something appearing a few frames late, the first
// time it is seen. Set before render_init; GCN_ASYNC_SHADERS=0 or 1 overrides it. No
// frontend asks for it since games ship their keys: a missing draw looked more broken than
// the frame its build holds up (graphics.md, "Programs built in the background").
void render_set_async_shaders(bool on);
// Keys a game ships (gcn_add_game's SHADER_KEYS, made by tools/shader_keys.cpp), built
// with the cache's own the first time: on a fresh install the programs a play-through
// needs are compiled before the game starts rather than in the middle of play, and from
// then on come back as binaries with the rest. Only where the GL keeps program binaries:
// elsewhere they would be compiled again on every run. `data` must outlive render_init.
// Set from a static constructor, so it only stores the pointer.
void render_set_shader_keys(const void* data, size_t size);
// After render_init: whether building the cache will compile its programs from source,
// because no binaries for this driver are stored yet -- the first run after an install,
// a driver update or a change to the shaders. False where the GL keeps no binaries.
bool render_shader_cache_compiling();

// Re-scale the EFB between frames. Call only when a whole frame is about to be drawn:
// it discards the EFB's contents and every texture an EFB copy has produced.
void render_set_internal_scale(int scale);

void render_set_window_size(int w, int h);
// Flat output at 16:9 instead of 4:3: perspective draws see 4/3 wider, orthographic (2D) draws
// keep their shape in the middle 4:3, and the frame fills a 16:9 window. Safe to call any time.
void render_set_widescreen(bool on);

// Redirect the finished frame somewhere other than the window framebuffer, e.g. a VR
// swapchain image. 0 restores the default.
void render_set_output_fbo(unsigned fbo);

bool render_execute(Batch& b);

// Frames presented so far, for tracing what a display frame is actually showing.
uint32_t present_count();


// ---- VR ----
// Set the eye projection, the eye's transform relative to the game's camera, and the
// frame the flat 2D elements are painted on (render_hud_frame).
void render_set_vr_eye(const float proj[16], const float view[16], const float hud[16]);

// Builds that frame: a quad `dist` game units in front of the game's camera, `scale` of
// the vertical field of view tall, its centre `height` game units above the forward axis,
// tilted back by `pitch_rad` (0 standing vertical). `tan_half_fovy` is tan of half that
// field. Both eyes must be given the same frame, or there is nothing for them to fuse
// into.
void render_hud_frame(float dist, float tan_half_fovy, float scale, float height,
                      float pitch_rad, float out[16]);

// Fold the stereo view part of the way back into theater. At 0 an eye shows the game's own
// frame flat on `panel` -- a matrix from the game's clip space to a rectangle in the eye's
// space, built like render_hud_frame -- cropped to it and black around it, which is what
// theater looks like from that eye. At 1 (or more) it is ordinary stereo. Every vertex
// moves between the two in step with its own disparity; see morph_chain. Applies to the
// eye passes that follow, until set again.
void render_set_vr_morph(float t, const float panel[16]);

// Rotate the world back by the game's chase-camera pitch, so the sea comes out level with
// the room instead of sloping. Applies to world geometry only: the HUD frame is placed in
// the headset's own space and stays where it is put. 0 renders what the game draws.
void render_set_world_pitch(float pitch_rad);
// For a game whose chase camera's pitch is not fixed: asked once per batch, on the render
// thread, as the eyes are prepared (unless the game's own eye has taken over), for the
// pitch to take out of that frame, in radians. Returning false keeps the last pitch set.
// Reading the camera on the guest thread and handing it over is the game's business; the
// hook is only where it reaches the renderer, on the renderer's own thread.
using WorldPitchHook = bool (*)(const Batch& b, float* pitch_rad);
void render_set_world_pitch_hook(WorldPitchHook hook);

// A game's depth layers in stereo, for a game that confines layers to bands of the depth
// buffer through the viewport's z range. A perspective draw confined to depths from
// `background_from` to 1 is background, drawn at infinity: turned with the head, the same in
// both eyes. One confined to depths no further than `foreground_to` is foreground -- a weapon
// or a visor, modelled large and far off because a flat picture shows only angular size --
// and is scaled towards the camera by `foreground_scale`, keeping its angular size in each
// eye. 0 turns either off, which is the default.
// A third layer, for a game whose HUD is geometry in a band of its own nearer than the
// weapon's: a perspective draw confined to depths no further than `hud_to` is scaled
// towards the camera by `hud_scale` instead, which is how such a HUD, modelled far out in
// view space, is brought to arm's length without moving the weapon. 0 turns it off.
void render_set_depth_layers(float background_from, float foreground_to, float foreground_scale,
                             float hud_to = 0.0f, float hud_scale = 1.0f);
// A HUD element modelled far nearer than the rest of its layer -- a flat piece of the
// visor a few units out where the rest is some tens -- is still scaled by `hud_scale`, and
// lands a few tens of centimetres from the eyes, where it hurts to look at. A draw in the
// HUD layer whose nearest vertex is nearer than `below` (view-space units, before the
// layer's scale) is scaled about the camera on top of that, so that its nearest vertex is
// at `to`: the same angular size, further off. The whole state goes together, judged by
// its draws in the frame. 0 turns it off, which is the default.
void render_set_hud_push(float below, float to);

// What a perspective draw the game placed in view space itself is in an eye (PixelState::
// view_space). Off, the default, it is held in front of the camera: drawn with the head's
// transform but outside the world's pitch correction and the game's own eye. On, it is a
// billboard in the world -- a sprite turned to face the camera but standing at a point in
// the scene -- and is drawn as world geometry.
void render_set_view_space_world(bool on);

// A game's draw filter for the eyes (gx::EyeFilter in render.h), installed by the
// frontend from vr::GameHooks::eye_filter.
void render_set_eye_filter(EyeFilter filter);
// A decoded texture's content hash (TexData::hash), 0 for an id the renderer does not hold.
uint64_t render_texture_hash(uint32_t id);

// The game's own eye. The renderer knows how to re-project a batch for an eye and how to
// take the chase camera's pitch out of the world; where else an eye might stand -- on the
// player's vehicle, say -- is a fact about a game, and comes from a hook the game project
// installs. While first person is on (render_set_first_person) the hook is called once per
// frame, before anything is drawn from the batch, with the off-screen passes already
// marked in `offscreen` (one byte per command, as the renderer skips them) and the
// renderer's frame count, which a hook can use to notice a gap. It fills in `out` and
// returns true, or returns false to leave the eye with the chase camera for that frame:
//   view_to_eye  what world geometry goes through before the eye's view, in place of the
//                world-pitch rotation (column-major 4x4)
//   hud_to_eye   what the HUD frame goes through before the eye's view: the identity, or
//                a rotation that keeps it upright as the view tilts (column-major 4x4)
//   hide         one byte per command, 1 to leave that draw out (the player's own model,
//                say); may be left empty. A hidden draw's textures are kept alive.
// Applies to the eye passes only; the flat view is untouched.
struct EyeOverride {
    float view_to_eye[16];
    float hud_to_eye[16];
    std::vector<uint8_t> hide;
};
using EyeHook = bool (*)(const Batch& b, const std::vector<uint8_t>& offscreen, uint32_t frame, EyeOverride& out);
void render_set_eye_hook(EyeHook hook);
void render_set_first_person(bool on);
bool render_first_person();   // what render_set_first_person last said

// A game's look at every frame the renderer is given, however it is drawn -- flat, as a
// stereo pair or through the eyes: called once per batch, before anything is drawn from it
// and before the eye hook, with the off-screen passes marked as the eye hook gets them. For
// a game that follows something on screen every frame whatever the view -- its player's
// vehicle, for motion telemetry, say.
using FrameHook = void (*)(const Batch& b, const std::vector<uint8_t>& offscreen);
void render_set_frame_hook(FrameHook hook);

// Draw one eye's view of a batch into `fbo`. Both eyes share the vertex buffer, the
// CPU-side transform and any render-to-texture results, so only uniforms and draw
// calls are repeated. Pass do_copies for the first eye only.
bool render_execute_eye(Batch& b, unsigned fbo, int w, int h, bool do_copies);

// For a frontend that times its frames: called after each frame's rendering with how long
// it took. A frame of more than 50 ms is logged with what in it is known to stall -- the
// programs compiled for it, and the draws that were the first of a program with a blend
// state, where a GL over Metal (ANGLE) builds a pipeline -- and either way the counts start
// again.
void render_report_frame(const char* what, double ms);

// The flat frame as a stereo pair for a theater panel, like a 3D film: the batch drawn
// twice, into `fbo_l` then `fbo_r`, with every perspective draw seen from half `eye_sep`
// to the side of the game's camera and its frustum sheared so that the depth at which it
// is `panel_width` wide has no disparity -- the panel's own plane, so the panel is a
// window -- and points at infinity have the eyes' full separation. Both in game units.
// Orthographic draws, the 2D elements, are the same in both and sit on the panel. The
// batch is uploaded once; the second pass redoes the EFB copies from its own EFB. The
// frame dumps get `_l` and `_r` suffixes. Returns whether a frame was presented.
bool render_execute_stereo_pair(Batch& b, unsigned fbo_l, unsigned fbo_r, float eye_sep,
                                float panel_width);
// In a stereo pair, a perspective draw the game confines to a band of the depth buffer no
// deeper than `band` (its HUD layer, say) is drawn on the panel itself, with no disparity,
// as a film's subtitles are; a draw with no band at all (a zero-width z range) is not
// affected. 0, the default, pins nothing.
void render_set_panel_band(float band);
extern bool g_cull_swap;
extern const char* g_dump_dir;
extern int g_dump_every;
}  // namespace gx
