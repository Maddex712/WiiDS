// The OpenXR frontend's core: see xr_frontend.h for how a platform drives it.
//
// Theater is an XrCompositionLayerQuad: the compositor is handed one flat texture and
// places it in space, reprojecting it at display rate, so head tracking stays smooth
// however slowly the game renders. Stereo is an XrCompositionLayerProjection drawn per eye
// from the same batch (render_execute_eye). Which of the two is shown is the game's call,
// through vr::GameHooks::wants_stereo; a game without that hook leaves it to the viewer.
#include "xr_frontend.h"
#include "runtime.h"
#include "platform.h"
#include "input_script.h"
#include "vr_game.h"

#include "gx/render.h"
#include "gx/render_gl.h"
#include "gx/gl_msrtt.h"
#include "hw/pad.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <unordered_map>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, GCN_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, GCN_LOG_TAG, __VA_ARGS__)
#else
#define LOGI(fmt, ...) fprintf(stderr, "[xr] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[xr] error: " fmt "\n", ##__VA_ARGS__)
#endif

void audio_ring_stats(int* underruns, int* min_fill_ms);   // runtime/audio.cpp

namespace xrf {
namespace {

// Where the theater panel hangs and how big it is: a cinema-sized 4:3 screen, matching
// the game's own shape, 2.5 m ahead of where the viewer started.
constexpr float kQuadDist = 2.5f;
constexpr float kQuadW = 3.2f, kQuadH = 2.4f;

// The console's field rate (NTSC), which the guest's frames are paced on.
constexpr float kFieldHz = 59.94f;

// Rendering an eye at the headset's own resolution and then dropping back to a panel
// drawn at the GameCube's is the jarring part of leaving stereo, so the panel is given as
// many texels as the headset can actually resolve across it -- see create_swapchains.
// Starts at what it used to be fixed at, in case the sizing finds nothing to work from.
int g_swap_w = 1024, g_swap_h = 768;
int64_t g_panel_format = 0;   // the swapchains' format, which the mirror's view matches

bool xr_ok(XrResult r, const char* what) {
    if (XR_SUCCEEDED(r)) return true;
    LOGE("%s failed: %d", what, (int)r);
    return false;
}
#define XR_TRY(expr) do { if (!xr_ok((expr), #expr)) return false; } while (0)

// XrSwapchainImageOpenGLKHR and XrSwapchainImageOpenGLESKHR, which are laid out alike.
struct SwapImage {
    XrStructureType type;
    void* next;
    uint32_t image;
};

struct Xr {
    Platform plat;
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    bool has_perf_settings = false;   // XR_EXT_performance_settings
    bool has_refresh_rate = false;    // XR_FB_display_refresh_rate
    XrSpace space = XR_NULL_HANDLE;
    XrSwapchain swapchain = XR_NULL_HANDLE;          // the theater quad
    std::vector<SwapImage> images;
    std::vector<GLuint> fbos;
    // The right eye's panel, when theater is a stereo pair (theater_stereo); the one
    // above is then the left eye's.
    XrSwapchain swapchain_r = XR_NULL_HANDLE;
    std::vector<SwapImage> images_r;
    std::vector<GLuint> fbos_r;

    // One swapchain per eye for the stereo projection layer.
    struct Eye {
        XrSwapchain handle = XR_NULL_HANDLE;
        int32_t w = 0, h = 0;
        std::vector<SwapImage> images;
        std::vector<GLuint> fbos;
    } eyes[2];
    GLuint eye_depth = 0;   // shared: the eyes render one after the other
#ifndef GCN_GL_ES
    // Desktop GL has no multisampled render-to-texture, so an eye is drawn into a target of
    // its own -- multisampled if asked -- and copied into the swapchain image afterwards,
    // through `resolve` when the target is multisampled. Shared by both eyes, like the depth.
    GLuint eye_fbo = 0, eye_color = 0;
    GLuint resolve_fbo = 0, resolve_tex = 0;
    int eye_samples = 0;
#endif
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false;
    bool exiting = false;   // the runtime is done with us: the session exited or was lost

    XrActionSet action_set = XR_NULL_HANDLE;
    XrAction a_btn, b_btn, x_btn, y_btn, menu, trig_l, trig_r, grip_l, grip_r, stick_l, stick_r;
    XrAction toggle;   // right thumbstick click: the game's camera or its first person, in stereo
    XrAction left_click;      // left thumbstick click: the game's (vr::GameHooks::left_click)
    XrAction aim;             // each controller's aim pose, for the game (vr::hand_pose)
    XrPath hand_paths[2] = {XR_NULL_PATH, XR_NULL_PATH};
    XrSpace aim_spaces[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
};
Xr g_xr;
VrConfig g_vrcfg;

// Where the head was when first person began, in metres in the reference space; the eye
// is placed relative to that rather than to the space's own origin. The space is LOCAL,
// whose origin is wherever the head was at launch, so without this the viewer's posture
// since then lands in the view -- multiplied by units_per_metre. Behind a chase camera
// a 30 cm lean is lost in the distance; on an eye placed close to the player's own model
// it can put the viewer inside it. Re-zeroed every time first person is entered.
float g_head_zero[3];
bool g_head_zeroed = false;

void set_first_person(bool on) {
    if (const auto hook = vr::game_hooks().set_first_person) hook(on, g_vrcfg);
}

// The viewer's choice of the game's camera or its first-person eye, kept across sessions in
// <state>/view.txt, which the app writes whenever the choice changes. It is a separate file
// from vr.txt so that the app never rewrites what a person edits by hand; vr.txt's
// first_person is only the choice before one has been made.
std::string g_view_path;

bool view_pref_load(bool fallback) {
    FILE* f = fopen(g_view_path.c_str(), "r");
    if (!f) return fallback;
    int v = fallback ? 1 : 0;
    if (fscanf(f, "first_person %d", &v) != 1) v = fallback ? 1 : 0;
    fclose(f);
    return v != 0;
}

void view_pref_save(bool first_person) {
    if (FILE* f = fopen(g_view_path.c_str(), "w")) {
        fprintf(f, "first_person %d\n", first_person ? 1 : 0);
        fclose(f);
    }
}

// ---------------------------------------------------------------------------
// Matrices, column-major for glUniformMatrix4fv with transpose = GL_FALSE.
// ---------------------------------------------------------------------------
void mat_proj(const XrFovf& fov, float nearZ, float farZ, float* m) {
    const float l = tanf(fov.angleLeft), r = tanf(fov.angleRight);
    const float u = tanf(fov.angleUp), d = tanf(fov.angleDown);
    const float w = r - l, h = u - d;
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / w;
    m[5] = 2.0f / h;
    m[8] = (r + l) / w;
    m[9] = (u + d) / h;
    m[10] = -(farZ + nearZ) / (farZ - nearZ);
    m[11] = -1.0f;
    m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
}

// World-to-eye for a view-space vertex. The game's camera is treated as the origin of
// the reference space, so head rotation looks around from wherever the game's camera
// is, and the eye offset gives the stereo separation. Positions are converted from
// metres into game units on the way in, after taking off `zero`, a head position in
// metres that counts as the origin instead -- see g_head_zero.
void mat_view(const XrPosef& pose, const VrConfig& c, const float zero[3], float* m) {
    const XrQuaternionf& q = pose.orientation;
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    // R in column-major: r[col * 3 + row].
    const float r[9] = {
        1 - 2 * (yy + zz), 2 * (xy + wz),     2 * (xz - wy),
        2 * (xy - wz),     1 - 2 * (xx + zz), 2 * (yz + wx),
        2 * (xz + wy),     2 * (yz - wx),     1 - 2 * (xx + yy),
    };
    const float t[3] = {
        (pose.position.x - zero[0]) * c.units_per_metre + c.offset_x,
        (pose.position.y - zero[1]) * c.units_per_metre + c.offset_y,
        (pose.position.z - zero[2]) * c.units_per_metre + c.offset_z,
    };
    // m = transpose(R) * translate(-t), i.e. the inverse of the eye's pose.
    m[0] = r[0]; m[1] = r[3]; m[2] = r[6]; m[3] = 0;
    m[4] = r[1]; m[5] = r[4]; m[6] = r[7]; m[7] = 0;
    m[8] = r[2]; m[9] = r[5]; m[10] = r[8]; m[11] = 0;
    m[12] = -(m[0] * t[0] + m[4] * t[1] + m[8] * t[2]);
    m[13] = -(m[1] * t[0] + m[5] * t[1] + m[9] * t[2]);
    m[14] = -(m[2] * t[0] + m[6] * t[1] + m[10] * t[2]);
    m[15] = 1;
}

// ---------------------------------------------------------------------------
// The viewer's view of the theater panel, for the mirror (Platform::mirror_view). The
// compositor draws the quad layer itself, so nothing the app holds shows it; this draws
// it again the same way -- the panel's last image on a quad kQuadDist ahead in the
// reference space, seen through the left eye's pose and field of view, on black -- into
// an eye-sized target of its own. The panel's image is copied out as it is drawn
// (keep_panel), since a swapchain image is the runtime's once released.
// ---------------------------------------------------------------------------
struct ViewMirror {
    GLuint panel_tex = 0, panel_fbo = 0;   // the panel's last image
    GLuint view_tex = 0, view_fbo = 0;     // what the eye sees
    int view_w = 0, view_h = 0;
    GLuint prog = 0, vao = 0, vbo = 0;
    bool failed = false;
    bool have_panel = false;
} g_vm;

void vm_target(GLuint& tex, GLuint& fbo, int w, int h) {
    if (!tex) {
        glGenTextures(1, &tex);
        glGenFramebuffers(1, &fbo);
    }
    // Stored as the swapchain stores it, so copying between them changes no value, and
    // the panel is filtered as the compositor filters it: in linear light.
    const GLenum fmt = g_panel_format == GL_SRGB8_ALPHA8 ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool wants_view_mirror() {
    return g_xr.plat.mirror && g_xr.plat.mirror_view && !g_vm.failed && g_xr.plat.mirror_view();
}

// The panel was just drawn into `fbo`: keep a copy for compose_view.
void keep_panel(GLuint fbo) {
    if (!g_vm.panel_tex) vm_target(g_vm.panel_tex, g_vm.panel_fbo, g_swap_w, g_swap_h);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_vm.panel_fbo);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, g_swap_w, g_swap_h, 0, 0, g_swap_w, g_swap_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_vm.have_panel = true;
}

GLuint vm_shader(GLenum type, const char* body) {
    const std::string src = std::string(GCN_GLSL_VERSION) + body;
    const char* p = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        LOGE("mirror view shader: %s", log);
    }
    return sh;
}

bool vm_program() {
    if (g_vm.prog) return true;
    const GLuint v = vm_shader(GL_VERTEX_SHADER, R"(
layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() { v_uv = a_uv; gl_Position = a_pos; }
)");
    const GLuint f = vm_shader(GL_FRAGMENT_SHADER, R"(
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 o_color;
void main() { o_color = texture(u_tex, v_uv); }
)");
    g_vm.prog = glCreateProgram();
    glAttachShader(g_vm.prog, v);
    glAttachShader(g_vm.prog, f);
    glLinkProgram(g_vm.prog);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(g_vm.prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("mirror view shader did not link; the mirror shows the panel's own image");
        glDeleteProgram(g_vm.prog);
        g_vm.prog = 0;
        g_vm.failed = true;
        return false;
    }
    glGenVertexArrays(1, &g_vm.vao);
    glGenBuffers(1, &g_vm.vbo);
    glBindVertexArray(g_vm.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vm.vbo);
    glBufferData(GL_ARRAY_BUFFER, 4 * 6 * sizeof(float), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(4 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

// Draws the viewer's view at display time `t` and hands it to the mirror.
void compose_view(XrTime t) {
    if (!g_vm.have_panel || !vm_program()) return;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    uint32_t nv = 0;
    XrView views[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = t;
    li.space = g_xr.space;
    if (XR_FAILED(xrLocateViews(g_xr.session, &li, &vs, 2, &nv, views)) || nv != 2 ||
        !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
        return;
    const int w = g_xr.eyes[0].w, h = g_xr.eyes[0].h;
    if (w <= 0 || h <= 0) return;
    if (w != g_vm.view_w || h != g_vm.view_h) {
        vm_target(g_vm.view_tex, g_vm.view_fbo, w, h);
        g_vm.view_w = w;
        g_vm.view_h = h;
    }

    // In metres in the reference space, as the layer is handed to the compositor.
    VrConfig plain = g_vrcfg;
    plain.units_per_metre = 1.0f;
    plain.offset_x = plain.offset_y = plain.offset_z = 0.0f;
    const float zero[3] = {0, 0, 0};
    float P[16], V[16], M[16];
    mat_proj(views[0].fov, 0.05f, 100.0f, P);
    mat_view(views[0].pose, plain, zero, V);
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) sum += P[k * 4 + r] * V[c * 4 + k];
            M[c * 4 + r] = sum;
        }
    // The quad's corners in clip space, with where each falls on the panel's image (whose
    // first row is its bottom, as GL keeps it). The GPU clips what is behind the eye.
    const float hx = 0.5f * kQuadW, hy = 0.5f * kQuadH;
    const float corner[4][4] = {{-hx, -hy, 0, 0}, {hx, -hy, 1, 0}, {-hx, hy, 0, 1}, {hx, hy, 1, 1}};
    float vtx[4][6];
    for (int i = 0; i < 4; i++) {
        const float p[4] = {corner[i][0], corner[i][1], -kQuadDist, 1.0f};
        for (int r = 0; r < 4; r++)
            vtx[i][r] = M[r] * p[0] + M[4 + r] * p[1] + M[8 + r] * p[2] + M[12 + r] * p[3];
        vtx[i][4] = corner[i][2];
        vtx[i][5] = corner[i][3];
    }

    // The renderer sets what each of its passes needs; this puts back what it changes.
    GLint prev_prog = 0, prev_vao = 0, prev_vp[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND),
                    cull = glIsEnabled(GL_CULL_FACE), scissor = glIsEnabled(GL_SCISSOR_TEST),
                    stencil = glIsEnabled(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glBindFramebuffer(GL_FRAMEBUFFER, g_vm.view_fbo);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
#ifndef GCN_GL_ES
    // Decoded as it is sampled, encoded again as it is written: what the compositor does
    // with an sRGB swapchain. Both are no-ops on a linear one.
    glEnable(GL_FRAMEBUFFER_SRGB);
#endif
    glUseProgram(g_vm.prog);
    glUniform1i(glGetUniformLocation(g_vm.prog, "u_tex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_vm.panel_tex);
    glBindVertexArray(g_vm.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vm.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vtx), vtx);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
#ifndef GCN_GL_ES
    glDisable(GL_FRAMEBUFFER_SRGB);
#endif
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(prev_vao);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(prev_prog);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    g_xr.plat.mirror(g_vm.view_fbo, w, h);
}

bool create_instance() {
    std::vector<const char*> exts = g_xr.plat.extensions;
    // Optional ones, enabled only where the runtime has them.
    {
        uint32_t n = 0;
        xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr);
        std::vector<XrExtensionProperties> props(n, {XR_TYPE_EXTENSION_PROPERTIES});
        xrEnumerateInstanceExtensionProperties(nullptr, n, &n, props.data());
        for (const auto& e : props) {
            if (!strcmp(e.extensionName, XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME)) {
                exts.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
                g_xr.has_perf_settings = true;
            }
            if (!strcmp(e.extensionName, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)) {
                exts.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
                g_xr.has_refresh_rate = true;
            }
            for (const char* o : g_xr.plat.optional_extensions)
                if (!strcmp(e.extensionName, o)) exts.push_back(o);
        }
    }

    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    ci.next = g_xr.plat.instance_next;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.enabledExtensionNames = exts.data();
    snprintf(ci.applicationInfo.applicationName, sizeof(ci.applicationInfo.applicationName),
             "%s", GCN_GAME_TITLE);
    strcpy(ci.applicationInfo.engineName, "gcn-recomp");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    XR_TRY(xrCreateInstance(&ci, &g_xr.instance));

    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(g_xr.instance, &ip)))
        LOGI("runtime: %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
             XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

    XrSystemGetInfo sys{XR_TYPE_SYSTEM_GET_INFO};
    sys.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    const XrResult r = xrGetSystem(g_xr.instance, &sys, &g_xr.system);
    if (r == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
        LOGE("the runtime has no headset to offer: is one connected and awake?");
        return false;
    }
    XR_TRY(r);
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystemProperties(g_xr.instance, g_xr.system, &sp)))
        LOGI("system: %s", sp.systemName);
    return true;
}

// display_hz. The console presents on its field rate, 59.94 Hz, so on a display at any
// rate that is not a multiple of it some game frames stay up for more display frames than
// others: at 72 Hz every fifth is shown twice, and whatever moves in the world -- the
// camera turning with the stick -- jerks twelve times a second while the head, which the
// compositor reprojects, stays smooth. A multiple holds every game frame alike: at 120 Hz,
// two display frames each.
void request_refresh_rate() {
    PFN_xrEnumerateDisplayRefreshRatesFB enumerate = nullptr;
    PFN_xrRequestDisplayRefreshRateFB request = nullptr;
    PFN_xrGetDisplayRefreshRateFB current = nullptr;
    xrGetInstanceProcAddr(g_xr.instance, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction*)&enumerate);
    xrGetInstanceProcAddr(g_xr.instance, "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction*)&request);
    xrGetInstanceProcAddr(g_xr.instance, "xrGetDisplayRefreshRateFB", (PFN_xrVoidFunction*)&current);
    if (!enumerate || !request || !current) return;
    uint32_t n = 0;
    if (XR_FAILED(enumerate(g_xr.session, 0, &n, nullptr)) || n == 0) return;
    std::vector<float> rates(n);
    enumerate(g_xr.session, n, &n, rates.data());
    std::string offered;
    for (float r : rates) offered += (offered.empty() ? "" : ", ") + std::to_string((int)(r + 0.5f));
    float now = 0.0f;
    current(g_xr.session, &now);
    const float want = g_vrcfg.display_hz;
    float pick = 0.0f;
    for (float r : rates) {
        const bool ok = want > 0.0f ? fabsf(r - want) < 0.5f
                      : want < 0.0f ? fabsf(r / kFieldHz - roundf(r / kFieldHz)) * kFieldHz < 0.5f
                      : false;
        if (ok && (pick == 0.0f || r < pick)) pick = r;
    }
    if (pick > 0.0f && XR_SUCCEEDED(request(g_xr.session, pick)))
        LOGI("display %.0f Hz, was %.0f (offered %s)", pick, now, offered.c_str());
    else
        LOGI("display stays at %.0f Hz (offered %s; display_hz %.0f)", now, offered.c_str(), want);
}

bool create_session() {
    const void* bind = g_xr.plat.graphics_binding(g_xr.instance, g_xr.system);
    if (!bind) return false;

    XrSessionCreateInfo ci{XR_TYPE_SESSION_CREATE_INFO};
    ci.next = bind;
    ci.systemId = g_xr.system;
    XR_TRY(xrCreateSession(g_xr.instance, &ci, &g_xr.session));

    // perf_boost: the highest CPU and GPU levels, instead of the runtime's governor.
    if (g_xr.has_perf_settings && g_vrcfg.perf_boost) {
        PFN_xrPerfSettingsSetPerformanceLevelEXT set_level = nullptr;
        xrGetInstanceProcAddr(g_xr.instance, "xrPerfSettingsSetPerformanceLevelEXT",
                              (PFN_xrVoidFunction*)&set_level);
        if (set_level) {
            const XrResult cpu = set_level(g_xr.session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_BOOST_EXT);
            const XrResult gpu = set_level(g_xr.session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_BOOST_EXT);
            LOGI("performance levels: boost (cpu %d, gpu %d)", (int)cpu, (int)gpu);
        }
    }
    if (g_xr.plat.session_created) g_xr.plat.session_created(g_xr.instance, g_xr.session);
    if (g_xr.has_refresh_rate) request_refresh_rate();

    XrReferenceSpaceCreateInfo sp{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    sp.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    sp.poseInReferenceSpace.orientation.w = 1.0f;
    XR_TRY(xrCreateReferenceSpace(g_xr.session, &sp, &g_xr.space));
    return true;
}

bool create_swapchains() {
    uint32_t n = 0;
    xrEnumerateSwapchainFormats(g_xr.session, 0, &n, nullptr);
    std::vector<int64_t> formats(n);
    xrEnumerateSwapchainFormats(g_xr.session, n, &n, formats.data());
    int64_t chosen = formats.empty() ? 0 : formats[0];
    for (auto it = g_xr.plat.formats.rbegin(); it != g_xr.plat.formats.rend(); ++it)
        if (std::find(formats.begin(), formats.end(), *it) != formats.end()) chosen = *it;
#ifdef GCN_GL_ES
    // An sRGB swapchain on GL ES (docs/graphics.md, "The swapchain is sRGB"): the renderer
    // draws the game's gamma-encoded colour straight into the swapchain images, and ES
    // encodes every write to an sRGB image unless EXT_sRGB_write_control turns that off.
    // Without the extension the linear format is the one that shows the game unchanged
    // (too bright, but not twice encoded).
    if (chosen == GL_SRGB8_ALPHA8) {
        bool write_control = false;
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n && !write_control; i++) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            write_control = e && !strcmp(e, "GL_EXT_sRGB_write_control");
        }
        if (!write_control) {
            LOGE("no GL_EXT_sRGB_write_control: taking a linear swapchain");
            chosen = std::find(formats.begin(), formats.end(), (int64_t)GL_RGBA8) != formats.end()
                         ? GL_RGBA8 : formats[0];
        } else {
            constexpr GLenum kFramebufferSrgbExt = 0x8DB9;
            glDisable(kFramebufferSrgbExt);
            // Proof, since the driver's word is all there is: clear an sRGB target to
            // mid-grey and read it back. Stored as it is, that is 128; encoded, 188.
            GLuint tex = 0, fbo = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            glDisable(GL_SCISSOR_TEST);
            glColorMask(1, 1, 1, 1);
            glClearColor(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            unsigned char px[4] = {0, 0, 0, 0};
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);
            LOGI("sRGB swapchain, encode on write off: mid-grey stored as %u (128 is right, 188 is encoded)", px[0]);
        }
    }
#endif
    g_panel_format = chosen;
    LOGI("swapchain format 0x%llx", (unsigned long long)chosen);

    // What the runtime recommends per eye. Needed before the panel as well as after it:
    // it is the one honest statement this headset makes about its own pixel density.
    uint32_t nv = 0;
    xrEnumerateViewConfigurationViews(g_xr.instance, g_xr.system,
                                      XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nv, nullptr);
    std::vector<XrViewConfigurationView> vcs(nv, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    XR_TRY(xrEnumerateViewConfigurationViews(g_xr.instance, g_xr.system,
                                             XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                             nv, &nv, vcs.data()));
    if (nv < 2) { LOGE("expected 2 views, got %u", nv); return false; }

    // How many texels the panel is worth.
    //
    // It covers 2*atan(1.6/2.5) = 65 degrees of the viewer's horizontal field, and an eye
    // swapchain spans that whole field -- call it a hundred degrees, which is roughly the
    // part of a headset's advertised figure that is in front of one eye. So the panel is
    // worth about two thirds of an eye's width at one texel per display pixel. A little
    // over that, because the compositor samples the panel at whatever angle the head
    // happens to be holding and texels that line up with pixels nowhere are better
    // filtered down than invented.
    //
    // Taken from the recommendation rather than fixed, so a denser headset gets a denser
    // panel: a Quest 3's 1680-wide eyes give 1260x945, against the 1024x768 this was.
    // Width is kept a multiple of four so that three quarters of it stays whole and the
    // panel stays exactly 4:3 -- it is submitted as a 4:3 rectangle, and any other shape
    // would be stretched into it.
    constexpr float kNominalEyeFovDeg = 100.0f;
    constexpr float kPanelOversample = 1.15f;
    const float quad_fov_deg = 2.0f * atanf(0.5f * kQuadW / kQuadDist) * 180.0f / 3.14159265f;
    const int panel_w = (int)(vcs[0].recommendedImageRectWidth *
                              (quad_fov_deg / kNominalEyeFovDeg) * kPanelOversample) & ~3;
    if (panel_w > g_swap_w) {
        g_swap_w = panel_w;
        g_swap_h = panel_w * 3 / 4;
    }

    auto enumerate = [](XrSwapchain sc, std::vector<SwapImage>& images) -> bool {
        uint32_t n = 0;
        xrEnumerateSwapchainImages(sc, 0, &n, nullptr);
        images.assign(n, SwapImage{g_xr.plat.image_type, nullptr, 0});
        XR_TRY(xrEnumerateSwapchainImages(sc, n, &n, (XrSwapchainImageBaseHeader*)images.data()));
        return true;
    };

    // The panel's swapchain, with one framebuffer per image so the renderer can blit
    // straight in. Two of them when theater is a stereo pair.
    auto make_panel = [&](XrSwapchain& sc, std::vector<SwapImage>& images,
                          std::vector<GLuint>& fbos, const char* what) -> bool {
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        ci.format = chosen;
        ci.sampleCount = 1;
        ci.width = g_swap_w;
        ci.height = g_swap_h;
        ci.faceCount = 1;
        ci.arraySize = 1;
        ci.mipCount = 1;
        XR_TRY(xrCreateSwapchain(g_xr.session, &ci, &sc));
        if (!enumerate(sc, images)) return false;

        const uint32_t n = (uint32_t)images.size();
        fbos.resize(n);
        glGenFramebuffers(n, fbos.data());
        for (uint32_t i = 0; i < n; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   images[i].image, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                LOGE("%s swapchain fbo %u incomplete", what, i);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        LOGI("%s swapchain %dx%d, %u images", what, g_swap_w, g_swap_h, n);
        return true;
    };
    if (!make_panel(g_xr.swapchain, g_xr.images, g_xr.fbos, "quad")) return false;
    if (g_vrcfg.theater_stereo &&
        !make_panel(g_xr.swapchain_r, g_xr.images_r, g_xr.fbos_r, "right quad"))
        return false;

    // Per-eye swapchains for the stereo projection layer, at what the runtime recommends
    // for this headset times `eye_scale`.
    for (int e = 0; e < 2; e++) {
        auto& eye = g_xr.eyes[e];
        // `eye_scale` multiplies the recommendation, within what the runtime will take.
        const float es = g_vrcfg.eye_scale > 0.0f ? g_vrcfg.eye_scale : 1.0f;
        eye.w = (int32_t)fminf(vcs[e].recommendedImageRectWidth * es,
                               (float)vcs[e].maxImageRectWidth);
        eye.h = (int32_t)fminf(vcs[e].recommendedImageRectHeight * es,
                               (float)vcs[e].maxImageRectHeight);
        XrSwapchainCreateInfo ec{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ec.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        ec.format = chosen;
        ec.sampleCount = 1;
        ec.width = eye.w;
        ec.height = eye.h;
        ec.faceCount = 1;
        ec.arraySize = 1;
        ec.mipCount = 1;
        XR_TRY(xrCreateSwapchain(g_xr.session, &ec, &eye.handle));
        if (!enumerate(eye.handle, eye.images)) return false;
        eye.fbos.resize(eye.images.size());
        glGenFramebuffers((GLsizei)eye.fbos.size(), eye.fbos.data());
    }
    const int ew = g_xr.eyes[0].w, eh = g_xr.eyes[0].h;
    int samples = g_vrcfg.msaa > 1 ? g_vrcfg.msaa : 0;

#ifdef GCN_GL_ES
    // Antialiasing, if asked for and available: see gl_msrtt.h for why this way.
    const gx::Msrtt ms = samples ? gx::msrtt_load(g_xr.plat.gl_proc) : gx::Msrtt{};
    if (samples && ms.max_samples < samples) {
        LOGE("msaa %d unavailable (max %d); eyes are not antialiased", samples, ms.max_samples);
        samples = 0;
    }

    // One depth buffer, shared: the eyes are rendered in sequence, and it is cleared
    // for each. Both eyes use the same recommended size.
    glGenRenderbuffers(1, &g_xr.eye_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_xr.eye_depth);
    if (samples)
        ms.renderbuffer_storage(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, ew, eh);
    else
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, ew, eh);
    for (int e = 0; e < 2; e++) {
        for (size_t i = 0; i < g_xr.eyes[e].fbos.size(); i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_xr.eyes[e].fbos[i]);
            if (samples)
                ms.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                          g_xr.eyes[e].images[i].image, 0, samples);
            else
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                       g_xr.eyes[e].images[i].image, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                      g_xr.eye_depth);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                LOGE("eye %d fbo %zu incomplete", e, i);
        }
    }
#else
    // The swapchain images only receive a finished eye, so they need no depth.
    for (int e = 0; e < 2; e++) {
        for (size_t i = 0; i < g_xr.eyes[e].fbos.size(); i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_xr.eyes[e].fbos[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   g_xr.eyes[e].images[i].image, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                LOGE("eye %d fbo %zu incomplete", e, i);
        }
    }
    GLint max_samples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    if (samples > max_samples) {
        LOGE("msaa %d unavailable (max %d); eyes are not antialiased", samples, max_samples);
        samples = 0;
    }
    g_xr.eye_samples = samples;
    // The eyes are drawn in linear RGBA8 whatever the swapchain holds: the renderer's
    // output is the game's own gamma-encoded colour, and the copy into an sRGB swapchain
    // (GL_FRAMEBUFFER_SRGB off) stores it as it is, for the compositor to decode.
    glGenRenderbuffers(1, &g_xr.eye_color);
    glBindRenderbuffer(GL_RENDERBUFFER, g_xr.eye_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, ew, eh);
    glGenRenderbuffers(1, &g_xr.eye_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_xr.eye_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, ew, eh);
    glGenFramebuffers(1, &g_xr.eye_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_xr.eye_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_xr.eye_color);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_xr.eye_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) LOGE("eye fbo incomplete");
    // A multisampled resolve may not change format, so it lands in RGBA8 first.
    if (samples) {
        glGenTextures(1, &g_xr.resolve_tex);
        glBindTexture(GL_TEXTURE_2D, g_xr.resolve_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ew, eh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &g_xr.resolve_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_xr.resolve_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_xr.resolve_tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) LOGE("resolve fbo incomplete");
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    LOGI("eye swapchains %dx%d, msaa %d", ew, eh, samples);
    return true;
}

// The framebuffer an eye is drawn into: the swapchain image itself on GL ES, a target of
// the eyes' own on desktop GL (see Xr::eye_fbo).
GLuint eye_target(int e, uint32_t image) {
#ifdef GCN_GL_ES
    return g_xr.eyes[e].fbos[image];
#else
    (void)e;
    (void)image;
    return g_xr.eye_fbo;
#endif
}

// Finishes an eye drawn into eye_target, w x h of it from the corner: on desktop GL,
// copies it into the swapchain image. Returns a single-sampled framebuffer holding it,
// for the mirror.
GLuint eye_finish(int e, uint32_t image, int w, int h) {
    const auto& eye = g_xr.eyes[e];
#ifdef GCN_GL_ES
    (void)w;
    (void)h;
    return eye.fbos[image];
#else
    GLuint src = g_xr.eye_fbo;
    if (g_xr.eye_samples) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_xr.eye_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_xr.resolve_fbo);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        src = g_xr.resolve_fbo;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, eye.fbos[image]);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return src;
#endif
}

// ---------------------------------------------------------------------------
// Input. The headset's controllers -> GC pad 1.
// ---------------------------------------------------------------------------
XrPath xr_path(const char* s) {
    XrPath p = XR_NULL_PATH;
    xrStringToPath(g_xr.instance, s, &p);
    return p;
}

XrAction make_action(const char* name, const char* label, XrActionType type) {
    XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
    ci.actionType = type;
    strcpy(ci.actionName, name);
    strcpy(ci.localizedActionName, label);
    XrAction a = XR_NULL_HANDLE;
    xrCreateAction(g_xr.action_set, &ci, &a);
    return a;
}

// Suggests one controller's bindings; a runtime that does not know the profile refuses
// it, which costs that controller and nothing else.
bool suggest(const char* profile, const std::vector<std::pair<XrAction, const char*>>& binds) {
    std::vector<XrActionSuggestedBinding> b;
    for (const auto& [action, path] : binds) b.push_back({action, xr_path(path)});
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = xr_path(profile);
    sb.suggestedBindings = b.data();
    sb.countSuggestedBindings = (uint32_t)b.size();
    const XrResult r = xrSuggestInteractionProfileBindings(g_xr.instance, &sb);
    if (XR_FAILED(r)) LOGE("bindings for %s refused: %d", profile, (int)r);
    return XR_SUCCEEDED(r);
}

bool create_actions() {
    XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(si.actionSetName, "gameplay");
    strcpy(si.localizedActionSetName, "Gameplay");
    XR_TRY(xrCreateActionSet(g_xr.instance, &si, &g_xr.action_set));

    g_xr.a_btn   = make_action("a_button", "A", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_xr.b_btn   = make_action("b_button", "B", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_xr.x_btn   = make_action("x_button", "X", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_xr.y_btn   = make_action("y_button", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_xr.menu    = make_action("menu", "Start", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_xr.trig_l  = make_action("trigger_l", "Left trigger", XR_ACTION_TYPE_FLOAT_INPUT);
    g_xr.trig_r  = make_action("trigger_r", "Right trigger", XR_ACTION_TYPE_FLOAT_INPUT);
    g_xr.grip_l  = make_action("grip_l", "D-pad mode", XR_ACTION_TYPE_FLOAT_INPUT);
    g_xr.grip_r  = make_action("grip_r", "Right grip", XR_ACTION_TYPE_FLOAT_INPUT);
    g_xr.stick_l = make_action("stick_l", "Left stick", XR_ACTION_TYPE_VECTOR2F_INPUT);
    g_xr.stick_r = make_action("stick_r", "Right stick", XR_ACTION_TYPE_VECTOR2F_INPUT);
    g_xr.toggle  = make_action("view_toggle", "First person", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_xr.left_click = make_action("left_click", "Left stick click", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_xr.hand_paths[vr::kLeftHand] = xr_path("/user/hand/left");
    g_xr.hand_paths[vr::kRightHand] = xr_path("/user/hand/right");
    {
        XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
        ci.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strcpy(ci.actionName, "aim");
        strcpy(ci.localizedActionName, "Aim");
        ci.countSubactionPaths = 2;
        ci.subactionPaths = g_xr.hand_paths;
        XR_TRY(xrCreateAction(g_xr.action_set, &ci, &g_xr.aim));
    }

    // The controllers, each laid out as close to a GameCube pad as it allows. Touch first:
    // it is what a Quest is, natively or streamed. A GameCube controller has no stick
    // clicks, so both are free: the right switches views, the left is the game's.
    int profiles = 0;
    profiles += suggest("/interaction_profiles/oculus/touch_controller", {
        {g_xr.a_btn,   "/user/hand/right/input/a/click"},
        {g_xr.b_btn,   "/user/hand/right/input/b/click"},
        {g_xr.x_btn,   "/user/hand/left/input/x/click"},
        {g_xr.y_btn,   "/user/hand/left/input/y/click"},
        {g_xr.menu,    "/user/hand/left/input/menu/click"},
        {g_xr.trig_l,  "/user/hand/left/input/trigger/value"},
        {g_xr.trig_r,  "/user/hand/right/input/trigger/value"},
        {g_xr.grip_l,  "/user/hand/left/input/squeeze/value"},
        {g_xr.grip_r,  "/user/hand/right/input/squeeze/value"},
        {g_xr.stick_l, "/user/hand/left/input/thumbstick"},
        {g_xr.stick_r, "/user/hand/right/input/thumbstick"},
        {g_xr.toggle,  "/user/hand/right/input/thumbstick/click"},
        {g_xr.left_click, "/user/hand/left/input/thumbstick/click"},
        {g_xr.aim,     "/user/hand/left/input/aim/pose"},
        {g_xr.aim,     "/user/hand/right/input/aim/pose"},
    });
    // Index: A and B on each hand, so the left's are X and Y. It has no menu button an app
    // may have, so Start is a firm press on the left trackpad.
    profiles += suggest("/interaction_profiles/valve/index_controller", {
        {g_xr.a_btn,   "/user/hand/right/input/a/click"},
        {g_xr.b_btn,   "/user/hand/right/input/b/click"},
        {g_xr.x_btn,   "/user/hand/left/input/a/click"},
        {g_xr.y_btn,   "/user/hand/left/input/b/click"},
        {g_xr.menu,    "/user/hand/left/input/trackpad/force"},
        {g_xr.trig_l,  "/user/hand/left/input/trigger/value"},
        {g_xr.trig_r,  "/user/hand/right/input/trigger/value"},
        {g_xr.grip_l,  "/user/hand/left/input/squeeze/value"},
        {g_xr.grip_r,  "/user/hand/right/input/squeeze/value"},
        {g_xr.stick_l, "/user/hand/left/input/thumbstick"},
        {g_xr.stick_r, "/user/hand/right/input/thumbstick"},
        {g_xr.toggle,  "/user/hand/right/input/thumbstick/click"},
        {g_xr.left_click, "/user/hand/left/input/thumbstick/click"},
        {g_xr.aim,     "/user/hand/left/input/aim/pose"},
        {g_xr.aim,     "/user/hand/right/input/aim/pose"},
    });
    // Windows Mixed Reality: face buttons are the trackpads and the right menu button.
    profiles += suggest("/interaction_profiles/microsoft/motion_controller", {
        {g_xr.a_btn,   "/user/hand/right/input/trackpad/click"},
        {g_xr.b_btn,   "/user/hand/right/input/menu/click"},
        {g_xr.x_btn,   "/user/hand/left/input/trackpad/click"},
        {g_xr.menu,    "/user/hand/left/input/menu/click"},
        {g_xr.trig_l,  "/user/hand/left/input/trigger/value"},
        {g_xr.trig_r,  "/user/hand/right/input/trigger/value"},
        {g_xr.grip_l,  "/user/hand/left/input/squeeze/click"},
        {g_xr.grip_r,  "/user/hand/right/input/squeeze/click"},
        {g_xr.stick_l, "/user/hand/left/input/thumbstick"},
        {g_xr.stick_r, "/user/hand/right/input/thumbstick"},
        {g_xr.toggle,  "/user/hand/right/input/thumbstick/click"},
        {g_xr.left_click, "/user/hand/left/input/thumbstick/click"},
        {g_xr.aim,     "/user/hand/left/input/aim/pose"},
        {g_xr.aim,     "/user/hand/right/input/aim/pose"},
    });
    // Whatever else the runtime has: select is A, menu is Start. Enough to get through the
    // menus to a race, with a gamepad for the race itself.
    profiles += suggest("/interaction_profiles/khr/simple_controller", {
        {g_xr.a_btn,   "/user/hand/right/input/select/click"},
        {g_xr.menu,    "/user/hand/left/input/menu/click"},
        {g_xr.aim,     "/user/hand/left/input/aim/pose"},
        {g_xr.aim,     "/user/hand/right/input/aim/pose"},
    });
    if (!profiles) {
        LOGE("the runtime took no controller bindings");
        return false;
    }

    XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    ai.countActionSets = 1;
    ai.actionSets = &g_xr.action_set;
    XR_TRY(xrAttachSessionActionSets(g_xr.session, &ai));

    for (int h = 0; h < 2; h++) {
        XrActionSpaceCreateInfo asi{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        asi.action = g_xr.aim;
        asi.subactionPath = g_xr.hand_paths[h];
        asi.poseInActionSpace.orientation.w = 1.0f;
        XR_TRY(xrCreateActionSpace(g_xr.session, &asi, &g_xr.aim_spaces[h]));
    }
    return true;
}

bool action_bool(XrAction a) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_FAILED(xrGetActionStateBoolean(g_xr.session, &gi, &st))) return false;
    return st.isActive && st.currentState;
}

float action_float(XrAction a) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_FAILED(xrGetActionStateFloat(g_xr.session, &gi, &st))) return 0.0f;
    return st.isActive ? st.currentState : 0.0f;
}

XrVector2f action_vec2(XrAction a) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (XR_FAILED(xrGetActionStateVector2f(g_xr.session, &gi, &st))) return {0, 0};
    return st.isActive ? st.currentState : XrVector2f{0, 0};
}

void read_pad(PadState& p) {
    XrActiveActionSet active{g_xr.action_set, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(g_xr.session, &si))) return;

    // GCN_XR_INPUT_LOG=1: every button action's state as the runtime reports it, on change --
    // whether it is active (bound to something present) as well as pressed.
    static const bool log_input = [] { const char* e = getenv("GCN_XR_INPUT_LOG"); return e && *e == '1'; }();
    if (log_input) {
        static int last[7] = {-1, -1, -1, -1, -1, -1, -1};
        const XrAction acts[7] = {g_xr.a_btn, g_xr.b_btn, g_xr.x_btn, g_xr.y_btn, g_xr.menu, g_xr.toggle, g_xr.left_click};
        static const char* names[7] = {"A", "B", "X", "Y", "menu", "right stick click", "left stick click"};
        for (int i = 0; i < 7; i++) {
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = acts[i];
            XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
            const XrResult r = xrGetActionStateBoolean(g_xr.session, &gi, &st);
            const int v = XR_FAILED(r) ? 100 + (int)r : st.isActive * 2 + st.currentState;
            if (v != last[i]) LOGI("input %s: %s", names[i], XR_FAILED(r) ? "error" : !st.isActive ? "inactive" : st.currentState ? "down" : "up");
            last[i] = v;
        }
    }

    if (action_bool(g_xr.a_btn)) p.buttons |= PAD_A;
    if (action_bool(g_xr.b_btn)) p.buttons |= PAD_B;
    if (action_bool(g_xr.x_btn)) p.buttons |= PAD_X;
    if (action_bool(g_xr.y_btn)) p.buttons |= PAD_Y;
    if (action_bool(g_xr.menu))  p.buttons |= PAD_START;

    const float lt = action_float(g_xr.trig_l), rt = action_float(g_xr.trig_r);
    p.trig_l = (uint8_t)(lt * 255.0f);
    p.trig_r = (uint8_t)(rt * 255.0f);
    if (lt > 0.85f) p.buttons |= PAD_L;
    if (rt > 0.85f) p.buttons |= PAD_R;
    if (action_float(g_xr.grip_r) > 0.5f) p.buttons |= PAD_Z;

    auto to_u8 = [](float f) {
        int v = 128 + (int)(f * 100.0f);
        return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    };
    const XrVector2f l = action_vec2(g_xr.stick_l), r = action_vec2(g_xr.stick_r);
    // The Touch controllers have no D-pad, so holding the left grip turns the left
    // thumbstick into one: pushed past halfway, its stronger axis is a D-pad direction, and
    // the control stick reads centred meanwhile so that the press does not also walk.
    if (action_float(g_xr.grip_l) > 0.5f) {
        if (fmaxf(fabsf(l.x), fabsf(l.y)) > 0.5f) {
            if (fabsf(l.x) > fabsf(l.y)) p.buttons |= l.x > 0 ? PAD_RIGHT : PAD_LEFT;
            else p.buttons |= l.y > 0 ? PAD_UP : PAD_DOWN;
        }
        p.stick_x = p.stick_y = 128;
    } else {
        p.stick_x = to_u8(l.x);
        p.stick_y = to_u8(l.y);
    }
    p.cstick_x = to_u8(r.x);
    p.cstick_y = to_u8(r.y);
}

// Where the controllers are, for the game (vr::hand_pose): located for the time the frame
// will be shown, like the eyes, and converted the way mat_view converts them. Withdrawn
// whenever the eyes are not being drawn.
void publish_hands(XrTime t, bool on) {
    for (int h = 0; h < 2; h++) {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if (!on || XR_FAILED(xrLocateSpace(g_xr.aim_spaces[h], g_xr.space, t, &loc)) ||
            (loc.locationFlags & need) != need) {
            vr::set_hand_pose((vr::Hand)h, nullptr);
            continue;
        }
        const XrPosef& p = loc.pose;
        vr::HandPose hp;
        hp.pos[0] = (p.position.x - g_head_zero[0]) * g_vrcfg.units_per_metre + g_vrcfg.offset_x;
        hp.pos[1] = (p.position.y - g_head_zero[1]) * g_vrcfg.units_per_metre + g_vrcfg.offset_y;
        hp.pos[2] = (p.position.z - g_head_zero[2]) * g_vrcfg.units_per_metre + g_vrcfg.offset_z;
        hp.rot[0] = p.orientation.x;
        hp.rot[1] = p.orientation.y;
        hp.rot[2] = p.orientation.z;
        hp.rot[3] = p.orientation.w;
        vr::set_hand_pose((vr::Hand)h, &hp);
    }
}

// What the eyes see, for the game's culling (vr::eye_views), converted the same way.
void publish_eyes(const XrView* views) {
    if (!views) {
        vr::set_eye_views(nullptr);
        return;
    }
    vr::EyeView ev[2];
    for (int e = 0; e < 2; e++) {
        const XrPosef& p = views[e].pose;
        ev[e].pos[0] = (p.position.x - g_head_zero[0]) * g_vrcfg.units_per_metre + g_vrcfg.offset_x;
        ev[e].pos[1] = (p.position.y - g_head_zero[1]) * g_vrcfg.units_per_metre + g_vrcfg.offset_y;
        ev[e].pos[2] = (p.position.z - g_head_zero[2]) * g_vrcfg.units_per_metre + g_vrcfg.offset_z;
        ev[e].rot[0] = p.orientation.x;
        ev[e].rot[1] = p.orientation.y;
        ev[e].rot[2] = p.orientation.z;
        ev[e].rot[3] = p.orientation.w;
        ev[e].tan_left = tanf(views[e].fov.angleLeft);
        ev[e].tan_right = tanf(views[e].fov.angleRight);
        ev[e].tan_up = tanf(views[e].fov.angleUp);
        ev[e].tan_down = tanf(views[e].fov.angleDown);
    }
    vr::set_eye_views(ev);
}

// ---------------------------------------------------------------------------
void handle_session_state(XrSessionState s) {
    g_xr.state = s;
    LOGI("session state %d", (int)s);
    if (s == XR_SESSION_STATE_READY) {
        XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
        bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        if (XR_SUCCEEDED(xrBeginSession(g_xr.session, &bi))) {
            g_xr.running = true;
            LOGI("session begun");
        }
    } else if (s == XR_SESSION_STATE_STOPPING) {
        xrEndSession(g_xr.session);
        g_xr.running = false;
    } else if (s == XR_SESSION_STATE_EXITING || s == XR_SESSION_STATE_LOSS_PENDING) {
        // The runtime is closing the session for good: the user quit from the runtime's
        // own menu, or the headset went away.
        g_xr.exiting = true;
    }
}

void poll_xr_events() {
    for (;;) {
        XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
        if (xrPollEvent(g_xr.instance, &ev) != XR_SUCCESS) break;
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            handle_session_state(((XrEventDataSessionStateChanged*)&ev)->state);
        else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            LOGE("instance loss pending");
            g_xr.exiting = true;
        } else if (ev.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) {
            // Which of the profiles suggested in create_actions the runtime chose, per hand.
            for (int h = 0; h < 2; h++) {
                XrInteractionProfileState st{XR_TYPE_INTERACTION_PROFILE_STATE};
                char name[XR_MAX_PATH_LENGTH] = "none";
                uint32_t len = 0;
                if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(g_xr.session, g_xr.hand_paths[h], &st)) &&
                    st.interactionProfile != XR_NULL_PATH)
                    xrPathToString(g_xr.instance, st.interactionProfile, sizeof(name), &len, name);
                LOGI("%s hand: %s", h == vr::kLeftHand ? "left" : "right", name);
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT) {
            // The runtime's view of how the app is doing in a domain (compositing,
            // rendering, thermal): normal, warning or impaired. Beside the compositor's own
            // lines in logcat (VrApi FPS=, with the clocks and the GPU's load) and the OS
            // clock governor's (crcs "Clock levels changed"), which is where a boost level
            // being taken away is announced.
            const auto* p = (const XrEventDataPerfSettingsEXT*)&ev;
            auto level = [](XrPerfSettingsNotificationLevelEXT l) {
                return l == XR_PERF_SETTINGS_NOTIF_LEVEL_NORMAL_EXT ? "normal"
                     : l == XR_PERF_SETTINGS_NOTIF_LEVEL_WARNING_EXT ? "warning"
                     : l == XR_PERF_SETTINGS_NOTIF_LEVEL_IMPAIRED_EXT ? "impaired" : "?";
            };
            auto sub = [](XrPerfSettingsSubDomainEXT s) {
                return s == XR_PERF_SETTINGS_SUB_DOMAIN_COMPOSITING_EXT ? "compositing"
                     : s == XR_PERF_SETTINGS_SUB_DOMAIN_RENDERING_EXT ? "rendering"
                     : s == XR_PERF_SETTINGS_SUB_DOMAIN_THERMAL_EXT ? "thermal" : "?";
            };
            LOGI("performance notice: %s %s %s -> %s", p->domain == XR_PERF_SETTINGS_DOMAIN_CPU_EXT ? "cpu" : "gpu",
                 sub(p->subDomain), level(p->fromLevel), level(p->toLevel));
        }
    }
}

// Whether to go on: the platform has not asked to leave and the runtime has not ended
// the session.
bool keep_going() {
    if (g_xr.plat.pump && !g_xr.plat.pump()) return false;
    poll_xr_events();
    return !g_xr.exiting;
}

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

}  // namespace

// ---------------------------------------------------------------------------
const VrConfig& configure(const std::string& config_dir, const std::string& state_dir,
                          void (*platform_defaults)(VrConfig&)) {
    g_vrcfg = vr::load_config(config_dir, platform_defaults);
    g_view_path = state_dir + "/view.txt";
    gx::render_set_world_pitch(g_vrcfg.world_pitch_deg * 3.14159265f / 180.0f);
    gx::render_set_depth_layers(g_vrcfg.background_band, g_vrcfg.foreground_band, g_vrcfg.foreground_scale, g_vrcfg.hud_band, g_vrcfg.hud_band_scale);
    gx::render_set_hud_push(g_vrcfg.hud_push_below, g_vrcfg.hud_push_to);
    gx::render_set_view_space_world(g_vrcfg.view_space_world);
    gx::render_set_eye_filter(vr::game_hooks().eye_filter);
    gx::render_set_panel_band(g_vrcfg.panel_band);
    static std::string dump_dir;
    if (g_vrcfg.dump_every > 0) {
        dump_dir = state_dir + "/frames";
        plat_make_dirs(dump_dir);
        gx::g_dump_dir = dump_dir.c_str();
        gx::g_dump_every = g_vrcfg.dump_every;
        LOGI("dumping every %d frames to %s", g_vrcfg.dump_every, dump_dir.c_str());
    }
    return g_vrcfg;
}

bool init(const Platform& p) {
    g_xr.plat = p;
    if (!create_instance()) { LOGE("no OpenXR instance"); return false; }
    if (!create_session()) { LOGE("no OpenXR session"); return false; }
    if (!create_swapchains()) { LOGE("no swapchain"); return false; }
    if (!create_actions()) { LOGE("no actions"); return false; }
    return true;
}

void panel_size(int* w, int* h) {
    *w = g_swap_w;
    *h = g_swap_h;
}

// The shader cache, built while the app keeps answering. After a change to the shaders every
// program in it is compiled again -- 6,000 programs, 25 s on a Quest 3 -- and done in one go
// on this thread the app answered neither the platform nor the runtime meanwhile: the
// viewer saw the runtime's waiting room and then "not responding". So it is built a slice
// at a time (render_shader_cache_step), with the platform's events and the runtime's frames
// kept up between slices and a progress bar on the theater panel, drawn with clears alone
// since no shader can be assumed yet -- the line above it too, a scissored clear for each
// run of lit pixels in a 5x7 font.
struct Glyph {
    char c;
    uint8_t rows[7];  // top first; bit 4 is the leftmost column
};
static const Glyph kGlyphs[] = {
    {'C', {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}},
    {'L', {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},
    {'a', {0b00000, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111}},
    {'d', {0b00001, 0b00001, 0b01101, 0b10011, 0b10001, 0b10001, 0b01111}},
    {'e', {0b00000, 0b00000, 0b01110, 0b10001, 0b11111, 0b10000, 0b01110}},
    {'f', {0b00110, 0b01001, 0b01000, 0b11100, 0b01000, 0b01000, 0b01000}},
    {'g', {0b00000, 0b01111, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110}},
    {'h', {0b10000, 0b10000, 0b10110, 0b11001, 0b10001, 0b10001, 0b10001}},
    {'i', {0b00100, 0b00000, 0b01100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'l', {0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'m', {0b00000, 0b00000, 0b11010, 0b10101, 0b10101, 0b10001, 0b10001}},
    {'n', {0b00000, 0b00000, 0b10110, 0b11001, 0b10001, 0b10001, 0b10001}},
    {'o', {0b00000, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'p', {0b00000, 0b00000, 0b11110, 0b10001, 0b11110, 0b10000, 0b10000}},
    {'r', {0b00000, 0b00000, 0b10110, 0b11001, 0b10000, 0b10000, 0b10000}},
    {'s', {0b00000, 0b00000, 0b01110, 0b10000, 0b01110, 0b00001, 0b11110}},
    {'t', {0b01000, 0b01000, 0b11100, 0b01000, 0b01000, 0b01001, 0b00110}},
    {'u', {0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b10011, 0b01101}},
    {'y', {0b00000, 0b00000, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110}},
    {'.', {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100}},
    {'(', {0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100, 0b00010}},
    {')', {0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100, 0b01000}},
};

// `s` centred on x = cx with its bottom at y = bottom (GL's, up), in pixels `px` square,
// in the current clear colour. A character without a glyph is a space.
static void clear_text(const char* s, int cx, int bottom, int px) {
    const int n = (int)strlen(s);
    const int x0 = cx - n * 6 * px / 2;
    for (int i = 0; i < n; i++) {
        const Glyph* g = nullptr;
        for (const Glyph& gl : kGlyphs)
            if (gl.c == s[i]) g = &gl;
        if (!g) continue;
        for (int r = 0; r < 7; r++) {
            const int y = bottom + (6 - r) * px;
            for (int c = 0; c < 5;) {
                if (!((g->rows[r] >> (4 - c)) & 1)) { c++; continue; }
                int e = c;
                while (e < 5 && ((g->rows[r] >> (4 - e)) & 1)) e++;
                glScissor(x0 + (i * 6 + c) * px, y, (e - c) * px, px);
                glClear(GL_COLOR_BUFFER_BIT);
                c = e;
            }
        }
    }
}

bool build_shaders() {
    const auto t0 = std::chrono::steady_clock::now();
    // Decided once: from render_init on, the cache knows whether it holds binaries to take.
    const char* message = gx::render_shader_cache_compiling() ? "Compiling shaders... (first time startup only)"
                                                              : "Loading shaders...";
    int done = 0, total = 0;
    bool finished = false;
    while (!finished) {
        if (!keep_going()) return false;
        finished = gx::render_shader_cache_step(40.0, &done, &total);
        if (finished || !g_xr.running) {
            if (!g_xr.running && !finished) sleep_ms(1);
            continue;
        }
        XrFrameState fs{XR_TYPE_FRAME_STATE};
        XrFrameWaitInfo fw{XR_TYPE_FRAME_WAIT_INFO};
        if (XR_FAILED(xrWaitFrame(g_xr.session, &fw, &fs))) continue;
        XrFrameBeginInfo fb{XR_TYPE_FRAME_BEGIN_INFO};
        xrBeginFrame(g_xr.session, &fb);
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
        bool drew = false;
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if (fs.shouldRender && XR_SUCCEEDED(xrAcquireSwapchainImage(g_xr.swapchain, &ai, &idx))) {
            XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wi.timeout = XR_INFINITE_DURATION;
            if (XR_SUCCEEDED(xrWaitSwapchainImage(g_xr.swapchain, &wi))) {
                const int w = g_swap_w, h = g_swap_h;
                const int bw = w * 6 / 10, bh = h / 24, bx = (w - bw) / 2, by = (h - bh) / 2;
                const int fill = total > 0 ? (int)((int64_t)bw * done / total) : 0;
                glBindFramebuffer(GL_FRAMEBUFFER, g_xr.fbos[idx]);
                glViewport(0, 0, w, h);
                glDisable(GL_SCISSOR_TEST);
                glColorMask(1, 1, 1, 1);
                glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glEnable(GL_SCISSOR_TEST);
                glScissor(bx - 3, by - 3, bw + 6, bh + 6);  // the frame
                glClearColor(0.35f, 0.4f, 0.45f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glScissor(bx, by, bw, bh);                  // the track
                glClearColor(0.06f, 0.07f, 0.08f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                if (fill > 0) {
                    glScissor(bx, by, fill, bh);            // what is built
                    glClearColor(0.2f, 0.6f, 0.9f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);
                }
                const int px = std::max(1, std::min(h / 100, bw / (6 * (int)strlen(message))));
                glClearColor(0.8f, 0.82f, 0.85f, 1.0f);
                clear_text(message, w / 2, by + bh + 3 + 2 * px * 3, px);
                glDisable(GL_SCISSOR_TEST);
                if (g_xr.plat.mirror) g_xr.plat.mirror(g_xr.fbos[idx], w, h);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glFlush();
                drew = true;
            }
            XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xrReleaseSwapchainImage(g_xr.swapchain, &ri);
        }
        XrFrameEndInfo fe{XR_TYPE_FRAME_END_INFO};
        fe.displayTime = fs.predictedDisplayTime;
        fe.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        const XrCompositionLayerBaseHeader* layer = (const XrCompositionLayerBaseHeader*)&quad;
        if (drew) {
            quad.space = g_xr.space;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = g_xr.swapchain;
            quad.subImage.imageRect = {{0, 0}, {g_swap_w, g_swap_h}};
            quad.pose.orientation = {0, 0, 0, 1};
            quad.pose.position = {0, 0, -kQuadDist};
            quad.size = {kQuadW, kQuadH};
            fe.layerCount = 1;
            fe.layers = &layer;
        }
        xrEndFrame(g_xr.session, &fe);
    }
    LOGI("shaders built: %d in %.1f s", total,
         std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return true;
}

// GCN_SCHEDLOG=1: once a second, for each named thread that ran at least 5 ms of it, how
// long it ran, how long it waited runnable for a core, and the core it last ran on --
// what tells a stage that is slow from one the OS is not giving a core to.
void log_thread_sched() {
    static const bool on = getenv("GCN_SCHEDLOG") != nullptr;
    if (!on) return;
    struct Prev { uint64_t run, wait; };
    static std::unordered_map<long, Prev> prev;
    std::string line;
    for (const PlatThread& t : plat_threads()) {
        uint64_t run, wait;
        int cpu;
        if (!plat_thread_sched(t.tid, &run, &wait, &cpu)) continue;
        Prev& p = prev[t.tid];
        const uint64_t dr = run - p.run, dw = wait - p.wait;
        const bool first = p.run == 0 && p.wait == 0;
        p = {run, wait};
        if (first || dr < 5000000) continue;
        char buf[96];
        snprintf(buf, sizeof(buf), " %s %llu/%llu@%d", t.name, (unsigned long long)(dr / 1000000),
                 (unsigned long long)(dw / 1000000), cpu);
        line += buf;
    }
    if (!line.empty()) LOGI("threads (run/waiting ms @core):%s", line.c_str());
}

// Dynamic resolution (gpu_budget_ms). A scene can cost the GPU many times its usual
// work for a second or two -- a burst of large translucent effects around the camera is
// overdraw on every one of an eye's pixels, at many times the console's resolution -- and
// a GPU that falls behind holds up the render thread, the threads feeding it and at last
// the guest, so the game slows and its sound runs dry. The eyes' GPU time is measured
// every game frame (timer queries, read a few frames late from a ring so that measuring
// stalls nothing), and when a frame goes over the budget the next ones are drawn into a
// smaller part of each eye's image, which the projection layer tells the compositor to
// scale up. The size drops at once to what would have fitted, taking the cost to go with
// the pixel count, and comes back a step at a time once there is room.
struct EyeResolution {
    float scale = 1.0f;         // of each side
    double sum_ms = 0.0, max_ms = 0.0, scale_sum = 0.0, scale_min = 1.0;
    uint32_t n = 0;

    bool init(GLLoadProc proc) {
        if (g_vrcfg.gpu_budget_ms <= 0.0f) return false;
        query_u64 = (void (*)(GLuint, GLenum, uint64_t*))proc("glGetQueryObjectui64vEXT");
        if (!query_u64) query_u64 = (void (*)(GLuint, GLenum, uint64_t*))proc("glGetQueryObjectui64v");
        if (!query_u64) {
            LOGI("dynamic resolution off: no timer queries");
            return false;
        }
        glGenQueries(kRing, queries);
        on = true;
        LOGI("dynamic resolution: eyes' GPU budget %.1f ms, down to %.2f of each side",
             g_vrcfg.gpu_budget_ms, g_vrcfg.min_eye_res);
        return true;
    }
    void begin() {
        if (!on) return;
        if (live == kRing) collect(true);
        query_scale[head] = scale;
        glBeginQuery(kTimeElapsed, queries[head]);
    }
    void end() {
        if (!on) return;
        glEndQuery(kTimeElapsed);
        head = (head + 1) % kRing;
        live++;
        collect(false);
    }
    // The eye's size this frame, in multiples of 16 texels.
    int size(int full) const {
        const int s = ((int)(full * scale) + 8) & ~15;
        return std::max(16, std::min(full, s));
    }
    void report_reset() { sum_ms = max_ms = scale_sum = 0.0; scale_min = 1.0; n = 0; }

private:
    static constexpr int kRing = 8;
    static constexpr GLenum kTimeElapsed = 0x88BF;   // GL_TIME_ELAPSED(_EXT)
    bool on = false;
    void (*query_u64)(GLuint, GLenum, uint64_t*) = nullptr;
    GLuint queries[kRing] = {};
    float query_scale[kRing] = {};   // the scale each query's frame was drawn at
    int head = 0, live = 0;
    uint32_t since_change = 0;

    void collect(bool all) {
        // A disjoint event (the GPU changing clock) spoils the queries in flight.
        GLint disjoint = 0;
        glGetIntegerv(0x8FBB /* GL_GPU_DISJOINT_EXT */, &disjoint);
        while (live > 0) {
            const int i = (head - live + kRing) % kRing;
            GLuint ready = 0;
            if (!all) glGetQueryObjectuiv(queries[i], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (!all && !ready) break;
            uint64_t ns = 0;
            query_u64(queries[i], GL_QUERY_RESULT, &ns);
            live--;
            if (!disjoint && ns < 1000000000ull) adjust(ns * 1e-6, query_scale[i]);
        }
    }
    void adjust(double ms, float at) {
        sum_ms += ms;
        max_ms = std::max(max_ms, ms);
        scale_sum += scale;
        scale_min = std::min<double>(scale_min, scale);
        n++;
        since_change++;
        const double budget = g_vrcfg.gpu_budget_ms;
        const float floor = std::max(0.1f, std::min(1.0f, g_vrcfg.min_eye_res));
        if (ms > budget) {
            // What would have fitted, from the size this frame was drawn at.
            const float fit = at * (float)sqrt(budget / ms) * 0.95f;
            if (fit < scale) {
                scale = std::max(floor, fit);
                since_change = 0;
            }
        } else if (scale < 1.0f && since_change >= 30) {
            const float up = std::min(1.0f, scale + 0.05f);
            if (ms * (up / at) * (up / at) < 0.85 * budget) {
                scale = up;
                since_change = 0;
            }
        }
    }
};

void run() {
    uint32_t xr_frames = 0, game_frames = 0, skipped = 0;
    // For the stats line: the longest wait between game frames, and the render thread's
    // time for each stereo frame, in milliseconds.
    double gap_max = 0, eyes_sum = 0, eyes_max = 0;
    uint32_t eyes_n = 0;
    auto last_game_frame = std::chrono::steady_clock::now();
    uint64_t disp_frames = 0;  // monotonic, unlike xr_frames which the stats line resets
    // Cadence, for the stats line: how many display frames each game frame stayed up, as
    // counts of 1, 2, 3 and 4 or more. Even pacing is all one column.
    uint32_t held = 0, holds[4] = {};
    EyeResolution eye_res;
    eye_res.init(g_xr.plat.gl_proc);
    int underruns_reported = 0;
    bool have_content = false;
    bool stereo = g_vrcfg.start_in_stereo, toggle_was_down = false, left_click_was_down = false;
    const auto wants_stereo = vr::game_hooks().wants_stereo;
    LOGI("stereo: %s", wants_stereo ? "the game decides" : stereo ? "always" : "never");
    // `first_person` is the viewer's choice; it is *in effect* only in stereo. Leaving
    // stereo drops back to the game's camera at once, before the morph to theater begins:
    // the morph folds the world onto the panel the game's own camera drew, and starting it
    // from somewhere else swings the whole scene across to that camera's view on the way.
    // Entering stereo picks the choice up again.
    bool first_person = view_pref_load(g_vrcfg.first_person);
    if (vr::game_hooks().set_first_person)
        LOGI("view preference: %s", first_person ? "first person" : "the game's camera");
    auto apply_view = [&]() {
        set_first_person(first_person && stereo);
        g_head_zeroed = false;
    };
    apply_view();
    // Kept across frames: a display frame with no new game frame re-submits these
    // rather than re-rendering. They carry the pose each image was rendered for, so
    // the compositor reprojects them for the current head pose.
    XrCompositionLayerProjectionView proj_views[2]{};
    bool have_proj = false;
    // Where the presentation stands between theater (0) and stereo (1). `stereo` says where
    // it is going; this follows it over `transition_s`. Anywhere above 0 the eyes are
    // rendered, with the stereo view folded part way back onto the theater panel -- see
    // render_set_vr_morph -- so the panel is only shown again once it is all the way home.
    float morph = stereo ? 1.0f : 0.0f;
    XrTime last_display = 0;
    // Whether the panel's swapchain holds a frame drawn since the eyes last took over.
    // Until it does, the eyes' last image stays up: it was rendered almost exactly flat,
    // and the panel's alternative is whatever it showed before stereo began.
    bool quad_fresh = !stereo;
    // Whether the panel's last frame was drawn as a stereo pair (theater_stereo), so the
    // right swapchain holds its other half.
    bool quad_pair = false;
    // The panel in the eye's space, in game units: where theater hangs it, seen from where
    // mat_view puts the eye. It is in the headset's room, not the game's, so it takes the
    // viewpoint offset and nothing of the world's pitch.
    float panel[16] = {0};
    {
        const float u = g_vrcfg.units_per_metre;
        panel[0] = 0.5f * kQuadW * u;
        panel[5] = 0.5f * kQuadH * u;
        panel[12] = g_vrcfg.offset_x;
        panel[13] = g_vrcfg.offset_y;
        panel[14] = g_vrcfg.offset_z - kQuadDist * u;
        panel[15] = 1.0f;
    }
    XrTime last_report = 0;

    while (keep_going()) {
        if (!g_xr.running) { sleep_ms(10); continue; }

        XrFrameState fs{XR_TYPE_FRAME_STATE};
        XrFrameWaitInfo fw{XR_TYPE_FRAME_WAIT_INFO};
        if (XR_FAILED(xrWaitFrame(g_xr.session, &fw, &fs))) continue;
        XrFrameBeginInfo fb{XR_TYPE_FRAME_BEGIN_INFO};
        xrBeginFrame(g_xr.session, &fb);

        // Where the frame hangs is a question about the viewer, so `log_frames 1` reports
        // the viewer: the frame sits on the forward axis of the LOCAL space, and that axis
        // is eye level only if the runtime fixed the space while the headset was being
        // worn. A head well above y=0 slides the frame down the view without anything in
        // the frame's own geometry being wrong, and from inside the headset the two look
        // identical. This runs wherever the app is -- a menu will do -- so the number does
        // not cost a drive to the start line.
        if (g_vrcfg.log_frames) {
            static uint32_t nlog;
            if ((nlog++ % 72) == 0) {
                XrViewState lvs{XR_TYPE_VIEW_STATE};
                uint32_t lnv = 0;
                XrView lv[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
                XrViewLocateInfo lli{XR_TYPE_VIEW_LOCATE_INFO};
                lli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                lli.displayTime = fs.predictedDisplayTime;
                lli.space = g_xr.space;
                if (XR_SUCCEEDED(xrLocateViews(g_xr.session, &lli, &lvs, 2, &lnv, lv)) && lnv == 2) {
                    const XrPosef& h = lv[0].pose;
                    const float pitch = asinf(fmaxf(-1.0f, fminf(1.0f,
                        2.0f * (h.orientation.w * h.orientation.x -
                                h.orientation.y * h.orientation.z))));
                    float tan_h = 0.0f;
                    for (int e = 0; e < 2; e++) {
                        tan_h = fmaxf(tan_h, fabsf(tanf(lv[e].fov.angleUp)));
                        tan_h = fmaxf(tan_h, fabsf(tanf(lv[e].fov.angleDown)));
                    }
                    // The frame's half-height as an angle is the number to hold against
                    // what it looks like: it is where the top edge of the HUD sits above
                    // the forward axis, and the game's own top row is at 0.83 of that.
                    const float half_m = g_vrcfg.hud_distance_m * tan_h * g_vrcfg.hud_scale;
                    LOGI("head y=%.2fm pitch=%+.1fdeg | eye fov up=%+.1f down=%+.1f deg | "
                         "frame %.2fm ahead %+.2fm up, half-height %.2fm = %.1fdeg, "
                         "HUD top row %.1fdeg",
                         h.position.y, pitch * 57.2958f,
                         lv[0].fov.angleUp * 57.2958f, lv[0].fov.angleDown * 57.2958f,
                         g_vrcfg.hud_distance_m, g_vrcfg.hud_height_m, half_m,
                         atanf(half_m / g_vrcfg.hud_distance_m) * 57.2958f,
                         atanf(0.83f * half_m / g_vrcfg.hud_distance_m) * 57.2958f);
                }
            }
        }

        PadState p;
        p.connected = true;
        read_pad(p);
        if (vr::game_hooks().map_pad) vr::game_hooks().map_pad(p);
        if (g_xr.plat.merge_input) g_xr.plat.merge_input(p);
        input_script_apply(p);
        pad_set_state(0, p);

        // Clicking the right thumbstick switches between the game's camera and its first
        // person eye, for a game that has one. Only in stereo: theater shows the game's own
        // frame, which has no other view to offer.
        if (action_bool(g_xr.toggle)) {
            if (!toggle_was_down) LOGI("right stick click%s", stereo ? "" : " (theater: no other view)");
            if (!toggle_was_down && stereo && vr::game_hooks().set_first_person) {
                first_person = !first_person;
                apply_view();
                view_pref_save(first_person);
                LOGI("view: %s", first_person ? "first person" : "the game's camera");
            }
            toggle_was_down = true;
        } else {
            toggle_was_down = false;
        }

        // The left thumbstick click is the game's. It used to switch between theater and
        // stereo by hand, and was only ever pressed by accident.
        if (action_bool(g_xr.left_click)) {
            if (!left_click_was_down) LOGI("left stick click");
            if (!left_click_was_down && vr::game_hooks().left_click) vr::game_hooks().left_click();
            left_click_was_down = true;
        } else {
            left_click_was_down = false;
        }

        // Take at most one game frame per display frame, before choosing a view: both
        // the auto-switch and both render paths need it, and a backlog drawn and
        // thrown away would be wasted work.
        std::unique_ptr<gx::Batch> batch = gx::take_batch(0);
        // A game frame the headset will not show (it is off the viewer's head, or the runtime
        // has the app hidden) is still executed, into the default framebuffer: what it
        // copies out of the EFB may be something the game makes once and samples for as long
        // as a screen lasts. Dropping it left a title screen's whole background black until
        // the game next rebuilt it, which it never does on that screen.
        if (batch && !fs.shouldRender) {
            gx::render_set_output_fbo(0);
            gx::render_execute(*batch);
            batch.reset();
        }
        if (batch) {
            game_frames++;
            if (held) holds[std::min<uint32_t>(held, 4) - 1]++;
            held = 0;
            const auto now = std::chrono::steady_clock::now();
            gap_max = std::max(gap_max, std::chrono::duration<double, std::milli>(now - last_game_frame).count());
            last_game_frame = now;
            // Which view to present, when the game says. The view follows a *change* in the
            // game's answer. A change is held for two game frames before the view
            // follows: the state behind it is written by the guest thread and read here, so
            // a sample can land on a transient: a game state flag read that way has been
            // caught non-zero for single frames over menus.
            if (wants_stereo) {
                const int want_stereo = wants_stereo(*batch) ? 1 : 0;
                static int followed = -1;   // the game's answer the view last followed
                static int pending = -1;
                static int agree = 0;
                if (want_stereo != followed) {
                    agree = (want_stereo == pending) ? agree + 1 : 1;
                    pending = want_stereo;
                    if (agree >= 2) {
                        followed = want_stereo;
                        agree = 0;
                        if (stereo != (want_stereo != 0)) {
                            stereo = want_stereo != 0;
                            apply_view();
                            LOGI("switching to %s", stereo ? "stereo" : "theater");
                        }
                    }
                } else {
                    agree = 0;
                }
            }
        }

        // Advance the morph on the compositor's clock. The eyes only take it up when the
        // game delivers a frame, so it moves at the game's rate, not this one -- but
        // stepping it here keeps its duration honest however unevenly frames arrive. The
        // way back may have a duration of its own (transition_out_s).
        {
            const float target = stereo ? 1.0f : 0.0f;
            const float secs = stereo || g_vrcfg.transition_out_s < 0.0f ? g_vrcfg.transition_s
                                                                          : g_vrcfg.transition_out_s;
            if (secs <= 0.0f || last_display == 0) {
                morph = target;
            } else {
                const float step = (float)(fs.predictedDisplayTime - last_display) * 1e-9f / secs;
                morph = target > morph ? fminf(target, morph + step) : fmaxf(target, morph - step);
            }
            last_display = fs.predictedDisplayTime;
        }
        const bool eyes = morph > 0.0f;
        if (eyes) quad_fresh = false;

        if (batch) {
            // The two views want the EFB at different sizes -- see theater_scale. This is
            // the moment to change it: a whole frame is about to be drawn into it, and a
            // display frame without a new game frame repaints the panel from what is
            // already there, which a re-scale would have thrown away. A no-op otherwise.
            // It follows the path that will draw, which mid-morph is the eyes'.
            gx::render_set_internal_scale(eyes ? g_vrcfg.stereo_scale
                                               : g_vrcfg.theater_scale);
        }

        std::vector<XrCompositionLayerBaseHeader*> layers;
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
        XrCompositionLayerQuad quad_r{XR_TYPE_COMPOSITION_LAYER_QUAD};
        XrCompositionLayerProjection proj_layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};

        if (fs.shouldRender && eyes) {
            // --- stereo: the world through each eye, as a projection layer ---
            XrViewState vs{XR_TYPE_VIEW_STATE};
            uint32_t nv = 0;
            XrView views[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
            XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
            li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            li.displayTime = fs.predictedDisplayTime;
            li.space = g_xr.space;
            // Only redraw the eyes when the game produced a frame. Acquiring and
            // releasing an image without drawing into it hands the compositor whatever
            // that image held several frames ago, which reads as a hard strobe.
            if (batch && XR_SUCCEEDED(xrLocateViews(g_xr.session, &li, &vs, 2, &nv, views)) && nv == 2) {
                const auto t_eyes = std::chrono::steady_clock::now();
                // Both eyes share one batch: the CPU-side transform and the vertex
                // buffer are produced once, only the uniforms and draws repeat.
                gx::Batch* b = batch.get();
                bool both_eyes = true;
                // One HUD frame for both eyes, built outside the loop. Each eye's own
                // frustum is asymmetric and the two differ; sizing the frame per eye
                // would give the viewer two different HUDs to fuse. The widest half-field
                // either eye sees is what `hud_scale` is a fraction of.
                float tan_half = 0.0f;
                for (int e = 0; e < 2; e++) {
                    tan_half = fmaxf(tan_half, fabsf(tanf(views[e].fov.angleUp)));
                    tan_half = fmaxf(tan_half, fabsf(tanf(views[e].fov.angleDown)));
                }
                float H[16];
                gx::render_hud_frame(g_vrcfg.hud_distance_m * g_vrcfg.units_per_metre,
                                     tan_half, g_vrcfg.hud_scale,
                                     g_vrcfg.hud_height_m * g_vrcfg.units_per_metre,
                                     g_vrcfg.hud_pitch_deg * 3.14159265f / 180.0f, H);
                // Eased at both ends, so the world neither lurches out of the panel nor
                // slams into place.
                gx::render_set_vr_morph(morph * morph * (3.0f - 2.0f * morph), panel);
                if (!g_head_zeroed) {
                    g_head_zeroed = true;
                    if (first_person && stereo) {
                        g_head_zero[0] = 0.5f * (views[0].pose.position.x + views[1].pose.position.x);
                        g_head_zero[1] = 0.5f * (views[0].pose.position.y + views[1].pose.position.y);
                        g_head_zero[2] = 0.5f * (views[0].pose.position.z + views[1].pose.position.z);
                    } else {
                        g_head_zero[0] = g_head_zero[1] = g_head_zero[2] = 0.0f;
                    }
                }
                publish_hands(fs.predictedDisplayTime, true);
                publish_eyes(views);
                eye_res.begin();
                for (int e = 0; e < 2; e++) {
                    auto& eye = g_xr.eyes[e];
                    uint32_t ei = 0;
                    XrSwapchainImageAcquireInfo eai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                    if (XR_FAILED(xrAcquireSwapchainImage(eye.handle, &eai, &ei))) {
                        both_eyes = false;
                        continue;
                    }
                    XrSwapchainImageWaitInfo ewi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                    ewi.timeout = XR_INFINITE_DURATION;
                    int w = eye.w, h = eye.h;   // what was drawn, which the layer shows
                    if (XR_SUCCEEDED(xrWaitSwapchainImage(eye.handle, &ewi))) {
                        float P[16], V[16];
                        const float n = g_vrcfg.near_m * g_vrcfg.units_per_metre;
                        const float f = g_vrcfg.far_m * g_vrcfg.units_per_metre;
                        mat_proj(views[e].fov, n, f, P);
                        mat_view(views[e].pose, g_vrcfg, g_head_zero, V);
                        gx::render_set_vr_eye(P, V, H);
                        w = eye_res.size(eye.w);
                        h = eye_res.size(eye.h);
                        if (b) gx::render_execute_eye(*b, eye_target(e, ei), w, h, e == 0);
                        const GLuint shown = eye_finish(e, ei, w, h);
                        if (e == 0 && g_xr.plat.mirror) g_xr.plat.mirror(shown, w, h);
                        glFlush();
                    }
                    XrSwapchainImageReleaseInfo eri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    xrReleaseSwapchainImage(eye.handle, &eri);

                    proj_views[e] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                    proj_views[e].pose = views[e].pose;
                    proj_views[e].fov = views[e].fov;
                    proj_views[e].subImage.swapchain = eye.handle;
                    proj_views[e].subImage.imageRect = {{0, 0}, {w, h}};
                    proj_views[e].subImage.imageArrayIndex = 0;
                }
                eye_res.end();
                // A half-filled projection layer is invalid, so only keep it when both
                // eyes were acquired.
                if (both_eyes) have_proj = true;
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_eyes).count();
                gx::render_report_frame("stereo", ms);
                eyes_sum += ms;
                eyes_max = std::max(eyes_max, ms);
                eyes_n++;
            }
        } else if (fs.shouldRender) {
            publish_hands(0, false);
            publish_eyes(nullptr);
            // Touch the swapchain only when there is a new game frame to put in it.
            //
            // The compositor does not need a new image every display frame: a quad layer
            // keeps showing the last image released to it, reprojected at display rate,
            // which is what makes head tracking smooth however slowly the game renders --
            // and is exactly what the re-submission below relies on. Re-blitting identical
            // pixels into a freshly acquired image 72 times a second bought nothing and
            // meant cycling the swapchain under the compositor while it sampled, at the
            // one rate where a fade makes any mismatch visible. It also cost a full-screen
            // blit per display frame.
            uint32_t idx = 0, idx_r = 0;
            const auto wants_pair = vr::game_hooks().wants_panel_pair;
            const bool pair = g_vrcfg.theater_stereo && g_xr.swapchain_r != XR_NULL_HANDLE && batch &&
                              (!wants_pair || wants_pair(*batch));
            XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (batch && XR_SUCCEEDED(xrAcquireSwapchainImage(g_xr.swapchain, &ai, &idx))) {
                XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wi.timeout = XR_INFINITE_DURATION;
                bool have_r = pair && XR_SUCCEEDED(xrAcquireSwapchainImage(g_xr.swapchain_r, &ai, &idx_r));
                if (have_r && XR_FAILED(xrWaitSwapchainImage(g_xr.swapchain_r, &wi))) have_r = false;
                if (XR_SUCCEEDED(xrWaitSwapchainImage(g_xr.swapchain, &wi))) {
                    // Consume at most one game frame per display frame; otherwise a
                    // backlog would be drawn and thrown away.
                    bool drew;
                    const auto t_panel = std::chrono::steady_clock::now();
                    if (have_r) {
                        // The pair is separated by the viewer's own eyes, as the runtime
                        // places them for this frame; a headset that will not say gets a
                        // typical 63 mm. The panel's width in game units is what the
                        // game's frustum is converged on.
                        float sep = 0.063f;
                        XrViewState vs{XR_TYPE_VIEW_STATE};
                        uint32_t nv = 0;
                        XrView views[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
                        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
                        li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        li.displayTime = fs.predictedDisplayTime;
                        li.space = g_xr.space;
                        if (XR_SUCCEEDED(xrLocateViews(g_xr.session, &li, &vs, 2, &nv, views)) && nv == 2 &&
                            (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)) {
                            const float dx = views[1].pose.position.x - views[0].pose.position.x;
                            const float dy = views[1].pose.position.y - views[0].pose.position.y;
                            const float dz = views[1].pose.position.z - views[0].pose.position.z;
                            const float d = sqrtf(dx * dx + dy * dy + dz * dz);
                            if (d > 0.04f && d < 0.09f) sep = d;
                        }
                        const float u = g_vrcfg.units_per_metre;
                        drew = gx::render_execute_stereo_pair(*batch, g_xr.fbos[idx], g_xr.fbos_r[idx_r],
                                                              sep * g_vrcfg.theater_depth * u, kQuadW * u);
                    } else {
                        gx::render_set_output_fbo(g_xr.fbos[idx]);
                        drew = gx::render_execute(*batch);
                        gx::render_set_output_fbo(0);
                    }
                    gx::render_report_frame(
                        "theater",
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_panel).count());
                    if (drew && wants_view_mirror()) keep_panel(g_xr.fbos[idx]);
                    else if (drew && g_xr.plat.mirror) g_xr.plat.mirror(g_xr.fbos[idx], g_swap_w, g_swap_h);
                    // `log_frames 1` in vr.txt traces the theater path one display frame
                    // at a time: which swapchain image was written, whether it got a new
                    // game frame or a repaint, and which game frame it is showing. The
                    // headless harness renders the same pixels but cannot reproduce the
                    // compositor, so flicker that is not in the EFB has to be caught here.
                    if (g_vrcfg.log_frames)
                        LOGI("[t] disp=%llu img=%u %s present=%u", (unsigned long long)disp_frames,
                             idx, drew ? "drew" : "no-present", gx::present_count());
                    // Issue the blit before handing the image back. A full fence wait
                    // here was tried against the character-select flicker and changed
                    // nothing, at about a tenth of the game's frame rate.
                    glFlush();
                    if (drew) have_content = quad_fresh = true;
                }
                XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(g_xr.swapchain, &ri);
                if (have_r) xrReleaseSwapchainImage(g_xr.swapchain_r, &ri);
                quad_pair = have_r;
            }
        } else {
            skipped++;
        }

        // Once the panel has a frame of its own, the eyes' last images are history.
        // Forgetting them keeps the next stretch of stereo from opening on the end of the last one
        // should its first eye frame fail to render.
        if (quad_fresh) have_proj = false;

        // Submit whichever layer is current on every frame, including ones the runtime
        // told us not to render. Dropping the layer shows the user an empty frame,
        // which strobes against the frames that do carry it; re-submitting it just
        // re-displays the last released image -- reprojected for the current head pose.
        if (have_proj && !quad_fresh) {
            proj_layer.space = g_xr.space;
            proj_layer.viewCount = 2;
            proj_layer.views = proj_views;
            layers.push_back((XrCompositionLayerBaseHeader*)&proj_layer);
        }
        // The panel also holds until the eyes have drawn something: a manual toggle can
        // land on a display frame with no game frame to render them from.
        if (have_content && (quad_fresh || !have_proj)) {
            quad.layerFlags = 0;
            quad.space = g_xr.space;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = g_xr.swapchain;
            quad.subImage.imageRect = {{0, 0}, {g_swap_w, g_swap_h}};
            quad.subImage.imageArrayIndex = 0;
            quad.pose.orientation = {0, 0, 0, 1};
            quad.pose.position = {0, 0, -kQuadDist};
            quad.size = {kQuadW, kQuadH};
            layers.push_back((XrCompositionLayerBaseHeader*)&quad);
            // A stereo pair: the same panel, one image per eye. The frame the panel
            // last got decides, so a pair is never mixed with a single image.
            if (quad_pair) {
                quad.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
                quad_r = quad;
                quad_r.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
                quad_r.subImage.swapchain = g_xr.swapchain_r;
                layers.push_back((XrCompositionLayerBaseHeader*)&quad_r);
            }
            // Every display frame, not every game frame: the head moves at this rate.
            if (fs.shouldRender && wants_view_mirror()) compose_view(fs.predictedDisplayTime);
        }

        XrFrameEndInfo fe{XR_TYPE_FRAME_END_INFO};
        fe.displayTime = fs.predictedDisplayTime;
        fe.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        fe.layerCount = (uint32_t)layers.size();
        fe.layers = layers.data();
        xrEndFrame(g_xr.session, &fe);

        xr_frames++;
        disp_frames++;
        held++;
        // Every second: what the viewer got. On a Quest the compositor's own line (VrApi
        // FPS=) has the clocks and the GPU's load beside it in logcat.
        if (fs.predictedDisplayTime - last_report > 1000000000LL) {  // 1 s in ns
            int underruns = 0, lowest_ms = 0;
            audio_ring_stats(&underruns, &lowest_ms);
            LOGI("compositor %u frames, game %u frames (longest gap %.0f ms), %u not rendered; "
                 "stereo render thread %.1f ms mean, %.1f max; held 1:%u 2:%u 3:%u 4+:%u; "
                 "eyes' GPU %.1f ms mean, %.1f max, resolution %.2f mean, %.2f min; "
                 "sound %d underruns, lowest %d ms",
                 xr_frames, game_frames, gap_max, skipped, eyes_n ? eyes_sum / eyes_n : 0.0, eyes_max,
                 holds[0], holds[1], holds[2], holds[3],
                 eye_res.n ? eye_res.sum_ms / eye_res.n : 0.0, eye_res.max_ms,
                 eye_res.n ? eye_res.scale_sum / eye_res.n : 1.0, eye_res.scale_min,
                 underruns - underruns_reported, lowest_ms);
            underruns_reported = underruns;
            eye_res.report_reset();
            log_thread_sched();
            xr_frames = game_frames = skipped = 0;
            holds[0] = holds[1] = holds[2] = holds[3] = 0;
            gap_max = eyes_sum = eyes_max = 0;
            eyes_n = 0;
            last_report = fs.predictedDisplayTime;
        }
    }
}

}  // namespace xrf
