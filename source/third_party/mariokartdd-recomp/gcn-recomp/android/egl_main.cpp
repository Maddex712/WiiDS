// Headless EGL frontend for Android: runs the real renderer offscreen and writes frames
// out as PNGs.
//
// No window, no OpenXR, no APK. The point is to validate the OpenGL ES renderer on a
// device over adb -- shader compilation, the TEV pipeline, textures, EFB copies -- before
// building a VR frontend on top of it. The renderer draws into its own framebuffer
// object and the frame dump reads that back, so the EGL surface only exists to make a
// context current and can be 16x16.
//
// Usage: <game>_egl [--scale=N] [--frames=N] [--seconds=N] [--fast]
//                   [--dump-dir=DIR] [--dump-every=N] [path/to/game.iso]
//
// The VR settings are the headset's: the game's defaults (vr::GameHooks::config_defaults)
// with ./vr.txt, if there is one, read over them. With --eye the EFB's scale is theirs too
// (stereo_scale) unless --scale says otherwise, as the app's is in stereo: a game that
// reads its own frame back (an id buffer it picks a target from) sees other pixels at
// another scale, and a replay of a headset session at scale 1 parted from its recording.
#include "runtime.h"
#include "platform.h"
#include "gx/render.h"
#include "gx/render_gl.h"
#include "vr_game.h"
#include "input_script.h"
#include "input_log.h"
#include "gx/gl.h"
#include "gx/gl_msrtt.h"
#include "hw/pad.h"
#include <EGL/egl.h>
#include <dlfcn.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>

uint32_t boot_load(const char* iso_path);
bool audio_open();
void debug_dump_threads();
bool write_png(const char* path, const uint8_t* rgba, int w, int h);
static void* gl_proc(const char* name);

// ---------------------------------------------------------------------------
// --eye renders through render_execute_eye into an offscreen target and dumps that,
// instead of the flat path. It exists so the stereo renderer can be looked at without
// a headset: an idle headset will not launch a 6DoF app, and shipping VR changes that
// cannot be checked first has already cost a regression.
//
// The view is the identity, so the eye sits exactly where the game's camera is and the
// result should closely match the flat render. Anything that differs -- geometry in the
// wrong place, missing render-to-texture results -- is a fault in the eye path.
// ---------------------------------------------------------------------------
static bool g_eye_mode = false;
// --fast runs the guest unpaced, as fast as the device allows, for measuring; the game still
// sees 60 Hz.
static bool g_fast = false;
static float g_eye_yaw = 0.0f;  // --eye-yaw: degrees of head turn, for spotting head-locked draws
static float g_eye_pitch = 0.0f;  // --eye-pitch: degrees of looking down
// --eye-fov=UP,DOWN: the eye's field above and below its axis, in degrees (45,45 by
// default). A headset's is asymmetric -- a Quest 3 sees further down than up -- and the
// HUD frame is sized from the larger of the two, so a 2D element's placement against the
// world can only be checked with the field the headset has.
static float g_eye_fov_up = 45.0f, g_eye_fov_down = 45.0f;
// --eye-pos=X,Y,Z: the head moved from the game's camera, in metres (x right, y up, z
// back). Rotation alone shows no parallax; a step to the side tells what is near from what
// is far -- a HUD meant to be at arm's length moves across the room, one at ten metres
// barely moves.
static float g_eye_pos[3] = {0.0f, 0.0f, 0.0f};
static GLuint g_eye_fbo, g_eye_tex, g_eye_depth;
static int g_eye_w = 960, g_eye_h = 720;

// --eyes=2 renders the second eye as well, as the headset does; --eye-size=WxH renders at
// the headset's size rather than this harness's; --msaa=N multisamples the eye the way the
// headset can. Together with GCN_EYE_GPU=1, which times the eye passes on the GPU, they are
// what answers "what does a higher eye resolution or antialiasing cost" without a headset.
static int g_eye_count = 1, g_eye_msaa = 0;

// --first-person turns on the game's own eye (vr::GameHooks::set_first_person), as the
// right thumbstick click does in the headset; --fp-window=a-b holds it on only for those
// presented frames, which is how a switch in and back out again is reproduced.
static bool g_fp = false;
static int g_fp_lo = -1, g_fp_hi = -1;
static VrConfig g_vrcfg;

static void eye_init() {
    // GCN_EYE_SRGB=1 gives the eye the headset's target: an sRGB image written with the
    // encode off (EXT_sRGB_write_control), as the OpenXR frontend's swapchain is, so that
    // the renderer's grab path into sRGB textures runs here, where it can be timed and dumped.
    const bool srgb = getenv("GCN_EYE_SRGB") && atoi(getenv("GCN_EYE_SRGB")) != 0;
    if (srgb) glDisable(0x8DB9 /* GL_FRAMEBUFFER_SRGB_EXT */);
    glGenTextures(1, &g_eye_tex);
    glBindTexture(GL_TEXTURE_2D, g_eye_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, g_eye_w, g_eye_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const gx::Msrtt ms = g_eye_msaa > 1 ? gx::msrtt_load(gl_proc) : gx::Msrtt{};
    if (g_eye_msaa > 1 && ms.max_samples < g_eye_msaa) {
        fprintf(stderr, "--msaa=%d unavailable (max %d); rendering without\n", g_eye_msaa,
                ms.max_samples);
        g_eye_msaa = 0;
    }
    glGenRenderbuffers(1, &g_eye_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_eye_depth);
    if (g_eye_msaa > 1)
        ms.renderbuffer_storage(GL_RENDERBUFFER, g_eye_msaa, GL_DEPTH_COMPONENT24, g_eye_w, g_eye_h);
    else
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, g_eye_w, g_eye_h);
    glGenFramebuffers(1, &g_eye_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_eye_fbo);
    if (g_eye_msaa > 1)
        ms.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_eye_tex, 0,
                                  g_eye_msaa);
    else
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_eye_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_eye_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "eye fbo incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Column-major, matching glUniformMatrix4fv with transpose = GL_FALSE. The game's view
// space is -Z forward, so this is an ordinary GL perspective in game units.
static void eye_matrices(float* proj, float* view, float* hud) {
    // The field: tan_up above the axis, tan_down below, the width in proportion to the
    // target. The frame the HUD hangs on is sized from the larger, as the app sizes it.
    const float tan_up = tanf(g_eye_fov_up * 3.14159265f / 180.0f);
    const float tan_down = tanf(g_eye_fov_down * 3.14159265f / 180.0f);
    const float fov = fmaxf(tan_up, tan_down);
    const float aspect = (float)g_eye_w / (float)g_eye_h;
    const float half_w = 0.5f * (tan_up + tan_down) * aspect;
    const float u = g_vrcfg.units_per_metre;
    const float n = g_vrcfg.near_m * u, f = g_vrcfg.far_m * u;
    memset(proj, 0, 16 * sizeof(float));
    // An off-axis frustum: [-half_w, half_w] across, [-tan_down, tan_up] up.
    proj[0] = 1.0f / half_w;
    proj[5] = 2.0f / (tan_up + tan_down);
    proj[9] = (tan_up - tan_down) / (tan_up + tan_down);
    proj[10] = -(f + n) / (f - n);
    proj[11] = -1.0f;
    proj[14] = -(2.0f * f * n) / (f - n);
    // --eye-yaw turns the head. With the view left at identity nothing in the image can
    // ever be seen to be head-locked, which is how a change that pinned part of the world
    // to the viewer's face once got through this harness looking correct.
    // Dump the same frame at two yaws: whatever does not move with the world is locked.
    const float a = g_eye_yaw * 3.14159265f / 180.0f;
    float Y[16];
    memset(Y, 0, sizeof(Y));
    Y[0] = cosf(a);  Y[2] = -sinf(a);
    Y[8] = sinf(a);  Y[10] = cosf(a);
    Y[5] = Y[15] = 1.0f;
    // --eye-pitch tips it down: the inverse of a rotation about X by -pitch is one by
    // +pitch.
    const float b = g_eye_pitch * 3.14159265f / 180.0f;
    float X[16];
    memset(X, 0, sizeof(X));
    X[0] = X[15] = 1.0f;
    X[5] = cosf(b);  X[6] = sinf(b);
    X[9] = -sinf(b); X[10] = cosf(b);
    // view = X * Y * T, column-major: the world moved by -pos, then turned.
    float T[16];
    memset(T, 0, sizeof(T));
    T[0] = T[5] = T[10] = T[15] = 1.0f;
    T[12] = -g_eye_pos[0] * u; T[13] = -g_eye_pos[1] * u; T[14] = -g_eye_pos[2] * u;
    float XY[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float v = 0.0f;
            for (int k = 0; k < 4; k++) v += X[k * 4 + r] * Y[c * 4 + k];
            XY[c * 4 + r] = v;
        }
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float v = 0.0f;
            for (int k = 0; k < 4; k++) v += XY[k * 4 + r] * T[c * 4 + k];
            view[c * 4 + r] = v;
        }
    // The HUD frame is anchored in front of the game's camera, not the head, so --eye-yaw
    // swings it out of view exactly as turning to look away does in the headset. A HUD
    // that sits still under a yaw is one that is still locked to the viewer's face.
    gx::render_hud_frame(g_vrcfg.hud_distance_m * u, fov, g_vrcfg.hud_scale,
                         g_vrcfg.hud_height_m * u, g_vrcfg.hud_pitch_deg * 3.14159265f / 180.0f, hud);

    // The same eyes for the game's culling (vr::eye_views), as the headset publishes them.
    // The view turns the world by +yaw about y and +pitch about x, so the head is turned by
    // -yaw, then -pitch about its own x. The second eye (--eyes=2) is 64 mm to its right.
    const float sy = sinf(-0.5f * a), cy = cosf(-0.5f * a), sx = sinf(-0.5f * b), cx = cosf(-0.5f * b);
    vr::EyeView ev[2];
    for (int e = 0; e < 2; e++) {
        ev[e].rot[0] = cy * sx;
        ev[e].rot[1] = sy * cx;
        ev[e].rot[2] = -sy * sx;
        ev[e].rot[3] = cy * cx;
        ev[e].tan_left = -half_w;
        ev[e].tan_right = half_w;
        ev[e].tan_up = tan_up;
        ev[e].tan_down = -tan_down;
    }
    const float sep = g_eye_count > 1 ? 0.064f * u : 0.0f;
    for (int i = 0; i < 3; i++) ev[0].pos[i] = g_eye_pos[i] * u;
    ev[1].pos[0] = ev[0].pos[0] + sep * cosf(a);  // the head's right, (cos yaw, 0, sin yaw) after the turn
    ev[1].pos[1] = ev[0].pos[1];
    ev[1].pos[2] = ev[0].pos[2] + sep * sinf(a);
    vr::set_eye_views(ev);
}

static void dump_fbo(GLuint fbo, int w, int h, const char* path) {
    std::vector<uint8_t> px((size_t)w * h * 4), fl(px.size());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    const size_t stride = (size_t)w * 4;
    for (int y = 0; y < h; y++)
        memcpy(&fl[y * stride], &px[(size_t)(h - 1 - y) * stride], stride);
    for (size_t i = 3; i < fl.size(); i += 4) fl[i] = 255;
    write_png(path, fl.data(), w, h);
}

static void eye_dump(const char* dir, uint32_t n, const char* suffix = "") {
    char path[512];
    snprintf(path, sizeof(path), "%s/eye_%05u%s.png", dir, n, suffix);
    dump_fbo(g_eye_fbo, g_eye_w, g_eye_h, path);
}

// GCN_EYE_MORPH=0,0.5,1 renders every dumped frame once per value, as eye_NNNNN_mXXXX.png
// (thousandths), with the stereo view folded that far back towards theater -- see
// render_set_vr_morph -- plus the flat frame as flat_NNNNN.png. The panel hangs where
// theater hangs it, so the identity view sees it 2.5 m ahead and 3.2 m wide: at 0 the
// middle of the eye image should be flat_NNNNN.png, and nothing else.
static std::vector<float> g_morphs;
static GLuint g_flat_fbo, g_flat_tex;
static void morph_panel(float* m) {
    memset(m, 0, 16 * sizeof(float));
    const float u = g_vrcfg.units_per_metre;
    m[0] = 1.6f * u;
    m[5] = 1.2f * u;
    m[14] = -2.5f * u;
    m[15] = 1.0f;
}

// ---------------------------------------------------------------------------
// GCN_EYE_GPU=1: how long the eye passes take on the GPU, from GL_EXT_disjoint_timer_query,
// and on the CPU to issue, averaged and printed every 2 s. The GPU figure covers both eyes
// and the flat pass the first eye runs to produce the frame's copies. Queries are read a few
// frames late from a ring, so measuring does not itself stall the pipeline.
static bool g_gpu_timing = false;
static void (*g_query_u64)(GLuint, GLenum, uint64_t*) = nullptr;
static GLuint g_queries[8];
static int g_q_head = 0, g_q_live = 0;
static double g_gpu_ms_sum = 0, g_cpu_ms_sum = 0;
static int g_gpu_n = 0, g_cpu_n = 0;
static constexpr GLenum kTimeElapsed = 0x88BF;   // GL_TIME_ELAPSED_EXT

static void gpu_timer_init() {
    g_gpu_timing = getenv("GCN_EYE_GPU") != nullptr;
    if (!g_gpu_timing) return;
    g_query_u64 = (void (*)(GLuint, GLenum, uint64_t*))gl_proc("glGetQueryObjectui64vEXT");
    if (!g_query_u64) {
        fprintf(stderr, "GCN_EYE_GPU: no GL_EXT_disjoint_timer_query; CPU times only\n");
        return;
    }
    glGenQueries(8, g_queries);
}

static void gpu_timer_collect(bool all) {
    // A disjoint event -- the GPU changing clock, say -- makes the queries in flight
    // meaningless; they came back as hours. Drop them. The flag alone did not catch every
    // such sample on a Quest 3, so anything over a second goes too.
    GLint disjoint = 0;
    glGetIntegerv(0x8FBB /* GL_GPU_DISJOINT_EXT */, &disjoint);
    while (g_q_live > 0) {
        const GLuint q = g_queries[(g_q_head - g_q_live + 8) % 8];
        GLuint ready = 0;
        if (!all) glGetQueryObjectuiv(q, GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!all && !ready) break;
        uint64_t ns = 0;
        g_query_u64(q, GL_QUERY_RESULT, &ns);
        if (!disjoint && ns < 1000000000ull) {
            g_gpu_ms_sum += ns * 1e-6;
            g_gpu_n++;
        }
        g_q_live--;
    }
}

static void gpu_timer_begin() {
    if (!g_query_u64) return;
    if (g_q_live == 8) gpu_timer_collect(true);
    glBeginQuery(kTimeElapsed, g_queries[g_q_head]);
}

static void gpu_timer_end(double cpu_ms) {
    if (!g_gpu_timing) return;
    g_cpu_ms_sum += cpu_ms;
    g_cpu_n++;
    if (!g_query_u64) return;
    glEndQuery(kTimeElapsed);
    g_q_head = (g_q_head + 1) % 8;
    g_q_live++;
    gpu_timer_collect(false);
}

static void gpu_timer_report() {
    if (!g_gpu_timing || !g_cpu_n) return;
    printf("  eyes: cpu %.2f ms", g_cpu_ms_sum / g_cpu_n);
    if (g_gpu_n) printf("  gpu %.2f ms", g_gpu_ms_sum / g_gpu_n);
    printf("  (%d frames)\n", g_cpu_n);
    g_gpu_ms_sum = g_cpu_ms_sum = 0;
    g_gpu_n = g_cpu_n = 0;
}

static void on_interrupt() {
    plat_watchdog(2, 2);
    debug_dump_threads();
    plat_exit_now(1);
}

static void on_fault(const void* fault_addr, int code) {
    uintptr_t a = (uintptr_t)fault_addr;
    if (a >= (uintptr_t)g_mem && a < (uintptr_t)g_mem + 0x40000000)
        fprintf(stderr, "\nFAULT: guest memory access at (addr & 0x3FFFFFFF) = %08lX\n",
                (unsigned long)(a - (uintptr_t)g_mem));
    else
        fprintf(stderr, "\nFAULT: host address %p (code %d)\n", fault_addr, code);
    plat_watchdog(2, 2);
    plat_backtrace_print();
    debug_dump_threads();
    plat_exit_now(1);
}

// ---------------------------------------------------------------------------
// GL entry points. Android's eglGetProcAddress does resolve core ES functions, but not
// on every driver, so prefer dlsym on the ES library and fall back to EGL.
// ---------------------------------------------------------------------------
static void* gl_proc(const char* name) {
    static void* lib = [] {
        void* h = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
        if (!h) h = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
        return h;
    }();
    if (lib) {
        if (void* p = dlsym(lib, name)) return p;
    }
    return (void*)eglGetProcAddress(name);
}

static EGLDisplay g_dpy = EGL_NO_DISPLAY;

static bool egl_init() {
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY) { fprintf(stderr, "eglGetDisplay failed\n"); return false; }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(g_dpy, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed (0x%X)\n", eglGetError());
        return false;
    }
    printf("EGL %d.%d  %s\n", major, minor, eglQueryString(g_dpy, EGL_VENDOR));

    // A pbuffer config rather than a window config: there is no surface to present to.
    const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE,
    };
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
        fprintf(stderr, "eglChooseConfig found no ES3 pbuffer config (0x%X)\n", eglGetError());
        return false;
    }

    const EGLint ctx_attr[] = {
        EGL_CONTEXT_MAJOR_VERSION, GCN_GL_MAJOR,
        EGL_CONTEXT_MINOR_VERSION, GCN_GL_MINOR,
        EGL_NONE,
    };
    EGLContext ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext for ES %d.%d failed (0x%X)\n",
                GCN_GL_MAJOR, GCN_GL_MINOR, eglGetError());
        return false;
    }

    const EGLint pb_attr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(g_dpy, cfg, pb_attr);
    if (surf == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreatePbufferSurface failed (0x%X)\n", eglGetError());
        return false;
    }
    if (!eglMakeCurrent(g_dpy, surf, surf, ctx)) {
        fprintf(stderr, "eglMakeCurrent failed (0x%X)\n", eglGetError());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    std::string iso = GCN_DEFAULT_ISO;
    int scale = 0, frames_wanted = 0, seconds = 120;
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--scale=", 8)) scale = atoi(argv[i] + 8);
        else if (!strncmp(argv[i], "--frames=", 9)) frames_wanted = atoi(argv[i] + 9);
        else if (!strncmp(argv[i], "--seconds=", 10)) seconds = atoi(argv[i] + 10);
        else if (!strcmp(argv[i], "--eye")) g_eye_mode = true;
        else if (!strcmp(argv[i], "--fast")) g_fast = true;
        else if (!strncmp(argv[i], "--eye-yaw=", 10)) g_eye_yaw = (float)atof(argv[i] + 10);
        else if (!strncmp(argv[i], "--eye-pitch=", 12)) g_eye_pitch = (float)atof(argv[i] + 12);
        else if (!strncmp(argv[i], "--eye-pos=", 10)) {
            sscanf(argv[i] + 10, "%f,%f,%f", &g_eye_pos[0], &g_eye_pos[1], &g_eye_pos[2]);
        }
        else if (!strncmp(argv[i], "--eye-fov=", 10)) {
            g_eye_fov_up = (float)atof(argv[i] + 10);
            const char* comma = strchr(argv[i] + 10, ',');
            g_eye_fov_down = comma ? (float)atof(comma + 1) : g_eye_fov_up;
        }
        else if (!strncmp(argv[i], "--eye-size=", 11)) sscanf(argv[i] + 11, "%dx%d", &g_eye_w, &g_eye_h);
        else if (!strncmp(argv[i], "--eyes=", 7)) g_eye_count = atoi(argv[i] + 7);
        else if (!strncmp(argv[i], "--msaa=", 7)) g_eye_msaa = atoi(argv[i] + 7);
        else if (!strcmp(argv[i], "--first-person")) g_fp = true;
        else if (!strncmp(argv[i], "--fp-window=", 12)) sscanf(argv[i] + 12, "%d-%d", &g_fp_lo, &g_fp_hi);
        else if (!strncmp(argv[i], "--dump-dir=", 11)) gx::g_dump_dir = argv[i] + 11;
        else if (!strncmp(argv[i], "--dump-every=", 13)) gx::g_dump_every = atoi(argv[i] + 13);
        else if (argv[i][0] != '-') iso = argv[i];
    }
    if (!plat_readable(iso.c_str())) iso = plat_find_file(".", ".iso");
    if (!plat_readable(iso.c_str())) iso = plat_find_file(".", ".ciso");
    if (!plat_readable(iso.c_str())) {
        fprintf(stderr, "No game image found. Pass the path to your .iso.\n");
        return 1;
    }
    if (gx::g_dump_dir) plat_make_dirs(gx::g_dump_dir);

    plat_install_crash_handlers(on_interrupt, on_fault);
    g_vrcfg = vr::load_config(".");
    if (scale <= 0) scale = g_eye_mode ? g_vrcfg.stereo_scale : 1;

    mem_init();
    timing_init();
    if (g_fast) clock_set_scale(0);
    input_script_init();
    // GCN_REPLAY=<dir> plays an input log back, as --replay does on the desktop: the way to
    // drive a run on a device through a recorded route, exactly, under the virtual clock.
    static std::string replay_card;
    if (const char* r = getenv("GCN_REPLAY")) {
        if (!input_replay_load(r, replay_card)) {
            fprintf(stderr, "GCN_REPLAY: no input log at %s\n", r);
            return 1;
        }
        if (!replay_card.empty()) g_memcard_path = replay_card.c_str();
    }
    // GCN_INPUT_LOG=<dir> records this run's input log there, as the app does for every
    // run; a replay's recorded clock jumps are then written into it too, so a route can
    // be re-recorded or extended on the device as it can on the desktop.
    if (const char* d = getenv("GCN_INPUT_LOG"))
        input_log_start(d, replay_card.empty() ? std::string(g_memcard_path ? g_memcard_path : "") : replay_card);

    if (!egl_init()) return 1;
    int glver = gl_load_with(gl_proc);
    if (glver < GCN_GL_VERSION_MIN) {
        fatal("OpenGL ES %d.%d is too old (need %d.%d)", glver / 10, glver % 10,
              GCN_GL_MAJOR, GCN_GL_MINOR);
    }
    printf("GL_VERSION  %s\nGL_RENDERER %s\nGL_VENDOR   %s\n",
           (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER),
           (const char*)glGetString(GL_VENDOR));
    fflush(stdout);

    gx::render_set_shader_cache("shaders.bin");
    gx::render_init(scale);
    // The app sets the game's depth layers and panel band whatever it is showing, and the
    // stereo pair (GCN_THEATER_STEREO) uses both: its HUD sits on the panel and its sky at
    // infinity.
    if (!g_eye_mode && getenv("GCN_THEATER_STEREO")) {
        gx::render_set_depth_layers(g_vrcfg.background_band, g_vrcfg.foreground_band, g_vrcfg.foreground_scale, g_vrcfg.hud_band, g_vrcfg.hud_band_scale);
        gx::render_set_panel_band(g_vrcfg.panel_band);
    }
    if (g_eye_mode) {
        eye_init();
        gpu_timer_init();
        gx::render_set_world_pitch(g_vrcfg.world_pitch_deg * 3.14159265f / 180.0f);
        gx::render_set_depth_layers(g_vrcfg.background_band, g_vrcfg.foreground_band, g_vrcfg.foreground_scale, g_vrcfg.hud_band, g_vrcfg.hud_band_scale);
        gx::render_set_hud_push(g_vrcfg.hud_push_below, g_vrcfg.hud_push_to);
        gx::render_set_view_space_world(g_vrcfg.view_space_world);
        gx::render_set_eye_filter(vr::game_hooks().eye_filter);
        gx::render_set_panel_band(g_vrcfg.panel_band);
        if (const char* s = getenv("GCN_EYE_MORPH")) {
            for (const char* q = s; *q;) {
                g_morphs.push_back((float)atof(q));
                q = strchr(q, ',');
                if (!q) break;
                q++;
            }
            glGenTextures(1, &g_flat_tex);
            glBindTexture(GL_TEXTURE_2D, g_flat_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glGenFramebuffers(1, &g_flat_fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, g_flat_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_flat_tex, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
    // The flat frame GCN_EYE_MORPH dumps is blitted at 640x480 whatever --scale says.
    gx::render_set_window_size(g_morphs.empty() ? 640 * scale : 640,
                               g_morphs.empty() ? 480 * scale : 480);

    // GCN_AUDIO=1 opens the device here too. Off by default because this harness exists
    // to be run over adb on a device somebody may be wearing, but it is the only way to
    // exercise the audio path without the VR frontend -- and with GCN_WAV it records what
    // the device was handed, which is checkable afterwards rather than by listening.
    if (getenv("GCN_AUDIO") && !audio_open())
        fprintf(stderr, "audio unavailable; continuing without sound\n");

    uint32_t entry = boot_load(iso.c_str());
    threads_start_boot(entry);
    host_profile_start();

    printf("rendering (scale %d)%s...\n", scale,
           gx::g_dump_dir ? "" : "  [no --dump-dir, nothing will be written]");
    fflush(stdout);

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const float theater_stereo_m = getenv("GCN_THEATER_STEREO") ? (float)atof(getenv("GCN_THEATER_STEREO")) : 0.0f;
    auto t_mark = t0;
    uint32_t presented = 0, presented_at_mark = 0, last_snap = 0;
    for (;;) {
        PadState p;
        p.connected = true;
        input_script_apply(p);
        pad_set_state(0, p);

        // GCN_RAMSNAP=dir dumps guest RAM every GCN_RAMSNAP_EVERY presented frames.
        // Diffing snapshots taken in known game states is how a variable for a stereo hook
        // -- "the player is in control", say -- gets found.
        static const char* ramsnap = getenv("GCN_RAMSNAP");
        static const uint32_t snap_every = getenv("GCN_RAMSNAP_EVERY")
                                               ? atoi(getenv("GCN_RAMSNAP_EVERY")) : 150;
        // The low 8 MB: enough to hold the game's own state without writing 24 MB a shot.
        static const uint32_t snap_bytes = 8u << 20;
        // GCN_RAMSNAP_RANGE=a-b bounds it to a window of frames. A transition that takes a
        // second needs a snapshot every few frames to bracket, and a whole run at that
        // interval is gigabytes; the states either side of one boundary are all that a
        // diff for that boundary needs.
        static int snap_lo = -1, snap_hi = -1;
        static bool snap_range_parsed = false;
        if (!snap_range_parsed) {
            snap_range_parsed = true;
            if (const char* r = getenv("GCN_RAMSNAP_RANGE")) {
                snap_lo = atoi(r);
                const char* dash = strchr(r, '-');
                snap_hi = dash ? atoi(dash + 1) : snap_lo;
            }
        }
        const bool snap_in_range = snap_lo < 0 ||
                                   ((int)presented >= snap_lo && (int)presented <= snap_hi);
        if (ramsnap && snap_in_range && presented && presented % snap_every == 0 &&
            presented != last_snap) {
            last_snap = presented;
            char path[512];
            snprintf(path, sizeof(path), "%s/ram_%05u.bin", ramsnap, presented);
            if (FILE* f = fopen(path, "wb")) {
                fwrite(mem_ptr(0x80000000), 1, snap_bytes, f);
                fclose(f);
            }
        }
        if (auto b = gx::take_batch(4)) {
            // GCN_RESCALE=<frame>:<scale> changes the internal scale before that presented
            // frame, as the app does when it moves between stereo and theater
            // (stereo_scale, theater_scale).
            static int rescale_at = -1, rescale_to = 0;
            static const bool rescale = getenv("GCN_RESCALE") &&
                                        sscanf(getenv("GCN_RESCALE"), "%d:%d", &rescale_at, &rescale_to) == 2;
            if (rescale && (int)presented == rescale_at) {
                gx::render_set_internal_scale(rescale_to);
                fprintf(stderr, "[rescale] frame %u: internal scale %d\n", presented, rescale_to);
                rescale_at = -1;
            }
            // GCN_STEREOLOG=1 prints what the game's stereo hook answers whenever the answer
            // changes, which is how a hook is checked against dumped frames without a headset.
            // Asked every frame whether or not it is logged, as the app asks it: a game's
            // hook may do its render-thread work here (finding the camera it answers from,
            // say), which the game's other hooks then rely on. Without this the scan visor
            // followed the head on the headset but not in the harness.
            static const bool stereolog = getenv("GCN_STEREOLOG") != nullptr;
            if (vr::game_hooks().wants_stereo) {
                static int last = -1;
                const int want = vr::game_hooks().wants_stereo(*b) ? 1 : 0;
                if (stereolog && want != last) {
                    last = want;
                    fprintf(stderr, "[stereo] frame %u: %s\n", presented, want ? "stereo" : "theater");
                }
            }
            // And whether the panel would be a pair (wants_panel_pair), so a replay in --eye,
            // where the game sees the eyes as it does in the headset, can show it too.
            if (stereolog && vr::game_hooks().wants_panel_pair) {
                static int last_pair = -1;
                const int pair = vr::game_hooks().wants_panel_pair(*b) ? 1 : 0;
                if (pair != last_pair) {
                    last_pair = pair;
                    fprintf(stderr, "[stereo] frame %u: panel %s\n", presented, pair ? "pair" : "flat");
                }
            }
            if (g_eye_mode) {
                float P[16], V[16], H[16];
                eye_matrices(P, V, H);
                gx::render_set_vr_morph(1.0f, nullptr);
                if (g_fp && vr::game_hooks().set_first_person) {
                    const bool on = g_fp_lo < 0 || ((int)presented >= g_fp_lo && (int)presented < g_fp_hi);
                    static int was = -1;
                    if ((int)on != was) {
                        was = on;
                        vr::game_hooks().set_first_person(on, g_vrcfg);
                        fprintf(stderr, "[fp] frame %u: first person %s\n", presented, on ? "on" : "off");
                    }
                }
                gpu_timer_begin();
                const auto t_eyes = clock::now();
                if (g_eye_count > 1) {
                    // The other eye first, into the same target, so what is dumped is the
                    // left one, 64 mm away.
                    float V2[16];
                    memcpy(V2, V, sizeof(V2));
                    V2[12] -= 0.064f * g_vrcfg.units_per_metre;
                    gx::render_set_vr_eye(P, V2, H);
                    gx::render_execute_eye(*b, g_eye_fbo, g_eye_w, g_eye_h, true);
                }
                gx::render_set_vr_eye(P, V, H);
                gx::render_execute_eye(*b, g_eye_fbo, g_eye_w, g_eye_h, g_eye_count < 2);
                gpu_timer_end(std::chrono::duration<double, std::milli>(clock::now() - t_eyes).count());
                // As the headset does after each eye. Nothing else here hands the GPU its
                // work, and a driver left to queue frames up stalls the next frame's vertex
                // upload until they drain: without this the flat pass once measured 17-21 ms
                // a frame, almost all of it waiting in glBufferData, against 6 ms with it.
                glFlush();
                presented++;
                // GCN_DUMP_COPIES=N dumps whenever a frame holds at least N EFB copies,
                // which is how a frame thick with spray is caught: the faults that only
                // appear at speed are in exactly those frames, and a fixed interval
                // almost never lands on one.
                static const int want_copies = getenv("GCN_DUMP_COPIES")
                                                   ? atoi(getenv("GCN_DUMP_COPIES")) : 0;
                int ncopies = 0;
                if (want_copies)
                    for (auto& c : b->cmds) ncopies += c.type == gx::CmdType::EfbCopy;
                // GCN_DUMP_RANGE=a-b narrows dumping to a window of frames, as it does in
                // the flat path, so a short stretch can be caught every few frames
                // without writing a gigabyte. Something that is only on screen for three
                // seconds is otherwise missed by any interval coarse enough to run a long
                // route with.
                static int range_lo = -1, range_hi = -1;
                static bool range_parsed = false;
                if (!range_parsed) {
                    range_parsed = true;
                    if (const char* r = getenv("GCN_DUMP_RANGE")) {
                        range_lo = atoi(r);
                        const char* dash = strchr(r, '-');
                        range_hi = dash ? atoi(dash + 1) : range_lo;
                    }
                }
                const bool in_range = range_lo < 0 ||
                                      ((int)presented >= range_lo && (int)presented <= range_hi);
                if (gx::g_dump_dir && in_range &&
                    ((gx::g_dump_every && presented % gx::g_dump_every == 0) ||
                     (want_copies && ncopies >= want_copies))) {
                    eye_dump(gx::g_dump_dir, presented);
                    if (!g_morphs.empty()) {
                        float panel[16];
                        morph_panel(panel);
                        for (float m : g_morphs) {
                            gx::render_set_vr_morph(m, panel);
                            gx::render_execute_eye(*b, g_eye_fbo, g_eye_w, g_eye_h, false);
                            char suffix[16];
                            snprintf(suffix, sizeof(suffix), "_m%04d", (int)lroundf(m * 1000.0f));
                            eye_dump(gx::g_dump_dir, presented, suffix);
                        }
                        gx::render_set_vr_morph(1.0f, nullptr);
                        // The flat frame, last: it runs the batch again, copies and all.
                        gx::render_set_output_fbo(g_flat_fbo);
                        gx::render_execute(*b);
                        gx::render_set_output_fbo(0);
                        char path[512];
                        snprintf(path, sizeof(path), "%s/flat_%05u.png", gx::g_dump_dir, presented);
                        dump_fbo(g_flat_fbo, 640, 480, path);
                    }
                }
            } else if (theater_stereo_m > 0.0f &&
                       (!vr::game_hooks().wants_panel_pair || vr::game_hooks().wants_panel_pair(*b))) {
                // GCN_THEATER_STEREO=<metres>: theater as the app draws it with
                // theater_stereo, two flat passes a frame, for its cost on the device. A
                // frame the game wants flat (wants_panel_pair) is drawn flat, as the app does.
                const float u = g_vrcfg.units_per_metre;
                if (gx::render_execute_stereo_pair(*b, 0, 0, theater_stereo_m * g_vrcfg.theater_depth * u, 3.2f * u))
                    presented++;
                glFlush();
            } else {
                if (gx::render_execute(*b)) presented++;
                glFlush();  // as the eye path does; see there
            }
        }

        const auto now = clock::now();
        const double elapsed = std::chrono::duration<double>(now - t0).count();
        if (std::chrono::duration<double>(now - t_mark).count() >= 2.0) {
            printf("  %4.0fs  presented %u (+%u)\n", elapsed, presented, presented - presented_at_mark);
            gpu_timer_report();
            fflush(stdout);
            presented_at_mark = presented;
            t_mark = now;
        }
        if (frames_wanted && presented >= (uint32_t)frames_wanted) break;
        if (elapsed >= seconds) break;
    }

    printf("done: %u frames presented in %.1f s\n", presented,
           std::chrono::duration<double>(clock::now() - t0).count());
    fflush(stdout);

    // Guest threads are parked in longjmp-based contexts; don't unwind them.
    host_profile_dump();
    plat_exit_now(0);
}
