// GL back end for the GX pixel pipeline. Runs on the main thread.
//
// One source serves desktop OpenGL 3.3 core and OpenGL ES 3.2; where the two profiles
// differ, the difference is confined to the small block of helpers below.
#include "../runtime.h"
#include "render.h"
#include "render_gl.h"
#include "shadergen.h"
#include "gl.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

bool write_png(const char* path, const uint8_t* rgba, int w, int h);

namespace gx {

static const int EFB_W = 640, EFB_H = 528;

// The renderer's half of GCN_FRAMETIME (see xf.cpp): one line per batch with the time
// spent uploading textures, uploading the vertex buffer, and issuing the frame, plus how
// many times the full pixel state had to be re-applied. CPU time only: the GPU runs
// behind, so a long frame here is driver and submission cost, not fill.
static bool frametime_on() {  // on first use: the app sets its environment after static init
    static const bool on = getenv("GCN_FRAMETIME") != nullptr;
    return on;
}
static inline double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

// Logic-op blending does not exist in GL ES: there is no GL_COLOR_LOGIC_OP, no
// glLogicOp, and none of the GL_CLEAR..GL_SET enums. The game uses GX logic ops for
// only a few effects, so on ES they are skipped -- the draw writes through with
// blending off, which matches the common GX_LO_COPY case. Reproducing the rest would
// mean reading the framebuffer in the generated TEV shader via
// GL_EXT_shader_framebuffer_fetch.
#ifdef GCN_GL_ES
static inline void set_logic_op_off() {}
static inline void set_logic_op(uint32_t) {}
// ES spells this with the float suffix; desktop GL 3.3 core only has the double form.
static inline void clear_depth(float d) { glClearDepthf(d); }
// ES has no sampler LOD bias. GX uses it to nudge mip selection, so skipping it can
// pick a slightly different mip level than hardware would.
static inline void set_lod_bias(GLuint, float) {}
#else
static const GLenum kLogicOp[16] = {GL_CLEAR, GL_AND, GL_AND_REVERSE, GL_COPY, GL_AND_INVERTED, GL_NOOP, GL_XOR, GL_OR,
                                    GL_NOR, GL_EQUIV, GL_INVERT, GL_OR_REVERSE, GL_COPY_INVERTED, GL_OR_INVERTED, GL_NAND, GL_SET};
static inline void set_logic_op_off() { glDisable(GL_COLOR_LOGIC_OP); }
static inline void set_logic_op(uint32_t mode) {
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(kLogicOp[mode & 15]);
}
static inline void clear_depth(float d) { glClearDepth(d); }
static inline void set_lod_bias(GLuint s, float bias) {
    glSamplerParameterf(s, GL_TEXTURE_LOD_BIAS, bias);
}
#endif

// The inputs a program's uniforms were last computed from; see AppliedState. `gen` and
// `view_gen` say whether the record is current at all (g_uniform_gen, g_view_gen).
struct UniformShadow {
    uint32_t gen = 0, view_gen = 0;
    float proj[7];
    uint8_t view_space, background, foreground, fg_layer, hud_layer, flat3d, screen, frame_screen, restore, facing;
    float hud_push;
    float wide;                // the widescreen squeeze this draw's projection was given
    float viewport[6];
    uint32_t scissor_off;      // bp[0x59]
    uint32_t psize_reg;        // bp[0x22]
    uint32_t point_tex;        // point_tex_key(): what u_points and u_point_off were sent for
    uint32_t tev_reg[4][2], tev_konst[4][2];
    uint32_t ksel[8];          // bp[0xF6..0xFD]
    uint32_t aref;             // bp[0xF3]
    uint32_t ind[9];           // bp[0x06..0x0E]
    uint32_t indcs[2];         // bp[0x25..0x26]
    uint32_t fog[5];           // bp[0xEE..0xF2]
    uint32_t screen_uv;
    float ripple[2];
    float tsz[16];
    uint32_t texsnap;
};

struct Program {
    GLuint prog;
    GLint u_proj, u_vp_a, u_vp_b, u_point_size, u_tex, u_reg, u_kstage, u_texsize, u_indmtx, u_indscale,
        u_alpharef, u_fog, u_fogmode, u_fogcolor, u_indcoordscale, u_texsnap, u_zbias, u_zplane, u_dstalpha;
    GLint u_vr, u_view, u_crop, u_zproj, u_screen_uv, u_screen_px, u_screen_ripple, u_uvgrad;
    GLint u_points, u_point_off;
    mutable UniformShadow shadow;
};

// Samples kept per hardware pixel, in each axis. See render_set_internal_scale.
static int g_scale = 2;

// VR eye state. When active, perspective batches are re-projected for the eye and the
// orthographic ones (the 2D HUD) are painted on a frame standing out in front of the
// game's camera -- see render_hud_frame.
static bool g_vr_active = false;
static float g_vr_proj[16], g_vr_view[16];
// The eye's view as the frontend gave it (render_set_vr_eye). g_vr_view is this times the
// batch's view turn (Batch::view_turn, set_view_turn), from the camera the game turned to
// the one the viewer stands at, so that the turn leaves the world where it was.
static float g_vr_view_head[16];
// The eye's view with the world's pitch taken out, for world geometry. The HUD frame uses
// g_vr_view untouched -- it is placed in the headset's space, not the game's.
static float g_vr_view_world[16];
// Depth layers in stereo (render_set_depth_layers). Both are off unless a game turns them
// on, because where a game puts its layers in the depth buffer -- if it uses them at all --
// is the game's own convention.
//
// Background: a perspective draw whose viewport confines it to depths from g_bg_from to
// 1 is drawn at infinity, through g_vr_view_sky, g_vr_view_world with its translation
// taken out. 0 is off.
static float g_vr_view_sky[16];
static float g_bg_from = 0.0f;
// Foreground: a perspective draw whose viewport confines it to depths no further than
// g_fg_to is scaled towards the camera by g_fg_scale -- the same angular size in each eye,
// but that much nearer and that much smaller. 0 is off.
static float g_fg_to = 0.0f;
static float g_fg_scale = 1.0f;
static float g_vr_view_fg[16], g_vr_view_world_fg[16];
// A HUD layer: the same, for a perspective draw confined to depths no further than
// g_hud_to, with its own scale -- a game that models its HUD as geometry a long way out
// in view space, in a band of its own nearer than the weapon's, wants it nearer than the
// weapon's scale would put it. Takes precedence over the foreground. 0 is off.
static float g_hud_to = 0.0f;
static float g_hud_scale = 1.0f;
// render_set_hud_push: below which distance a HUD-layer draw is pushed out, and to where.
// g_hud_push holds each state's factor for the frame (1 for none), g_cur_hud_push the
// current draw's, read by apply_state.
static float g_hud_push_below = 0.0f, g_hud_push_to = 0.0f;
static std::vector<float> g_hud_push;
static float g_cur_hud_push = 1.0f;
static float g_vr_view_hud[16], g_vr_view_world_hud[16];
// Whether a perspective draw placed in view space is a billboard in the world rather than
// something held before the camera (render_set_view_space_world). Off unless a game says.
static bool g_view_space_world = false;
static float g_world_pitch = 0.0f;
static float g_vr_hud[16];
// What the HUD frame goes through before the eye's view: the identity, unless the game's
// eye hook put something there -- an eye that tilts with a vehicle wants the HUD turned
// back to its level frame. g_vr_hud_eff is this times g_vr_hud, and is what the HUD is
// drawn with.
static float g_hud_xform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
static float g_vr_hud_eff[16];
// What world geometry goes through before the eye's own view: the chase camera's pitch
// taken back out (world_pitch_matrix), or whatever the game's eye hook put there.
// g_vr_view_world is g_vr_view times this.
static float g_world_xform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
// First person (render_set_first_person): while on, the game's eye hook is asked per frame
// where the eye stands, and g_eye_overridden says it answered for the frame being drawn.
static EyeHook g_eye_hook = nullptr;
static WorldPitchHook g_pitch_hook = nullptr;
// Which placement apply_state gave the last state in an eye, for the eye's draw log.
static const char* g_eye_layer_name = "-";
static EyeFilter g_eye_filter = nullptr;
static bool g_fp_on = false;
static bool g_eye_overridden = false;
// Per command of the batch being drawn: 1 for a draw the game's eye leaves out (the
// player's own model, say). Empty when nothing is hidden.
static std::vector<uint8_t> g_hide;
// Per command: 1 for a draw of an object the game's eye filter asked for last
// (EyeLateObject), which g_hide also leaves out of its place. Empty when there is none.
static std::vector<uint8_t> g_late;
// Per pixel state of the batch being drawn: 1 for one the game's eye filter called a 3D
// draw composed into the 2D picture (EyeFlat), which an eye paints on the HUD frame where
// the flat frame puts it. Empty when there is none. g_cur_flat3d is the state apply_state
// is applying, in an eye.
static std::vector<uint8_t> g_flat3d;
static bool g_cur_flat3d = false;
// Likewise for an orthographic draw over the whole picture (EyeScreen), which an eye draws
// across its whole field, fixed to the head: the game's frame mapped onto the eye's own clip
// space, enlarged by this much so that its edges fall outside what the eye can see.
// 1 per such state; 2 for one drawn on the HUD frame with its edges taken out past the
// field (EyeFrameScreen).
static std::vector<uint8_t> g_screen;
static uint8_t g_cur_screen = 0;
static constexpr float kEyeScreenOverscan = 1.1f;
// How far an EyeFrameScreen draw's edge vertices go along the HUD frame's plane, in the
// frame's own half-sizes: far enough to be well past any eye's field. The vertex shader
// has it as a literal (u_vr 6).
static constexpr float kFrameScreenReach = 40.0f;
// Per EyeFrameScreen state, eight texture coordinates' gradients over the game's clip x
// and y (ds/dx, ds/dy, dt/dx, dt/dy), fitted to the state's own vertices: what the shader
// carries a coordinate out by with its vertex. Zero for a coordinate that is not linear in
// position over the draw, which then stays as it is. g_cur_uvgrad is the current draw's.
static std::vector<float> g_frame_grad;
static const float* g_cur_uvgrad = nullptr;
// And for a copy of part of the frame put back over itself with a depth test (EyeRestore):
// per state, 1 for such a draw, with the box its draws cover in the game's clip space
// (four floats a state), which an eye stretches over its whole field. g_restore_tex holds
// the copies those draws sample, at which the eye takes its own grab (g_restore_grab).
static std::vector<uint8_t> g_restore;
static std::vector<float> g_restore_box;
static std::vector<uint32_t> g_restore_tex;
static bool g_cur_restore = false;
static const float* g_cur_restore_box = nullptr;
// And for an orthographic panel the game moves about its picture by its projection's
// translation (EyeFacing): 1 per such state, drawn on the HUD frame turned to face it.
static std::vector<uint8_t> g_facing;
static bool g_cur_facing = false;
// And for a perspective draw in a scaled layer's band that is drawn at its modelled
// distance all the same (EyeTrueScale).
static std::vector<uint8_t> g_true_scale;
static bool g_cur_true_scale = false;
// What the frame's facing panels (EyeFacing) cover together in the game's clip space, for
// the late objects inside it (late_panel_view); g_has_facing_box false when there is none.
// g_cur_late_view is the view apply_state draws such an object with, null otherwise.
static float g_facing_box[4];
static bool g_has_facing_box = false;
static const float* g_cur_late_view = nullptr;
// The morph between theater and stereo; see render_set_vr_morph. 1 is plain stereo.
static float g_morph = 1.0f;
// A flat frame drawn as one of a stereo pair (render_execute_stereo_pair): where the eye
// stands beside the game's camera, 0 for the plain flat frame, and half the width of the
// panel the pair is shown on, both in game units.
static float g_flat_eye = 0.0f, g_flat_halfw = 1.0f;
static float g_panel_band = 0.0f;   // render_set_panel_band
static float g_panel[16];
// Whether the vertex shader can write clip distances, which is what crops the morph.
static bool g_clip_ok = false;
// What a draw is to the morph: where it ends up in stereo decides what it is folded from.
enum class MorphKind { World, CameraHeld, Hud, Screen };
// An EyeScreen draw's matrix: the game's frame mapped onto the eye's own clip space,
// enlarged past the field's edges, with no view between them.
static void eye_screen_matrix(const float P[16], float M[16]) {
    for (int c = 0; c < 4; c++) {
        M[c * 4 + 0] = kEyeScreenOverscan * P[c * 4 + 0];
        M[c * 4 + 1] = kEyeScreenOverscan * P[c * 4 + 1];
        M[c * 4 + 2] = 0.0f;
        M[c * 4 + 3] = P[c * 4 + 3];
    }
}
static void morph_chain(const float P[16], MorphKind kind, float chain[16], float fog[16],
                        float crop[16]);
// What the eye has drawn so far, standing in for the game's copy of the finished frame.
// There are two because the two users want different moments: the water refracts the
// scene as it stood *before* the water was drawn, and the spray refracts it after, water
// included. One texture serving both would quietly re-point the water's lookup -- and the
// game's own final composite, which samples the same whole-frame copy -- at whatever the
// spray grabbed later. See grab_eye().
// A third, g_restore_grab, is taken where the game copies part of the frame that an
// EyeRestore draw later puts back.
struct EyeGrab { GLuint tex; int w, h; bool srgb; };
static EyeGrab g_eye_grab, g_spray_grab, g_restore_grab;
static int g_eye_w, g_eye_h;
static bool is_fullscreen_tex(uint32_t id);
static GLuint g_efb_fbo, g_efb_color, g_efb_depth;
// Copy of the EFB as it looked at the last present. The display copy is immediately
// followed by an EFB clear, so repainting has to come from here, not the live EFB.

// The vertex and index buffers a batch is drawn from, and the VAO holding both. There are
// several, used in turn, because refilling one the GPU is still drawing from makes the
// driver wait for it. At the headset's own eye size that wait is rare, but the eyes' GPU
// time is what decides it: at 1.4x with 4x MSAA, one set left the flat pass at 27 ms a
// race frame on a Quest 3 (p50), nearly all of it in glBufferData waiting for the last
// frame's eyes; three sets took it to 4.9 ms, the same as at the old size. The GPU runs
// at most a frame or two behind, so three is enough. g_vao, g_vbo and g_ebo are the set
// in use; execute_batch moves them on.
static constexpr int kVertexRing = 3;
//
// Each set's vertex buffer holds the batch's packed vertices (Batch::packed), a region per
// texture coordinate count, so each set has a vertex array per count over the same two
// buffers: g_vaos[set][n] reads n coordinates at packed_stride(n). g_vao is the one bound.
static GLuint g_vaos[kVertexRing][9], g_vbos[kVertexRing], g_ebos[kVertexRing];
static int g_vertex_set = 0;
static GLuint g_vao, g_vbo, g_ebo;

// Binds the vertex layout for a draw with `ntex` texture coordinates.
static void bind_vertex_format(uint32_t ntex) {
    const GLuint vao = g_vaos[g_vertex_set][ntex];
    if (vao == g_vao) return;
    g_vao = vao;
    glBindVertexArray(vao);
}

// Without glDrawElementsBaseVertex (GL ES 3.0 without the extension), the base vertex is
// folded into each draw's indices when the batch is uploaded (upload_indices) and the draw
// is an ordinary one. A base vertex is often negative -- it is where the draw's vertices
// were packed less where they were first numbered -- so pointing the vertex arrays at it
// instead would not do.
static bool g_base_vertex = true;

// What stalled a frame, for render_report_frame: programs compiled, and draws that were the
// first of their program and blend state (g_seen_states), timed.
static struct {
    int compiled = 0, new_states = 0, waited = 0;
    double compile_ms = 0.0, new_state_ms = 0.0;
} g_stall;
static std::unordered_set<uint64_t> g_seen_states;
static bool g_state_new;  // the state applied for the next draw has not been drawn with
// The state applied for the next draw has a program still being built in the background
// (render_set_async_shaders), so the draw is left out.
static bool g_draw_skip;

// One draw command, from the packed region its texture coordinate count selects.
static void draw_cmd(const Batch& b, const Cmd& c) {
    static const GLenum mode[3] = {GL_TRIANGLES, GL_LINES, GL_POINTS};
    if (g_draw_skip) {
        g_stall.waited++;
        return;
    }
    bind_vertex_format(draw_ntex(b.states[c.state]));
    const void* first = (const void*)(uintptr_t)(c.first * sizeof(uint32_t));
    const bool timed = g_state_new;
    const auto t0 = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (g_base_vertex) glDrawElementsBaseVertex(mode[c.prim], c.count, GL_UNSIGNED_INT, first, c.base_vertex);
    else glDrawElements(mode[c.prim], c.count, GL_UNSIGNED_INT, first);
    if (timed) {
        g_state_new = false;
        g_stall.new_states++;
        g_stall.new_state_ms += ms_since(t0);
    }
}

void render_report_frame(const char* what, double ms) {
    if (ms > 50.0)
        fprintf(stderr, "[stall] %s frame: %.0f ms; %d programs compiled (%.0f ms), %d new program/blend states drawn (%.0f ms), "
                "%d draws left out awaiting a program\n",
                what, ms, g_stall.compiled, g_stall.compile_ms, g_stall.new_states, g_stall.new_state_ms, g_stall.waited);
    g_stall = {};
}

// The batch's indices into the bound element buffer, each draw's base vertex added in
// where the GL cannot add it (see g_base_vertex).
static void upload_indices(const Batch& b) {
    if (g_base_vertex) {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, b.indices.size() * sizeof(uint32_t), b.indices.data(), GL_STREAM_DRAW);
        return;
    }
    static std::vector<uint32_t> rebased;
    rebased = b.indices;
    for (const Cmd& c : b.cmds) {
        if (c.type != CmdType::Draw) continue;
        for (uint32_t i = c.first; i < c.first + c.count; i++) rebased[i] += (uint32_t)c.base_vertex;
    }
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, rebased.size() * sizeof(uint32_t), rebased.data(), GL_STREAM_DRAW);
}
static GLuint g_copy_prog, g_copy_vao;
static GLint g_copy_u_src, g_copy_u_rect, g_copy_u_mode, g_copy_u_depth;
static GLuint g_blit_prog;
static GLint g_blit_u_src, g_blit_u_rect;
static GLuint g_copy_fbo;
// The morph's background: see draw_morph_background.
static GLuint g_bg_prog;
static GLint g_bg_u_mvp, g_bg_u_crop, g_bg_u_quad, g_bg_u_col;
static GLuint g_vs;
static std::unordered_map<ShaderKey, Program, ShaderKeyHash> g_programs;
// `grab` marks one of the spray's screen-space grabs; see is_grab_copy().
// `depth`: an EFB copy of the depth buffer, whose texels are a depth packed into bytes. A
// filter would average neighbouring depths a byte at a time, which at an edge is nonsense
// -- a fog volume showed it as bright specks along a waterline -- so it is always sampled
// nearest. (At the console's own resolution a full-screen quad lands on texel centres and
// a filter changes nothing; at a higher internal scale it does not.)
struct GlTex { GLuint tex; uint32_t w, h, levels; bool efb; bool grab; bool depth; uint32_t last_used; uint64_t hash; };
static std::unordered_map<uint32_t, GlTex> g_textures;
// Frames counted here rather than reusing the GX frame counter, so eviction works the
// same for any frontend. Textures the game stops using are released: a race streams
// them continuously, and without this both the GL objects and the decoded copies grow
// without bound until the device runs out of memory and crawls.
static uint32_t g_render_frame;
static std::unordered_map<uint32_t, GLuint> g_samplers;
static int g_win_w, g_win_h;
bool g_cull_swap = false;

static GLuint compile(GLenum type, const std::string& src) {
    GLuint sh = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        fprintf(stderr, "%s\n", src.c_str());
        fatal("shader compile failed: %s", log);
    }
    return sh;
}

static GLuint link(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fatal("program link failed: %s", log);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Shader cache
//
// A TEV configuration is compiled the first time a draw uses it, on the render thread, in
// the middle of a frame. The race start brings in nine at once (spray, wake, the speed
// effects) and later stretches of a course add more: on this machine's compiler that
// frame took 60 ms against a 4 ms norm, and a mobile driver takes tens of milliseconds
// per program, so a burst of them is a visible hitch at a fixed spot in the course.
//
// So every key that gets compiled is written to a file, and the next run builds all of
// them in render_init before the game starts. Where the driver can hand back program
// binaries (GL ES 3.0 can; macOS reports no binary formats) those are stored too, which
// turns the second run's startup from compiling into loading. A binary is only trusted
// with the same driver and the same generated source: the file carries the GL strings
// and each record a hash of the GLSL its key generates, and either changing drops back
// to compiling that record and rewriting the file.
// ---------------------------------------------------------------------------
#if defined(GCN_GL_ES) || defined(__APPLE__)
#define GCN_HAVE_PROGRAM_BINARY 1
#else
// The desktop glad loader stops at 3.3; program binaries are 4.1.
#define GCN_HAVE_PROGRAM_BINARY 0
#endif

static std::string g_shader_cache_path;
static bool g_binaries_supported = false;
static constexpr uint32_t kCacheMagic = 0x43535257;  // "WRSC"
static constexpr uint32_t kCacheFormat = kShaderKeyFormat;

static uint64_t hash_str(const char* s, uint64_t h = 1469598103934665603ull) {
    for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull;
    return h;
}

// A stored binary is a link of one fragment shader against the one vertex shader, and
// nothing in the key says which vertex shader that was -- so its source goes in beside
// the fragment's. Changing it then retires every record in the file rather than quietly
// restoring binaries that predate the change.
static uint64_t g_vs_hash = 1469598103934665603ull;
static uint64_t program_src_hash(const char* fs) { return hash_str(fs, g_vs_hash); }

// Identifies the driver whose binaries the file holds.
static uint64_t driver_id() {
    uint64_t h = hash_str((const char*)glGetString(GL_VENDOR));
    h = hash_str((const char*)glGetString(GL_RENDERER), h);
    return hash_str((const char*)glGetString(GL_VERSION), h);
}

static void shader_cache_write_record(FILE* f, const ShaderKey& k, uint64_t src_hash, GLuint prog) {
    fwrite(&k, sizeof(k), 1, f);
    fwrite(&src_hash, sizeof(src_hash), 1, f);
    uint32_t fmt = 0, len = 0;
#if GCN_HAVE_PROGRAM_BINARY
    std::vector<uint8_t> bin;
    if (g_binaries_supported) {
        GLint n = 0;
        glGetProgramiv(prog, GL_PROGRAM_BINARY_LENGTH, &n);
        if (n > 0) {
            bin.resize((size_t)n);
            GLenum e = 0;
            GLsizei got = 0;
            glGetProgramBinary(prog, n, &got, &e, bin.data());
            if (got > 0) { fmt = (uint32_t)e; len = (uint32_t)got; }
        }
    }
    fwrite(&fmt, sizeof(fmt), 1, f);
    fwrite(&len, sizeof(len), 1, f);
    if (len) fwrite(bin.data(), len, 1, f);
#else
    (void)prog;
    fwrite(&fmt, sizeof(fmt), 1, f);
    fwrite(&len, sizeof(len), 1, f);
#endif
}

static void shader_cache_write_header(FILE* f) {
    const uint32_t magic = kCacheMagic, ver = kCacheFormat;
    const uint64_t drv = driver_id();
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&ver, sizeof(ver), 1, f);
    fwrite(&drv, sizeof(drv), 1, f);
}

// Starts compiling and linking a program from source. Nothing here waits for the result:
// with parallel compilation (g_parallel_compile) the GL builds it in the background, and
// finish_program, or any query of the program, waits for it.
static GLuint start_program(const std::string& src, GLuint* fs_out) {
    if (getenv("GCN_DUMP_SHADERS")) fprintf(stderr, "---- shader %zu ----\n%s\n", g_programs.size(), src.c_str());
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    const char* text = src.c_str();
    glShaderSource(fs, 1, &text, nullptr);
    glCompileShader(fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, g_vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
#if GCN_HAVE_PROGRAM_BINARY
    if (g_binaries_supported) glProgramParameteri(p, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
#endif
    glLinkProgram(p);
    *fs_out = fs;
    return p;
}

// The link's outcome, which a program that fails to build makes fatal.
static void finish_program(GLuint p, GLuint fs, const std::string& src) {
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
            fprintf(stderr, "%s\n", src.c_str());
            fatal("shader compile failed: %s", log);
        }
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fatal("program link failed: %s", log);
    }
    glDeleteShader(fs);
}

// Compiles a program from source, or restores it from a binary when one is given and
// the driver accepts it. Returns 0 if the binary was refused.
static GLuint build_program(const ShaderKey& k, const std::string& src, const uint8_t* bin, uint32_t fmt, uint32_t len) {
#if GCN_HAVE_PROGRAM_BINARY
    if (bin && len && g_binaries_supported) {
        GLuint p = glCreateProgram();
        glProgramBinary(p, (GLenum)fmt, bin, (GLsizei)len);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (ok) return p;
        glDeleteProgram(p);
        return 0;
    }
#else
    (void)bin; (void)fmt; (void)len;
#endif
    GLuint fs;
    GLuint p = start_program(src, &fs);
    finish_program(p, fs, src);
    (void)k;
    return p;
}

static const Program& register_program(const ShaderKey& k, GLuint p) {
    Program pr{};
    pr.prog = p;
    pr.u_proj = glGetUniformLocation(p, "u_proj");
    pr.u_vp_a = glGetUniformLocation(p, "u_vp_a");
    pr.u_vp_b = glGetUniformLocation(p, "u_vp_b");
    pr.u_point_size = glGetUniformLocation(p, "u_point_size");
    pr.u_vr = glGetUniformLocation(p, "u_vr");
    pr.u_view = glGetUniformLocation(p, "u_view");
    pr.u_crop = glGetUniformLocation(p, "u_crop");
    pr.u_uvgrad = glGetUniformLocation(p, "u_uvgrad");
    pr.u_zproj = glGetUniformLocation(p, "u_zproj");
    pr.u_screen_uv = glGetUniformLocation(p, "u_screen_uv");
    pr.u_screen_px = glGetUniformLocation(p, "u_screen_px");
    pr.u_screen_ripple = glGetUniformLocation(p, "u_screen_ripple");
    pr.u_tex = glGetUniformLocation(p, "u_tex");
    pr.u_reg = glGetUniformLocation(p, "u_reg");
    pr.u_kstage = glGetUniformLocation(p, "u_kstage");
    pr.u_texsize = glGetUniformLocation(p, "u_texsize");
    pr.u_indmtx = glGetUniformLocation(p, "u_indmtx");
    pr.u_indscale = glGetUniformLocation(p, "u_indscale");
    pr.u_alpharef = glGetUniformLocation(p, "u_alpharef");
    pr.u_fog = glGetUniformLocation(p, "u_fog");
    pr.u_fogmode = glGetUniformLocation(p, "u_fogmode");
    pr.u_fogcolor = glGetUniformLocation(p, "u_fogcolor");
    pr.u_indcoordscale = glGetUniformLocation(p, "u_indcoordscale");
    pr.u_texsnap = glGetUniformLocation(p, "u_texsnap");
    pr.u_zbias = glGetUniformLocation(p, "u_zbias");
    pr.u_dstalpha = glGetUniformLocation(p, "u_dstalpha");
    pr.u_points = glGetUniformLocation(p, "u_points");
    pr.u_point_off = glGetUniformLocation(p, "u_point_off");
    pr.u_zplane = glGetUniformLocation(p, "u_zplane");
    glUseProgram(p);
    GLint units[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    glUniform1iv(pr.u_tex, 8, units);
    // The palettes of EFB copies read as CI4 or CI8, on the eight units after the maps'.
    const GLint u_pal = glGetUniformLocation(p, "u_pal");
    if (u_pal >= 0) {
        GLint pal_units[8] = {8, 9, 10, 11, 12, 13, 14, 15};
        glUniform1iv(u_pal, 8, pal_units);
    }
    return g_programs.emplace(k, pr).first->second;
}

void render_set_shader_cache(const char* path) { g_shader_cache_path = path ? path : ""; }

// Building every program the file remembers: shader_cache_open reads the file, then
// shader_cache_step builds its records in order, then the game's keys the file lacks --
// all at once from render_init, or a few milliseconds at a time for a frontend that asked
// for that (render_set_shader_cache_deferred) -- and the last step rewrites the file if
// it has to.
struct CacheLoad {
    std::vector<uint8_t> data;
    size_t pos = 0;
    std::vector<ShaderKey> shipped;  // the game's keys that are not in the file
    size_t shipped_pos = 0;
    // Programs compiled from source are started a batch at a time and only then finished,
    // so the GL's workers build the batch side by side; one at a time, through ANGLE on
    // Metal, 745 took 58.5 s.
    struct Started {
        ShaderKey k;
        std::string src;
        GLuint prog, fs;
    };
    std::vector<Started> batch;
    bool active = false, same_driver = false, stale = false, binaries = false;
    int total = 0, done = 0, from_binary = 0, compiled = 0;
    std::chrono::steady_clock::time_point t0;
};
static CacheLoad g_cache_load;
static bool g_cache_deferred = false;
static const uint8_t* g_shipped_keys = nullptr;
static size_t g_shipped_keys_size = 0;

void render_set_shader_cache_deferred(bool on) { g_cache_deferred = on; }
void render_set_shader_keys(const void* data, size_t size) {
    g_shipped_keys = (const uint8_t*)data;
    g_shipped_keys_size = size;
}
bool render_shader_cache_compiling() {
    return g_binaries_supported && g_cache_load.active && !g_cache_load.binaries;
}

// One record's fields, read from `p`; false at the end or at a truncated record.
struct CacheRecord {
    ShaderKey k;
    uint64_t src_hash;
    uint32_t fmt, len;
    const uint8_t* bin;
};
static bool cache_next(const uint8_t*& p, const uint8_t* end, CacheRecord& r) {
    auto take = [&](void* dst, size_t n) {
        if ((size_t)(end - p) < n) return false;
        memcpy(dst, p, n);
        p += n;
        return true;
    };
    if (!take(&r.k, sizeof(r.k)) || !take(&r.src_hash, 8) || !take(&r.fmt, 4) || !take(&r.len, 4)) return false;
    if ((size_t)(end - p) < r.len) return false;
    r.bin = p;
    p += r.len;
    return true;
}

static void shader_cache_open() {
    CacheLoad& L = g_cache_load;
    L = CacheLoad{};
    if (g_shader_cache_path.empty()) return;
    L.t0 = std::chrono::steady_clock::now();
    if (FILE* f = fopen(g_shader_cache_path.c_str(), "rb")) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0) {
            L.data.resize((size_t)n);
            if (fread(L.data.data(), 1, (size_t)n, f) != (size_t)n) L.data.clear();
        }
        fclose(f);
    }
    uint32_t magic = 0, ver = 0;
    uint64_t drv = 0;
    if (L.data.size() >= 16) {
        memcpy(&magic, L.data.data(), 4);
        memcpy(&ver, L.data.data() + 4, 4);
        memcpy(&drv, L.data.data() + 8, 8);
    }
    if (!L.data.empty() && (magic != kCacheMagic || ver != kCacheFormat)) {
        // Started afresh, not just skipped. The programs compiled from here on are appended
        // to this file, and behind the old header they would be skipped again on every run
        // after, each one compiled anew in the middle of play.
        fprintf(stderr, "[shaders] replacing unrecognised cache %s\n", g_shader_cache_path.c_str());
        L.data.clear();
        if (FILE* f = fopen(g_shader_cache_path.c_str(), "wb")) {
            shader_cache_write_header(f);
            fclose(f);
        }
    }
    // The file's keys, canonical, to leave out of the game's those it already holds.
    std::unordered_set<ShaderKey, ShaderKeyHash> have;
    if (!L.data.empty()) {
        L.same_driver = drv == driver_id();
        L.stale = !L.same_driver;
        L.pos = 16;
        // Counted first, for the progress a frontend shows.
        const uint8_t* p = L.data.data() + L.pos;
        const uint8_t* end = L.data.data() + L.data.size();
        CacheRecord r;
        bool checked = false;
        while (cache_next(p, end, r)) {
            L.total++;
            canonicalize_shader_key(r.k);
            have.insert(r.k);
            // Whether the stored binaries will be taken, judged by the first: they all
            // go together, with the driver or with a change to the shaders.
            if (!checked && L.same_driver && r.len) {
                checked = true;
                L.binaries = program_src_hash(gen_pixel_shader(r.k).c_str()) == r.src_hash;
            }
        }
    }
    if (g_shipped_keys && g_binaries_supported) {
        uint32_t kmagic = 0, kver = 0;
        if (g_shipped_keys_size >= 8) {
            memcpy(&kmagic, g_shipped_keys, 4);
            memcpy(&kver, g_shipped_keys + 4, 4);
        }
        if (kmagic != kShaderKeysMagic || kver != kShaderKeyFormat) {
            fprintf(stderr, "[shaders] the game's key list is in another format (%08x, not %08x); "
                    "make it again with tools/shader_keys\n", kver, kShaderKeyFormat);
        } else {
            for (size_t o = 8; o + sizeof(ShaderKey) <= g_shipped_keys_size; o += sizeof(ShaderKey)) {
                ShaderKey k;
                memcpy(&k, g_shipped_keys + o, sizeof(k));
                canonicalize_shader_key(k);
                if (have.insert(k).second) L.shipped.push_back(k);
            }
            L.total += (int)L.shipped.size();
        }
    }
    L.active = !L.data.empty() || !L.shipped.empty();
}

static constexpr size_t kLoadBatch = 16;

static void cache_load_start(CacheLoad& L, const ShaderKey& k, std::string src) {
    CacheLoad::Started s{k, std::move(src), 0, 0};
    s.prog = start_program(s.src, &s.fs);
    L.batch.push_back(std::move(s));
    L.compiled++;
    L.stale = true;
}

static void cache_load_finish(CacheLoad& L) {
    for (CacheLoad::Started& s : L.batch) {
        finish_program(s.prog, s.fs, s.src);
        register_program(s.k, s.prog);
    }
    L.batch.clear();
}

static bool cache_load_has(const CacheLoad& L, const ShaderKey& k) {
    if (g_programs.count(k)) return true;
    for (const CacheLoad::Started& s : L.batch)
        if (s.k == k) return true;
    return false;
}

bool render_shader_cache_step(double budget_ms, int* done, int* total) {
    CacheLoad& L = g_cache_load;
    if (L.active) {
        const auto t_step = std::chrono::steady_clock::now();
        const uint8_t* p = L.data.data() + L.pos;
        const uint8_t* end = L.data.data() + L.data.size();
        while (p < end) {
            CacheRecord r;
            if (!cache_next(p, end, r)) {
                L.stale = true;
                p = end;
                break;
            }
            L.done++;
            // A file from before keys were canonical holds many records for each program;
            // all but the first are dropped, and the rewrite keeps one. A binary stays good
            // under its canonical key: the source it was built from is the same.
            canonicalize_shader_key(r.k);
            if (cache_load_has(L, r.k)) {
                L.stale = true;  // a duplicate: written by a crash mid-append, or see above
            } else {
                const auto t_rec = std::chrono::steady_clock::now();
                std::string src = gen_pixel_shader(r.k);
                const bool bin_ok = L.same_driver && r.len && program_src_hash(src.c_str()) == r.src_hash;
                if (GLuint prog = bin_ok ? build_program(r.k, src, r.bin, r.fmt, r.len) : 0) {
                    L.from_binary++;
                    register_program(r.k, prog);
                } else {
                    cache_load_start(L, r.k, std::move(src));
                }
                if (L.batch.size() >= kLoadBatch) cache_load_finish(L);
                // A load that hangs or crawls says where.
                const double rec_ms = ms_since(t_rec);
                if (rec_ms > 100.0)
                    fprintf(stderr, "[shaders] record %d of %d took %.0f ms (%s)\n", L.done, L.total, rec_ms,
                            bin_ok ? "binary" : "a batch compiled");
            }
            if (ms_since(t_step) >= budget_ms) break;
        }
        L.pos = (size_t)(p - L.data.data());
        static auto t_progress = std::chrono::steady_clock::now();
        if (p < end && ms_since(t_progress) > 2000.0) {
            t_progress = std::chrono::steady_clock::now();
            fprintf(stderr, "[shaders] %d of %d loaded, %.0f ms\n", L.done, L.total, ms_since(L.t0));
        }
        // Then the game's keys the file lacked, compiled; the rewrite below stores them.
        while (p >= end && L.shipped_pos < L.shipped.size() && ms_since(t_step) < budget_ms) {
            const ShaderKey& k = L.shipped[L.shipped_pos++];
            L.done++;
            if (!cache_load_has(L, k)) cache_load_start(L, k, gen_pixel_shader(k));
            if (L.batch.size() >= kLoadBatch) cache_load_finish(L);
        }
        if (p >= end && L.shipped_pos == L.shipped.size()) {
            cache_load_finish(L);
            // Anything that could not be used as stored is replaced: the whole file is
            // rewritten from the programs now in hand, with fresh binaries where the
            // driver gives them.
            if (L.stale || (g_binaries_supported && L.from_binary == 0 && L.compiled > 0)) {
                if (FILE* f = fopen(g_shader_cache_path.c_str(), "wb")) {
                    shader_cache_write_header(f);
                    for (auto& kv : g_programs)
                        shader_cache_write_record(f, kv.first, program_src_hash(gen_pixel_shader(kv.first).c_str()),
                                                  kv.second.prog);
                    fclose(f);
                }
            }
            fprintf(stderr, "[shaders] %d programs from cache (%d from binaries, %d compiled, %zu of them from the game's key list) in %.0f ms\n",
                    L.from_binary + L.compiled, L.from_binary, L.compiled, L.shipped.size(), ms_since(L.t0));
            L.active = false;
            L.data.clear();
            L.data.shrink_to_fit();
            L.shipped.clear();
            L.shipped.shrink_to_fit();
        }
    }
    if (done) *done = L.done;
    if (total) *total = L.total;
    return !L.active;
}

// Called once from render_init.
static void shader_cache_load() {
    shader_cache_open();
    if (!g_cache_deferred) render_shader_cache_step(1e30, nullptr, nullptr);
}

// A newly built program goes into the cache file. Appended rather than rewritten, so a
// crash later in the run keeps what was learned so far. A missing or truncated file gets
// a header first.
static void shader_cache_append(const ShaderKey& k, uint64_t src_hash, GLuint p) {
    if (g_shader_cache_path.empty()) return;
    FILE* f = fopen(g_shader_cache_path.c_str(), "ab");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0) shader_cache_write_header(f);
    shader_cache_write_record(f, k, src_hash, p);
    fclose(f);
}

// Programs being built in the background, under render_set_async_shaders.
struct PendingProgram {
    GLuint prog, fs;
    std::string src;
    std::chrono::steady_clock::time_point t0;
};
static std::unordered_map<ShaderKey, PendingProgram, ShaderKeyHash> g_pending;
static bool g_async_wanted;      // render_set_async_shaders
static bool g_parallel_compile;  // the GL can build programs in the background
#ifndef GL_COMPLETION_STATUS_KHR
#define GL_COMPLETION_STATUS_KHR 0x91B1
#endif

void render_set_async_shaders(bool on) { g_async_wanted = on; }

// The program for a key, built now if it has not been. With background builds, a program
// not yet built is started and nullptr returned, as it is on each call until it is done.
static const Program* get_program(const ShaderKey& k) {
    auto it = g_programs.find(k);
    if (it != g_programs.end()) return &it->second;
    const auto t0 = std::chrono::steady_clock::now();
    if (!g_parallel_compile) {
        std::string src = gen_pixel_shader(k);
        GLuint p = build_program(k, src, nullptr, 0, 0);
        const Program& pr = register_program(k, p);
        g_stall.compiled++;
        g_stall.compile_ms += ms_since(t0);
        if (frametime_on()) fprintf(stderr, "[shaders] compiled program %zu in %.1f ms\n", g_programs.size(), ms_since(t0));
        shader_cache_append(k, program_src_hash(src.c_str()), p);
        return &pr;
    }
    auto pit = g_pending.find(k);
    if (pit == g_pending.end()) {
        PendingProgram pp;
        pp.src = gen_pixel_shader(k);
        pp.prog = start_program(pp.src, &pp.fs);
        pp.t0 = t0;
        g_pending.emplace(k, std::move(pp));
        g_stall.compiled++;
        g_stall.compile_ms += ms_since(t0);
        return nullptr;
    }
    PendingProgram& pp = pit->second;
    GLint done = 0;
    glGetProgramiv(pp.prog, GL_COMPLETION_STATUS_KHR, &done);
    if (!done) return nullptr;
    finish_program(pp.prog, pp.fs, pp.src);
    const Program& pr = register_program(k, pp.prog);
    if (frametime_on())
        fprintf(stderr, "[shaders] built program %zu in the background in %.1f ms\n", g_programs.size(), ms_since(pp.t0));
    shader_cache_append(k, program_src_hash(pp.src.c_str()), pp.prog);
    g_pending.erase(pit);
    return &pr;
}

// Without a version line: glsl_header_with_clip() goes in front. Clip distances 4 and 5
// are enabled for every program on desktop GL (see render_init), so this writes them.
static const char* kCopyVS = R"(
out vec2 v_uv;
uniform vec4 u_rect;  // source rect in normalized EFB coords (x0,y0,x1,y1), y down
void main() {
    vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    v_uv = mix(u_rect.xy, u_rect.zw, p);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
#ifdef GCN_CLIP
    gl_ClipDistance[4] = 1.0;
    gl_ClipDistance[5] = 1.0;
#endif
}
)";

// Converts EFB color (or depth) to the requested GX copy format. Output row 0 = top.
static const char* kCopyFS = GCN_GLSL_VERSION R"(
in vec2 v_uv;
uniform sampler2D u_src;
uniform sampler2D u_depth;
uniform int u_mode;   // copy texture format (+16 intensity, +32 depth)
out vec4 o;
void main() {
    vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y);
    vec4 c = texture(u_src, uv);
    if ((u_mode & 32) != 0) {
        float z = texture(u_depth, uv).r;
        // Clamped: at the far plane z * 16777215 + 0.5 rounds up to 16777216 in single
        // precision, whose top byte is 0, so a cleared depth buffer copied as Z8 read as
        // the nearest depth instead of the farthest.
        uint zi = min(uint(z * 16777215.0 + 0.5), 16777215u);
        c = vec4(float((zi >> 16) & 255u), float((zi >> 8) & 255u), float(zi & 255u), 255.0) / 255.0;
        c = vec4(c.r, c.g, c.b, c.r);
        // The two-byte depth formats store the lower of their two bytes first: Z16 is
        // (middle, top) and Z16L (low, middle), the reverse of the colour order the
        // swizzles below assume. Read back as IA8 that puts the top byte in the
        // intensity and the middle in the alpha, and an indirect lookup that takes its
        // column from the alpha and its row from the intensity indexes a ramp whose rows
        // are the coarse axis. With the bytes the other way round the lookup is
        // transposed, and the ramp's steep last rows light up wherever the middle byte
        // nears 255, in lines across the fog.
        int dfmt = u_mode & 15;
        if (dfmt == 11) c = vec4(c.g, c.r, c.b, c.a);
        else if (dfmt == 12) c = vec4(c.r, c.b, c.g, c.a);
    }
    int fmt = u_mode & 15;
    bool intensity = (u_mode & 16) != 0;
    if (intensity) {
        float y = clamp(dot(c.rgb, vec3(0.257, 0.504, 0.098)) + 16.0 / 255.0, 0.0, 1.0);
        c.rgb = vec3(y);
        // I4 and I8 hold the intensity alone, which a texture of either format reads back
        // in all four components, the alpha too; IA4 and IA8 keep the frame's alpha. I4
        // keeps the top four bits of the 8-bit value: a game that reads the copy back as
        // CI4 indexes its palette with exactly those.
        if (fmt == 0) c = vec4(floor(floor(y * 255.0 + 0.5) / 16.0) / 15.0);
        else if (fmt == 1) c = vec4(y);
    }
    // Single-channel formats replicate into all components like the texture decoder does.
    // R4 (0) is the red channel at 4 bits, read back as I4: a projected shadow draws its
    // caster white into a corner of the frame and copies that as its mask, whose alpha
    // is then the red, not whatever the frame's alpha held there.
    if (fmt == 7) c = vec4(c.a);              // A8
    else if (fmt == 0 && !intensity) c = vec4(floor(c.r * 15.0 + 0.5) / 15.0);   // R4
    else if (fmt == 8) c = vec4(c.r);         // R8
    else if (fmt == 9) c = vec4(c.g);         // G8
    else if (fmt == 10) c = vec4(c.b);        // B8
    else if (fmt == 1 && !intensity) c = vec4(c.r);
    // The two-channel formats are read back as IA8: the first byte is the alpha and the
    // second the intensity. (A 16-bit depth copy is RG8 with its bytes swapped above.)
    else if (fmt == 11) c = vec4(c.g, c.g, c.g, c.r);   // RG8
    else if (fmt == 12) c = vec4(c.b, c.b, c.b, c.g);   // GB8
    if (fmt == 4) c.a = 1.0;                  // RGB565
    o = c;
}
)";

static const char* kBlitFS = GCN_GLSL_VERSION R"(
in vec2 v_uv;
uniform sampler2D u_src;
out vec4 o;
void main() { o = vec4(texture(u_src, vec2(v_uv.x, 1.0 - v_uv.y)).rgb, 1.0); }
)";

void render_init(int internal_scale) {
    g_scale = internal_scale < 1 ? 1
                                 : (internal_scale > kMaxInternalScale ? kMaxInternalScale
                                                                       : internal_scale);
    const std::string vs_src = gen_vertex_shader();
#ifdef GCN_GL_ES
    {
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n && !g_clip_ok; i++) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            g_clip_ok = e && !strcmp(e, "GL_EXT_clip_cull_distance");
        }
        if (!g_clip_ok)
            fprintf(stderr, "no GL_EXT_clip_cull_distance: the theater/stereo morph is uncropped\n");
        for (GLint i = 0; i < n && !g_dual_source; i++) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            g_dual_source = e && !strcmp(e, "GL_EXT_blend_func_extended");
        }
        if (!g_dual_source)
            fprintf(stderr, "no GL_EXT_blend_func_extended: a blended draw stores its own alpha, not GXSetDstAlpha's\n");
        g_base_vertex = glDrawElementsBaseVertex != nullptr;
        if (!g_base_vertex)
            fprintf(stderr, "no glDrawElementsBaseVertex: base vertices are added into the indices instead\n");
    }
#else
    g_clip_ok = true;
    g_dual_source = true;
    // GX clips depth in clip space and clamps what the viewport makes of it, so a viewport
    // may map depth well past the buffer's range; the vertex shader clips at GX's planes
    // through clip distances 4 and 5, which every program here writes. ES has no depth
    // clamp in core, and keeps GL's clipping at the viewport's depth.
    glEnable(GL_DEPTH_CLAMP);
    glEnable(GL_CLIP_DISTANCE0 + 4);
    glEnable(GL_CLIP_DISTANCE0 + 5);
#endif
    g_renderer_writes_back = true;  // see set_copy_writeback in gx.h
    g_vs_hash = hash_str(vs_src.c_str());
    g_vs = compile(GL_VERTEX_SHADER, vs_src);
#if GCN_HAVE_PROGRAM_BINARY
    {
        GLint n = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &n);
        g_binaries_supported = n > 0;
    }
#endif
    {
        // GL_COMPLETION_STATUS_KHR is the same enum in the ARB extension. Neither says how
        // many threads build programs; ANGLE uses as many as it likes unless told otherwise.
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        bool parallel = false;
        for (GLint i = 0; i < n && !parallel; i++) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            parallel = e && (!strcmp(e, "GL_KHR_parallel_shader_compile") || !strcmp(e, "GL_ARB_parallel_shader_compile"));
        }
        bool want = g_async_wanted;
        if (const char* v = getenv("GCN_ASYNC_SHADERS")) want = atoi(v) != 0;
        g_parallel_compile = want && parallel;
        if (want)
            fprintf(stderr, parallel ? "[shaders] built in the background; a draw waiting for its program is left out\n"
                                     : "[shaders] no parallel shader compilation: a new program stalls the frame it is built in\n");
    }
    shader_cache_load();

    glGenFramebuffers(1, &g_efb_fbo);
    glGenTextures(1, &g_efb_color);
    glBindTexture(GL_TEXTURE_2D, g_efb_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, EFB_W * g_scale, EFB_H * g_scale, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &g_efb_depth);
    glBindTexture(GL_TEXTURE_2D, g_efb_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, EFB_W * g_scale, EFB_H * g_scale, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, g_efb_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_efb_color, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_efb_depth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fatal("EFB framebuffer incomplete");
    glClearColor(0, 0, 0, 1);
    clear_depth(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glGenVertexArrays(kVertexRing * 9, &g_vaos[0][0]);
    glGenBuffers(kVertexRing, g_vbos);
    glGenBuffers(kVertexRing, g_ebos);
    for (int r = 0; r < kVertexRing; r++)
        for (uint32_t n = 0; n < 9; n++) {
            glBindVertexArray(g_vaos[r][n]);
            glBindBuffer(GL_ARRAY_BUFFER, g_vbos[r]);
            // The element buffer binding is part of the VAO's state, so binding it once here
            // keeps it bound whenever this VAO is.
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ebos[r]);
            const GLsizei stride = (GLsizei)packed_stride(n);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GpuVertex, pos));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(GpuVertex, col[0]));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(GpuVertex, col[1]));
            for (uint32_t i = 0; i < n; i++) {
                glEnableVertexAttribArray(3 + i);
                glVertexAttribPointer(3 + i, 3, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(GpuVertex, tex) + i * 12));
            }
        }
    // A coordinate past a draw's count reads the attribute's constant value instead, which
    // is what the transform writes into a GpuVertex's unused ones.
    for (int i = 0; i < 8; i++) glVertexAttrib4f(3 + i, 0.0f, 0.0f, 1.0f, 1.0f);
    g_vao = g_vaos[0][0];
    g_vbo = g_vbos[0];
    g_ebo = g_ebos[0];
    glBindVertexArray(g_vao);

    GLuint cvs = compile(GL_VERTEX_SHADER, glsl_header_with_clip() + kCopyVS);
    g_copy_prog = link(cvs, compile(GL_FRAGMENT_SHADER, kCopyFS));
    g_copy_u_src = glGetUniformLocation(g_copy_prog, "u_src");
    g_copy_u_depth = glGetUniformLocation(g_copy_prog, "u_depth");
    g_copy_u_rect = glGetUniformLocation(g_copy_prog, "u_rect");
    g_copy_u_mode = glGetUniformLocation(g_copy_prog, "u_mode");
    g_blit_prog = link(cvs, compile(GL_FRAGMENT_SHADER, kBlitFS));
    g_blit_u_src = glGetUniformLocation(g_blit_prog, "u_src");
    g_blit_u_rect = glGetUniformLocation(g_blit_prog, "u_rect");
    glGenVertexArrays(1, &g_copy_vao);
    glGenFramebuffers(1, &g_copy_fbo);
    {
        const std::string bvs = glsl_header_with_clip() + R"(
uniform mat4 u_mvp;
uniform mat4 u_crop;
uniform vec4 u_quad;   // half-width, half-height, view-space z, unused
void main() {
    vec2 c = vec2(gl_VertexID & 1, gl_VertexID >> 1) * 2.0 - 1.0;
    vec4 a = vec4(c * u_quad.xy, u_quad.z, 1.0);
    gl_Position = u_mvp * a;
#ifdef GCN_CLIP
    vec4 cd = u_crop * a;
    gl_ClipDistance[0] = cd.x;
    gl_ClipDistance[1] = cd.y;
    gl_ClipDistance[2] = cd.z;
    gl_ClipDistance[3] = cd.w;
    gl_ClipDistance[4] = 1.0;
    gl_ClipDistance[5] = 1.0;
#endif
}
)";
        const std::string bfs = std::string(GCN_GLSL_VERSION) + R"(
uniform vec3 u_col;
out vec4 o;
void main() { o = vec4(u_col, 1.0); }
)";
        g_bg_prog = link(compile(GL_VERTEX_SHADER, bvs), compile(GL_FRAGMENT_SHADER, bfs));
        g_bg_u_mvp = glGetUniformLocation(g_bg_prog, "u_mvp");
        g_bg_u_crop = glGetUniformLocation(g_bg_prog, "u_crop");
        g_bg_u_quad = glGetUniformLocation(g_bg_prog, "u_quad");
        g_bg_u_col = glGetUniformLocation(g_bg_prog, "u_col");
    }
#ifndef GCN_GL_ES
    // Desktop core profile needs this to honour gl_PointSize; ES always does.
    glEnable(GL_PROGRAM_POINT_SIZE);
#endif
}

void render_set_window_size(int w, int h) { g_win_w = w; g_win_h = h; }
// Horizontal squeeze applied in clip space (1 = off, 0.75 = 4:3 content on a 16:9 frame).
static std::atomic<float> g_wide{1.0f};
void render_set_widescreen(bool on) { g_wide.store(on ? 0.75f : 1.0f); }

// Change how many samples the EFB keeps per hardware pixel, between frames.
//
// The two VR views want different answers. Theater shows the EFB itself, blown up to fill
// a panel wider than the frame was ever drawn for, so every extra sample is detail the
// viewer sees. Stereo never shows it: there it holds only what the eye passes sample out
// of it -- the water reflection, the sheet the spray is cut from -- and the pass that
// fills it is a whole extra scene render on top of the two eyes. So the scale follows the
// view, which means changing it while the game runs.
//
// Nothing in the EFB has to survive the change: it is cleared and redrawn every frame.
// The textures EFB copies land in do not get off so lightly -- they are allocated at
// dst * scale but matched for reuse by their logical size alone, so a survivor would be
// reused at the old resolution and quietly sample wrong. They go with the EFB.
static void gl_state_invalidate();
static void uniforms_invalidate();
void render_set_internal_scale(int scale) {
    if (scale < 1) scale = 1;
    if (scale > kMaxInternalScale) scale = kMaxInternalScale;
    if (scale == g_scale) return;
    const int old_scale = g_scale;
    g_scale = scale;
    gl_state_invalidate();
    uniforms_invalidate();
    // The EFB keeps what it held, scaled. A frame on the console does not begin with a
    // clear -- the previous frame's display copy cleared the EFB -- so the first frame drawn
    // after a change of scale would otherwise start from undefined colour and depth, and
    // depth that fails every test leaves the frame empty. A frontend changes the scale as it
    // moves between its views, and the first pass of the new view showed it: the left image
    // of a stereo pair came out black for a frame while the right, drawn after the left's
    // display copy had cleared the EFB, was whole.
    GLuint color = 0, depth = 0;
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, EFB_W * g_scale, EFB_H * g_scale, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &depth);
    glBindTexture(GL_TEXTURE_2D, depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, EFB_W * g_scale, EFB_H * g_scale, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_efb_fbo);
    // A blit is cut by the scissor and the write masks; the state cache was just
    // invalidated, so the next draw sets them again.
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    const GLint ow = EFB_W * old_scale, oh = EFB_H * old_scale, nw = EFB_W * g_scale, nh = EFB_H * g_scale;
    glBlitFramebuffer(0, 0, ow, oh, 0, 0, nw, nh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBlitFramebuffer(0, 0, ow, oh, 0, 0, nw, nh, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, g_efb_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &g_efb_color);
    glDeleteTextures(1, &g_efb_depth);
    g_efb_color = color;
    g_efb_depth = depth;
    for (auto it = g_textures.begin(); it != g_textures.end();) {
        if (it->second.efb) {
            glDeleteTextures(1, &it->second.tex);
            it = g_textures.erase(it);
        } else {
            ++it;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_efb_fbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fatal("EFB framebuffer incomplete at internal scale %d", g_scale);
}

// ---------------------------------------------------------------------------
uint64_t render_texture_hash(uint32_t id) {
    auto it = g_textures.find(id);
    return it == g_textures.end() ? 0 : it->second.hash;
}

// `from_pbo`: the texels are in the bound pixel unpack buffer from `pbo_off` on, every level
// in turn, rather than in `t` (upload_textures).
static void upload_texture(const TexData& t, bool from_pbo = false, size_t pbo_off = 0) {
    GlTex g{};
    glGenTextures(1, &g.tex);
    glBindTexture(GL_TEXTURE_2D, g.tex);
    g.w = t.width; g.h = t.height;
    g.hash = t.hash;
    uint32_t w = t.width, h = t.height;
#ifdef GCN_GL_ES
    // Storage for the whole chain at once, rather than a level at a time into a mutable
    // texture: on an Adreno 740 that also took the first draw from the new textures from
    // about 100 ms to 2 (a room's worth of them at a door).
    uint32_t max_levels = 1;
    for (uint32_t d = w > h ? w : h; d > 1; d /= 2) max_levels++;
    const size_t nlevels = t.levels.size() < max_levels ? t.levels.size() : max_levels;
    glTexStorage2D(GL_TEXTURE_2D, (GLsizei)nlevels, GL_RGBA8, w, h);
    for (size_t l = 0; l < nlevels; l++) {
        glTexSubImage2D(GL_TEXTURE_2D, (GLint)l, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
                        from_pbo ? (const void*)(uintptr_t)pbo_off : (const void*)t.levels[l].data());
        pbo_off += t.levels[l].size() * 4;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
#else
    for (size_t l = 0; l < t.levels.size(); l++) {
        glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.levels[l].data());
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)t.levels.size() - 1);
    g.levels = (uint32_t)t.levels.size();
    g.last_used = g_render_frame;
    g_textures[t.id] = g;
    // GCN_TEXDUMP=<dir> writes every texture's top level as <dir>/tex_<id>.png, alpha
    // forced opaque, so a decoded texture can be looked at apart from the draw using it.
    static const char* dump = getenv("GCN_TEXDUMP");
    if (dump) {
        std::vector<uint8_t> px((size_t)t.width * t.height * 4);
        memcpy(px.data(), t.levels[0].data(), px.size());
        for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
        char path[512];
        snprintf(path, sizeof(path), "%s/tex_%u.png", dump, t.id);
        write_png(path, px.data(), (int)t.width, (int)t.height);
    }
}

static GLuint get_sampler(uint32_t mode0, uint32_t mode1, uint32_t levels) {
    uint32_t key = (mode0 & 0x3FFFFF) ^ (mode1 << 22) ^ (levels << 30);
    auto it = g_samplers.find(key);
    if (it != g_samplers.end()) return it->second;
    GLuint s;
    glGenSamplers(1, &s);
    static const GLenum wrap[4] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT};
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, wrap[mode0 & 3]);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, wrap[(mode0 >> 2) & 3]);
    bool mag_lin = (mode0 >> 4) & 1;
    uint32_t minf = (mode0 >> 5) & 7;
    bool min_lin = minf & 4;
    uint32_t mip = minf & 3;
    glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, mag_lin ? GL_LINEAR : GL_NEAREST);
    GLenum mf;
    if (!mip || levels <= 1) mf = min_lin ? GL_LINEAR : GL_NEAREST;
    else if (mip == 1) mf = min_lin ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    else mf = min_lin ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, mf);
    int8_t bias = (int8_t)((mode0 >> 9) & 0xFF);
    set_lod_bias(s, bias / 32.0f);
    glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, (mode1 & 0xFF) / 16.0f);
    glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, ((mode1 >> 8) & 0xFF) / 16.0f);
    g_samplers[key] = s;
    return s;
}

static inline int32_t sx11(uint32_t v) { return (int32_t)(v << 21) >> 21; }

static float fog_float(uint32_t v) {
    uint32_t mant = v & 0x7FF, exp = (v >> 11) & 0xFF, sign = (v >> 19) & 1;
    uint32_t bits = (sign << 31) | (exp << 23) | (mant << 12);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

// Dual-source blending's factors: core in desktop GL 3.3, and GL_EXT_blend_func_extended's
// (the same values) on ES, whose loader does not define them.
#ifndef GL_SRC1_ALPHA
#define GL_SRC1_ALPHA 0x8589
#endif
#ifndef GL_ONE_MINUS_SRC1_ALPHA
#define GL_ONE_MINUS_SRC1_ALPHA 0x88FB
#endif
static const GLenum kBlendSrc[8] = {GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
static const GLenum kBlendDst[8] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
static const GLenum kDepthFunc[8] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
static bool samples_fullscreen_copy(const PixelState& st);
static bool samples_frame_copy(const PixelState& st);
static bool is_fullscreen_tex(uint32_t id);
static void note_fullscreen_copies(const Batch& b);

// The origin that the scissor box and the viewport are both measured from, in EFB pixels.
//
// GX stores it halved and biased by 342, so GXSetScissorBoxOffset(0, 0) reads back as 342.
// This game sets (-2, -2) -- the register reads 340 -- and moves every viewport by the same
// (-2, -2), so on hardware the two cancel and the picture sits at the EFB's origin.
// Assuming 342 instead left every pass two pixels up and left of where the game believes
// it is. That shows wherever the game samples a copy of the EFB at screen coordinates it
// worked out itself: the water looked its refraction up two pixels out of register, which
// stood a water-tinted second copy of the racer beside the real one, and the last two
// columns and rows of every pass were never drawn.
// The viewport's far depth, as GX writes it: a depth past the buffer's 24 bits is clamped
// to its largest value. A game that pins a sky to the back of the buffer can give it a
// viewport of no depth at 2^24 itself (scale 0, offset 16777216: a viewport from 1 to 1 of a
// depth range); GX writes 0xFFFFFF, which passes LEQUAL against a cleared buffer. Taken as
// it is, that depth is a hair past the far plane, and GL clipped the whole sky away, leaving
// the clear colour behind the world.
static float vp_far(const float* vp) { return std::min(vp[5], 16777215.0f); }

struct ViewportDepth {
    float offset, scale;
};

static ViewportDepth vp_depth_transform(const float* vp) {
#ifdef GCN_GL_ES
    // GXSetViewport's standard [0, 1] range writes 2^24 to both XF depth fields.
    // GLES clips after this transform. Clamping only the offset to 2^24 - 1
    // puts GX's exact near plane outside the GLES clip volume. Map both
    // endpoints of this standard range to GLES's inclusive [-1, 1].
    if (vp[2] == 16777216.0f && vp[5] == 16777216.0f) return {1.0f, 2.0f};
#endif
    return {2.0f * vp_far(vp) / 16777215.0f - 1.0f, 2.0f * vp[2] / 16777215.0f};
}

static void scissor_offset(const uint32_t* bp, int& xoff, int& yoff) {
    xoff = (int)(bp[0x59] & 0x3FF) * 2;
    yoff = (int)((bp[0x59] >> 10) & 0x3FF) * 2;
}

// c = a * b, column-major, element (row r, column k) at m[k * 4 + r].
static void mat4_mul(const float* a, const float* b, float* c) {
    for (int k = 0; k < 4; k++)
        for (int r = 0; r < 4; r++) {
            float sum = 0;
            for (int i = 0; i < 4; i++) sum += a[i * 4 + r] * b[k * 4 + i];
            c[k * 4 + r] = sum;
        }
}

// The turn yaw(y) then pitch(x), column-major: one that takes -z, the way ahead, to the
// direction `yaw` left and `pitch` up of it.
static void yaw_pitch_matrix(float yaw, float pitch, float m[16]) {
    const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    const float R[16] = {cy, 0, -sy, 0, sy * sp, cp, cy * sp, 0, sy * cp, -sp, cy * cp, 0, 0, 0, 0, 1};
    memcpy(m, R, sizeof(R));
}

// An EyeFacing draw's frame. The game centred its panel on the point (cx, cy) of its clip
// space by translating its orthographic projection, so on the HUD frame (g_vr_hud_eff,
// clip to view space) the panel lies off the frame's centre and is seen at a slant. Here
// the panel goes back to the centre, and the frame is turned about the camera -- yaw, then
// pitch, no roll -- from the direction of its centre to that of the point: the panel is
// where it was, as far away as the frame's centre, square on to the camera.
static void facing_frame_at(float cx, float cy, float out[16]);
static void facing_frame(const float P[16], float out[16]) {
    facing_frame_at(P[12], P[13], out);  // the panel's origin in clip space, w being 1
}

// The same for a panel whose origin is at (cx, cy) of the game's clip space, applied to
// that clip space (no projection of its own).
static void facing_frame_at(float cx, float cy, float out[16]) {
    const float* F = g_vr_hud_eff;
    const float C[3] = {F[12], F[13], F[14]};
    const float Q[3] = {F[0] * cx + F[4] * cy + F[12], F[1] * cx + F[5] * cy + F[13], F[2] * cx + F[6] * cy + F[14]};
    auto yaw = [](const float* v) { return atan2f(-v[0], -v[2]); };
    auto pitch = [](const float* v) { return atan2f(v[1], sqrtf(v[0] * v[0] + v[2] * v[2])); };
    float to[16], from[16], back[16], R[16], RF[16];
    yaw_pitch_matrix(yaw(Q), pitch(Q), to);
    yaw_pitch_matrix(yaw(C), pitch(C), from);
    for (int k = 0; k < 4; k++)
        for (int r = 0; r < 4; r++) back[k * 4 + r] = from[r * 4 + k];   // a rotation's inverse
    mat4_mul(to, back, R);
    mat4_mul(R, F, RF);
    const float T[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -cx, -cy, 0, 1};
    mat4_mul(RF, T, out);
}

// The water surface refracts by looking the finished frame up at the screen position the
// game computed for each of its vertices. In an eye those positions are the flat view's,
// and the copy is the flat view's too, so water the eye can see beyond the game's own
// 60-degree frustum samples off the edge of the copy and clamps -- the scene smeared down
// the sea in streaks, the racer among it.
//
// Nothing about that is fixable by moving the lookup around, because the data is not in
// the copy. So the eye grabs what it has drawn itself, which covers exactly what the eye
// can see, and the fragment samples it at its own position rather than at the flat view's.
// The indirect stage that ripples the lookup still applies on top, so the water keeps its
// wobble. Costs one full-target copy per eye, taken only on a frame that has such a
// draw -- and the spray wants the same thing at a later moment, so a frame throwing
// spray pays for two. See is_grab_tex().
static void grab_eye(EyeGrab& g, int w, int h) {
    // The copy must keep the target's encoding: GL ES refuses glCopyTexSubImage2D between
    // an sRGB attachment and a linear texture or the other way round, and a headset's eye
    // is an sRGB swapchain image (openxr/xr_frontend.cpp, create_swapchains). An sRGB grab
    // is then read without decoding (EXT_texture_sRGB_decode), so the shader sees the
    // same values it would from a linear one: the game's own, never converted.
    GLint encoding = GL_LINEAR, target = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
    if (target)  // the default framebuffer has no GL_COLOR_ATTACHMENT0 to ask about
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                              GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &encoding);
    const bool srgb = encoding == GL_SRGB;
    if (!g.tex || g.w != w || g.h != h || g.srgb != srgb) {
        if (!g.tex) glGenTextures(1, &g.tex);
        glBindTexture(GL_TEXTURE_2D, g.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        if (srgb) {
            constexpr GLenum kTextureSrgbDecodeExt = 0x8A48, kSkipDecodeExt = 0x8A4A;
            glTexParameteri(GL_TEXTURE_2D, kTextureSrgbDecodeExt, kSkipDecodeExt);
        }
        g.srgb = srgb;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g.w = w;
        g.h = h;
    } else {
        glBindTexture(GL_TEXTURE_2D, g.tex);
    }
#ifndef GCN_GL_ES
    // A desktop eye target is an ordinary multisampled framebuffer, which cannot be copied
    // from, only resolved by a blit. (On GL ES the samples are the render-to-texture
    // extension's and the copy resolves them itself.) The blit honours the scissor; the
    // caller invalidates the state cache after a grab, which puts it back.
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLE_BUFFERS, &samples);
    if (samples > 0) {
        static GLuint resolve_fbo;
        if (!resolve_fbo) glGenFramebuffers(1, &resolve_fbo);
        GLint target = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.tex, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)target);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)target);
        return;
    }
#endif
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
}

// The spray is composited from screen-space grabs -- the same mechanism as the water
// surface, one size down. At speed it lifts some fifty small rects out of the live scene
// and redraws each as a billboard sampling its own rect, so on hardware a droplet
// refracts exactly the pixels it covers and its edges cannot be seen. An eye re-projects
// the billboard but not the rect, which belongs to the flat view, so the patch agrees
// with nothing behind it and reads as a square with a piece of scene in it.
//
// So these lookups are taken over the way the water's are: the texture becomes the eye's
// own grab and the coordinate becomes the fragment's own position, which is the one place
// the pixels the droplet wants actually are. What survives of the game's own lookup is
// the indirect offset that distorts it -- the droplet's whole visible character, since
// the undistorted part is by construction the background it sits on.
//
// Unlike the whole-frame copies these go to addresses that rotate, so a fresh texture id
// appears for every sprite of every frame; a set of ids would grow without bound and be
// scanned per draw. The flag rides on the texture instead.
static bool is_grab_tex(uint32_t id) {
    // GCN_EYE_SPRAY=0 restores the stale lookup, which is the only way to see from inside
    // the headset what this changed.
    static const bool off = getenv("GCN_EYE_SPRAY") && atoi(getenv("GCN_EYE_SPRAY")) == 0;
    if (off || !id) return false;
    auto it = g_textures.find(id);
    return it != g_textures.end() && it->second.grab;
}

// A copy an EyeRestore draw puts back, while an eye is drawing one (g_cur_restore): only
// that draw's lookups are taken over, at the grab taken where the copy was made.
static bool is_restore_tex(uint32_t id) {
    if (!g_cur_restore || !g_restore_grab.tex || !id) return false;
    for (uint32_t t : g_restore_tex)
        if (t == id) return true;
    return false;
}

// Whether an eye takes this texture's lookups over at all: a copy of the whole frame
// (the water surface samples one), one of the spray's grabs, or a copy put back by an
// EyeRestore draw.
static bool is_eye_screen_tex(uint32_t id) {
    return (g_eye_grab.tex && is_fullscreen_tex(id)) || (g_spray_grab.tex && is_grab_tex(id)) ||
           is_restore_tex(id);
}
static const EyeGrab& eye_grab_for(uint32_t id) {
    return is_restore_tex(id) ? g_restore_grab : is_grab_tex(id) ? g_spray_grab : g_eye_grab;
}

// What apply_state last put into GL, so that it can put in only what differs.
//
// A race frame applies around a thousand states, and measured over a million consecutive
// pairs, 94% of them differ in a texture -- 1.1 texmaps on average -- while the projection,
// viewport, scissor, fog, indirect matrices and point size are the same more than 99% of
// the time, the program 81%, the TEV registers 76%, blend and depth 90%. Re-issuing the
// forty-odd GL calls of a full application for every one of those was most of the
// renderer's CPU time; the typical application is now a texture bind and its size.
//
// Uniforms belong to a program, so they are only skipped while the program is the one they
// were last uploaded to. Bindings and fixed-function state are global and skipped on their
// own terms. The shadow holds within one pass: an EFB copy, a present or an eye's grab
// changes GL state behind it, and so does the start of a new pass, whose eye matrices
// differ -- each of those invalidates it.
// What apply_state last put into GL, so that it can put in only what differs.
//
// A race frame applies around a thousand states, and measured over a million consecutive
// pairs, 94% of them differ in a texture -- 1.1 texmaps on average -- while the projection,
// viewport, scissor, fog, indirect matrices and point size are the same more than 99% of
// the time, the program 81%, the TEV registers 76%, blend and depth 90%. Re-issuing the
// forty-odd GL calls of a full application for every one of those was most of the
// renderer's CPU time; the typical application is now a texture bind and its size.
//
// This holds the bindings and the fixed-function state, which are global to the context.
// The uniforms are kept with their program instead (UniformShadow): a uniform belongs to
// its program and keeps its value across switches, so a program switched back to still
// holds what it was last given, and a group whose inputs match is left alone whether or not
// the program changed. Before that, every switch re-uploaded every group -- a dozen and more
// calls -- and a frame here switched programs one draw in six. The shadow holds within one
// pass: an EFB copy, a present or an eye's grab changes GL state behind it, and so does the
// start of a new pass, whose eye matrices differ -- each of those invalidates it.
struct AppliedState {
    bool valid = false;
    ShaderKey key;
    const Program* pr = nullptr;
    uint32_t bp[256];
    uint32_t tex_id[8];
    uint8_t tex_sub[8];       // the unit holds an eye grab, not the game's texture
    GLuint sampler[8];
    uint32_t pal_id[8];       // unit 8+m: the palette map m's EFB copy is read through
    int prim;
    bool hud_depth;
};
static AppliedState g_applied;
// What GL's face culling is set to: -1 unknown, 0 off, 1 front, 2 back, 3 both. Kept apart
// from g_applied because GL culls polygons only: a line or a point leaves it as it is, so a
// game alternating filled faces with outlines -- a 3D map, thousands of draws a frame -- need
// not turn culling off and on again around each outline. Those toggles were a quarter of
// the render thread's time on such a screen on a Quest 3.
static int g_gl_cull = -1;
static void gl_state_invalidate() {
    g_applied.valid = false;
    g_gl_cull = -1;
}

// A program's uniform record is current while its `gen` matches g_uniform_gen, and the
// groups that depend on the pass -- the eye's matrices, the morph, the eye target's size,
// whether this is the flat view at all -- while `view_gen` matches g_view_gen as well.
// Nothing inside the renderer touches a program's uniforms except apply_state, so the
// first counter moves only for the EFB scale (the point size is in EFB pixels), and the
// second at the start of every pass and whenever the eye is placed.
static uint32_t g_uniform_gen = 1, g_view_gen = 1;
static void uniforms_invalidate() { g_uniform_gen++; }
static void view_invalidate() { g_view_gen++; }

// GCN_APPLYSTATS=1: which groups of state apply_state actually had to send, summed over a
// pass, printed every 64th frame. What a game changes between draws decides what a state
// application costs, and this is how to see it.
static bool applystats_on() {  // on first use: the app sets its environment after static init
    static const bool on = getenv("GCN_APPLYSTATS") != nullptr;
    return on;
}
static struct ApplyStats {
    uint32_t applies, program, proj, viewport, psize, tev, aref, ind, indcs, fog, screen, ripple,
        texbind, sampbind, tsz, blend, depth, cull, scissor;
} g_astat;
static void apply_stats_flush(const char* pass, uint32_t frame) {
    if (!applystats_on()) return;
    if ((frame & 63) == 0) {
        const ApplyStats& a = g_astat;
        fprintf(stderr, "[apply] f%u %s applies %u: prog %u proj %u vp %u psz %u tev %u aref %u ind %u "
                "indcs %u fog %u scr %u rip %u tex %u samp %u tsz %u blend %u depth %u cull %u sciss %u\n",
                frame, pass, a.applies, a.program, a.proj, a.viewport, a.psize, a.tev, a.aref, a.ind,
                a.indcs, a.fog, a.screen, a.ripple, a.texbind, a.sampbind, a.tsz, a.blend, a.depth, a.cull,
                a.scissor);
    }
    memset(&g_astat, 0, sizeof(g_astat));
}

// The z freeze plane (see the pixel shader): the depth plane of the last triangle drawn
// without z freeze, in the EFB target's window coordinates, w = 1 once there is one. Set
// by the flat pass as it goes; an eye keeps its draws' own depth (w = 0).
static float g_zplane[4];

// The plane of a draw's last triangle, as the flat path's vertex shader places it: the
// GX projection, then the viewport (less the scissor offset), Y flipped for the EFB blit.
static bool zfreeze_plane(const Batch& b, const Cmd& c, float out[4]) {
    if (c.prim != 0 || c.count < 3) return false;
    const PixelState& st = b.states[c.state];
    const float* p = st.proj;
    const float* vp = st.viewport;
    const bool perspective = (int)p[6] == 0;
    int xoff, yoff;
    scissor_offset(st.bp, xoff, yoff);
    const float vpa[4] = {2.0f * (vp[3] - xoff) / EFB_W - 1.0f, 2.0f * vp[0] / EFB_W, 2.0f * (vp[4] - yoff) / EFB_H - 1.0f, 2.0f * vp[1] / EFB_H};
    const ViewportDepth depth = vp_depth_transform(vp);
    double X[3], Y[3], Z[3];
    for (int i = 0; i < 3; i++) {
        const float* v = b.verts[b.indices[c.first + c.count - 3 + i]].pos;
        float cx, cy, cz, cw;
        if (perspective) { cx = p[0] * v[0] + p[1] * v[2]; cy = p[2] * v[1] + p[3] * v[2]; cw = -v[2]; }
        else { cx = p[0] * v[0] + p[1]; cy = p[2] * v[1] + p[3]; cw = 1.0f; }
        cz = p[4] * v[2] + p[5];
        if (cw <= 0.0f) return false;
        const float px = vpa[0] * cw + vpa[1] * cx, py = vpa[2] * cw + vpa[3] * cy, pz = depth.offset * cw + depth.scale * cz;
        X[i] = (px / cw + 1.0) * 0.5 * EFB_W * g_scale;
        Y[i] = (-py / cw + 1.0) * 0.5 * EFB_H * g_scale;
        Z[i] = (pz / cw + 1.0) * 0.5;
    }
    const double ux = X[1] - X[0], uy = Y[1] - Y[0], uz = Z[1] - Z[0];
    const double wx = X[2] - X[0], wy = Y[2] - Y[0], wz = Z[2] - Z[0];
    const double det = ux * wy - uy * wx;
    if (fabs(det) < 1e-9) return false;
    const double a = (uz * wy - uy * wz) / det, bb = (ux * wz - uz * wx) / det;
    out[0] = (float)a;
    out[1] = (float)bb;
    out[2] = (float)(Z[0] - a * X[0] - bb * Y[0]);
    out[3] = 1.0f;
    return true;
}

// The same plane as an eye sees the triangle: the eye's projection and its view of the
// world, the depth mapped into the draw's band as the vertex shader maps it, in the eye
// target's window coordinates. A sky drawn with z freeze after a triangle culled at the
// far end of the frustum otherwise keeps its own depth in an eye, a hundred units out
// around the camera, and hides everything beyond it. A triangle placed by the camera (its
// positions in view space) is seen through the eye's view of the camera, as apply_state
// draws it. A 2D draw sets none (w = 0), and the draws that follow keep their own depth.
static bool zfreeze_plane_eye(const Batch& b, const Cmd& c, float out[4]) {
    out[3] = 0.0f;
    if (c.prim != 0 || c.count < 3) return false;
    const PixelState& st = b.states[c.state];
    if ((int)st.proj[6] != 0) return false;
    float M[16];
    mat4_mul(g_vr_proj, st.view_space ? g_vr_view : g_vr_view_world, M);
    const float band_lo = (st.viewport[5] - fabsf(st.viewport[2])) / 16777215.0f;
    const float band_hi = st.viewport[5] / 16777215.0f;
    double X[3], Y[3], Z[3];
    for (int i = 0; i < 3; i++) {
        const float* v = b.verts[b.indices[c.first + c.count - 3 + i]].pos;
        float clip[4];
        for (int r = 0; r < 4; r++) clip[r] = M[r] * v[0] + M[4 + r] * v[1] + M[8 + r] * v[2] + M[12 + r];
        if (clip[3] <= 0.0f) return false;
        const double zn = (band_hi + band_lo - 1.0) + (band_hi - band_lo) * (clip[2] / clip[3]);
        X[i] = (clip[0] / clip[3] + 1.0) * 0.5 * g_eye_w;
        Y[i] = (clip[1] / clip[3] + 1.0) * 0.5 * g_eye_h;
        Z[i] = (zn + 1.0) * 0.5;
    }
    const double ux = X[1] - X[0], uy = Y[1] - Y[0], uz = Z[1] - Z[0];
    const double wx = X[2] - X[0], wy = Y[2] - Y[0], wz = Z[2] - Z[0];
    const double det = ux * wy - uy * wx;
    if (fabs(det) < 1e-9) return false;
    const double a = (uz * wy - uy * wz) / det, bb = (ux * wz - uz * wx) / det;
    out[0] = (float)a;
    out[1] = (float)bb;
    out[2] = (float)(Z[0] - a * X[0] - bb * Y[0]);
    out[3] = 1.0f;
    return true;
}
// The texture offset a point's coordinates run across (SU_LPSIZE bits 19-21: 1/16 to 1),
// and which of a draw's coordinates have it enabled (bit 19 of each SU_SSIZE): the
// coordinates in the low byte, with bit 8 set so that a draw of points with none enabled
// still differs from 0, which is every other draw's.
static const float kPointTexOffset[8] = {0.0f, 1.0f / 16, 1.0f / 8, 1.0f / 4, 1.0f / 2, 1.0f, 1.0f, 1.0f};
static uint32_t point_tex_key(const PixelState& st) {
    if (kPointTexOffset[(st.bp[0x22] >> 19) & 7] == 0.0f) return 0;
    uint32_t mask = 0;
    for (uint32_t t = 0; t < st.num_texgens && t < 8; t++)
        if ((st.bp[0x30 + 2 * t] >> 19) & 1) mask |= 1u << t;
    return mask ? (mask | 0x100u | (((st.bp[0x22] >> 19) & 7) << 9)) : 0;
}


static void apply_state(const PixelState& st, int prim) {
    int xoff, yoff;
    scissor_offset(st.bp, xoff, yoff);
    const uint32_t* bp = st.bp;
    const uint32_t* abp = g_applied.bp;
    ShaderKey key = make_shader_key(st);
    {
        // A draw an eye will give a foreground layer's game depth (u_vr 4, below) gets the
        // program that works that depth out per fragment; the shader still checks u_vr, so
        // a draw that ends up elsewhere keeps its own depth.
        static const bool game_depth = !(getenv("GCN_EYE_FGDEPTH") && atoi(getenv("GCN_EYE_FGDEPTH")) == 0);
        static const bool frag_depth = !(getenv("GCN_EYE_FGFRAGDEPTH") && atoi(getenv("GCN_EYE_FGFRAGDEPTH")) == 0);
        if (g_vr_active && game_depth && frag_depth && g_morph >= 1.0f && (int)st.proj[6] == 0 && g_fg_to > 0.0f &&
            st.viewport[5] / 16777215.0f <= g_fg_to && !g_cur_flat3d)
            key.fg_depth = 1;
    }
    g_astat.applies++;
    // `same` means the previous application is still in GL and the program is unchanged.
    const bool same = g_applied.valid && key == g_applied.key;
    const Program* ready = same ? g_applied.pr : get_program(key);
    // Its program is still being built: the draw is left out, and nothing applied, so
    // what GL holds is still what g_applied says.
    g_draw_skip = !ready;
    if (!ready) return;
    const Program& pr = *ready;
    if (!same) {
        g_astat.program++;
        glUseProgram(pr.prog);
        g_applied.key = key;
        g_applied.pr = &pr;
    }
    // What this program was last given; see UniformShadow. A group is uploaded when its
    // inputs differ from the record, or the record is not current.
    UniformShadow& u = pr.shadow;
    const bool held = u.gen == g_uniform_gen;
    const bool view_held = held && u.view_gen == g_view_gen;
    auto held_words = [&](const uint32_t* a, const uint32_t* b, size_t n) {
        return held && memcmp(a, b, n * sizeof(uint32_t)) == 0;
    };

    // Projection. In VR a perspective batch is world geometry and gets the eye's
    // projection instead of the game's; an orthographic one is a 2D element and goes on
    // the HUD frame -- and so does a perspective batch the game placed in view space
    // itself, which is 3D but is no more part of the course than the lap counter is.
    // That is the countdown light rig: left in the world it stands in the water between
    // the viewer and the racer, because the game means it to hang in front of the camera.
    float P[16] = {0};
    const float* p = st.proj;
    const bool perspective = (int)p[6] == 0;
    // Something the game placed in view space but drew with a perspective frustum is a 3D
    // object held in front of the camera, not a 2D overlay. Painting it on the HUD frame
    // flattens it -- that frame's Z column is zero, so every vertex lands on one plane --
    // which throws away the object's own depth. The countdown rig lost the occlusion that
    // hides the lamp behind its lens, and the lamps showed through as white squares.
    //
    // So it keeps the eye's projection and stays 3D. It takes g_vr_view rather than
    // g_vr_view_world: the world pitch is taken out of the *world* to level the sea, and
    // applying it to something attached to the camera is what tilted the rig by 23
    // degrees. Without the correction it hangs in front of the viewer the way the game
    // means it to, and the head still moves within that.
    //
    // GCN_EYE_VS3D=0 puts it back on the HUD frame, which is where it was.
    //
    // Unless the game says its view-space draws are billboards in the world
    // (render_set_view_space_world): a sprite the game turns to face its camera, placed at
    // a point in the scene. Those are world geometry like anything else -- their vertices
    // are in the camera's frame as every world vertex is -- and held before the camera
    // they stayed where the chase camera saw them while the game's own eye moved off.
    static const bool vs3d = !(getenv("GCN_EYE_VS3D") && atoi(getenv("GCN_EYE_VS3D")) == 0);
    const bool view_space = st.view_space && !g_view_space_world;
    // A 3D draw the game composes into its 2D picture (EyeFlat): a model in a window of the
    // HUD, drawn with a frustum of its own into a viewport no bigger than that window. Its
    // positions are in a little scene of its own, so in the world it floats at whatever
    // depth that scene happens to put it, which matches nothing the HUD frame shows. It goes
    // on the HUD frame instead, through its own projection and viewport -- exactly where the
    // flat frame has it -- and keeps the game's own depth to sort its parts by. A model
    // drawn orthographically is the same thing: on the HUD frame either way, but without
    // this it had no depth test there, and its parts lay in the order they were drawn.
    const bool flat3d = g_vr_active && g_cur_flat3d;
    // A draw the game asked to have across the eye (EyeScreen): on the HUD frame's path,
    // but mapped onto the eye's clip space directly. Orthographic, or a perspective effect
    // the game hangs just before its camera's lens, which the game's projection maps to its
    // frame just the same.
    const bool screen = g_vr_active && g_cur_screen == 1;
    // One on the HUD frame with its edges taken far out (EyeFrameScreen).
    const bool frame_screen = g_vr_active && g_cur_screen == 2;
    // A copy of part of the frame put back over itself with a depth test (EyeRestore):
    // across the eye too, but at the game's depth and with its depth test.
    const bool restore = g_vr_active && g_cur_restore && g_cur_restore_box && !perspective;
    // A panel the game moved about its picture by its projection (EyeFacing): on the HUD
    // frame, turned to face the viewer.
    const bool facing = g_vr_active && g_cur_facing && !perspective && !screen && !restore && !frame_screen;
    const bool view_space_3d = perspective && view_space && vs3d && !flat3d;
    const bool on_hud_frame = screen || frame_screen || flat3d || ((!perspective || view_space) && !view_space_3d);
    if (perspective) {
        P[0] = p[0]; P[8] = p[1]; P[5] = p[2]; P[9] = p[3]; P[10] = p[4]; P[14] = p[5]; P[11] = -1.0f;
    } else {
        P[0] = p[0]; P[12] = p[1]; P[5] = p[2]; P[13] = p[3]; P[10] = p[4]; P[14] = p[5]; P[15] = 1.0f;
    }
    // Widescreen (flat output only): the frame is stretched 4/3 to 16:9 on output, so clip x
    // is squeezed by 3/4 here. For a perspective draw that is a wider field of view; for a 2D
    // one it keeps the picture in proportion, in the middle of the screen.
    // A 2D draw that puts a copy of the whole frame back (the game's own composite) is the
    // exception: the copy already holds the wide picture, so it is drawn across the frame as is.
    float wide = 1.0f;
    if (!g_vr_active) {
        wide = g_wide.load(std::memory_order_relaxed);
        if (wide != 1.0f && !perspective && samples_frame_copy(st)) wide = 1.0f;
        if (wide != 1.0f) { P[0] *= wide; P[8] *= wide; P[12] *= wide; }
    }
    // The game's own clip space bounds a flat3d draw to its viewport, which the eye's
    // target does not have: distance i of these planes is >= 0 inside it.
    float flat_crop[16] = {0};
    if (flat3d)
        for (int c = 0; c < 4; c++) {
            const float x = P[c * 4 + 0], y = P[c * 4 + 1], w = P[c * 4 + 3];
            flat_crop[c * 4 + 0] = w - x;
            flat_crop[c * 4 + 1] = w + x;
            flat_crop[c * 4 + 2] = w - y;
            flat_crop[c * 4 + 3] = w + y;
        }
    if (flat3d) {
        // The viewport, as a map from the draw's clip space into the 640x480 frame's that
        // the HUD frame is painted with (y up): x' = a*x + b*w, y' = c*y + d*w.
        int xoff, yoff;
        scissor_offset(st.bp, xoff, yoff);
        const float* vp = st.viewport;
        float V[16] = {0};
        V[0] = 2.0f * vp[0] / 640.0f;
        V[12] = 2.0f * (vp[3] - xoff) / 640.0f - 1.0f;
        V[5] = -2.0f * vp[1] / 480.0f;
        V[13] = 1.0f - 2.0f * (vp[4] - yoff) / 480.0f;
        V[10] = 1.0f;
        V[15] = 1.0f;
        float VP[16];
        mat4_mul(V, P, VP);
        memcpy(P, VP, sizeof(P));
    }
    // Depth layers. GX has no depth-range call, but a viewport carries a z range, and a game
    // can confine a draw to a band of the depth buffer with it -- a sky kept behind the
    // world whatever its geometry's real distance, a weapon kept in front of it. An eye
    // keeps those bands (see the vertex shader). What it cannot keep is the illusion: a sky
    // modelled a short way out around the camera has the disparity of something near, and
    // a weapon modelled large and far off reads as large in two eyes. So a game can name
    // its background and foreground bands (render_set_depth_layers): the background is
    // drawn at infinity, turned with the head and the same in both eyes, and the
    // foreground is scaled towards the camera. GCN_EYE_SKY=0 turns the background off.
    static const bool sky_at_infinity = !(getenv("GCN_EYE_SKY") && atoi(getenv("GCN_EYE_SKY")) == 0);
    const float band_lo = (st.viewport[5] - fabsf(st.viewport[2])) / 16777215.0f;
    const float band_hi = st.viewport[5] / 16777215.0f;
    const bool background = sky_at_infinity && g_bg_from > 0.0f && perspective && !view_space &&
                            band_lo >= g_bg_from;
    // The HUD layer takes precedence over the foreground, whose band it sits inside. A
    // draw the game's filter called EyeTrueScale -- something placed in the world, a
    // marker at a target, drawn in a near band only to be drawn over everything -- is in
    // neither: it stays where it was modelled, and keeps its band's depth.
    const bool true_scale = g_vr_active && g_cur_true_scale;
    const bool hud_layer = !true_scale && g_hud_to > 0.0f && g_hud_scale != 1.0f && perspective && band_hi <= g_hud_to;
    const bool foreground = !true_scale && !hud_layer && g_fg_to > 0.0f && g_fg_scale != 1.0f && perspective && band_hi <= g_fg_to;
    // A foreground layer also keeps the game's own depth within its band (u_vr 4 in the
    // vertex shader), whether or not it is scaled.
    const bool fg_layer = g_fg_to > 0.0f && perspective && band_hi <= g_fg_to;
    const float hud_push = g_vr_active && hud_layer ? g_cur_hud_push : 1.0f;
    const bool same_proj = view_held && memcmp(st.proj, u.proj, sizeof(st.proj)) == 0 &&
                           st.view_space == u.view_space && background == u.background &&
                           foreground == u.foreground && fg_layer == u.fg_layer && hud_layer == u.hud_layer &&
                           flat3d == u.flat3d && screen == u.screen && frame_screen == u.frame_screen && facing == u.facing && !restore && !u.restore &&
                           hud_push == u.hud_push && wide == u.wide && (!flat3d || memcmp(st.viewport, u.viewport, sizeof(st.viewport)) == 0);
    // Note: a draw sampling a copy of the whole frame (the water surface is one) must
    // stay in the world, however tempting its screen-space origin makes the overlay path
    // look. Sending the water through it put the water, and the racer baked into the
    // copy, on a flat panel hanging in front of the camera while the real racer went on
    // moving in the world. The seam at the billboard's edge is the lesser problem.
    if (!same_proj) {
        g_astat.proj++;
        if (g_vr_active && g_morph < 1.0f) {
            // Part way between theater and stereo: see morph_chain.
            float M[16], F[16], C[16];
            morph_chain(P, screen ? MorphKind::Screen : on_hud_frame ? MorphKind::Hud
                           : view_space_3d ? MorphKind::CameraHeld : MorphKind::World,
                        M, F, C);
            glUniformMatrix4fv(pr.u_proj, 1, GL_FALSE, M);
            glUniformMatrix4fv(pr.u_view, 1, GL_FALSE, F);
            glUniformMatrix4fv(pr.u_crop, 1, GL_FALSE, C);
            glUniform1i(pr.u_vr, 3);
            g_eye_layer_name = "morph";
        } else if (g_vr_active && !on_hud_frame) {
            // A camera-placed 3D object is viewed with the head transform but without the
            // world's pitch correction; see view_space_3d above.
            const float* view = background ? g_vr_view_sky
                              : view_space_3d ? (hud_layer ? g_vr_view_hud : foreground ? g_vr_view_fg : g_vr_view)
                              : (hud_layer ? g_vr_view_world_hud : foreground ? g_vr_view_world_fg : g_vr_view_world);
            if (g_cur_late_view) view = g_cur_late_view;
            // GCN_EYE_GAMEPROJ keeps the game's own frustum and applies only the head
            // transform, which tells apart "the eye sees less than it should" from "the game
            // never drew anything out there".
            static const bool game_proj = getenv("GCN_EYE_GAMEPROJ") != nullptr;
            static bool logged = false;
            if (!logged && getenv("GCN_EYELOG")) {
                logged = true;
                fprintf(stderr, "[eye] game fov: x=%.1fdeg y=%.1fdeg (p0=%f p2=%f)\n",
                        2.0f * atanf(1.0f / p[0]) * 57.2958f, 2.0f * atanf(1.0f / p[2]) * 57.2958f,
                        p[0], p[2]);
            }
            // A HUD element pushed out (render_set_hud_push): scaled about the camera in the
            // game's view space, before the layer's own scale.
            float pushed[16];
            if (hud_push != 1.0f) {
                const float K[16] = {hud_push, 0, 0, 0, 0, hud_push, 0, 0, 0, 0, hud_push, 0, 0, 0, 0, 1};
                mat4_mul(view, K, pushed);
                view = pushed;
            }
            glUniformMatrix4fv(pr.u_proj, 1, GL_FALSE, game_proj ? P : g_vr_proj);
            glUniformMatrix4fv(pr.u_view, 1, GL_FALSE, view);
            g_eye_layer_name = background ? "sky" : hud_push != 1.0f ? "hud-band(pushed)"
                             : hud_layer ? (view_space_3d ? "hud-band(view)" : "hud-band(world)")
                             : foreground ? (view_space_3d ? "foreground(view)" : "foreground(world)")
                             : view_space_3d ? "view-space-3d" : g_cur_late_view ? "late(at-panel)"
                             : true_scale ? "true-scale" : "world";
            static const bool game_depth = !(getenv("GCN_EYE_FGDEPTH") && atoi(getenv("GCN_EYE_FGDEPTH")) == 0);
            glUniform1i(pr.u_vr, fg_layer && game_depth ? 4 : 1);
        } else if (g_vr_active) {
            // A HUD element in an eye. The game's own projection already puts its frame in
            // [-1,1], so that is where the chain picks up -- for the perspective rig too,
            // since the frame's matrix carries clip w through and the divide lands it on
            // the plane just the same.
            //
            // GX's viewport transform is deliberately not in the chain: it places the frame
            // within the 640x528 EFB, and an eye's render target is not the EFB -- the same
            // reason the scissor rect is dropped below. Including it would map the EFB
            // rather than the 480 lines the game displays, leaving the HUD a few per cent
            // small and off centre.
            //
            // Every term is constant for the draw, so the whole chain folds into one matrix
            // here and the shader is left with a single multiply.
            float a[16], b[16], M[16];
            if (screen) {
                // The game's frame is [-1,1] after its own projection, and so is the eye's
                // field after the eye's: the one becomes the other, enlarged past the
                // field's edges, with no view between them -- fixed to the head. Depth is
                // the frame's own concern only (the HUD frame's draws are drawn without a
                // depth test), so it is left at the middle of the range.
                eye_screen_matrix(P, M);
            } else if (restore) {
                // The game puts its copy back over the rect it came from, which in the flat
                // view is where what it cuts away is. In an eye that is somewhere else, so
                // the quad is stretched from its box over the eye's whole field, past the
                // edges as EyeScreen's is: the grab it samples is what lies under every
                // fragment, so only what was drawn since the copy, nearer than the quad,
                // changes. Its depth is what the game gives it in its band -- the depth the
                // vertex shader writes for a foreground layer (u_vr 4), so the two compare
                // as they do on the screen: z' = b.x + b.y * z for the game's clip z, whose
                // w is 1 under an ortho projection.
                const float* r = g_cur_restore_box;
                const float sx = r[1] > r[0] ? 2.0f * kEyeScreenOverscan / (r[1] - r[0]) : 1.0f;
                const float sy = r[3] > r[2] ? 2.0f * kEyeScreenOverscan / (r[3] - r[2]) : 1.0f;
                const float cx = 0.5f * (r[0] + r[1]), cy = 0.5f * (r[2] + r[3]);
                const ViewportDepth depth = vp_depth_transform(st.viewport);
                for (int c = 0; c < 4; c++) {
                    M[c * 4 + 0] = sx * (P[c * 4 + 0] - cx * P[c * 4 + 3]);
                    M[c * 4 + 1] = sy * (P[c * 4 + 1] - cy * P[c * 4 + 3]);
                    M[c * 4 + 2] = depth.scale * P[c * 4 + 2] + depth.offset * P[c * 4 + 3];
                    M[c * 4 + 3] = P[c * 4 + 3];
                }
            } else {
                float faced[16];
                if (facing) facing_frame(P, faced);
                mat4_mul(facing ? faced : g_vr_hud_eff, P, a);   // the game's 2D frame, placed in view space
                mat4_mul(g_vr_view, a, b);  // that frame seen from this eye
                mat4_mul(g_vr_proj, b, M);
                if (frame_screen) {
                    // The shader works in the game's clip space, which the game's own
                    // projection gives it, taking a vertex on the picture's edge
                    // kFrameScreenReach times out on that axis; then the frame and the
                    // eye, without the projection (u_view). The other vertices go onto the
                    // frame as the frame's facing panel (the box of the EyeFacing draws,
                    // about its centre) turns it, or onto the frame as it is (u_proj).
                    float A[16], F[16];
                    mat4_mul(g_vr_view, g_vr_hud_eff, a);
                    mat4_mul(g_vr_proj, a, A);
                    glUniformMatrix4fv(pr.u_view, 1, GL_FALSE, A);
                    glUniformMatrix4fv(pr.u_crop, 1, GL_FALSE, P);
                    if (g_has_facing_box) {
                        facing_frame_at(0.5f * (g_facing_box[0] + g_facing_box[1]), 0.5f * (g_facing_box[2] + g_facing_box[3]), F);
                        mat4_mul(g_vr_view, F, a);
                        mat4_mul(g_vr_proj, a, M);
                    } else {
                        memcpy(M, A, sizeof(M));
                    }
                }
            }
            glUniformMatrix4fv(pr.u_proj, 1, GL_FALSE, M);
            if (flat3d) glUniformMatrix4fv(pr.u_crop, 1, GL_FALSE, flat_crop);
            glUniform1i(pr.u_vr, flat3d ? 5 : frame_screen ? 6 : 2);
            g_eye_layer_name = flat3d ? "hud-frame(3d)" : screen ? "eye-screen" : restore ? "eye-restore"
                             : facing ? "hud-frame(facing)" : frame_screen ? "hud-frame(screen)" : "hud-frame";
        } else if (g_flat_eye != 0.0f && perspective &&
                   !(g_panel_band > 0.0f && band_hi > band_lo && band_hi <= g_panel_band)) {
            // One of a stereo pair on the panel (a draw in the panel band falls through to
            // the plain projection, and so sits on the panel). The view is moved g_flat_eye to the side,
            // which shifts every point by -P00*e in clip x, and the frustum is sheared back
            // by e/halfw per unit of depth, which cancels that at the depth where the
            // frustum is as wide as the panel (D = P00 * halfw): a point there sits on the
            // panel in both eyes, and the disparity of one at depth z is e * (1 - D/z) of
            // the panel's half-width -- the eyes' own separation at infinity. In the
            // vertex shader clip x is P00*x + P[8]*z + P[12], so both terms are in P.
            //
            // The game's background band (render_set_depth_layers) is the exception: a sky
            // modelled some tens of units out -- a planet, the stars -- is at infinity, as
            // the eyes draw it. The shear alone gives every point the disparity of one at
            // infinity, so the move to the side is left out. With it, the planet of a
            // cinematic stood a few metres behind the panel while hiding nothing it should
            // not: its band kept its occlusion right and its depth wrong.
            float Q[16];
            memcpy(Q, P, sizeof(Q));
            Q[12] = background ? 0.0f : -P[0] * g_flat_eye;
            Q[8] -= g_flat_eye / g_flat_halfw;
            glUniformMatrix4fv(pr.u_proj, 1, GL_FALSE, Q);
            glUniform1i(pr.u_vr, 0);
        } else {
            glUniformMatrix4fv(pr.u_proj, 1, GL_FALSE, P);
            glUniform1i(pr.u_vr, 0);
        }
        // Fog runs off the depth GX would have written, which an eye does not write, so
        // the shader rebuilds it from the game's own projection -- the z row is all that
        // takes, plus how w is formed: -z under a frustum, 1 under an ortho batch.
        const float zp[4] = {p[4], p[5], perspective ? -1.0f : 0.0f, perspective ? 0.0f : 1.0f};
        glUniform4fv(pr.u_zproj, 1, zp);
        memcpy(u.proj, st.proj, sizeof(st.proj));
        u.view_space = st.view_space;
        u.background = background;
        u.foreground = foreground;
        u.fg_layer = fg_layer;
        u.hud_layer = hud_layer;
        u.flat3d = flat3d;
        u.screen = screen;
        u.frame_screen = frame_screen;
        u.restore = restore;
        u.facing = facing;
        u.hud_push = hud_push;
        u.wide = wide;
    }
    // Per state, whatever the projection: see g_frame_grad.
    if (frame_screen && g_cur_uvgrad && pr.u_uvgrad >= 0) glUniform4fv(pr.u_uvgrad, 8, g_cur_uvgrad);
    const float* vp = st.viewport;  // sx, sy, sz, ox, oy, oz
    if (!(held && bp[0x59] == u.scissor_off && memcmp(vp, u.viewport, sizeof(st.viewport)) == 0)) {
        g_astat.viewport++;
        float vpa[4] = {2.0f * (vp[3] - xoff) / EFB_W - 1.0f, 2.0f * vp[0] / EFB_W, 2.0f * (vp[4] - yoff) / EFB_H - 1.0f, 2.0f * vp[1] / EFB_H};
        const ViewportDepth depth = vp_depth_transform(vp);
        const float vpb[4] = {depth.offset, depth.scale, 0, 0};
        glUniform4fv(pr.u_vp_a, 1, vpa);
        glUniform4fv(pr.u_vp_b, 1, vpb);
        memcpy(u.viewport, vp, sizeof(st.viewport));
        u.scissor_off = bp[0x59];
    }
    // Only points read the point size, and a game that sets a line width per draw changes
    // this register with every line: so it is sent when a draw of points needs it, and a
    // record that is not current forgets what it held rather than keep a value it never sent.
    if (prim == 2) {
        if (!(held && bp[0x22] == u.psize_reg)) {
            g_astat.psize++;
            float psize = ((bp[0x22] >> 8) & 0xFF) / 6.0f * g_scale;
            glUniform1f(pr.u_point_size, psize < 1 ? 1 : psize);
            u.psize_reg = bp[0x22];
        }
    } else if (!held) {
        u.psize_reg = ~0u;
    }
    // A point's texture coordinates run across it, from the vertex's value to that plus the
    // point's texture offset, in each coordinate that has offsets enabled (GX's point
    // sprites: smoke, sparks and dust). GL gives each point one interpolated coordinate, so
    // the fragment shader spreads it with gl_PointCoord; every other draw sends 0.
    {
        const uint32_t key = prim == 2 ? point_tex_key(st) : 0;
        if (!(held && key == u.point_tex) && pr.u_points >= 0) {
            float off[8] = {};
            if (key) {
                const float range = kPointTexOffset[(bp[0x22] >> 19) & 7];
                for (int t = 0; t < 8; t++)
                    if ((key >> t) & 1) off[t] = range;
            }
            glUniform1i(pr.u_points, key != 0);
            if (pr.u_point_off >= 0) glUniform1fv(pr.u_point_off, 8, off);
            u.point_tex = key;
        }
    }

    // TEV registers
    if (!(held && memcmp(st.tev_reg, u.tev_reg, sizeof(st.tev_reg)) == 0 &&
          memcmp(st.tev_konst, u.tev_konst, sizeof(st.tev_konst)) == 0 && memcmp(bp + 0xF6, u.ksel, sizeof(u.ksel)) == 0)) {
        g_astat.tev++;
        GLint regs[16], kon[16];
        for (int r = 0; r < 4; r++) {
            uint32_t ra = st.tev_reg[r][0], bg = st.tev_reg[r][1];
            regs[r * 4 + 0] = sx11(ra); regs[r * 4 + 3] = sx11(ra >> 12);
            regs[r * 4 + 2] = sx11(bg); regs[r * 4 + 1] = sx11(bg >> 12);
            uint32_t ka = st.tev_konst[r][0], kb = st.tev_konst[r][1];
            kon[r * 4 + 0] = ka & 0xFF; kon[r * 4 + 3] = (ka >> 12) & 0xFF;
            kon[r * 4 + 2] = kb & 0xFF; kon[r * 4 + 1] = (kb >> 12) & 0xFF;
        }
        glUniform4iv(pr.u_reg, 4, regs);
        GLint kstage[64];
        stage_konsts(bp, kon, kstage);
        glUniform4iv(pr.u_kstage, 16, kstage);
        memcpy(u.tev_reg, st.tev_reg, sizeof(st.tev_reg));
        memcpy(u.tev_konst, st.tev_konst, sizeof(st.tev_konst));
        memcpy(u.ksel, bp + 0xF6, sizeof(u.ksel));
    }
    if (!(held && bp[0xF3] == u.aref)) {
        g_astat.aref++;
        GLint aref[2] = {(GLint)(bp[0xF3] & 0xFF), (GLint)((bp[0xF3] >> 8) & 0xFF)};
        glUniform2iv(pr.u_alpharef, 1, aref);
        u.aref = bp[0xF3];
    }
    // Only a shader with a Z texture has the bias, and few draws do: set every time.
    if (pr.u_zbias >= 0) glUniform1i(pr.u_zbias, (GLint)(bp[0xF4] & 0xFFFFFF));
    if (pr.u_dstalpha >= 0) glUniform1f(pr.u_dstalpha, (float)(bp[0x42] & 0xFF) / 255.0f);
    if (pr.u_zplane >= 0) glUniform4fv(pr.u_zplane, 1, g_zplane);
    // Indirect matrices
    if (!held_words(bp + 0x06, u.ind, 9)) {
        g_astat.ind++;
        GLint im[18];
        GLint isc[3];
        for (int m = 0; m < 3; m++) {
            uint32_t r0 = bp[0x06 + 3 * m], r1 = bp[0x07 + 3 * m], r2 = bp[0x08 + 3 * m];
            im[m * 6 + 0] = sx11(r0); im[m * 6 + 1] = sx11(r1); im[m * 6 + 2] = sx11(r2);
            im[m * 6 + 3] = sx11(r0 >> 11); im[m * 6 + 4] = sx11(r1 >> 11); im[m * 6 + 5] = sx11(r2 >> 11);
            isc[m] = (int)(((r0 >> 22) & 3) | (((r1 >> 22) & 3) << 2) | (((r2 >> 22) & 3) << 4)) - 17;
        }
        glUniform3iv(pr.u_indmtx, 6, im);
        glUniform1iv(pr.u_indscale, 3, isc);
        memcpy(u.ind, bp + 0x06, sizeof(u.ind));
    }
    if (!held_words(bp + 0x25, u.indcs, 2)) {
        g_astat.indcs++;
        float ics[8];
        for (int i = 0; i < 4; i++) {
            uint32_t ss = bp[0x25 + i / 2] >> ((i & 1) * 8);
            ics[i * 2] = 1.0f / (float)(1u << (ss & 15));
            ics[i * 2 + 1] = 1.0f / (float)(1u << ((ss >> 4) & 15));
        }
        glUniform2fv(pr.u_indcoordscale, 4, ics);
        memcpy(u.indcs, bp + 0x25, sizeof(u.indcs));
    }
    // Fog
    if (!held_words(bp + 0xEE, u.fog, 5)) {
        g_astat.fog++;
        float fog[4] = {fog_float(bp[0xEE]), fog_float(bp[0xF1]), (float)(bp[0xEF] & 0xFFFFFF), (float)(bp[0xF0] & 0x1F)};
        glUniform4fv(pr.u_fog, 1, fog);
        // GXSetFog's type: fsel (bits 21-23) in the low three bits, and the projection bit
        // (20, set for an orthographic one) on top. Until 2026-10-16 bits 20-23 were read as
        // the type, the projection bit as its lowest: exponential fog (4) came out as none,
        // linear (2) as exponential and exp2 (5) as an orthographic linear one.
        glUniform1i(pr.u_fogmode, (int)(((bp[0xF1] >> 21) & 7) | (((bp[0xF1] >> 20) & 1) << 3)));
        float fogc[3] = {((bp[0xF2] >> 16) & 0xFF) / 255.0f, ((bp[0xF2] >> 8) & 0xFF) / 255.0f, (bp[0xF2] & 0xFF) / 255.0f};
        glUniform3fv(pr.u_fogcolor, 1, fogc);
        memcpy(u.fog, bp + 0xEE, sizeof(u.fog));
    }

    // Which texgens feed a copy of the whole frame. In an eye those lookups are taken
    // over: the texture becomes the eye's own grab and the coordinate becomes the
    // fragment's own position, since the game's coordinate belongs to a view this eye is
    // not looking from. See grab_eye().
    uint32_t screen_uv = 0;
    if (g_vr_active) {
        const uint32_t nstg = ((bp[0x00] >> 10) & 15) + 1;
        for (uint32_t s = 0; s < nstg; s++) {
            const uint32_t order = bp[0x28 + s / 2] >> ((s & 1) * 12);
            const uint32_t map = order & 7;
            if ((order & 0x40) && st.tex_is_efb[map] && st.tex_id[map] &&
                is_eye_screen_tex(st.tex_id[map]))
                screen_uv |= 1u << ((order >> 3) & 7);
        }
    }
    if (!(view_held && screen_uv == u.screen_uv)) {
        g_astat.screen++;
        glUniform1i(pr.u_screen_uv, (GLint)screen_uv);
        glUniform2f(pr.u_screen_px, g_eye_w ? 1.0f / (float)g_eye_w : 0.0f,
                    g_eye_h ? 1.0f / (float)g_eye_h : 0.0f);
        u.screen_uv = screen_uv;
    }

    // The indirect offset that distorts a substituted lookup is an absolute displacement
    // in the copy's texels, and the copy's texels are EFB pixels. The eye grab's are not:
    // the eye sees a wider field across more pixels, so one of its pixels covers a
    // different angle. Both grids are pixels over an angle, so the conversion is the
    // ratio of their pixels per unit of frustum tangent -- the game's from its own
    // viewport and projection, the eye's from the target size and the headset's.
    //
    // Without it the droplets keep a displacement of a few pixels on a target several
    // times the EFB's width, which is a fraction of the distortion the game asked for:
    // each one degenerates into an almost exact copy of its own background and the spray
    // disappears rather than reading wrongly.
    //
    // The y term is negated because the two grids run opposite ways. An EFB copy's row 0
    // is the top of its source rect, so +t walks down the screen; the eye grab comes
    // straight off the render target, so its row 0 is the bottom and +t walks up -- which
    // is also the direction gl_FragCoord.y counts, and why the undistorted part of the
    // lookup needs no flip of its own.
    float ripple[2] = {1.0f, 1.0f};
    // GCN_EYE_RIPPLE=0 keeps the substitution but leaves the offset unconverted, which is
    // what the water surface shipped with and separates "the lookup is in the wrong
    // space" from "the distortion is the wrong size".
    static const bool no_ripple = getenv("GCN_EYE_RIPPLE") && atoi(getenv("GCN_EYE_RIPPLE")) == 0;
    if (screen_uv && perspective && !no_ripple) {
        const float gx = fabsf(vp[0]) * p[0], gy = fabsf(vp[1]) * p[2];
        if (gx > 0.0f) ripple[0] = 0.5f * (float)g_eye_w * g_vr_proj[0] / gx;
        if (gy > 0.0f) ripple[1] = -0.5f * (float)g_eye_h * g_vr_proj[5] / gy;
        // Mid-morph the scene is partly on the panel, where a unit of the game's frustum
        // tangent spans the panel's (half-width / distance) * p rather than one -- so the
        // eye's pixels cover a different share of it.
        if (g_morph < 1.0f) {
            const float D = -g_panel[14], k = g_morph;
            ripple[0] *= (1.0f - k) * p[0] * g_panel[0] / D + k;
            ripple[1] *= (1.0f - k) * p[2] * g_panel[5] / D + k;
        }
    }
    if (!(view_held && ripple[0] == u.ripple[0] && ripple[1] == u.ripple[1])) {
        g_astat.ripple++;
        glUniform2fv(pr.u_screen_ripple, 1, ripple);
        u.ripple[0] = ripple[0];
        u.ripple[1] = ripple[1];
    }

    // Textures. The bindings are global, so a unit already holding this texture under
    // this sampler is left alone whatever the program; the sizes are a uniform. The
    // texture and the sampler are bound separately: a frame here changed the texture on
    // a unit at nearly every draw and the sampler almost never.
    float tsz[16];
    // Depth copies are sampled nearest (GlTex::depth), and the shader snaps their
    // coordinate to the centre of the texel GX would read. A game reads such a copy back
    // with a quad whose coordinates run from texel centre to texel centre over exactly as
    // many pixels, which at internal scale 1 leaves the last row and column's coordinate
    // within 1/256 of a texel edge: the GPU's own subtexel rounding then reads the wrong
    // texel, a different depth, and a fog volume indexing its ramp with those bytes drew a
    // line along the edge of every chunk it was copied in. In texels the margin is clear
    // (half a texel over the chunk), so the snap is done there in float.
    uint32_t texsnap = 0;
    for (int m = 0; m < 8; m++) {
        tsz[m * 2] = tsz[m * 2 + 1] = 1.0f;
        uint32_t id = st.tex_id[m];
        const bool sub = screen_uv && st.tex_is_efb[m] && id && is_eye_screen_tex(id);
        GLuint sampler = 0;
        const GlTex* gt = nullptr;
        if (sub) {
            const EyeGrab& g = eye_grab_for(id);
            tsz[m * 2] = (float)g.w;
            tsz[m * 2 + 1] = (float)g.h;
        } else if (id) {
            auto it = g_textures.find(id);
            if (it != g_textures.end()) {
                gt = &it->second;
                it->second.last_used = g_render_frame;
                uint32_t base = m < 4 ? 0x80 + m : 0xA0 + (m - 4);
                uint32_t img0 = bp[base + 8];
                tsz[m * 2] = (float)((img0 & 0x3FF) + 1);
                tsz[m * 2 + 1] = (float)(((img0 >> 10) & 0x3FF) + 1);
                const uint32_t mode0 = gt->depth ? bp[base] & ~0xF0u : bp[base];  // see GlTex::depth
                sampler = get_sampler(mode0, bp[base + 4], gt->efb ? 1 : gt->levels);
                if (gt->depth) texsnap |= 1u << m;
            } else {
                id = 0;
            }
        }
        const bool tex_held = g_applied.valid && g_applied.tex_id[m] == id && g_applied.tex_sub[m] == sub;
        const bool samp_held = g_applied.valid && g_applied.sampler[m] == sampler;
        if (tex_held && samp_held) continue;
        glActiveTexture(GL_TEXTURE0 + m);
        if (!tex_held) {
            g_astat.texbind++;
            if (sub) glBindTexture(GL_TEXTURE_2D, eye_grab_for(id).tex);
            else glBindTexture(GL_TEXTURE_2D, gt ? gt->tex : 0);
            g_applied.tex_id[m] = id;
            g_applied.tex_sub[m] = sub;
        }
        if (!samp_held) {
            // The grab carries its own filtering; a sampler object would override it, so
            // a substituted unit gets none (0), as does an empty one.
            g_astat.sampbind++;
            glBindSampler(m, sampler);
            g_applied.sampler[m] = sampler;
        }
    }
    // The palettes an EFB copy is read through (PixelState::tex_pal), fetched texel by
    // texel, so whatever sampler the unit has does not matter.
    for (int m = 0; m < 8; m++) {
        uint32_t id = st.tex_is_efb[m] ? st.tex_pal[m] : 0;
        GLuint tex = 0;
        if (id) {
            auto it = g_textures.find(id);
            if (it != g_textures.end()) {
                it->second.last_used = g_render_frame;
                tex = it->second.tex;
            } else {
                id = 0;
            }
        }
        if (g_applied.valid && g_applied.pal_id[m] == id) continue;
        if (!id && !g_applied.valid) { g_applied.pal_id[m] = 0; continue; }
        glActiveTexture(GL_TEXTURE8 + m);
        glBindTexture(GL_TEXTURE_2D, tex);
        g_applied.pal_id[m] = id;
    }
    if (!(held && memcmp(tsz, u.tsz, sizeof(tsz)) == 0)) {
        g_astat.tsz++;
        glUniform2fv(pr.u_texsize, 8, tsz);
        memcpy(u.tsz, tsz, sizeof(tsz));
    }
    if (!(held && u.texsnap == texsnap)) {
        glUniform1i(pr.u_texsnap, (GLint)texsnap);
        u.texsnap = texsnap;
    }
    u.gen = g_uniform_gen;
    u.view_gen = g_view_gen;
    // Blend / logic op, colour mask
    const bool same_blend = g_applied.valid && bp[0x41] == abp[0x41] && bp[0x42] == abp[0x42] && bp[0x43] == abp[0x43];
    uint32_t bm = bp[0x41];
    if (!same_blend) {
        g_astat.blend++;
        bool blend = bm & 1, logic = (bm >> 1) & 1, sub = (bm >> 11) & 1;
        // A constant destination alpha (dst_alpha_mode) is the shader's alpha output, kept
        // as it is; the colour's source-alpha factors read the TEV's from source 1.
        const uint32_t dst_alpha = dst_alpha_mode(bp);
        if (sub) {
            glEnable(GL_BLEND);
            set_logic_op_off();
            if (dst_alpha) {
                glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
                glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
            } else {
                glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
                glBlendFunc(GL_ONE, GL_ONE);
            }
        } else if (blend) {
            glEnable(GL_BLEND);
            set_logic_op_off();
            glBlendEquation(GL_FUNC_ADD);
            GLenum sf = kBlendSrc[(bm >> 8) & 7], df = kBlendDst[(bm >> 5) & 7];
            if (dst_alpha) {
                auto src1 = [](GLenum f) {
                    return f == GL_SRC_ALPHA ? (GLenum)GL_SRC1_ALPHA
                         : f == GL_ONE_MINUS_SRC_ALPHA ? (GLenum)GL_ONE_MINUS_SRC1_ALPHA : f;
                };
                glBlendFuncSeparate(src1(sf), src1(df), GL_ONE, GL_ZERO);
            } else {
                glBlendFunc(sf, df);
            }
        } else if (logic) {
            glDisable(GL_BLEND);
            set_logic_op(bm >> 12);
        } else {
            glDisable(GL_BLEND);
            set_logic_op_off();
        }
        bool has_alpha = (bp[0x43] & 7) == 1;
        GLboolean cw = (bm >> 3) & 1, aw = ((bm >> 4) & 1) && has_alpha;
        glColorMask(cw, cw, cw, aw);
    }
    if (!same || !same_blend) {
        const uint64_t state = (uint64_t)pr.prog << 32 ^ bp[0x41] ^ (uint64_t)(bp[0x43] & 7) << 16 ^
                               (uint64_t)dst_alpha_mode(bp) << 20;
        if (g_seen_states.insert(state).second) g_state_new = true;
    }
    // Depth. In an eye the HUD is a quad out in the world rather than something laid over
    // the finished image, so the game's depth state no longer places it: the scene it is
    // meant to sit over is mostly nearer than the frame, and every element of the HUD is
    // on one plane. It is submitted last, so submission order is the layering. A 3D draw
    // composed into the HUD (flat3d) keeps the game's depth state: the eye cleared its
    // depth before the first of them (render_execute_eye), so they sort only among
    // themselves, by the game's own depth. A cut-away (EyeRestore) keeps it too: the depth
    // test is the whole of what it does.
    const bool hud_depth = g_vr_active && on_hud_frame && !flat3d && !restore;
    if (!(g_applied.valid && bp[0x40] == abp[0x40] && hud_depth == g_applied.hud_depth)) {
        g_astat.depth++;
        uint32_t zm = bp[0x40];
        if (zm & 1) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(kDepthFunc[(zm >> 1) & 7]);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask((zm >> 4) & 1);
        if (hud_depth) {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
        }
        g_applied.hud_depth = hud_depth;
    }
    // Cull
    uint32_t cull = (bp[0x00] >> 14) & 3;
    if (prim == 0) {
        static bool nocull = getenv("GCN_NOCULL") != nullptr;
        // GX: 1 = cull front, 2 = cull back. GX front faces are clockwise with y down,
        // which is counter-clockwise after our y flip (GL's default front face).
        const int want = (cull == 0 || nocull) ? 0 : cull == 3 ? 3 : ((cull == 2) != g_cull_swap ? 2 : 1);
        if (want != g_gl_cull) {
            g_astat.cull++;
            if (!want) {
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glCullFace(want == 3 ? GL_FRONT_AND_BACK : want == 2 ? GL_BACK : GL_FRONT);
            }
            g_gl_cull = want;
        }
    }
    g_applied.prim = prim;
    // Scissor (EFB coords, y down), measured from the same origin as the viewport.
    const bool same_scissor = g_applied.valid && bp[0x20] == abp[0x20] && bp[0x21] == abp[0x21] && bp[0x59] == abp[0x59];
    if (!same_scissor) {
        g_astat.scissor++;
        int x0 = (int)(bp[0x20] >> 12 & 0x7FF) - xoff, y0 = (int)(bp[0x20] & 0x7FF) - yoff;
        int x1 = (int)(bp[0x21] >> 12 & 0x7FF) - xoff + 1, y1 = (int)(bp[0x21] & 0x7FF) - yoff + 1;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > EFB_W) x1 = EFB_W;
        if (y1 > EFB_H) y1 = EFB_H;
        if (x1 < x0) x1 = x0;
        if (y1 < y0) y1 = y0;
        if (g_vr_active) {
            // The scissor rect is in EFB coordinates, which say nothing about an eye's
            // render target.
            //
            // GCN_EYE_SCISSORLOG reports the draws whose rect is not the whole frame, which
            // are the ones this is throwing away clipping for. Anything the game relies on
            // the scissor to hide is drawn in full in an eye.
            static const bool slog = getenv("GCN_EYE_SCISSORLOG") != nullptr;
            if (slog && (x0 > 0 || y0 > 0 || x1 < EFB_W || y1 < EFB_H)) {
                static int shown;
                if (shown++ < 40)
                    fprintf(stderr, "[scissor] f%u rect=%d,%d..%d,%d (frame is 0,0..%d,%d)\n",
                            g_render_frame, x0, y0, x1, y1, EFB_W, EFB_H);
            }
            glDisable(GL_SCISSOR_TEST);
        } else {
            glEnable(GL_SCISSOR_TEST);
            glScissor(x0 * g_scale, (EFB_H - y1) * g_scale, (x1 - x0) * g_scale, (y1 - y0) * g_scale);
        }
    }
    // Everything above has now been compared against bp, so it becomes the reference.
    memcpy(g_applied.bp, bp, sizeof(g_applied.bp));
    g_applied.valid = true;
}

// A copy the spray composites from. It neither scans out nor clears -- a clearing copy
// ends an off-screen pass, and a grab deliberately leaves the scene it lifted from intact
// -- and it is small: at speed the spray takes 32x32 and 64x64 rects, while the other
// partial copies in this game (the water reflection, the rect the submerged tint samples)
// are far larger, which is what keeps them out of this. See is_grab_tex().
static constexpr uint32_t kGrabMax = 64;
static bool is_grab_copy(const EfbCopyCmd& c) {
    return !c.to_xfb && !c.clear && c.dst_w <= kGrabMax && c.dst_h <= kGrabMax;
}

// Presented frames so far; the number the frame dumps carry. Defined here because the
// copy dump below keys on it.
static uint32_t g_present_count;
// A texture copy written to RAM (EfbCopyCmd::writeback), in GX's tiled layout: read back
// from the copy's texture, which the copy fbo still holds, one sample from the middle of
// each hardware pixel's block, and encoded as the format the game asked for. The hardware
// writes whole tiles; rows and columns past the rect repeat its edge here. The bytes go to
// copy_writeback_done, for the guest thread to put in RAM; a format this does not encode is
// handed over empty, so that nothing waits on it.
//
// The copy shader has already written each texel as the game will read it (kCopyFS), so a
// format is only a matter of packing: the one-byte formats (I8, A8, R8, G8, B8, and the
// depth bytes Z8, Z8M, Z8L, which GX copies by the same nibbles) hold their byte in every
// channel, in 8x4 tiles; the two-byte ones (IA8, RG8, GB8, and Z16 and Z16L as the last
// two) are read back as IA8, alpha then intensity, in 4x4 tiles. A depth copy into the
// colour formats is not encoded.
static void copy_writeback(const EfbCopyCmd& c) {
    const uint32_t fmt = c.format & 0xF, w = c.dst_w, h = c.dst_h, s = (uint32_t)g_scale;
    const bool one_byte = fmt == 1 || (fmt >= 7 && fmt <= 10);
    const bool two_byte = fmt == 3 || fmt == 11 || fmt == 12;
    const bool colour = fmt == 4 || fmt == 5 || fmt == 6;
    const uint32_t tw = one_byte ? 8 : 4;
    const uint32_t bw = (w + tw - 1) / tw, bh = (h + 3) / 4;
    const uint32_t bytes = fmt == 6 ? bw * bh * 64 : bw * bh * 32;
    const bool known = one_byte || two_byte || (colour && !c.depth);
    if (!known || (c.dest & 0x01FFFFFF) + bytes > RAM_SIZE) {
        static bool warned;
        if (!warned) { warned = true; fprintf(stderr, "[copy] write-back of format %X%s not done\n", c.format, c.depth ? " (depth)" : ""); }
        copy_writeback_done(c.writeback_ticket, {});
        return;
    }
    std::vector<uint8_t> px((size_t)w * s * h * s * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, (GLsizei)(w * s), (GLsizei)(h * s), GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    auto at = [&](uint32_t x, uint32_t y) {
        x = std::min(x, w - 1);
        y = std::min(y, h - 1);
        return &px[(((size_t)y * s + s / 2) * w * s + (size_t)x * s + s / 2) * 4];
    };
    std::vector<uint8_t> encoded(bytes);
    uint8_t* out = encoded.data();
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            uint8_t* blk = out + ((size_t)by * bw + bx) * (fmt == 6 ? 64 : 32);
            if (one_byte) {
                for (uint32_t i = 0; i < 32; i++) blk[i] = at(bx * 8 + i % 8, by * 4 + i / 8)[0];
                continue;
            }
            for (uint32_t i = 0; i < 16; i++) {
                const uint8_t* p = at(bx * 4 + i % 4, by * 4 + i / 4);
                uint16_t v;
                if (two_byte) {
                    blk[2 * i] = p[3]; blk[2 * i + 1] = p[0];
                    continue;
                }
                if (fmt == 6) {  // RGBA8: AR pairs in the first half of the tile, GB in the second
                    blk[2 * i] = p[3]; blk[2 * i + 1] = p[0];
                    blk[32 + 2 * i] = p[1]; blk[32 + 2 * i + 1] = p[2];
                    continue;
                }
                if (fmt == 4) v = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
                else if (p[3] >= 0xE0) v = (uint16_t)(0x8000 | ((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3));
                else v = (uint16_t)(((p[3] >> 5) << 12) | ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4));
                blk[2 * i] = (uint8_t)(v >> 8);
                blk[2 * i + 1] = (uint8_t)v;
            }
        }
    copy_writeback_done(c.writeback_ticket, std::move(encoded));
}

static void do_efb_copy(const EfbCopyCmd& c) {
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    set_logic_op_off();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    g_gl_cull = 0;
    glColorMask(1, 1, 1, 1);
    if (!c.to_xfb && c.tex_id) {
        // The same target redrawn each frame keeps its id, so reuse the texture it
        // already has rather than allocating and freeing one per copy per frame.
        GlTex t{};
        auto old = g_textures.find(c.tex_id);
        const bool reuse = old != g_textures.end() && old->second.efb &&
                           old->second.w == c.dst_w && old->second.h == c.dst_h;
        if (reuse) {
            t = old->second;
            glBindTexture(GL_TEXTURE_2D, t.tex);
        } else {
            glGenTextures(1, &t.tex);
            t.w = c.dst_w; t.h = c.dst_h; t.levels = 1; t.efb = true;
            glBindTexture(GL_TEXTURE_2D, t.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, c.dst_w * g_scale, c.dst_h * g_scale, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        }
        t.last_used = g_render_frame;
        // Set after the reuse path, which carries the previous copy's flags in.
        t.grab = is_grab_copy(c);
        t.depth = c.depth;
        glBindFramebuffer(GL_FRAMEBUFFER, g_copy_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
        glViewport(0, 0, c.dst_w * g_scale, c.dst_h * g_scale);
        glUseProgram(g_copy_prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_efb_color);
        glBindSampler(0, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_efb_depth);
        glBindSampler(1, 0);
        glUniform1i(g_copy_u_src, 0);
        glUniform1i(g_copy_u_depth, 1);
        // Destination texel row 0 = top of the source rect.
        float rect[4] = {(float)c.src_x / EFB_W, (float)c.src_y / EFB_H,
                         (float)(c.src_x + c.src_w) / EFB_W, (float)(c.src_y + c.src_h) / EFB_H};
        glUniform4fv(g_copy_u_rect, 1, rect);
        glUniform1i(g_copy_u_mode, (int)c.format | (c.depth ? 32 : 0));
        glBindVertexArray(g_copy_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (c.writeback) copy_writeback(c);
        // GCN_COPYDUMP=<dir> with GCN_DRAWLOG=<frame>: every texture copy made in that
        // frame is written out as it was produced, twice -- its colour, and its alpha
        // as grey -- named by the frame, the copy's order in it and its texture id.
        {
            static const char* copydump = getenv("GCN_COPYDUMP");
            static const uint32_t copydump_frame = getenv("GCN_DRAWLOG") ? (uint32_t)atoi(getenv("GCN_DRAWLOG")) : 0;
            static uint32_t copydump_n = 0;
            // GCN_COPYDUMP_SIZE=WxH dumps every copy of that size, in whatever frame: a copy
            // made once, at a moment that differs from run to run, is caught without a frame.
            static uint32_t size_w = 0, size_h = 0;
            static const bool size_parsed = [] {
                if (const char* e = getenv("GCN_COPYDUMP_SIZE")) sscanf(e, "%ux%u", &size_w, &size_h);
                return true;
            }();
            (void)size_parsed;
            const bool by_size = size_w && c.dst_w == size_w && c.dst_h == size_h;
            if (copydump && (by_size || (copydump_frame && g_present_count + 1 == copydump_frame))) {
                const int w = c.dst_w * g_scale, h = c.dst_h * g_scale;
                std::vector<uint8_t> px((size_t)w * h * 4), out((size_t)w * h * 4);
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                char path[512];
                for (int pass = 0; pass < 2; pass++) {
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++) {
                            const uint8_t* q = &px[((size_t)y * w + x) * 4];
                            uint8_t* o = &out[((size_t)y * w + x) * 4];
                            if (pass == 0) { o[0] = q[0]; o[1] = q[1]; o[2] = q[2]; }
                            else { o[0] = o[1] = o[2] = q[3]; }
                            o[3] = 255;
                        }
                    snprintf(path, sizeof(path), "%s/copy_%05u_%03u_tex%u_%s.png", copydump, g_present_count + 1, copydump_n,
                             c.tex_id, pass == 0 ? "rgb" : "alpha");
                    write_png(path, out.data(), w, h);
                }
                copydump_n++;
            }
        }
        if (!reuse && old != g_textures.end()) glDeleteTextures(1, &old->second.tex);
        g_textures[c.tex_id] = t;
        glBindFramebuffer(GL_FRAMEBUFFER, g_efb_fbo);
        glBindVertexArray(g_vao);
    }
    if (c.clear) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_efb_fbo);
        glEnable(GL_SCISSOR_TEST);
        glScissor(c.src_x * g_scale, (EFB_H - c.src_y - c.src_h) * g_scale, c.src_w * g_scale, c.src_h * g_scale);
        GLbitfield bits = 0;
        glColorMask(c.clear_color, c.clear_color, c.clear_color, c.clear_alpha);
        if (c.clear_color || c.clear_alpha) bits |= GL_COLOR_BUFFER_BIT;
        if (c.clear_z) { bits |= GL_DEPTH_BUFFER_BIT; glDepthMask(GL_TRUE); }
        glClearColor(((c.clear_rgba >> 24) & 0xFF) / 255.0f, ((c.clear_rgba >> 16) & 0xFF) / 255.0f,
                     ((c.clear_rgba >> 8) & 0xFF) / 255.0f, (c.clear_rgba & 0xFF) / 255.0f);
        clear_depth(c.clear_z_value / 16777215.0f);
        if (bits) glClear(bits);
        glColorMask(1, 1, 1, 1);
    }
}

const char* g_dump_dir = nullptr;
int g_dump_every = 0;
// The second pass of a stereo pair (render_execute_stereo_pair) presents the same frame
// again: it is not counted twice, and its dump is told apart by a suffix.
static bool g_pair_second = false;
static const char* g_dump_suffix = "";

static void dump_efb(const EfbCopyCmd& c) {
    int w = c.src_w * g_scale, h = c.src_h * g_scale;
    std::vector<uint8_t> px((size_t)w * h * 4), flipped((size_t)w * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_efb_fbo);
    glReadPixels(c.src_x * g_scale, (EFB_H - c.src_y - c.src_h) * g_scale, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; y++) {
        memcpy(&flipped[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
        for (int x = 0; x < w; x++) flipped[((size_t)y * w + x) * 4 + 3] = 255;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%05u%s.png", g_dump_dir, g_present_count, g_dump_suffix);
    write_png(path, flipped.data(), w, h);
}

// Where the finished frame is blitted. 0 is the window's framebuffer; a VR frontend
// points this at one of its swapchain images instead.
static GLuint g_output_fbo = 0;
// The most recent presented rect, so the output can be refreshed without the game
// having produced a new frame (a VR compositor wants one every display frame, which is
// far more often than this game renders).
// Blit a rect of a texture laid out like the EFB (guest row r at GL row h - 1 - r) to the
// output, letterboxed to 4:3.
static void blit_rect_to_output(GLuint src_tex, uint32_t tex_w, uint32_t tex_h, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh);
static void blit_to_output(const EfbCopyCmd& c, GLuint src_tex) {
    blit_rect_to_output(src_tex, EFB_W, EFB_H, c.src_x, c.src_y, c.src_w, c.src_h);
}
static void blit_rect_to_output(GLuint src_tex, uint32_t tex_w, uint32_t tex_h, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh) {
    glBindFramebuffer(GL_FRAMEBUFFER, g_output_fbo);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    set_logic_op_off();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    g_gl_cull = 0;
    glColorMask(1, 1, 1, 1);
    glViewport(0, 0, g_win_w, g_win_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    // Letterbox to 4:3 (16:9 in widescreen)
    int w = g_win_w, h = g_win_h;
    const bool wide = g_wide.load(std::memory_order_relaxed) != 1.0f;
    int vw = w, vh = wide ? w * 9 / 16 : w * 3 / 4;
    if (vh > h) { vh = h; vw = wide ? h * 16 / 9 : h * 4 / 3; }
    glViewport((w - vw) / 2, (h - vh) / 2, vw, vh);
    glUseProgram(g_blit_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glBindSampler(0, 0);
    glUniform1i(g_blit_u_src, 0);
    float rect[4] = {(float)rx / tex_w, (float)(ry + rh) / tex_h, (float)(rx + rw) / tex_w, (float)ry / tex_h};
    glUniform4fv(g_blit_u_rect, 1, rect);
    glBindVertexArray(g_copy_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(g_vao);
}

// The external framebuffers. A game whose EFB cannot hold a whole frame (anti-aliased,
// the EFB is 640x264) renders it in halves and copies each to its place in the XFB, and
// may copy strips elsewhere besides; presenting each display copy as a frame showed the
// last strip. So display copies are assembled here into an image per XFB, keyed by the
// address range the copies land in, and the image is presented when every one of its
// rows has been written since it was last shown. The VI's timing registers say how many
// rows a frame has; a copy that covers them all presents at once, as before.
struct Xfb {
    uint32_t base, stride, h;
    GLuint tex, fbo;
    std::vector<uint8_t> written;
    uint32_t written_rows;
};
static std::vector<Xfb> g_xfbs;

static Xfb& xfb_for(uint32_t dest, uint32_t w, uint32_t stride, uint32_t h) {
    for (auto& x : g_xfbs)
        if (x.stride == stride && x.h == h && dest >= x.base && dest < x.base + stride * h) return x;
    Xfb x{};
    x.base = dest;
    x.stride = stride;
    x.h = h;
    glGenTextures(1, &x.tex);
    glBindTexture(GL_TEXTURE_2D, x.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)(w * g_scale), (GLsizei)(h * g_scale), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &x.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, x.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, x.tex, 0);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    x.written.assign(h, 0);
    g_xfbs.push_back(x);
    if (g_xfbs.size() > 8) g_xfbs.erase(g_xfbs.begin());
    return g_xfbs.back();
}

static void dump_xfb(const Xfb& x, uint32_t w) {
    int pw = w * g_scale, ph = x.h * g_scale;
    std::vector<uint8_t> px((size_t)pw * ph * 4), flipped((size_t)pw * ph * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, x.fbo);
    glReadPixels(0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < ph; y++) {
        memcpy(&flipped[(size_t)y * pw * 4], &px[(size_t)(ph - 1 - y) * pw * 4], (size_t)pw * 4);
        for (int xx = 0; xx < pw; xx++) flipped[((size_t)y * pw + xx) * 4 + 3] = 255;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%05u%s.png", g_dump_dir, g_present_count, g_dump_suffix);
    write_png(path, flipped.data(), pw, ph);
}

// A display copy into its XFB. Returns the XFB when the copy completed a frame of it.
static Xfb* xfb_write(const EfbCopyCmd& c) {
    uint32_t h = vi_display_height();
    if (!h || c.dst_h >= h) return nullptr;  // the copy is the whole frame: present it as such
    uint32_t stride = c.dst_w * 2;
    Xfb& x = xfb_for(c.dest, c.dst_w, stride, h);
    uint32_t row0 = (c.dest - x.base) / stride;
    uint32_t rows = std::min(c.dst_h, h - row0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_efb_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, x.fbo);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(c.src_x * g_scale, (EFB_H - c.src_y - c.src_h) * g_scale, (c.src_x + c.src_w) * g_scale, (EFB_H - c.src_y) * g_scale,
                      0, (h - row0 - rows) * g_scale, c.dst_w * g_scale, (h - row0) * g_scale, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    for (uint32_t r = row0; r < row0 + rows; r++)
        if (!x.written[r]) { x.written[r] = 1; x.written_rows++; }
    if (x.written_rows < h) return nullptr;
    std::fill(x.written.begin(), x.written.end(), 0);
    x.written_rows = 0;
    return &x;
}

static void present(const EfbCopyCmd& c) {
    // A copy that is part of a frame: into the XFB, and nothing to show unless it was the
    // last part.
    Xfb* xfb = nullptr;
    if (!g_pair_second) {
        uint32_t h = vi_display_height();
        if (h && c.dst_h < h) {
            xfb = xfb_write(c);
            if (!xfb) return;
        }
    }
    if (!g_pair_second) g_present_count++;
    // GCN_DUMP_RANGE=a-b restricts dumping to a window of presented frames, so a short
    // stretch can be captured every single frame. Flicker is only visible frame by frame.
    static int range_lo = -1, range_hi = -1;
    static bool range_parsed = false;
    if (!range_parsed) {
        range_parsed = true;
        if (const char* s = getenv("GCN_DUMP_RANGE")) {
            range_lo = atoi(s);
            const char* dash = strchr(s, '-');
            range_hi = dash ? atoi(dash + 1) : range_lo;
        }
    }
    const bool in_range = range_lo < 0 || ((int)g_present_count >= range_lo &&
                                           (int)g_present_count <= range_hi);
    if (xfb) {
        if (g_dump_dir && g_dump_every && in_range && g_present_count % g_dump_every == 0) dump_xfb(*xfb, c.dst_w);
        blit_rect_to_output(xfb->tex, c.dst_w, xfb->h, 0, 0, c.dst_w, xfb->h);
        return;
    }
    if (g_dump_dir && g_dump_every && in_range && g_present_count % g_dump_every == 0) dump_efb(c);
    blit_to_output(c, g_efb_color);
}

void render_set_output_fbo(unsigned fbo) { g_output_fbo = (GLuint)fbo; }
uint32_t present_count() { return g_present_count; }


// Executes a batch. Returns true if it contained a Present.
// The camera looks down by `pitch`, so the world's up arrives at (0, cos, sin) in the
// vertices' own frame. Rotating about X by -pitch takes it back to (0, 1, 0), which is the
// headset's up, and the sea with it.
static void world_pitch_matrix(float R[16]) {
    const float c = cosf(g_world_pitch), s = sinf(g_world_pitch);
    memset(R, 0, 16 * sizeof(float));
    R[0] = 1.0f;
    R[5] = c;  R[6] = -s;
    R[9] = s;  R[10] = c;
    R[15] = 1.0f;
}

static void view_invalidate();
static void compose_world_view() {
    view_invalidate();
    mat4_mul(g_vr_view, g_world_xform, g_vr_view_world);
    memcpy(g_vr_view_sky, g_vr_view_world, sizeof(g_vr_view_sky));
    g_vr_view_sky[12] = g_vr_view_sky[13] = g_vr_view_sky[14] = 0.0f;
    const float S[16] = {g_fg_scale, 0, 0, 0, 0, g_fg_scale, 0, 0, 0, 0, g_fg_scale, 0, 0, 0, 0, 1};
    mat4_mul(g_vr_view, S, g_vr_view_fg);
    mat4_mul(g_vr_view_world, S, g_vr_view_world_fg);
    const float H[16] = {g_hud_scale, 0, 0, 0, 0, g_hud_scale, 0, 0, 0, 0, g_hud_scale, 0, 0, 0, 0, 1};
    mat4_mul(g_vr_view, H, g_vr_view_hud);
    mat4_mul(g_vr_view_world, H, g_vr_view_world_hud);
    mat4_mul(g_hud_xform, g_vr_hud, g_vr_hud_eff);
}

static void hud_upright() {
    memset(g_hud_xform, 0, sizeof(g_hud_xform));
    g_hud_xform[0] = g_hud_xform[5] = g_hud_xform[10] = g_hud_xform[15] = 1.0f;
}

void render_set_world_pitch(float pitch_rad) {
    g_world_pitch = pitch_rad;
    if (!g_eye_overridden) world_pitch_matrix(g_world_xform);
    compose_world_view();
}

void render_set_depth_layers(float background_from, float foreground_to, float foreground_scale,
                             float hud_to, float hud_scale) {
    g_bg_from = background_from;
    g_fg_to = foreground_to;
    g_fg_scale = foreground_scale > 0.0f ? foreground_scale : 1.0f;
    g_hud_to = hud_to;
    g_hud_scale = hud_scale > 0.0f ? hud_scale : 1.0f;
    compose_world_view();
}

void render_set_view_space_world(bool on) { g_view_space_world = on; }
void render_set_hud_push(float below, float to) {
    g_hud_push_below = below > 0.0f && to > 0.0f ? below : 0.0f;
    g_hud_push_to = to;
}

void render_set_eye_hook(EyeHook hook) { g_eye_hook = hook; }
void render_set_world_pitch_hook(WorldPitchHook hook) { g_pitch_hook = hook; }
static FrameHook g_frame_hook = nullptr;
void render_set_frame_hook(FrameHook hook) { g_frame_hook = hook; }
static void mark_offscreen_passes(const Batch& b, std::vector<uint8_t>& skip);
// The game's look at a batch about to be drawn (render_set_frame_hook). `skip` is the
// off-screen passes if the caller has them already.
static void frame_observe(const Batch& b, const std::vector<uint8_t>* skip) {
    if (!g_frame_hook) return;
    static std::vector<uint8_t> own;
    if (!skip) {
        mark_offscreen_passes(b, own);
        skip = &own;
    }
    g_frame_hook(b, *skip);
}
void render_set_eye_filter(EyeFilter filter) { g_eye_filter = filter; }

// What an eye filter is told about a draw; see EyeFilter.
static EyeDrawFacts draw_facts(const PixelState& st) {
    EyeDrawFacts f{};
    f.ortho = (int)st.proj[6] != 0;
    f.indirect = ((st.bp[0x00] >> 16) & 7) != 0;
    f.band_hi = st.viewport[5] / 16777215.0f;
    f.band_lo = (st.viewport[5] - fabsf(st.viewport[2])) / 16777215.0f;
    int xoff, yoff;
    scissor_offset(st.bp, xoff, yoff);
    f.vp_w = 2.0f * fabsf(st.viewport[0]);
    f.vp_h = 2.0f * fabsf(st.viewport[1]);
    f.vp_x = st.viewport[3] - xoff - 0.5f * f.vp_w;
    f.vp_y = st.viewport[4] - yoff - 0.5f * f.vp_h;
    for (int i = 0; i < 8; i++) {
        if (!st.tex_id[i]) continue;
        if (st.tex_is_efb[i]) {
            f.samples_copy = true;
            if (is_fullscreen_tex(st.tex_id[i])) f.samples_fullscreen_copy = true;
        } else {
            f.tex_hash[i] = render_texture_hash(st.tex_id[i]);
        }
    }
    return f;
}

bool render_first_person() { return g_fp_on; }

void render_set_first_person(bool on) {
    g_fp_on = on;
    if (!on) {
        g_eye_overridden = false;
        hud_upright();
        world_pitch_matrix(g_world_xform);
        compose_world_view();
    }
}

void render_set_vr_eye(const float proj[16], const float view[16], const float hud[16]) {
    memcpy(g_vr_proj, proj, sizeof(g_vr_proj));
    memcpy(g_vr_view_head, view, sizeof(g_vr_view_head));
    memcpy(g_vr_view, view, sizeof(g_vr_view));
    memcpy(g_vr_hud, hud, sizeof(g_vr_hud));
    compose_world_view();
}

// g_vr_view for `b`: the head's view times the batch's view turn, a rotation taking what
// the game placed about its turned camera back to where the viewer's frame has it.
static void apply_view_turn(const Batch& b) {
    const float x = b.view_turn[0], y = b.view_turn[1], z = b.view_turn[2], w = b.view_turn[3];
    const float R[16] = {
        1 - 2 * (y * y + z * z), 2 * (x * y + w * z),     2 * (x * z - w * y),     0,
        2 * (x * y - w * z),     1 - 2 * (x * x + z * z), 2 * (y * z + w * x),     0,
        2 * (x * z + w * y),     2 * (y * z - w * x),     1 - 2 * (x * x + y * y), 0,
        0,                       0,                       0,                       1,
    };
    mat4_mul(g_vr_view_head, R, g_vr_view);
    compose_world_view();
}

// The game's draw filter's answer for each state the batch's draws use (-1 for a state no
// draw outside `skip` uses), and what each orthographic state's draws cover together, in its
// clip space (x = p0*x + p1, y = p2*y + p3 for GX's orthographic projection), four floats a
// state, for EyeDrawFacts' box. A draw's facts are the state's, so each state is asked once
// and its answer stands for its draws.
static void filter_answers(const Batch& b, const std::vector<uint8_t>* skip, std::vector<int8_t>& answer,
                           std::vector<float>& box) {
    note_fullscreen_copies(b);
    box.assign(b.states.size() * 4, 0.0f);
    std::vector<float> area(b.states.size(), 0.0f);
    std::vector<uint8_t> boxed(b.states.size(), 0);
    for (size_t i = 0; i < b.cmds.size(); i++) {
        const Cmd& c = b.cmds[i];
        if (c.type != CmdType::Draw || (skip && (*skip)[i])) continue;
        const PixelState& st = b.states[c.state];
        if ((int)st.proj[6] == 0) continue;
        float* r = &box[c.state * 4];
        for (uint32_t v = 0; v < c.count; v++) {
            const float* pos = b.verts[b.indices[c.first + v]].pos;
            const float x = st.proj[0] * pos[0] + st.proj[1], y = st.proj[2] * pos[1] + st.proj[3];
            if (!boxed[c.state]) { r[0] = r[1] = x; r[2] = r[3] = y; boxed[c.state] = 1; }
            r[0] = std::min(r[0], x); r[1] = std::max(r[1], x);
            r[2] = std::min(r[2], y); r[3] = std::max(r[3], y);
        }
        if (c.prim != 0) continue;  // lines and points cover nothing
        auto clip = [&](uint32_t v, float& x, float& y) {
            const float* pos = b.verts[b.indices[c.first + v]].pos;
            x = st.proj[0] * pos[0] + st.proj[1];
            y = st.proj[2] * pos[1] + st.proj[3];
        };
        for (uint32_t t = 0; t + 2 < c.count; t += 3) {
            float x0, y0, x1, y1, x2, y2;
            clip(t, x0, y0); clip(t + 1, x1, y1); clip(t + 2, x2, y2);
            area[c.state] += 0.5f * fabsf((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0));
        }
    }
    answer.assign(b.states.size(), -1);
    for (size_t i = 0; i < b.cmds.size(); i++) {
        const Cmd& c = b.cmds[i];
        if (c.type != CmdType::Draw || (skip && (*skip)[i])) continue;
        int8_t& a = answer[c.state];
        if (a >= 0) continue;
        EyeDrawFacts f = draw_facts(b.states[c.state]);
        const float* r = &box[c.state * 4];
        f.clip_x0 = r[0]; f.clip_x1 = r[1]; f.clip_y0 = r[2]; f.clip_y1 = r[3];
        f.clip_area = area[c.state];
        a = (int8_t)g_eye_filter(b.states[c.state], f);
    }
}

// An EyeFrameScreen state's texture coordinates as functions of the game's clip x and y,
// where the shader takes them out: only a vertex on the picture's edge moves, so only the
// triangles with one matter. Each gives its gradients exactly (three points make a plane),
// and they are kept where all of those triangles agree. A whole-draw fit failed on a
// filter that draws a full-screen quad and a window's rect in one draw with different
// mappings: the rect never moves, and its mapping is nothing to the quad's.
static void fit_uv_gradients(const Batch& b, uint32_t state, float out[32]) {
    memset(out, 0, 32 * sizeof(float));
    const PixelState& st = b.states[state];
    float grad[32];
    bool have = false, agree[8][2];
    for (int i = 0; i < 8; i++) agree[i][0] = agree[i][1] = true;
    for (const Cmd& c : b.cmds) {
        if (c.type != CmdType::Draw || c.state != state || c.prim != 0) continue;
        for (uint32_t t = 0; t + 2 < c.count; t += 3) {
            const GpuVertex* v[3];
            float x[3], y[3];
            bool edge = false;
            for (int j = 0; j < 3; j++) {
                v[j] = &b.verts[b.indices[c.first + t + j]];
                x[j] = st.proj[0] * v[j]->pos[0] + st.proj[1];
                y[j] = st.proj[2] * v[j]->pos[1] + st.proj[3];
                edge |= fabsf(x[j]) >= 0.98f || fabsf(y[j]) >= 0.98f;
            }
            if (!edge) continue;
            const float dx1 = x[1] - x[0], dy1 = y[1] - y[0], dx2 = x[2] - x[0], dy2 = y[2] - y[0];
            const float det = dx1 * dy2 - dx2 * dy1;
            if (fabsf(det) < 1e-8f) continue;
            float g[32];
            for (int i = 0; i < 8; i++)
                for (int k = 0; k < 2; k++) {
                    const float d1 = v[1]->tex[i][k] - v[0]->tex[i][k], d2 = v[2]->tex[i][k] - v[0]->tex[i][k];
                    g[i * 4 + k * 2] = (d1 * dy2 - d2 * dy1) / det;
                    g[i * 4 + k * 2 + 1] = (dx1 * d2 - dx2 * d1) / det;
                }
            if (!have) {
                memcpy(grad, g, sizeof(grad));
                have = true;
                continue;
            }
            for (int i = 0; i < 8; i++)
                for (int k = 0; k < 2; k++)
                    for (int e = 0; e < 2; e++) {
                        const float a0 = grad[i * 4 + k * 2 + e], a1 = g[i * 4 + k * 2 + e];
                        if (fabsf(a0 - a1) > 1e-3f * (1.0f + fabsf(a0))) agree[i][k] = false;
                    }
        }
    }
    if (!have) return;
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 2; k++)
            if (agree[i][k]) {
                out[i * 4 + k * 2] = grad[i * 4 + k * 2];
                out[i * 4 + k * 2 + 1] = grad[i * 4 + k * 2 + 1];
            }
    // GCN_EYELOG: what was fitted, per coordinate the state uses.
    static const bool log = getenv("GCN_EYELOG") != nullptr;
    if (log)
        for (int i = 0; i < st.num_texgens && i < 8; i++)
            fprintf(stderr, "[eye] frame-screen state %u coord %d: s %s %.4f %.4f, t %s %.4f %.4f\n", state, i,
                    agree[i][0] ? "kept" : "dropped", grad[i * 4], grad[i * 4 + 1], agree[i][1] ? "kept" : "dropped",
                    grad[i * 4 + 2], grad[i * 4 + 3]);
}

// Where the eye stands and what it leaves out, for the batch about to be drawn: the chase
// camera with its pitch taken out (as the game's pitch hook says, if it has one), unless
// first person is on and the game's eye hook has somewhere better to put it
// (render_set_eye_hook). `skip` marks the off-screen passes, as
// mark_offscreen_passes leaves it.
static void eye_prepare(const Batch& b, const std::vector<uint8_t>& skip) {
    g_hide.clear();
    g_late.clear();
    g_flat3d.clear();
    g_screen.clear();
    g_frame_grad.clear();
    g_restore.clear();
    g_restore_tex.clear();
    g_facing.clear();
    g_has_facing_box = false;
    g_true_scale.clear();
    g_hud_push.clear();
    if (g_hud_push_below > 0.0f && g_hud_to > 0.0f) {
        // Each HUD-layer state's nearest vertex over the frame, in view space.
        std::vector<float> nearest(b.states.size(), 1e30f);
        bool any = false;
        for (size_t i = 0; i < b.cmds.size(); i++) {
            const Cmd& c = b.cmds[i];
            if (c.type != CmdType::Draw || skip[i]) continue;
            const PixelState& st = b.states[c.state];
            if ((int)st.proj[6] != 0 || st.viewport[5] / 16777215.0f > g_hud_to) continue;
            float& n = nearest[c.state];
            for (uint32_t v = 0; v < c.count; v++) {
                const float d = -b.verts[b.indices[c.first + v]].pos[2];
                if (d > 0.0f && d < n) n = d;
            }
            any = true;
        }
        if (any) {
            g_hud_push.assign(b.states.size(), 1.0f);
            for (size_t s = 0; s < b.states.size(); s++)
                if (nearest[s] < g_hud_push_below) g_hud_push[s] = g_hud_push_to / nearest[s];
        }
    }
    g_eye_overridden = false;
    hud_upright();
    EyeOverride o;
    if (g_fp_on && g_eye_hook && g_eye_hook(b, skip, g_render_frame, o)) {
        memcpy(g_world_xform, o.view_to_eye, sizeof(g_world_xform));
        memcpy(g_hud_xform, o.hud_to_eye, sizeof(g_hud_xform));
        g_hide.swap(o.hide);
        g_eye_overridden = true;
    } else {
        if (g_pitch_hook) g_pitch_hook(b, &g_world_pitch);
        world_pitch_matrix(g_world_xform);
    }
    // The game's draw filter, on top of whatever the eye hook hid.
    if (!g_eye_filter) return;
    std::vector<int8_t> answer;
    std::vector<float> box;
    filter_answers(b, &skip, answer, box);
    std::vector<uint8_t> hidden_mtx;   // EyeHideObject: the position matrices to clear of draws
    std::vector<uint8_t> late_mtx;     // EyeLateObject: those whose draws go last
    for (size_t i = 0; i < b.cmds.size(); i++) {
        const Cmd& c = b.cmds[i];
        if (c.type != CmdType::Draw || skip[i]) continue;
        const int8_t a = answer[c.state];
        if (a == EyeKeep || a < 0) continue;
        if (a == EyeFlat) {
            if (g_flat3d.empty()) g_flat3d.assign(b.states.size(), 0);
            g_flat3d[c.state] = 1;
            continue;
        }
        if (a == EyeScreen || a == EyeFrameScreen) {
            if (g_screen.empty()) g_screen.assign(b.states.size(), 0);
            if (a == EyeFrameScreen && g_screen[c.state] != 2) {
                if (g_frame_grad.empty()) g_frame_grad.assign(b.states.size() * 32, 0.0f);
                fit_uv_gradients(b, c.state, &g_frame_grad[c.state * 32]);
            }
            g_screen[c.state] = a == EyeScreen ? 1 : 2;
            continue;
        }
        if (a == EyeFacing) {
            if (g_facing.empty()) g_facing.assign(b.states.size(), 0);
            g_facing[c.state] = 1;
            const float* r = &box[c.state * 4];
            if (!g_has_facing_box) {
                memcpy(g_facing_box, r, sizeof(g_facing_box));
                g_has_facing_box = true;
            } else {
                g_facing_box[0] = std::min(g_facing_box[0], r[0]);
                g_facing_box[1] = std::max(g_facing_box[1], r[1]);
                g_facing_box[2] = std::min(g_facing_box[2], r[2]);
                g_facing_box[3] = std::max(g_facing_box[3], r[3]);
            }
            continue;
        }
        if (a == EyeTrueScale) {
            if (g_true_scale.empty()) g_true_scale.assign(b.states.size(), 0);
            g_true_scale[c.state] = 1;
            continue;
        }
        if (a == EyeRestore) {
            if (g_restore.empty()) {
                g_restore.assign(b.states.size(), 0);
                g_restore_box.assign(b.states.size() * 4, 0.0f);
            }
            if (!g_restore[c.state]) {
                g_restore[c.state] = 1;
                memcpy(&g_restore_box[c.state * 4], &box[c.state * 4], 4 * sizeof(float));
                const PixelState& st = b.states[c.state];
                for (int m = 0; m < 8; m++)
                    if (st.tex_is_efb[m] && st.tex_id[m]) g_restore_tex.push_back(st.tex_id[m]);
            }
            continue;
        }
        if (a == EyeLateObject) {
            if (late_mtx.size() <= c.mtx) late_mtx.resize(c.mtx + 1, 0);
            late_mtx[c.mtx] = 1;
            continue;
        }
        if (g_hide.empty()) g_hide.assign(b.cmds.size(), 0);
        g_hide[i] = 1;
        if (a == EyeHideObject) {
            if (hidden_mtx.size() <= c.mtx) hidden_mtx.resize(c.mtx + 1, 0);
            hidden_mtx[c.mtx] = 1;
        }
    }
    if (hidden_mtx.empty() && late_mtx.empty()) return;
    for (size_t i = 0; i < b.cmds.size(); i++) {
        const Cmd& c = b.cmds[i];
        if (c.type != CmdType::Draw || skip[i]) continue;
        const bool hidden = c.mtx < hidden_mtx.size() && hidden_mtx[c.mtx];
        const bool late = !hidden && c.mtx < late_mtx.size() && late_mtx[c.mtx];
        if (!hidden && !late) continue;
        if (g_hide.empty()) g_hide.assign(b.cmds.size(), 0);
        g_hide[i] = 1;
        if (!late) continue;
        if (g_late.empty()) g_late.assign(b.cmds.size(), 0);
        g_late[i] = 1;
    }
}

void render_set_vr_morph(float t, const float panel[16]) {
    // Exactly 0 puts every vertex on the panel's plane, where the depth test has nothing
    // to sort by: the sea drew over the racer. Any step off it restores the order, since
    // depth along each line of sight stays monotonic -- 0.002 already matches the flat
    // frame on the device, and puts the scene within a few millimetres of the panel.
    constexpr float kFloor = 0.002f;
    g_morph = t >= 1.0f ? 1.0f : kFloor + (1.0f - kFloor) * (t < 0.0f ? 0.0f : t);
    if (panel) memcpy(g_panel, panel, sizeof(g_panel));
    view_invalidate();
}

// One draw's matrices part way between theater and stereo.
//
// Theater is the game's frame on a panel: a vertex goes through the game's projection P and
// lands on the panel at the frame position that gives -- g_panel * P, the same thing the
// HUD path does with its own frame. Stereo is wherever the eye path would put it. The two
// are blended as homogeneous points, which keeps the whole chain one matrix per draw and
// keeps the GPU's clipping correct all the way through: a vertex behind the game's camera
// still has w < 0 on the panel side and is still cut, which a per-vertex divide would lose.
//
// Blending homogeneous points weights each by its w. The panel side's w is the vertex's
// depth from the game's camera; the stereo side's is 1, so it is scaled by the panel's own
// distance D. The weights then come out as (1-k)*z and k*D, which is exactly interpolation
// in 1/depth: every vertex's disparity grows linearly with k, rather than the far scenery
// sitting on the panel until the last moment and then leaving all at once. When the stereo
// side was itself built from P (the HUD frame), the two share a w and no scaling is wanted.
//
// For the same reason, a vertex slides along a line between two points that the game's
// camera sees in nearly the same direction; from where that camera stands the picture
// barely moves, and only depth comes in. It is not exact, because the panel is not exactly
// the game's field of view and the world is pitched in stereo but not on the panel.
//
// `crop` gives four clip distances that cut the scene to a window of the game's frustum
// widened by s in each axis: s = 1 is the frame the panel shows, and s grows without bound
// as k reaches 1. In stereo the eye sees far more than the game's frustum, and on the panel
// none of that may show -- it would spill round the edges of the frame.
static void morph_chain(const float P[16], MorphKind kind, float chain[16], float fog[16],
                        float crop[16]) {
    const float k = g_morph;
    const float D = -g_panel[14];
    float T[16], S[16], sigma = D;
    mat4_mul(g_panel, P, T);
    memset(fog, 0, 16 * sizeof(float));
    fog[0] = fog[5] = fog[10] = fog[15] = 1.0f;
    if (kind == MorphKind::Screen) {
        // An EyeScreen draw ends across the whole eye, in the eye's clip space, so the two
        // ends are blended there: the panel's chain, whose w is the panel's distance, and
        // the screen's, whose w is 1 and is scaled by that distance to match. It leaves the
        // panel for the eye's edges rather than for the HUD frame, where a film with dark
        // borders showed them as bands across the HUD for the whole transition.
        float VT[16], PVT[16], Ms[16];
        mat4_mul(g_vr_view, T, VT);
        mat4_mul(g_vr_proj, VT, PVT);
        eye_screen_matrix(P, Ms);
        for (int i = 0; i < 16; i++) chain[i] = (1.0f - k) * PVT[i] + k * D * Ms[i];
        memset(crop, 0, 16 * sizeof(float));
        for (int i = 0; i < 4; i++) crop[12 + i] = 1.0f;
        return;
    }
    switch (kind) {
    case MorphKind::World:
        memcpy(S, g_world_xform, sizeof(S));
        for (int i = 0; i < 16; i++) fog[i] = (1.0f - k) * fog[i] + k * g_vr_view_world[i];
        break;
    case MorphKind::CameraHeld:
        memset(S, 0, sizeof(S));
        S[0] = S[5] = S[10] = S[15] = 1.0f;
        for (int i = 0; i < 16; i++) fog[i] = (1.0f - k) * fog[i] + k * g_vr_view[i];
        break;
    case MorphKind::Hud:
        mat4_mul(g_vr_hud_eff, P, S);
        sigma = 1.0f;
        break;
    }
    float M[16], VM[16];
    for (int i = 0; i < 16; i++) M[i] = (1.0f - k) * T[i] + k * sigma * S[i];
    mat4_mul(g_vr_view, M, VM);
    mat4_mul(g_vr_proj, VM, chain);

    memset(crop, 0, 16 * sizeof(float));
    if (kind == MorphKind::Hud) {
        // The HUD maps the game's frame onto its own at both ends; there is nothing outside
        // it to hide.
        for (int i = 0; i < 4; i++) crop[12 + i] = 1.0f;
        return;
    }
    const float s = 1.0f / fmaxf(1.0f - k, 1e-4f);
    // Row r of P is (P[r], P[4+r], P[8+r], P[12+r]); distance i is crop's row i.
    for (int c = 0; c < 4; c++) {
        const float w = s * P[c * 4 + 3], x = P[c * 4 + 0], y = P[c * 4 + 1];
        crop[c * 4 + 0] = w - x;
        crop[c * 4 + 1] = w + x;
        crop[c * 4 + 2] = w - y;
        crop[c * 4 + 3] = w + y;
    }
    // However wide s opens them, planes through the game's camera still hide everything
    // behind its image plane -- and that camera looks down, so with the head turned the
    // sky off to the side is behind it, and stayed black until the morph finished and it
    // popped in. So the planes also back away from the camera, by an amount that is
    // nothing at the start and passes the far end of the world well before the end.
    const float back = D * (s * s - 1.0f);
    for (int i = 0; i < 4; i++) crop[12 + i] += back;
}

// Paints what the panel shows where the game drew nothing: the EFB's clear colour, inside
// the window the crop leaves open and black outside it, as theater is black around its
// panel. A quad well out in the main camera's frustum, morphed like the world -- so it is
// the panel's own rectangle at the start and opens out with the crop. Drawn first, with no
// depth, so the scene simply covers it.
static void draw_morph_background(const float P[16], const float rgb[3]) {
    float chain[16], fog[16], crop[16];
    morph_chain(P, MorphKind::World, chain, fog, crop);
    const float s = 1.0f / fmaxf(1.0f - g_morph, 1e-4f);
    const float z = 4000.0f;   // game units; anywhere inside both frustums does
    glUseProgram(g_bg_prog);
    glUniformMatrix4fv(g_bg_u_mvp, 1, GL_FALSE, chain);
    glUniformMatrix4fv(g_bg_u_crop, 1, GL_FALSE, crop);
    // Half again past the crop on each side, so the crop and not the quad is the edge.
    glUniform4f(g_bg_u_quad, 1.5f * s * z / P[0], 1.5f * s * z / P[5], -z, 0.0f);
    glUniform3f(g_bg_u_col, rgb[0], rgb[1], rgb[2]);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    set_logic_op_off();
    glDisable(GL_CULL_FACE);
    g_gl_cull = 0;
    glColorMask(1, 1, 1, 1);
    glBindVertexArray(g_copy_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(g_vao);
    glDepthMask(GL_TRUE);
}

// The frame the HUD is painted on in stereo: a quad `dist` game units ahead of the game's
// camera, `scale` of the vertical field of view tall and 4:3 wide, with the game's own 2D
// frame mapped onto it corner to corner. It is anchored to the camera rather than to the
// head, so it frames the race while the viewer looks forward and stays where it is when
// they turn to look at something else.
//
// `height` lifts its centre off the forward axis and `pitch_rad` leans the top away, both
// because where a panel wants to hang is a question about a person and not about geometry.
// At 0 and 0 the frame stands vertical and centred on the axis, which is where the eye was
// at the moment the runtime fixed its LOCAL space.
//
// Writing NDC straight into the eye -- what this replaced -- cannot work in stereo. The
// headset's per-eye frustums are asymmetric, so one NDC position is a different direction
// in each eye; there is no depth at which the two images agree, and they never fuse.
void render_hud_frame(float dist, float tan_half_fovy, float scale, float height,
                      float pitch_rad, float out[16]) {
    const float half_h = dist * tan_half_fovy * scale;
    const float half_w = half_h * 4.0f / 3.0f;   // the game's frame is 4:3
    const float c = cosf(pitch_rad), s = sinf(pitch_rad);
    memset(out, 0, 16 * sizeof(float));
    // X: the frame's own right, which the tilt leaves alone.
    out[0] = half_w;
    // Y: its up, leaned back by the pitch. GX clip space and view space both point Y up.
    out[5] = half_h * c;
    out[6] = -half_h * s;
    // W: the centre, `height` up from the forward axis and `dist` down it (-Z forward).
    out[13] = height;
    out[14] = -dist;
    out[15] = 1.0f;
    // The Z column stays zero: every element lands on the plane of the frame. What that
    // costs is the overlay's own depth ordering, which is why the eye path drops the
    // depth test for these draws and lets submission order do the layering.
}

static bool execute_batch(Batch& b, bool do_present, const std::vector<uint8_t>* leave_out = nullptr,
                          bool again = false);

// The size of the frame the game actually scans out, which is the display copy's. It is
// not the EFB's size: the EFB is 640x528 and the game displays 640x480 of it. A frame
// rendered in parts (see Xfb) is as wide as its copies and as tall as the VI's frame;
// its strips and parts are smaller than the frame they belong to.
static void display_size(const Batch& b, uint32_t& w, uint32_t& h) {
    w = EFB_W;
    h = EFB_H;
    bool any = false;
    for (auto& c : b.cmds) {
        if (c.type != CmdType::EfbCopy || !c.copy.to_xfb) continue;
        if (!any || c.copy.dst_h > h) { w = c.copy.dst_w; h = c.copy.dst_h; }
        any = true;
    }
    const uint32_t vh = vi_display_height();
    if (any && vh > h) h = vh;
}

// Where the first part of a frame rendered in parts ends: the first partial display copy
// after a draw of the main scene (`skip` marks the off-screen passes). Each part draws the
// same scene through its own window of the frame, and an eye has no window, so it draws
// the first part's commands and leaves the rest out; the flat pass still runs them all.
// Strips copied before the first part, left over from the previous frame, have no draws in
// front of them. Returns the command count when the frame is not in parts.
static size_t first_part_end(const Batch& b, const std::vector<uint8_t>& skip) {
    const uint32_t vh = vi_display_height();
    bool drew = false;
    for (size_t i = 0; i < b.cmds.size(); i++) {
        const Cmd& c = b.cmds[i];
        if (c.type == CmdType::Draw && !skip[i]) drew = true;
        if (drew && c.type == CmdType::EfbCopy && c.copy.to_xfb && vh && c.copy.dst_h < vh) return i;
    }
    return b.cmds.size();
}

// A copy that takes the whole displayed frame is the game compositing its finished
// image: a post pass reads the EFB out and draws it straight back as a screen-filling
// quad. Smaller copies are real scene content -- the water reflection, the sprite sheet
// the spray uses -- and the eye pass needs them.
//
// A copy of depth is never that, whatever its size: an eye's grab is of colour and cannot
// stand in for it. A shadow map rendered from a light at the screen's size and copied out
// as depth is a render target like any other, and the eyes must sample the flat pass's;
// taken for the frame, its pass was replayed from each eye and the eye's own colour grab
// sampled in its place, which lit every surface it fell on a solid colour.
static bool is_fullscreen_copy(const EfbCopyCmd& c, uint32_t dw, uint32_t dh) {
    return !c.to_xfb && !c.depth && c.dst_w >= dw && c.dst_h >= dh;
}

// Marks the commands an eye must not replay.
//
// An off-screen pass draws into a target and then copies it out -- a reflection, the
// sheet the spray uses. Its draws are in that target's space, so re-aiming them at an
// eye is meaningless, and the first eye has already produced all of them against the
// EFB. Everything else -- the main scene, the composite over it, the HUD -- is replayed.
//
// What marks such a pass is that its copy *clears* the EFB, since the next pass needs it
// empty. A copy that does not clear is a grab: the game lifting a piece of the live
// scene to texture with, which is how the spray is done -- at speed it takes some fifty
// 32x32 and 64x64 rects from scattered screen positions, each preceded by no draws of
// its own. Treating every copy as ending a pass instead throws away whatever draws
// happen to sit in front of the first grab, and the water surface is among them: the
// ocean disappeared the moment the racer was fast enough to throw spray.
//
// Nor does a clearing copy make a pass off-screen when the pass drew outside the rect it
// copies. GX clears only that rect, so a game can lift a piece of the finished scene and
// clear it to draw something of its own there -- a light's glow, say -- with the rest of
// the frame left standing. Taken as an off-screen pass, everything drawn before such a
// copy, the whole world and the arm in front of it, was missing from the eyes while the
// flat frame showed it all. A pass is off-screen only if every draw in it stays inside
// the copied rect (its viewport within its scissor, a pixel either way allowed).
static bool draw_inside_rect(const PixelState& st, const EfbCopyCmd& c) {
    int xoff, yoff;
    scissor_offset(st.bp, xoff, yoff);
    const float vw = 2.0f * fabsf(st.viewport[0]), vh = 2.0f * fabsf(st.viewport[1]);
    const float vx = st.viewport[3] - xoff - 0.5f * vw, vy = st.viewport[4] - yoff - 0.5f * vh;
    const float sx0 = (float)((int)(st.bp[0x20] >> 12 & 0x7FF) - xoff), sy0 = (float)((int)(st.bp[0x20] & 0x7FF) - yoff);
    const float sx1 = (float)((int)(st.bp[0x21] >> 12 & 0x7FF) - xoff + 1), sy1 = (float)((int)(st.bp[0x21] & 0x7FF) - yoff + 1);
    const float x0 = std::max(vx, sx0), y0 = std::max(vy, sy0);
    const float x1 = std::min(vx + vw, sx1), y1 = std::min(vy + vh, sy1);
    if (x1 <= x0 || y1 <= y0) return true;   // draws nothing
    return x0 >= (float)c.src_x - 1.0f && y0 >= (float)c.src_y - 1.0f &&
           x1 <= (float)(c.src_x + c.src_w) + 1.0f && y1 <= (float)(c.src_y + c.src_h) + 1.0f;
}

static void mark_offscreen_passes(const Batch& b, std::vector<uint8_t>& skip) {
    uint32_t dw, dh;
    display_size(b, dw, dh);
    skip.assign(b.cmds.size(), 0);
    size_t pass_start = 0;
    for (size_t i = 0; i < b.cmds.size(); i++) {
        if (b.cmds[i].type != CmdType::EfbCopy) continue;
        const EfbCopyCmd& c = b.cmds[i].copy;
        bool offscreen = !c.to_xfb && c.clear && !is_fullscreen_copy(c, dw, dh);
        for (size_t j = pass_start; offscreen && j < i; j++)
            if (b.cmds[j].type == CmdType::Draw && !draw_inside_rect(b.states[b.cmds[j].state], c)) offscreen = false;
        if (offscreen)
            for (size_t j = pass_start; j <= i; j++) skip[j] = 1;
        pass_start = i + 1;
    }
}

// Texture ids holding a copy of a whole frame.
//
// A draw sampling one is screen-space, so in an eye it is a flat billboard rather than
// something in the world -- it leaves a faint rectangular seam where its edges fall. It
// is tempting to drop those draws, but in this game the water surface is one of them:
// dropping it leaves the seabed showing through bare sand instead of blue-green water,
// which is far worse than the seam. GCN_EYE_SKIPCOMP drops them anyway, for comparing.
static std::vector<uint32_t> g_fullscreen_tex;

// These ids are stable across frames -- an EFB copy keeps the id its destination address
// was registered under -- so the set only ever needs adding to.
// Texture copies of (nearly) the whole frame's width at any size, a downsampled one included:
// they hold the picture as drawn, so in widescreen a 2D draw of one is not squeezed again.
static std::vector<uint32_t> g_wide_copy_tex;
static bool samples_frame_copy(const PixelState& st) {
    for (int i = 0; i < 8; i++)
        if (st.tex_is_efb[i] && st.tex_id[i])
            for (uint32_t t : g_wide_copy_tex) if (t == st.tex_id[i]) return true;
    return false;
}

static void note_fullscreen_copies(const Batch& b) {
    uint32_t dw, dh;
    display_size(b, dw, dh);
    for (auto& c : b.cmds) {
        if (c.type == CmdType::EfbCopy && !c.copy.to_xfb && !c.copy.depth && c.copy.tex_id &&
            c.copy.src_w * 10 >= dw * 9 && c.copy.src_h * 2 >= dh) {
            bool known = false;
            for (uint32_t id : g_wide_copy_tex) known |= (id == c.copy.tex_id);
            if (!known) g_wide_copy_tex.push_back(c.copy.tex_id);
        }
        if (c.type != CmdType::EfbCopy || !is_fullscreen_copy(c.copy, dw, dh) || !c.copy.tex_id)
            continue;
        bool known = false;
        for (uint32_t id : g_fullscreen_tex) known |= (id == c.copy.tex_id);
        if (!known) g_fullscreen_tex.push_back(c.copy.tex_id);
    }
}

static bool is_fullscreen_tex(uint32_t id) {
    for (uint32_t t : g_fullscreen_tex)
        if (t == id) return true;
    return false;
}

static bool samples_fullscreen_copy(const PixelState& st) {
    for (int i = 0; i < 8; i++)
        if (st.tex_is_efb[i] && st.tex_id[i] && is_fullscreen_tex(st.tex_id[i])) return true;
    return false;
}

static bool samples_grab_copy(const PixelState& st) {
    for (int i = 0; i < 8; i++)
        if (st.tex_is_efb[i] && st.tex_id[i] && is_grab_tex(st.tex_id[i])) return true;
    return false;
}

// Which draws the flat pass can leave out when it runs only to feed the eyes.
//
// In stereo the flat pass exists for the render-to-texture copies: the eyes sample them,
// and an eye never draws into the EFB. The picture it paints is never shown. So it need not
// draw past the last copy whose contents something will read -- the main scene after that
// point is drawn by each eye anyway, and here it was a thousand draw calls a frame, a third
// of the render thread, spent on pixels that went straight to the next clear.
//
// What is read: every copy an off-screen pass samples (those draws run only here), and
// every copy an eye samples as copied. An eye takes over the lookups of a whole-frame copy
// and of the small grabs the spray uses, once it has a grab of its own to substitute
// (is_eye_screen_tex), and those need no flat scene behind them. A copy with no reader in
// this frame is kept all the same: a game can copy the scene once and sample it for the
// frames that follow, so an unread copy is not an unneeded one. Only a whole-frame copy or a
// grab the eye would substitute for is taken as unread. GCN_EYE_FULLFLAT=1 draws the whole
// flat pass as before.
//
// `skip` is mark_offscreen_passes' answer; `out` gets one byte per command, 1 to leave out.
// Empty when nothing is to be left out.
static void flat_pass_trim(const Batch& b, const std::vector<uint8_t>& skip, std::vector<uint8_t>& out) {
    out.clear();
    static const bool full = getenv("GCN_EYE_FULLFLAT") != nullptr;
    if (full) return;
    uint32_t dw, dh;
    display_size(b, dw, dh);
    // Copy textures read as copied this frame. A frame has a handful of copies, so a list.
    std::vector<uint32_t> read;
    auto note_read = [&](uint32_t id) {
        for (uint32_t r : read) if (r == id) return;
        read.push_back(id);
    };
    for (size_t i = 0; i < b.cmds.size(); i++) {
        const Cmd& c = b.cmds[i];
        if (c.type != CmdType::Draw) continue;
        const PixelState& st = b.states[c.state];
        // Whether an eye substitutes its grab in this draw: the same test apply_state makes
        // for screen_uv, which then takes over every eye-screen texture the draw binds.
        bool subst = false;
        if (!skip[i]) {
            const uint32_t nstg = ((st.bp[0x00] >> 10) & 15) + 1;
            for (uint32_t s = 0; s < nstg && !subst; s++) {
                const uint32_t order = st.bp[0x28 + s / 2] >> ((s & 1) * 12);
                const uint32_t map = order & 7;
                subst = (order & 0x40) && st.tex_is_efb[map] && st.tex_id[map] &&
                        is_eye_screen_tex(st.tex_id[map]);
            }
        }
        for (int m = 0; m < 8; m++) {
            if (!st.tex_is_efb[m] || !st.tex_id[m]) continue;
            if (subst && is_eye_screen_tex(st.tex_id[m])) continue;
            note_read(st.tex_id[m]);
        }
    }
    size_t last = 0;
    bool any = false;
    for (size_t i = 0; i < b.cmds.size(); i++) {
        const Cmd& c = b.cmds[i];
        if (c.type != CmdType::EfbCopy || c.copy.to_xfb) continue;
        const EfbCopyCmd& cc = c.copy;
        bool needed = true;
        if (cc.tex_id && !cc.depth &&
            ((g_eye_grab.tex && is_fullscreen_copy(cc, dw, dh)) || (g_spray_grab.tex && is_grab_copy(cc)))) {
            needed = false;
            for (uint32_t r : read) needed |= r == cc.tex_id;
        }
        if (needed) { last = i; any = true; }
    }
    const size_t from = any ? last + 1 : 0;
    bool trimmed = false;
    for (size_t i = from; i < b.cmds.size(); i++)
        if (b.cmds[i].type == CmdType::Draw && !skip[i]) trimmed = true;
    if (!trimmed) return;
    out.assign(b.cmds.size(), 0);
    for (size_t i = from; i < b.cmds.size(); i++)
        if (b.cmds[i].type == CmdType::Draw && !skip[i]) out[i] = 1;
}

// Draw one eye's view of a batch into `fbo`.
//
// The vertex buffer, the CPU-side transform in xf.cpp and any render-to-texture results
// are all shared between the eyes -- only the uniforms and the draw calls are repeated,
// which is what makes stereo affordable here. Pass do_copies for the first eye only;
// the second reuses what it produced.
// A late object (EyeLateObject) the game draws where its window covers the screen: on the
// screen the window magnifies it, and it reads as part of the window. In an eye the window
// is a panel near the viewer (EyeFacing, on the HUD frame) and the object stands in the
// world behind it, metres further off; seen through the panel's outline, at the panel's
// place on the screen but far behind it, it is hard on the eyes. So an object whose origin
// the game projects inside the facing panels' box is scaled about the viewer onto the
// panel's plane: the same direction and the same angular size, at the panel's depth.
// Returns false, leaving `out` alone, for an object outside the box, one nearer than the
// panel, or a frame with no facing panel.
static bool late_panel_view(const Batch& b, const Cmd& c, float out[16]) {
    const PixelState& st = b.states[c.state];
    if (!g_has_facing_box || (int)st.proj[6] != 0) return false;
    // The object's origin in the game's view space: its position matrix's translation,
    // or, for one drawn with the identity, the middle of its vertices.
    float p[3] = {0, 0, 0};
    if ((size_t)c.mtx * 12 + 12 <= b.mtxs.size()) {
        const float* m = &b.mtxs[(size_t)c.mtx * 12];
        p[0] = m[3]; p[1] = m[7]; p[2] = m[11];
    }
    if (p[0] * p[0] + p[1] * p[1] + p[2] * p[2] < 1e-6f && c.vcount) {
        for (uint32_t v = 0; v < c.vcount; v++)
            for (int k = 0; k < 3; k++) p[k] += b.verts[c.vfirst + v].pos[k];
        for (int k = 0; k < 3; k++) p[k] /= (float)c.vcount;
    }
    if (p[2] >= -1e-3f) return false;
    // Where the game's perspective puts it: x' = p0*x + p1*z, y' = p2*y + p3*z, w = -z.
    const float x = (st.proj[0] * p[0] + st.proj[1] * p[2]) / -p[2];
    const float y = (st.proj[2] * p[1] + st.proj[3] * p[2]) / -p[2];
    const float* r = g_facing_box;
    if (x < r[0] || x > r[1] || y < r[2] || y > r[3]) return false;
    // The panel's plane, as facing_frame places it: square on to the direction of the
    // box's centre on the HUD frame, as far out as the frame's centre.
    const float* F = g_vr_hud_eff;
    const float cx = 0.5f * (r[0] + r[1]), cy = 0.5f * (r[2] + r[3]);
    float n[3] = {F[0] * cx + F[4] * cy + F[12], F[1] * cx + F[5] * cy + F[13], F[2] * cx + F[6] * cy + F[14]};
    const float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    const float d = sqrtf(F[12] * F[12] + F[13] * F[13] + F[14] * F[14]);
    if (nl <= 0.0f || d <= 0.0f) return false;
    for (float& v : n) v /= nl;
    // The origin where the eyes see it (g_world_xform takes the game's view space there),
    // and the scale about the viewer that puts it on the plane.
    const float* W = g_world_xform;
    float q[3];
    for (int k = 0; k < 3; k++) q[k] = W[k] * p[0] + W[4 + k] * p[1] + W[8 + k] * p[2] + W[12 + k];
    const float along = n[0] * q[0] + n[1] * q[1] + n[2] * q[2];
    if (along <= d) return false;
    const float s = d / along;
    const float S[16] = {s, 0, 0, 0, 0, s, 0, 0, 0, 0, s, 0, 0, 0, 0, 1};
    float SW[16];
    mat4_mul(S, g_world_xform, SW);
    mat4_mul(g_vr_view, SW, out);
    return true;
}

bool render_execute_eye(Batch& b, unsigned fbo, int w, int h, bool do_copies) {
    // The scene samples textures the game produces by copying them back out of the EFB:
    // the water reflection, the sprite sheet the spray uses. An eye pass never draws into
    // the EFB, so on its own it would copy out an empty one -- which once left a model
    // untextured and put a black quad on the water. So the first eye runs the whole frame
    // flat into the EFB exactly as the hardware would, minus the scanout, and the eyes
    // then re-project only the main scene on top of correct textures.
    static std::vector<uint8_t> skip, trim;
    apply_view_turn(b);
    mark_offscreen_passes(b, skip);
    const auto t_start = std::chrono::steady_clock::now();
    double ms_flat = 0.0;
    size_t n_trimmed = 0;
    if (do_copies) {
        frame_observe(b, &skip);
        // Where the eye stands and what it leaves out are read off the batch, so they
        // are settled before anything is drawn from it -- the flat pass included, which
        // leaves the hidden draws out of the EFB too.
        eye_prepare(b, skip);
        compose_world_view();
        g_vr_active = false;
        flat_pass_trim(b, skip, trim);
        for (uint8_t t : trim) n_trimmed += t;
        execute_batch(b, false, trim.empty() ? nullptr : &trim);  // which also notes this batch's whole-frame copies
        if (frametime_on()) ms_flat = ms_since(t_start);
    }
    // GCN_EYELOG=1 reports how a frame was split, which is the only way to tell a scene
    // rendered at the wrong field of view from a composite quad standing in for one.
    static const bool eyelog = getenv("GCN_EYELOG") != nullptr;
    int n_drawn = 0, n_skipped = 0, n_spray = 0;
    if (eyelog && do_copies) {
        uint32_t dw, dh;
        display_size(b, dw, dh);
        fprintf(stderr, "[eye] f%u cmds=%zu display=%ux%u\n", g_render_frame, b.cmds.size(),
                dw, dh);
        size_t pass_start = 0;
        for (size_t i = 0; i < b.cmds.size(); i++) {
            if (b.cmds[i].type != CmdType::EfbCopy) continue;
            int nd = 0;
            for (size_t j = pass_start; j < i; j++) nd += b.cmds[j].type == CmdType::Draw;
            const EfbCopyCmd& cc = b.cmds[i].copy;
            fprintf(stderr, "[eye]   copy %ux%u xfb=%d tex=%u fmt=%02X%s full=%d draws=%d clr=%d%d src=%u,%u+%ux%u %s\n",
                    cc.dst_w, cc.dst_h, (int)cc.to_xfb, cc.tex_id, cc.format, cc.depth ? " depth" : "",
                    (int)is_fullscreen_copy(cc, dw, dh), nd, (int)cc.clear,
                    (int)cc.clear_color, cc.src_x, cc.src_y, cc.src_w, cc.src_h,
                    skip[i] ? "SKIP" : "replay");
            pass_start = i + 1;
        }
    }

    g_vr_active = true;
    g_eye_w = w;
    g_eye_h = h;
    memset(g_zplane, 0, sizeof(g_zplane));  // the flat pass's planes are the EFB's
    view_invalidate();   // this eye's matrices and target
    bool grabbed = false, grabbed_spray = false, grabbed_restore = false;
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(1, 1, 1, 1);
    glDepthMask(GL_TRUE);
    // The eye's background has to be the EFB's. Where the scene draws nothing behind the
    // water -- open sea, past where the course gives the sea a bottom -- the water
    // refracts whatever the buffer was cleared to, and an eye cleared to black turned
    // that into a black sea in stereo while the flat view, cleared to the game's own
    // colour, showed open water. So take the clear the game set for the main scene: the
    // last copy to clear the colour buffer ahead of the first draw an eye replays.
    float bg[3] = {0, 0, 0};
    for (size_t i = 0; i < b.cmds.size(); i++) {
        if (b.cmds[i].type == CmdType::Draw) {
            if (!skip[i]) break;
            continue;
        }
        if (b.cmds[i].type != CmdType::EfbCopy) continue;
        const EfbCopyCmd& c = b.cmds[i].copy;
        if (c.clear && c.clear_color) {
            bg[0] = ((c.clear_rgba >> 24) & 0xFF) / 255.0f;
            bg[1] = ((c.clear_rgba >> 16) & 0xFF) / 255.0f;
            bg[2] = ((c.clear_rgba >> 8) & 0xFF) / 255.0f;
        }
    }
    if (eyelog && do_copies)
        fprintf(stderr, "[eye]   background %.3f %.3f %.3f\n", bg[0], bg[1], bg[2]);
    // Mid-morph, theater's black surround comes up to the scene's clear colour as the
    // window opens, so the edge of the background quad is gone by the time it would show.
    const bool morph = g_morph < 1.0f;
    const float surround = morph ? g_morph * g_morph : 1.0f;
    glClearColor(bg[0] * surround, bg[1] * surround, bg[2] * surround, 1);
    clear_depth(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    // GL_CLIP_DISTANCE0, which ES knows only by its extension's name.
    constexpr GLenum kClipDistance0 = 0x3000;
    if (morph) {
        // The main camera's projection, from the first world draw an eye replays: the
        // background is a piece of its frustum. A frame with no world in it -- a menu --
        // gets a stand-in the shape of the panel, since only where it lands matters.
        float P[16] = {0};
        P[0] = 2.5f / 1.6f; P[5] = 2.5f / 1.2f; P[10] = -1.0f; P[11] = -1.0f; P[14] = -1.0f;
        for (size_t i = 0; i < b.cmds.size(); i++) {
            if (b.cmds[i].type != CmdType::Draw || skip[i]) continue;
            const PixelState& st = b.states[b.cmds[i].state];
            const float* p = st.proj;
            if ((int)p[6] != 0 || st.view_space) continue;
            memset(P, 0, sizeof(P));
            P[0] = p[0]; P[8] = p[1]; P[5] = p[2]; P[9] = p[3]; P[10] = p[4]; P[14] = p[5];
            P[11] = -1.0f;
            break;
        }
        if (g_clip_ok)
            for (GLenum i = 0; i < 4; i++) glEnable(kClipDistance0 + i);
        draw_morph_background(P, bg);
    }
    // The same four planes keep a 3D draw composed into the HUD (EyeFlat) inside its
    // viewport; every other draw outside the morph writes distances of 1.
    const bool clip4 = g_clip_ok && (morph || !g_flat3d.empty());
    if (clip4 && !morph)
        for (GLenum i = 0; i < 4; i++) glEnable(kClipDistance0 + i);

    uint32_t cur_state = UINT32_MAX;
    gl_state_invalidate();
    int cur_prim = -1;
    static int eye_skip_lo = -1, eye_skip_hi = -1;
    static bool eye_skip_parsed = false;
    if (!eye_skip_parsed) {
        eye_skip_parsed = true;
        if (const char* s = getenv("GCN_DRAW_SKIP")) {
            eye_skip_lo = atoi(s);
            const char* dash = strchr(s, '-');
            eye_skip_hi = dash ? atoi(dash + 1) : eye_skip_lo;
        }
    }
    static const uint32_t eye_drawlog_frame = getenv("GCN_DRAWLOG") ? (uint32_t)atoi(getenv("GCN_DRAWLOG")) : 0;
    // Keyed on the presented frame, the number the dumps carry; a frame may take several batches.
    const bool eye_drawlog = eye_drawlog_frame && g_present_count + 1 == eye_drawlog_frame;
    int eye_draw_index = 0;   // the flat draw log's numbering: every draw command, in order
    bool flat3d_cleared = false;
    const size_t eye_end = first_part_end(b, skip);
    uint32_t disp_w, disp_h;
    display_size(b, disp_w, disp_h);
    for (size_t i = 0; i < eye_end; i++) {
        Cmd& c = b.cmds[i];
        // Copies and the present are the first eye's business, done against the EFB. A
        // copy that an EyeRestore draw puts back is taken from the eye as well, at the
        // same moment: what the eye has drawn so far is what the game's copy holds.
        if (c.type != CmdType::Draw) {
            if (c.type == CmdType::EfbCopy && !morph && c.copy.tex_id &&
                std::find(g_restore_tex.begin(), g_restore_tex.end(), c.copy.tex_id) != g_restore_tex.end())
            {
                grab_eye(g_restore_grab, w, h);
                grabbed_restore = true;
            }
            // A second copy of the whole frame in one frame is a later state of it: a
            // pass that copies the frame, draws over it, and copies it again to post-
            // process what it drew (a false-colour filter, then the hot things over it
            // and a second filter on those). The first grab is of the frame before the
            // second pass drew anything, so the next draw that samples the copy grabs
            // again.
            if (c.type == CmdType::EfbCopy && grabbed && c.copy.tex_id && is_fullscreen_copy(c.copy, disp_w, disp_h))
                grabbed = false;
            cur_state = UINT32_MAX;
            gl_state_invalidate();
            continue;
        }
        // Mid-morph the foreground's depth is the eye's, not the game's (u_vr 3), so the
        // depth a cut-away is drawn at means nothing against it: it is left out, and what it
        // would have cut away stays for the length of the morph.
        const bool restore_draw = !g_restore.empty() && g_restore[c.state];
        if (restore_draw && (morph || !grabbed_restore)) { eye_draw_index++; continue; }
        if (skip[i]) { n_skipped++; eye_draw_index++; continue; }
        if (!g_hide.empty() && g_hide[i]) { eye_draw_index++; continue; }
        // GCN_EYE_SKIPCOMP drops the screen-space passes entirely, for comparing against
        // drawing them flat across the eye. Dropping the water one leaves bare seabed.
        static const bool skipcomp = getenv("GCN_EYE_SKIPCOMP") != nullptr;
        if (skipcomp && samples_fullscreen_copy(b.states[c.state])) { n_skipped++; continue; }
        // The first draw that wants the finished frame is the moment to take it: the
        // scene behind the water is in the target by now and the water is not yet.
        if (!grabbed && samples_fullscreen_copy(b.states[c.state])) {
            grabbed = true;
            grab_eye(g_eye_grab, w, h);
            cur_state = UINT32_MAX;   // the grab left its own texture bound
            gl_state_invalidate();
        }
        // The spray composites itself over the finished scene, the water included, so the
        // grab above -- taken deliberately before the water, which is what the water
        // itself needs -- is a layer short by the time the droplets draw. Re-take it at
        // the first of them, once, and every droplet in the frame then refracts the scene
        // as it actually stands. Costs a second full-target copy per eye on any frame
        // that throws spray, which at speed is every frame.
        if (samples_grab_copy(b.states[c.state])) {
            // How a droplet actually addresses its grab, which decides what the
            // substitution has to carry over: the indirect matrix id per stage (1..3 is
            // a static offset in texels, 5..11 scales the coordinate itself) and the
            // wrap, which folds the coordinate into the copy's own size.
            if (eyelog && do_copies && !grabbed_spray) {
                const PixelState& st = b.states[c.state];
                const uint32_t nstg = ((st.bp[0x00] >> 10) & 15) + 1;
                fprintf(stderr, "[eye]   spray draw: stages=%u nind=%u", nstg,
                        (st.bp[0x00] >> 16) & 7);
                for (uint32_t t = 0; t < nstg; t++) {
                    const uint32_t ic = st.bp[0x10 + t];
                    fprintf(stderr, " s%u[mid=%u sw=%u tw=%u bt=%u]", t, (ic >> 9) & 15,
                            (ic >> 13) & 7, (ic >> 16) & 7, ic & 3);
                }
                fprintf(stderr, "\n");
            }
            if (!grabbed_spray) {
                grabbed_spray = true;
                grab_eye(g_spray_grab, w, h);
                cur_state = UINT32_MAX;
                gl_state_invalidate();
            }
            n_spray++;
        }
        n_drawn++;
        // GCN_DRAW_SKIP=a-b leaves those draws out of the eye too, by the index the flat
        // pass's draw log gives them, and GCN_DRAWLOG=<frame> says which layer the eye
        // gave each draw (see eye_layer_name), beside its band: how a draw that lands
        // somewhere surprising in an eye is told from one the game put there.
        if (eye_skip_lo >= 0 && eye_draw_index >= eye_skip_lo && eye_draw_index <= eye_skip_hi) { eye_draw_index++; continue; }
        // The z freeze plane as this eye sees it (zfreeze_plane_eye).
        const bool zfreeze = (b.states[c.state].bp[0x00] >> 19) & 1;
        if (!zfreeze) zfreeze_plane_eye(b, c, g_zplane);
        if (c.state != cur_state || c.prim != cur_prim) {
            g_cur_flat3d = !g_flat3d.empty() && g_flat3d[c.state];
            // The 3D draws composed into the HUD sort among themselves by the game's
            // depth, and the world's depth in this eye means nothing to them: they are
            // on the HUD frame, in front of everything. The first of them clears it.
            if (g_cur_flat3d && !flat3d_cleared) {
                flat3d_cleared = true;
                glDepthMask(GL_TRUE);
                glClear(GL_DEPTH_BUFFER_BIT);
                gl_state_invalidate();
            }
            g_cur_screen = g_screen.empty() ? 0 : g_screen[c.state];
            g_cur_uvgrad = g_cur_screen == 2 && !g_frame_grad.empty() ? &g_frame_grad[c.state * 32] : nullptr;
            g_cur_facing = !g_facing.empty() && g_facing[c.state];
            g_cur_true_scale = !g_true_scale.empty() && g_true_scale[c.state];
            g_cur_hud_push = g_hud_push.empty() ? 1.0f : g_hud_push[c.state];
            g_cur_restore = restore_draw;
            g_cur_restore_box = restore_draw ? &g_restore_box[c.state * 4] : nullptr;
            apply_state(b.states[c.state], c.prim);
            g_cur_flat3d = false;
            g_cur_screen = 0;
            g_cur_uvgrad = nullptr;
            g_cur_facing = false;
            g_cur_true_scale = false;
            g_cur_hud_push = 1.0f;
            g_cur_restore = false;
            g_cur_restore_box = nullptr;
            // apply_state binds the EFB's scissor and viewport expectations; the eye
            // target overrides both.
            glViewport(0, 0, w, h);
            glDisable(GL_SCISSOR_TEST);
            cur_state = c.state;
            cur_prim = c.prim;
        } else if (zfreeze && g_applied.pr && g_applied.pr->u_zplane >= 0) {
            glUniform4fv(g_applied.pr->u_zplane, 1, g_zplane);
        }
        if (eye_drawlog && do_copies) {
            const PixelState& st = b.states[c.state];
            fprintf(stderr, "[eyedraw] %d st=%u %s band=%.5f..%.5f view_space=%d\n", eye_draw_index, c.state, g_eye_layer_name,
                    (st.viewport[5] - fabsf(st.viewport[2])) / 16777215.0f, st.viewport[5] / 16777215.0f, (int)st.view_space);
        }
        eye_draw_index++;
        draw_cmd(b, c);
    }
    // The objects the game asked for last (EyeLateObject), over everything the eye drew,
    // with their own depth state against the depth the eye has written: a marker in the
    // world stays hidden behind what is in front of it there, and shows over the 2D
    // window the game painted on top of where it stands. The flat pass left them out, so
    // a copy the window magnifies does not hold them a second time. One inside the window
    // is drawn on the window's plane (late_panel_view).
    float late_view[16], cur_late_view[16];
    bool cur_at_panel = false;
    for (size_t i = 0; i < eye_end && !g_late.empty(); i++) {
        if (!g_late[i] || skip[i]) continue;
        Cmd& c = b.cmds[i];
        const bool zfreeze = (b.states[c.state].bp[0x00] >> 19) & 1;
        if (!zfreeze) zfreeze_plane_eye(b, c, g_zplane);
        const bool at_panel = late_panel_view(b, c, late_view);
        const bool view_changed = at_panel != cur_at_panel ||
                                  (at_panel && memcmp(late_view, cur_late_view, sizeof(late_view)) != 0);
        if (view_changed) {
            view_invalidate();
            cur_at_panel = at_panel;
            if (at_panel) memcpy(cur_late_view, late_view, sizeof(late_view));
        }
        if (view_changed || c.state != cur_state || c.prim != cur_prim) {
            g_cur_hud_push = g_hud_push.empty() ? 1.0f : g_hud_push[c.state];
            // Otherwise at its modelled distance, as EyeTrueScale: a late object marks
            // something in the world, and a scaled layer's band (one a reticle round the
            // marked thing is drawn in, to be drawn over everything) would take it off what
            // it marks. The band's depth is kept for the depth test.
            g_cur_true_scale = true;
            g_cur_late_view = at_panel ? cur_late_view : nullptr;
            apply_state(b.states[c.state], c.prim);
            g_cur_hud_push = 1.0f;
            g_cur_true_scale = false;
            g_cur_late_view = nullptr;
            glViewport(0, 0, w, h);
            glDisable(GL_SCISSOR_TEST);
            cur_state = c.state;
            cur_prim = c.prim;
        } else if (zfreeze && g_applied.pr && g_applied.pr->u_zplane >= 0) {
            glUniform4fv(g_applied.pr->u_zplane, 1, g_zplane);
        }
        if (eye_drawlog && do_copies) fprintf(stderr, "[eyedraw] late cmd %zu st=%u %s\n", i, c.state, g_eye_layer_name);
        n_drawn++;
        draw_cmd(b, c);
    }
    if (cur_at_panel) view_invalidate();  // nothing after keeps the panel's view
    if (eyelog && do_copies)
        fprintf(stderr, "[eye]   drawn=%d skipped_composite=%d spray=%d flat_trimmed=%zu\n", n_drawn,
                n_skipped, n_spray, n_trimmed);
    // The renderer's half of GCN_FRAMETIME for an eye: the flat pass (first eye only) and
    // this eye's own draws, CPU time.
    if (frametime_on())
        fprintf(stderr, "[eye-rt] f%u %s flat %6.2fms eye %6.2fms  draws %d\n", g_render_frame,
                do_copies ? "first" : "second", ms_flat, ms_since(t_start) - ms_flat, n_drawn);
    apply_stats_flush(do_copies ? "eye0" : "eye1", g_render_frame);
    if (clip4)
        for (GLenum i = 0; i < 4; i++) glDisable(kClipDistance0 + i);
#ifdef GCN_GL_ES
    // The eye's depth buffer is finished with: nothing reads it, and the next pass clears
    // it. On a tiled GPU, which every standalone headset has, a depth attachment left
    // valid at the end of a pass is written out of tile memory to RAM tile by tile, and
    // at the eye's size with multisampling that is a store of several tens of megabytes
    // per eye per frame, for nothing. Saying so lets the driver drop it. Desktop GL 3.3
    // has no glInvalidateFramebuffer, and an immediate-mode GPU would gain nothing.
    // GCN_EYE_KEEPDEPTH=1 leaves it in, for comparing.
    static const bool keep_depth = getenv("GCN_EYE_KEEPDEPTH") != nullptr;
    if (!keep_depth) {
        const GLenum attachment = GL_DEPTH_ATTACHMENT;
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &attachment);
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_vr_active = false;
    // An eye frame presents nothing through the EFB, so the presented count -- which the
    // draw log, the copy dump and the stereo logs key on -- is stepped here instead, once
    // per frame: the frontends number their eye dumps the same way, so GCN_DRAWLOG=<n>
    // names the frame eye_<n>.png shows.
    if (do_copies) g_present_count++;
    return true;
}

// Marks a draw's textures as in use without drawing it. Eviction goes by what was bound,
// and the guest-side cache, which is what would send a texture again, goes by what the
// game referenced; a draw left out on purpose is the one case where those differ.
static void touch_textures(const PixelState& st) {
    for (int m = 0; m < 8; m++) {
        if (!st.tex_id[m]) continue;
        auto it = g_textures.find(st.tex_id[m]);
        if (it != g_textures.end()) it->second.last_used = g_render_frame;
    }
}

// Release GL textures the game has stopped using: decoded textures and EFB copies alike go
// when the front end says (Batch::dead_textures), a batch late, so that nothing still
// drawing from one loses it: the ids `b` names are deleted at the next batch. Copies were
// once aged here too, on the renderer's own clock, a little longer than the front end's;
// the front end now keeps a copy for as long as the RAM at its destination is unchanged
// (texture_evict), which no clock here can know.
static std::vector<uint32_t> g_dead_pending;
static void evict_textures(const Batch& b) {
    // EFB copies included: the front end lists a copy's id when it lets the copy go, and a
    // copy aged here on the renderer's own clock could go while the game still used it.
    for (uint32_t id : g_dead_pending) {
        auto it = g_textures.find(id);
        if (it != g_textures.end()) {
            glDeleteTextures(1, &it->second.tex);
            g_textures.erase(it);
        }
    }
    g_dead_pending = b.dead_textures;
}

// A batch's new textures. Where the GL is ES, all their texels go into one pixel unpack
// buffer first and each level is uploaded from there: a new room brings a few dozen
// textures at once, and on an Adreno 740 the uploads from client memory took 400-460 ms
// for the 47 and 87 of two rooms (5.7 and 12.4 MB), against 255-310 ms this way. Most of
// what was left was not the copying but the driver's allocations slowed by 23,673
// resident programs (a level's upload took 0.01 ms or whole multiples of about 0.76 ms);
// with the keys canonical (canonicalize_shader_key), 2,193, the two took 30-60 ms.
static void upload_textures(const Batch& b) {
    if (b.new_textures.empty()) return;
#ifdef GCN_GL_ES
    static GLuint pbo = 0;
    size_t total = 0;
    for (auto& t : b.new_textures)
        for (auto& l : t->levels) total += l.size() * 4;
    if (!pbo) glGenBuffers(1, &pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, (GLsizeiptr)total, nullptr, GL_STREAM_DRAW);
    if (uint8_t* m = (uint8_t*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, (GLsizeiptr)total,
                                                GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT)) {
        for (auto& t : b.new_textures)
            for (auto& l : t->levels) {
                memcpy(m, l.data(), l.size() * 4);
                m += l.size() * 4;
            }
        if (glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER)) {
            size_t off = 0;
            for (auto& t : b.new_textures) {
                upload_texture(*t, true, off);
                for (auto& l : t->levels) off += l.size() * 4;
            }
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            return;
        }
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
#endif
    for (auto& t : b.new_textures) upload_texture(*t);
}

// Runs a batch into the EFB the way the hardware would. With do_present false the final
// scanout is skipped but everything else -- including every render-to-texture copy -- still
// happens, which is how the stereo path obtains the textures its eye passes sample.
// `again` draws a batch this function has just drawn: the textures and vertices are
// already up, and it is the same frame to the texture cache.
static bool execute_batch(Batch& b, bool do_present, const std::vector<uint8_t>* leave_out, bool again) {
    const auto t_start = std::chrono::steady_clock::now();
    double ms_tex = 0.0, ms_vbo = 0.0, ms_copy = 0.0;
    if (!again) {
        g_render_frame++;
        const size_t n_dead = g_dead_pending.size();
        evict_textures(b);
        const double ms_evict = frametime_on() ? ms_since(t_start) : 0.0;
        upload_textures(b);
        ms_tex = frametime_on() ? ms_since(t_start) : 0.0;
        if (frametime_on() && ms_tex > 5.0) {
            size_t bytes = 0;
            for (auto& t : b.new_textures)
                for (auto& l : t->levels) bytes += l.size() * 4;
            fprintf(stderr, "[rt-tex] f%u deleted %zu in %.2f ms, uploaded %zu (%zu KB) in %.2f ms\n", g_render_frame,
                    n_dead, ms_evict, b.new_textures.size(), bytes / 1024, ms_tex - ms_evict);
        }
        // A set the GPU has finished with; see g_vaos. The eye passes after this draw from
        // the same one, so a batch is still uploaded once however many views it is drawn
        // into.
        g_vertex_set = (g_vertex_set + 1) % kVertexRing;
        g_vao = g_vaos[g_vertex_set][0];
        g_vbo = g_vbos[g_vertex_set];
        g_ebo = g_ebos[g_vertex_set];
        glBindVertexArray(g_vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferData(GL_ARRAY_BUFFER, b.packed.size(), b.packed.data(), GL_STREAM_DRAW);
        upload_indices(b);
        ms_vbo = frametime_on() ? ms_since(t_start) - ms_tex : 0.0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_efb_fbo);
    glViewport(0, 0, EFB_W * g_scale, EFB_H * g_scale);
    glBindVertexArray(g_vao);
    uint32_t n_apply = 0;
    note_fullscreen_copies(b);
    static const bool batchlog = getenv("GCN_EYELOG") != nullptr;
    if (batchlog) {
        int nd = 0, nc = 0, np = 0;
        for (auto& c : b.cmds) {
            nd += c.type == CmdType::Draw;
            nc += c.type == CmdType::EfbCopy;
            np += c.type == CmdType::Present;
        }
        fprintf(stderr, "[batch] f%u cmds=%zu draws=%d copies=%d present=%d verts=%zu indices=%zu\n",
                g_render_frame, b.cmds.size(), nd, nc, np, b.verts.size(), b.indices.size());
    }
    bool presented = false;
    uint32_t cur_state = UINT32_MAX;
    gl_state_invalidate();
    view_invalidate();   // the flat view, after an eye
    memset(g_zplane, 0, sizeof(g_zplane));  // none yet this frame
    int cur_prim = -1;
    // GCN_NO_COMP drops draws that sample a copy of the whole frame, in the flat path too.
    // The duplicate racer on the water is visible without any of the VR code, so this is
    // how to tell whether that draw is responsible for it.
    static const bool no_comp = getenv("GCN_NO_COMP") != nullptr;
    static const bool only_comp = getenv("GCN_ONLY_COMP") != nullptr;
    static const bool complog = getenv("GCN_COMPLOG") != nullptr;
    int draw_index = 0;
    // GCN_DRAWLOG_COPY=WxH logs, like GCN_DRAWLOG, every batch holding a copy of that size:
    // a render to texture done once, at a frame that differs from run to run, is logged
    // without knowing the frame.
    static uint32_t dlc_w = 0, dlc_h = 0;
    static const bool dlc_parsed = [] {
        if (const char* e = getenv("GCN_DRAWLOG_COPY")) sscanf(e, "%ux%u", &dlc_w, &dlc_h);
        return true;
    }();
    (void)dlc_parsed;
    bool log_batch = false;
    if (dlc_w)
        for (auto& c : b.cmds)
            if (c.type == CmdType::EfbCopy && !c.copy.to_xfb && c.copy.dst_w == dlc_w && c.copy.dst_h == dlc_h) log_batch = true;
    if (log_batch) fprintf(stderr, "[drawlog] batch with a %ux%u copy, presented frame %u\n", dlc_w, dlc_h, g_present_count + 1);
    static const uint32_t drawlog_frame = getenv("GCN_DRAWLOG") ? (uint32_t)atoi(getenv("GCN_DRAWLOG")) : 0;
    // Copies are listed between the draws in either form of the log.
    const bool log_copies = log_batch || (drawlog_frame && g_present_count + 1 == drawlog_frame);
    for (size_t ci = 0; ci < b.cmds.size(); ci++) {
        Cmd& c = b.cmds[ci];
        switch (c.type) {
        case CmdType::Draw: {
            if (!g_hide.empty() && g_hide[ci]) {
                // Not drawn, but still the game's: its textures must not age out. The game
                // goes on referencing them, so it never sends them again, and a hidden
                // model came back from first person with parts of it untextured.
                touch_textures(b.states[c.state]);
                break;
            }
            // A draw the eyes will make themselves, whose only product here would be a
            // picture nobody sees; see flat_pass_trim.
            if (leave_out && (*leave_out)[ci]) break;
            const bool comp = (no_comp || complog || only_comp) &&
                              samples_fullscreen_copy(b.states[c.state]);
            if (only_comp && !comp) break;
            // GCN_DRAWLOG=<frame> lists every draw in one frame with its index, so a
            // specific piece of geometry can be found and then skipped by index.
            static const uint32_t drawlog = getenv("GCN_DRAWLOG") ? atoi(getenv("GCN_DRAWLOG")) : 0;
            // GCN_DRAW_SKIP=a-b drops a range of draw indices, to attribute a piece of the
            // image to the draws that made it.
            static int skip_lo = -1, skip_hi = -1;
            static bool skip_parsed = false;
            if (!skip_parsed) {
                skip_parsed = true;
                if (const char* s = getenv("GCN_DRAW_SKIP")) {
                    skip_lo = atoi(s);
                    const char* dash = strchr(s, '-');
                    skip_hi = dash ? atoi(dash + 1) : skip_lo;
                }
            }
            if (log_batch || (drawlog && g_present_count + 1 == drawlog)) {
                const PixelState& st = b.states[c.state];
                // The projection type and the view-space depth are what decide a draw's
                // fate in an eye: a perspective batch is re-projected as world geometry,
                // anything else goes on the HUD frame. And a draw a few tens of units
                // from the camera is in front of the viewer's face either way.
                float zlo = 1e30f, zhi = -1e30f, xlo = 1e30f, xhi = -1e30f, ylo = 1e30f, yhi = -1e30f;
                for (uint32_t v = 0; v < c.count; v++) {
                    const float* q = b.verts[b.indices[c.first + v]].pos;
                    zlo = std::min(zlo, q[2]); zhi = std::max(zhi, q[2]);
                    xlo = std::min(xlo, q[0]); xhi = std::max(xhi, q[0]);
                    ylo = std::min(ylo, q[1]); yhi = std::max(yhi, q[1]);
                }
                // Vertices within a unit of the view-space origin sit on top of the camera,
                // and a polygon that uses one is drawn as a sliver radiating from the
                // screen's centre; counted so such draws can be found.
                uint32_t at_origin = 0;
                for (uint32_t v = 0; v < c.count; v++) {
                    const float* p = b.verts[b.indices[c.first + v]].pos;
                    if (fabsf(p[0]) < 1.0f && fabsf(p[1]) < 1.0f && fabsf(p[2]) < 1.0f) at_origin++;
                }
                // Sliver triangles: longer than 40 units in view space with a height under
                // a thousandth of their length. Geometry that is right never looks like
                // that; a vertex that is wrong makes exactly that, drawn as a hairline
                // across the screen. The first few are printed with their vertices.
                uint32_t slivers = 0;
                std::string sliver_text;
                if (c.prim == 0) {
                    for (uint32_t t = 0; t + 2 < c.count; t += 3) {
                        const float* a = b.verts[b.indices[c.first + t]].pos;
                        const float* bb = b.verts[b.indices[c.first + t + 1]].pos;
                        const float* cc = b.verts[b.indices[c.first + t + 2]].pos;
                        auto len = [](const float* p, const float* q) {
                            return sqrtf((p[0]-q[0])*(p[0]-q[0]) + (p[1]-q[1])*(p[1]-q[1]) + (p[2]-q[2])*(p[2]-q[2]));
                        };
                        const float e0 = len(a, bb), e1 = len(bb, cc), e2 = len(cc, a);
                        const float longest = std::max(e0, std::max(e1, e2));
                        if (longest < 40.0f) continue;
                        // Heron's formula for the area; height = 2A / longest.
                        const float sp = (e0 + e1 + e2) * 0.5f;
                        const float area2 = std::max(0.0f, sp * (sp - e0) * (sp - e1) * (sp - e2));
                        const float height = 2.0f * sqrtf(area2) / longest;
                        static const float ratio = getenv("GCN_SLIVER_RATIO") ? (float)atof(getenv("GCN_SLIVER_RATIO")) : 0.001f;
                        if (height < longest * ratio) {
                            if (slivers < 3) {
                                char buf[200];
                                const GpuVertex& va = b.verts[b.indices[c.first + t]];
                                const GpuVertex& vb = b.verts[b.indices[c.first + t + 1]];
                                const GpuVertex& vc = b.verts[b.indices[c.first + t + 2]];
                                snprintf(buf, sizeof(buf), "\n        sliver src d%u:%d d%u:%d d%u:%d (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f) len=%.0f",
                                         va.src_draw, (int)va.src_idx, vb.src_draw, (int)vb.src_idx, vc.src_draw, (int)vc.src_idx,
                                         a[0], a[1], a[2], bb[0], bb[1], bb[2], cc[0], cc[1], cc[2], longest);
                                sliver_text += buf;
                            }
                            slivers++;
                        }
                    }
                }
                // The viewport's depth band, and the view-space box, to two decimals: a
                // weapon held in front of the camera is a unit or two across.
                const float band_hi = st.viewport[5] / 16777215.0f;
                const float band_lo = band_hi - fabsf(st.viewport[2]) / 16777215.0f;
                // The projection's scales and offsets (XF 0x1020..0x1023) go with the box: a
                // box is inside or outside a frustum only against the projection it was
                // drawn with, and a frame may use several.
                fprintf(stderr, "[draw] %d verts=%u st=%u texgens=%u proj=%c,%.4f,%.4f,%.4f,%.4f,%.6g,%.6g z=%.2f..%.2f x=%.2f..%.2f y=%.2f..%.2f band=%.5f..%.5f%s origin=%u slivers=%u%s",
                        draw_index, c.count, c.state, st.num_texgens,
                        (int)st.proj[6] == 0 ? 'p' : 'o', st.proj[0], st.proj[1], st.proj[2], st.proj[3], st.proj[4], st.proj[5],
                        zlo, zhi, xlo, xhi, ylo, yhi, band_lo, band_hi, st.clip_disable ? " noclip" : "",
                        at_origin, slivers, sliver_text.c_str());
                if (st.tag) fprintf(stderr, " tag=%u", st.tag);   // set_draw_tag
                if (slivers) fprintf(stderr, "\n       ");
                // Each texture by id and, for a decoded one, by its content hash, which is
                // the same in every run (TexData::hash); an EFB copy is starred.
                for (int i = 0; i < 8; i++) {
                    if (!st.tex_id[i]) continue;
                    if (st.tex_is_efb[i]) fprintf(stderr, " t%d=%u*", i, st.tex_id[i]);
                    else fprintf(stderr, " t%d=%u[%016llx]", i, st.tex_id[i], (unsigned long long)render_texture_hash(st.tex_id[i]));
                }
                fprintf(stderr, "\n");
                // GCN_DRAWLOG_VERBOSE=1 adds the TEV setup -- stage count, each stage's
                // order, colour and alpha combiners and konst selectors, the colour
                // registers and konsts -- and the first few vertices, so a draw's shading
                // can be read off without a shader dump.
                static const bool verbose = getenv("GCN_DRAWLOG_VERBOSE") != nullptr;
                if (verbose) {
                    const uint32_t* bp = st.bp;
                    const uint32_t nstages = ((bp[0] >> 10) & 15) + 1;
                    fprintf(stderr, "        genmode=%06X stages=%u colors=%u", bp[0], nstages, st.num_colors);
                    for (uint32_t i = 0; i < nstages; i++)
                        fprintf(stderr, "\n        st%u order=%06X cenv=%06X aenv=%06X ksel=%06X", i,
                                bp[0x28 + i / 2], bp[0xC0 + 2 * i], bp[0xC1 + 2 * i], bp[0xF6 + i / 2]);
                    fprintf(stderr, "\n        alphafunc=%06X zmode=%06X blend=%06X dstalpha=%06X pecntrl=%06X", bp[0xF3], bp[0x40], bp[0x41], bp[0x42], bp[0x43]);
                    // The indirect stages: the matrices (BP 0x06-0x0E), the coordinate
                    // scales (0x25-0x26), the stage references (0x27) and each TEV
                    // stage's indirect command (0x10-0x1F).
                    if (((bp[0] >> 16) & 7) != 0) {
                        fprintf(stderr, "\n        indmtx=");
                        for (int i = 0; i < 9; i++) fprintf(stderr, "%06X%s", bp[0x06 + i], i == 8 ? "" : ",");
                        fprintf(stderr, " indcs=%06X,%06X iref=%06X indcmd=", bp[0x25], bp[0x26], bp[0x27]);
                        for (uint32_t i = 0; i < nstages; i++) fprintf(stderr, "%06X%s", bp[0x10 + i], i + 1 == nstages ? "" : ",");
                    }
                    fprintf(stderr, "\n        viewport=%.1f,%.1f,%.1f %.1f,%.1f,%.1f scissor=%06X %06X offset=%06X", st.viewport[0],
                            st.viewport[1], st.viewport[2], st.viewport[3], st.viewport[4], st.viewport[5], bp[0x20], bp[0x21], bp[0x59]);
                    // Each bound map's mode register (wrap, filters, LOD bias): BP 0x80-0x83
                    // and 0xA0-0xA3 for maps 0-3 and 4-7.
                    fprintf(stderr, "\n       ");
                    for (int i = 0; i < 8; i++)
                        if (st.tex_id[i]) fprintf(stderr, " mode%d=%06X", i, bp[i < 4 ? 0x80 + i : 0xA0 + (i - 4)]);
                    // Each map's image format (BP 0x88-0x8B, 0xA8-0xAB), and for an EFB copy
                    // the format it was copied in and the palette it is read through.
                    for (int i = 0; i < 8; i++) {
                        if (!st.tex_id[i]) continue;
                        fprintf(stderr, " fmt%d=%u", i, (bp[i < 4 ? 0x88 + i : 0xA8 + (i - 4)] >> 20) & 15);
                        if (st.tex_is_efb[i])
                            fprintf(stderr, "(copy %02X%s)", st.tex_efb_fmt[i],
                                    st.tex_pal[i] ? (st.tex_pal_bits[i] == 8 ? " pal8" : " pal4") : "");
                    }
                    // Fog (BP 0xEE-0xF2): the type as GXSetFog takes it (2 linear, 4 exp, 5
                    // exp2, 6-7 the reverse ones; 8 on top for an orthographic one), the
                    // registers, and for a perspective fog the start and end distances they
                    // come from -- GXSetFog makes A = n*f / ((f - n)(end - start)) and
                    // C = start / (end - start), and n*f / (f - n) is minus the projection's
                    // z offset.
                    const uint32_t fogsel = ((bp[0xF1] >> 21) & 7) | (((bp[0xF1] >> 20) & 1) << 3);
                    if ((fogsel & 7) >= 2) {
                        const float fa = fog_float(bp[0xEE]), fc = fog_float(bp[0xF1]);
                        fprintf(stderr, "\n        fog type=%u a=%g bmag=%u bshift=%u c=%g color=%06X", fogsel, fa,
                                bp[0xEF] & 0xFFFFFF, bp[0xF0] & 0x1F, fc, bp[0xF2] & 0xFFFFFF);
                        if (!(fogsel & 8) && fa != 0.0f) {
                            const float range = -st.proj[5] / fa;
                            fprintf(stderr, " (start %g end %g)", fc * range, fc * range + range);
                        }
                    }
                    for (int r = 0; r < 4; r++)
                        fprintf(stderr, "\n        reg%d=%08X %08X  konst%d=%08X %08X", r, st.tev_reg[r][0], st.tev_reg[r][1],
                                r, st.tev_konst[r][0], st.tev_konst[r][1]);
                    for (uint32_t v = 0; v < c.count && v < 24; v++) {
                        const uint32_t ix = b.indices[c.first + v];
                        const GpuVertex& gv = b.verts[ix];
                        fprintf(stderr, "\n        i%u=%u pos=%.1f,%.1f,%.1f col0=%02X%02X%02X%02X col1=%02X%02X%02X%02X uv0=%.3f,%.3f,%.3f", v, ix,
                                gv.pos[0], gv.pos[1], gv.pos[2], gv.col[0][0], gv.col[0][1], gv.col[0][2], gv.col[0][3],
                                gv.col[1][0], gv.col[1][1], gv.col[1][2], gv.col[1][3], gv.tex[0][0], gv.tex[0][1], gv.tex[0][2]);
                    }
                    fprintf(stderr, "\n");
                }
            }
            const int this_draw = draw_index++;
            if (skip_lo >= 0 && this_draw >= skip_lo && this_draw <= skip_hi) break;
            // GCN_NO_EFBTEX drops draws that sample a partial EFB copy -- here, the copy of
            // the water surface that the game tints submerged geometry with. Unlike a draw
            // index this is stable from frame to frame, which matters because the game's
            // timebase is wall-clock driven and frame N is not the same moment twice.
            static const bool no_efbtex = getenv("GCN_NO_EFBTEX") != nullptr;
            if (no_efbtex) {
                const PixelState& st = b.states[c.state];
                bool partial = false;
                for (int i = 0; i < 8; i++)
                    if (st.tex_is_efb[i] && st.tex_id[i]) partial = true;
                if (partial && !samples_fullscreen_copy(st)) break;
            }
            if (complog) {
                // Every draw that samples a render-to-texture result, not just the
                // whole-frame ones: the duplicate racer is in one of these layers and
                // the whole-frame one turned out to be innocent.
                const PixelState& st = b.states[c.state];
                bool any_efb = false;
                for (int i = 0; i < 8; i++) any_efb |= st.tex_is_efb[i] && st.tex_id[i];
                if (any_efb) {
                    fprintf(stderr, "[efb] f%u verts=%u texgens=%u cols=%u full=%d", g_render_frame,
                            c.count, st.num_texgens, st.num_colors, (int)comp);
                    for (int i = 0; i < 8; i++)
                        if (st.tex_id[i]) fprintf(stderr, " t%d=%u%s", i, st.tex_id[i],
                                                  st.tex_is_efb[i] ? "*" : "");
                    fprintf(stderr, "\n");
                }
            }
            if (comp && no_comp) break;
            // A triangle drawn without z freeze is the plane the next z freeze draws take,
            // culled or not: a game draws one it culls to set the plane.
            const bool zfreeze = (b.states[c.state].bp[0x00] >> 19) & 1;
            if (!zfreeze) zfreeze_plane(b, c, g_zplane);
            if (c.state != cur_state || c.prim != cur_prim) {
                apply_state(b.states[c.state], c.prim);
                cur_state = c.state;
                cur_prim = c.prim;
                n_apply++;
            } else if (zfreeze && g_applied.pr && g_applied.pr->u_zplane >= 0) {
                glUniform4fv(g_applied.pr->u_zplane, 1, g_zplane);
            }
            draw_cmd(b, c);
            // GCN_PIXEL=x,y[;x,y...] (EFB pixels) with GCN_DRAWLOG=<frame>: after every draw
            // of that frame, each listed pixel is read back and printed when it changed,
            // with the draw's index in the draw log. Which draws touch a pixel, and what
            // each leaves there, read off one frame.
            {
                static std::vector<std::pair<int, int>> probes = [] {
                    std::vector<std::pair<int, int>> v;
                    if (const char* e = getenv("GCN_PIXEL")) {
                        for (const char* q = e; *q; ) {
                            int x = atoi(q);
                            const char* comma = strchr(q, ',');
                            if (!comma) break;
                            v.push_back({x, atoi(comma + 1)});
                            const char* semi = strchr(comma, ';');
                            if (!semi) break;
                            q = semi + 1;
                        }
                    }
                    return v;
                }();
                static std::vector<uint32_t> last(probes.size(), 0xFFFFFFFFu), last_z(probes.size(), 0xFFFFFFFFu);
                if (!probes.empty() && (log_batch || (drawlog && g_present_count + 1 == drawlog))) {
                    glFinish();
                    for (size_t pi = 0; pi < probes.size(); pi++) {
                        uint8_t px[4] = {0, 0, 0, 0};
                        const int gx = probes[pi].first * g_scale, gy = (EFB_H - 1 - probes[pi].second) * g_scale;
                        glReadPixels(gx, gy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                        float depth = -1.0f;
                        glReadPixels(gx, gy, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
                        const uint32_t v = (px[0] << 24) | (px[1] << 16) | (px[2] << 8) | px[3];
                        const uint32_t z = (uint32_t)(depth * 16777215.0f + 0.5f);
                        if (v != last[pi] || z != last_z[pi]) {
                            fprintf(stderr, "[pix] %d,%d after draw %d: %08X z=%06X\n", probes[pi].first, probes[pi].second, this_draw, v, z);
                            last[pi] = v;
                            last_z[pi] = z;
                        }
                    }
                }
            }
            break;
        }
        case CmdType::EfbCopy: {
            // Timed on its own: a copy mid-frame ends the EFB's render pass on a tiled GPU
            // and the driver's share of that lands here, on the CPU.
            const auto t_copy = frametime_on() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            if (log_copies)
                fprintf(stderr, "[copy] after draw %d: %ux%u from %u,%u to %08X tex %u fmt %X%s\n", draw_index - 1, c.copy.dst_w, c.copy.dst_h,
                        c.copy.src_x, c.copy.src_y, c.copy.dest, c.copy.tex_id, c.copy.format, c.copy.to_xfb ? " (display)" : "");
            do_efb_copy(c.copy);
            if (frametime_on()) ms_copy += ms_since(t_copy);
            glViewport(0, 0, EFB_W * g_scale, EFB_H * g_scale);
            cur_state = UINT32_MAX;
            gl_state_invalidate();
            break;
        }
        case CmdType::Present:
            if (do_present) present(c.copy);
            presented = true;
            glBindFramebuffer(GL_FRAMEBUFFER, g_efb_fbo);
            glViewport(0, 0, EFB_W * g_scale, EFB_H * g_scale);
            cur_state = UINT32_MAX;
            gl_state_invalidate();
            break;
        }
    }
    if (frametime_on())
        fprintf(stderr, "[rt] f%u render %6.2fms (tex %5.2f vbo %5.2f copies %5.2f)  applies %4u  programs %zu\n",
                g_render_frame, ms_since(t_start), ms_tex, ms_vbo, ms_copy, n_apply, g_programs.size());
    apply_stats_flush("flat", g_render_frame);
    return presented;
}

// The flat view shows everything the eyes leave out, but what the game's filter says to
// leave out everywhere (EyeHideEverywhere).
static void panel_hide(const Batch& b) {
    g_hide.clear();
    if (!g_eye_filter) return;
    std::vector<int8_t> answer;
    std::vector<float> box;
    filter_answers(b, nullptr, answer, box);
    for (size_t i = 0; i < b.cmds.size(); i++) {
        const Cmd& c = b.cmds[i];
        if (c.type != CmdType::Draw || answer[c.state] != EyeHideEverywhere) continue;
        if (g_hide.empty()) g_hide.assign(b.cmds.size(), 0);
        g_hide[i] = 1;
    }
}

bool render_execute(Batch& b) {
    frame_observe(b, nullptr);
    panel_hide(b);
    return execute_batch(b, true);
}

void render_set_panel_band(float band) { g_panel_band = band; }

bool render_execute_stereo_pair(Batch& b, unsigned fbo_l, unsigned fbo_r, float eye_sep,
                                float panel_width) {
    frame_observe(b, nullptr);
    panel_hide(b);
    const GLuint out = g_output_fbo;
    g_flat_halfw = panel_width > 0.0f ? 0.5f * panel_width : 1.0f;
    g_flat_eye = -0.5f * eye_sep;
    g_output_fbo = (GLuint)fbo_l;
    g_dump_suffix = "_l";
    const bool presented = execute_batch(b, true);
    g_flat_eye = 0.5f * eye_sep;
    g_output_fbo = (GLuint)fbo_r;
    g_dump_suffix = "_r";
    g_pair_second = true;
    execute_batch(b, true, nullptr, true);
    g_pair_second = false;
    g_dump_suffix = "";
    g_flat_eye = 0.0f;
    g_output_fbo = out;
    return presented;
}

}  // namespace gx
