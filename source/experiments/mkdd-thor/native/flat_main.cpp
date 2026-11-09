// Flat Android frontend for the AYN Thor: the game on the Activity's window (upper display),
// GameCube pads from Android controllers. The lower display is Java's (LowerScreenController).
//
// Modelled on gcn-recomp's android/openxr_main.cpp start-up order, without OpenXR: EGL on the
// NativeActivity window, then the same loop as the desktop frontend (take_batch, render_execute,
// swap). Files live in the app's external files dir: game.iso, memory card, shader cache.
#include "runtime.h"
#include "platform.h"
#include "input_script.h"
#include "gx/render.h"
#include "gx/render_gl.h"
#include "gx/gl.h"
#include "hw/pad.h"
#include "controller_ports.h"
#include "widescreen_hud.h"

#include <android/input.h>
#include <android/looper.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <jni.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#include <pthread.h>
#include <string>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, GCN_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, GCN_LOG_TAG, __VA_ARGS__)

uint32_t boot_load(const char* iso_path);
bool audio_open();
void wiids_mkdd_guest_frame(uint32_t frame);  // lower_bridge.cpp: the lower screen's sampler

namespace {
android_app* g_app;
// For Java's thread (set_pause): published once android_main has the looper.
std::atomic<ALooper*> g_looper{nullptr};
EGLDisplay g_dpy = EGL_NO_DISPLAY;
EGLContext g_ctx = EGL_NO_CONTEXT;
EGLSurface g_surf = EGL_NO_SURFACE, g_pbuf = EGL_NO_SURFACE;
EGLConfig g_cfg;

// The Activity reports device changes on Java's thread. The app thread consumes the
// per-port key and axis state without sharing held inputs between devices.
std::mutex g_input_lock;
wiids::mkdd::ControllerPorts g_controllers;

// While the Activity is paused or the WiiDS menu is open, the guest waits at its next frame end:
// no racing, no sound (the audio ring drains to silence) and no battery use. The runtime's clock
// re-anchors after a stall this long (cpu.cpp clock_pace, kForgiveNs), so the game resumes where
// it was.
std::mutex g_pause_lock;
std::condition_variable g_pause_cv;
bool g_activity_paused, g_menu_open, g_quitting;

// Once quitting, nothing pauses again: the guest must run on to its end.
bool paused_locked() { return !g_quitting && (g_activity_paused || g_menu_open); }

void gated_guest_frame(uint32_t frame) {
    {
        std::unique_lock<std::mutex> lock(g_pause_lock);
        g_pause_cv.wait(lock, [] { return !paused_locked(); });
    }
    wiids_mkdd_guest_frame(frame);
}

// From the app thread (Activity) or Java's main thread (menu): the app thread may be asleep in
// its looper while paused, so it is woken to see the change.
void set_pause(bool& flag, bool value, const char* what) {
    {
        std::lock_guard<std::mutex> lock(g_pause_lock);
        flag = value;
    }
    g_pause_cv.notify_all();
    if (ALooper* looper = g_looper.load()) ALooper_wake(looper);
    LOGI("%s %s", what, value ? "on" : "off");
}

// Standard (as printed on the Thor) or GameCube positions; set from the WiiDS menu.
std::atomic<bool> g_standard_layout{false};
std::atomic<bool> g_widescreen{false};

void clear_input() {
    std::lock_guard<std::mutex> lock(g_input_lock);
    g_controllers.clear_inputs();
}

void* log_pump(void*) {
    int fds[2];
    if (pipe(fds) != 0) return nullptr;
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    char buf[512];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf) - 1)) > 0) {
        if (buf[n - 1] == '\n') n--;
        buf[n] = 0;
        __android_log_write(ANDROID_LOG_INFO, GCN_LOG_TAG, buf);
    }
    return nullptr;
}

void* gl_proc(const char* name) {
    static void* lib = [] {
        void* h = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
        return h ? h : dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    }();
    if (lib)
        if (void* p = dlsym(lib, name)) return p;
    return (void*)eglGetProcAddress(name);
}

[[noreturn]] void app_exit(const char* why) {
    LOGI("exiting: %s", why);
    fflush(nullptr);
    plat_exit_now(0);
}

// One context for the process; window surfaces come and go with the Activity's window.
bool egl_init() {
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(g_dpy, nullptr, nullptr)) return false;
    const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE,
    };
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, cfg_attr, &g_cfg, 1, &n) || n < 1) return false;
    const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ctx_attr);
    if (g_ctx == EGL_NO_CONTEXT) return false;
    // Current on a tiny pbuffer until the window arrives, so the renderer can start.
    const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    g_pbuf = eglCreatePbufferSurface(g_dpy, g_cfg, pb);
    return eglMakeCurrent(g_dpy, g_pbuf, g_pbuf, g_ctx);
}

void attach_window() {
    if (!g_app->window || g_surf != EGL_NO_SURFACE) return;
    g_surf = eglCreateWindowSurface(g_dpy, g_cfg, g_app->window, nullptr);
    if (g_surf == EGL_NO_SURFACE || !eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) {
        LOGE("window surface failed: 0x%x", eglGetError());
        g_surf = EGL_NO_SURFACE;
        eglMakeCurrent(g_dpy, g_pbuf, g_pbuf, g_ctx);
        return;
    }
    eglSwapInterval(g_dpy, 1);
    LOGI("window %dx%d", ANativeWindow_getWidth(g_app->window), ANativeWindow_getHeight(g_app->window));
}

void detach_window() {
    if (g_surf == EGL_NO_SURFACE) return;
    eglMakeCurrent(g_dpy, g_pbuf, g_pbuf, g_ctx);
    eglDestroySurface(g_dpy, g_surf);
    g_surf = EGL_NO_SURFACE;
}

void on_cmd(android_app*, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: attach_window(); break;
        case APP_CMD_TERM_WINDOW: detach_window(); break;
        case APP_CMD_PAUSE: set_pause(g_activity_paused, true, "activity paused"); break;
        case APP_CMD_RESUME: set_pause(g_activity_paused, false, "activity paused"); break;
        // Held buttons and sticks would otherwise stay held after the shade or a dialog.
        case APP_CMD_LOST_FOCUS: clear_input(); break;
    }
}

// GameCube positions, as upstream's SDL frontend: bottom face A, left B, right X, top Y, R1 Z.
// Standard: B and X by their printed letters (A bottom, B right, X left, Y top).
uint16_t pad_bit(int32_t key) {
    const bool standard = g_standard_layout.load(std::memory_order_relaxed);
    switch (key) {
        case AKEYCODE_BUTTON_A: return PAD_A;
        case AKEYCODE_BUTTON_X: return standard ? PAD_X : PAD_B;
        case AKEYCODE_BUTTON_B: return standard ? PAD_B : PAD_X;
        case AKEYCODE_BUTTON_Y: return PAD_Y;
        case AKEYCODE_BUTTON_R1: return PAD_Z;
        case AKEYCODE_BUTTON_START: return PAD_START;
        case AKEYCODE_DPAD_UP: return PAD_UP;
        case AKEYCODE_DPAD_DOWN: return PAD_DOWN;
        case AKEYCODE_DPAD_LEFT: return PAD_LEFT;
        case AKEYCODE_DPAD_RIGHT: return PAD_RIGHT;
        default: return 0;
    }
}

int32_t on_input(android_app*, AInputEvent* e) {
    const int32_t source = AInputEvent_getSource(e);
    const bool gamepad = (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD ||
                         (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;
    std::lock_guard<std::mutex> lock(g_input_lock);
    auto* pad = g_controllers.for_event(AInputEvent_getDeviceId(e), gamepad);
    if (!pad) return 0;
    if (AInputEvent_getType(e) == AINPUT_EVENT_TYPE_KEY) {
        const int32_t key = AKeyEvent_getKeyCode(e);
        if (key == AKEYCODE_BACK) return 0;  // Java's onBackPressed opens the WiiDS menu.
        // Digital triggers, on controllers that send them as keys.
        if (key == AKEYCODE_BUTTON_L2 || key == AKEYCODE_BUTTON_R2) {
            static bool noted;
            if (!noted) { noted = true; LOGI("triggers arrive as keys on this controller"); }
            (key == AKEYCODE_BUTTON_L2 ? pad->lt_key : pad->rt_key) = AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_UP ? 0.f : 1.f;
            return 1;
        }
        const uint16_t bit = pad_bit(key);
        if (!bit) return 0;
        if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN) pad->buttons |= bit;
        else if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_UP) pad->buttons &= ~bit;
        return 1;
    }
    if (AInputEvent_getType(e) == AINPUT_EVENT_TYPE_MOTION && gamepad) {
        auto axis = [&](int32_t a) { return AMotionEvent_getAxisValue(e, a, 0); };
        pad->lx = axis(AMOTION_EVENT_AXIS_X); pad->ly = axis(AMOTION_EVENT_AXIS_Y);
        pad->rx = axis(AMOTION_EVENT_AXIS_Z); pad->ry = axis(AMOTION_EVENT_AXIS_RZ);
        pad->lt = std::max(axis(AMOTION_EVENT_AXIS_LTRIGGER), axis(AMOTION_EVENT_AXIS_BRAKE));
        pad->rt = std::max(axis(AMOTION_EVENT_AXIS_RTRIGGER), axis(AMOTION_EVENT_AXIS_GAS));
        pad->hx = axis(AMOTION_EVENT_AXIS_HAT_X); pad->hy = axis(AMOTION_EVENT_AXIS_HAT_Y);
        return 1;
    }
    return 0;
}

uint8_t stick(float v, bool invert, float dead) {
    const float m = std::fabs(v);
    if (m < dead) return 0x80;
    // Rescaled past the deadzone, so the output starts at centre instead of jumping.
    const float s = std::min(1.f, (m - dead) / (1.f - dead)) * ((v < 0) != invert ? -1.f : 1.f);
    return (uint8_t)std::lround(128 + s * 100);  // the GameCube stick's reach is about +-100
}

void update_pad() {
    std::lock_guard<std::mutex> lock(g_input_lock);
    const auto& ports = g_controllers.ports;
    const bool virtual_only = std::none_of(ports.begin(), ports.end(),
            [](const auto& port) { return port.device >= 0; });
    for (unsigned channel = 0; channel < ports.size(); ++channel) {
        const auto& pad = ports[channel];
        PadState p;
        p.connected = pad.device >= 0 || (channel == 0 && virtual_only);
        if (p.connected) {
            p.buttons = pad.buttons;
            if (pad.hx < -0.5f) p.buttons |= PAD_LEFT;
            if (pad.hx > 0.5f) p.buttons |= PAD_RIGHT;
            if (pad.hy < -0.5f) p.buttons |= PAD_UP;
            if (pad.hy > 0.5f) p.buttons |= PAD_DOWN;
            p.stick_x = stick(pad.lx, false, 0.12f); p.stick_y = stick(pad.ly, true, 0.12f);
            p.cstick_x = stick(pad.rx, false, 0.2f); p.cstick_y = stick(pad.ry, true, 0.2f);
            const float lt = std::max(pad.lt, pad.lt_key), rt = std::max(pad.rt, pad.rt_key);
            p.trig_l = (uint8_t)std::lround(std::clamp(lt, 0.f, 1.f) * 255);
            p.trig_r = (uint8_t)std::lround(std::clamp(rt, 0.f, 1.f) * 255);
            if (lt > 0.85f) p.buttons |= PAD_L;
            if (rt > 0.85f) p.buttons |= PAD_R;
        }
        if (channel == 0) input_script_apply(p);
        pad_set_state(channel, p);
    }
}

bool pump(int timeout_ms) {
    int events;
    android_poll_source* src;
    while (ALooper_pollOnce(timeout_ms, nullptr, &events, (void**)&src) >= 0) {
        if (src) src->process(g_app, src);
        if (g_app->destroyRequested) return false;
        timeout_ms = 0;
    }
    return !g_app->destroyRequested;
}
}  // namespace

void android_main(android_app* app) {
    static bool entered;
    if (entered) app_exit("android_main re-entered; this process has already run a game");
    entered = true;
    g_app = app;
    g_looper.store(app->looper);
    pthread_t t;
    pthread_create(&t, nullptr, log_pump, nullptr);
    pthread_detach(t);
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;

    static const std::string dir = app->activity->externalDataPath ? app->activity->externalDataPath : "";
    // WiiDS imports the disc into game/ (a folder its importer replaces whole); the lab keeps it flat.
    std::string iso = dir + "/game/game.iso";
    if (!plat_readable(iso.c_str())) iso = dir + "/game.iso";
    if (!plat_readable(iso.c_str())) app_exit("no game image (import your GM4E01 disc)");
    if (!egl_init()) app_exit("EGL init failed");
    const int glver = gl_load_with(gl_proc);
    if (glver < GCN_GL_VERSION_MIN) app_exit("OpenGL ES too old");
    LOGI("GL %s / %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));

    mem_init();
    timing_init();
    input_script_init("");
    static const std::string shader_cache = dir + "/shaders.bin";
    gx::render_set_shader_cache(shader_cache.c_str());
    const char* scale = getenv("GCN_SCALE");
    gx::render_init(scale ? std::max(1, atoi(scale)) : 2);
    // The memory card alone, in a folder the save manager can back up and restore.
    static const std::string saves = dir + "/saves";
    plat_make_dirs(saves.c_str());
    g_save_dir = saves.c_str();
    if (!audio_open()) LOGE("audio unavailable; continuing without sound");

    // Replaces frame_hook.cpp's static install with the same sampler behind the pause gate.
    gx::set_guest_frame_hook(gated_guest_frame);
    const uint32_t entry = boot_load(iso.c_str());
    threads_start_boot(entry);
    LOGI("game started");

    auto paused = [] { std::lock_guard<std::mutex> lock(g_pause_lock); return paused_locked(); };
    wiids::mkdd::WidescreenHud hud;
    bool hud_noted = false;
    // Paused: sleep in the looper until the Activity comes back or the menu closes, instead of spinning.
    while (pump(paused() ? -1 : 0)) {
        update_pad();
        auto b = gx::take_batch(4);
        if (!b) continue;
        if (g_surf == EGL_NO_SURFACE) continue;  // in the background: frames are dropped, not drawn
        const auto fitted = hud.fit(*b, g_widescreen.load(std::memory_order_relaxed), wiids::mkdd::latest());
        if (!hud_noted && fitted.left && fitted.right) {
            LOGI("16:9 race HUD fitted: %u left and %u right draws; original icon proportions", fitted.left, fitted.right);
            hud_noted = true;
        }
        gx::render_set_window_size(ANativeWindow_getWidth(app->window), ANativeWindow_getHeight(app->window));
        if (gx::render_execute(*b)) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            eglSwapBuffers(g_dpy, g_surf);
        }
    }
    set_pause(g_quitting, true, "quitting");  // let a waiting guest thread run on to the quit
    threads_request_quit();
    app_exit("activity destroyed");
}

// MkddNative (Java's main thread).
extern "C" JNIEXPORT void JNICALL Java_dev_thor_mkdd_MkddNative_setControllerDevices(
        JNIEnv* env, jclass, jintArray ids) {
    const jsize count = env->GetArrayLength(ids);
    jint* devices = env->GetIntArrayElements(ids, nullptr);
    if (!devices) return;
    {
        std::lock_guard<std::mutex> lock(g_input_lock);
        g_controllers.set_devices({devices, static_cast<size_t>(count)});
    }
    env->ReleaseIntArrayElements(ids, devices, JNI_ABORT);
}

extern "C" JNIEXPORT void JNICALL Java_dev_thor_mkdd_MkddNative_setMenuOpen(JNIEnv*, jclass, jboolean open) {
    set_pause(g_menu_open, open != JNI_FALSE, "menu");
}

extern "C" JNIEXPORT void JNICALL Java_dev_thor_mkdd_MkddNative_setWidescreen(JNIEnv*, jclass, jboolean on) {
    g_widescreen.store(on != JNI_FALSE, std::memory_order_relaxed);
    gx::render_set_widescreen(on != JNI_FALSE);
}

extern "C" JNIEXPORT void JNICALL Java_dev_thor_mkdd_MkddNative_setStandardLayout(JNIEnv*, jclass, jboolean standard) {
    g_standard_layout.store(standard != JNI_FALSE, std::memory_order_relaxed);
}
