// The standalone headset's shell around the OpenXR frontend (openxr/xr_frontend.h): EGL
// and GL ES, the NativeActivity glue, logcat, and the order the game is brought up in.
//
// The disc image is read from the app's external files directory:
//   /sdcard/Android/data/<package>/files/game.iso (or game.ciso)
#include "runtime.h"
#include "platform.h"
#include "input_script.h"
#include "input_log.h"
#include "xr_frontend.h"

#include "gx/render_gl.h"
#include "gx/gl.h"

#include <android/log.h>
#include <android_native_app_glue.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <EGL/egl.h>
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <string>

#define TAG GCN_LOG_TAG
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

uint32_t boot_load(const char* iso_path);
bool audio_open();

// ---------------------------------------------------------------------------
// Pipe printf/fprintf into logcat; they go nowhere in an Android app.
// ---------------------------------------------------------------------------
static void* log_pump(void*) {
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
        __android_log_write(ANDROID_LOG_INFO, TAG, buf);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// EGL. Immersive VR renders into swapchain images, so the surface only has to exist.
// ---------------------------------------------------------------------------
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static EGLSurface g_surf = EGL_NO_SURFACE;
static EGLConfig g_cfg;

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

static bool egl_init() {
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(g_dpy, nullptr, nullptr)) { LOGE("eglInitialize failed"); return false; }
    const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE,
    };
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, cfg_attr, &g_cfg, 1, &n) || n < 1) { LOGE("no ES3 config"); return false; }
    const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ctx_attr);
    if (g_ctx == EGL_NO_CONTEXT) { LOGE("eglCreateContext failed"); return false; }
    const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    g_surf = eglCreatePbufferSurface(g_dpy, g_cfg, pb);
    if (!eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) { LOGE("eglMakeCurrent failed"); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// What the frontend needs from Android.
// ---------------------------------------------------------------------------
static android_app* g_app;

static const void* graphics_binding(XrInstance instance, XrSystemId system) {
    // Required before xrCreateSession, even though the result is only advisory here.
    PFN_xrGetOpenGLESGraphicsRequirementsKHR getReq = nullptr;
    xrGetInstanceProcAddr(instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                          (PFN_xrVoidFunction*)&getReq);
    if (getReq) {
        XrGraphicsRequirementsOpenGLESKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        getReq(instance, system, &req);
    }
    static XrGraphicsBindingOpenGLESAndroidKHR bind{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    bind.display = g_dpy;
    bind.config = g_cfg;
    bind.context = g_ctx;
    return &bind;
}

[[noreturn]] static void app_exit(const char* why);

static bool pump() {
    int events;
    android_poll_source* src;
    while (ALooper_pollOnce(0, nullptr, &events, (void**)&src) >= 0) {
        if (src) src->process(g_app, src);
        if (g_app->destroyRequested) return false;
    }
    return !g_app->destroyRequested;
}

static void on_cmd(android_app*, int32_t) {}

// Thread hints (thread_hints in vr.txt). Without them the runtime knows only the threads
// it made itself, and the game's busy ones -- the guest, the GX front end, the transform
// workers and this frame loop -- get no say in where they run beside the compositor's and
// the tracking's.
static bool g_thread_hints;
static XrSession g_hint_session;
static PFN_xrSetAndroidApplicationThreadKHR g_set_thread;

static void hint_thread(ThreadRole role, long tid) {
    static const XrAndroidThreadTypeKHR types[] = {
        XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, XR_ANDROID_THREAD_TYPE_APPLICATION_WORKER_KHR,
        XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR, XR_ANDROID_THREAD_TYPE_RENDERER_WORKER_KHR};
    static const char* names[] = {"application main", "application worker", "renderer main", "renderer worker"};
    const XrResult r = g_set_thread(g_hint_session, types[(int)role], (uint32_t)tid);
    LOGI("thread %ld: %s%s", tid, names[(int)role], XR_SUCCEEDED(r) ? "" : " (refused)");
}

static void session_created(XrInstance instance, XrSession session) {
    if (!g_thread_hints) return;
    xrGetInstanceProcAddr(instance, "xrSetAndroidApplicationThreadKHR", (PFN_xrVoidFunction*)&g_set_thread);
    if (!g_set_thread) { LOGI("no thread hints: the runtime lacks XR_KHR_android_thread_settings"); return; }
    g_hint_session = session;
    plat_thread_role(ThreadRole::RenderMain, "xr-frame-loop");
    plat_set_thread_role_listener(hint_thread);
}

static std::string read_script(const std::string& dir) {
    FILE* f = fopen((dir + "/gcn_input.txt").c_str(), "rb");
    if (!f) return {};
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    LOGI("input script: %s", s.c_str());
    return s;
}

// The runtime's GCN_* diagnostics are environment variables, and an APK launched from the
// headset's own launcher has no environment to speak of. `gcn_env.txt` beside `vr.txt`, one
// KEY=VALUE per line, stands in for it: each line is put into the environment before the
// runtime starts, so GCN_HEAP=1 there does what GCN_HEAP=1 on a desktop shell does. Only
// variables read lazily see it -- a static initializer that called getenv at library load
// has already run -- which is most of them.
static void apply_env_file(const std::string& dir) {
    FILE* f = fopen((dir + "/gcn_env.txt").c_str(), "rb");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        size_t eq = l.find('=');
        if (l.empty() || l[0] == '#' || eq == std::string::npos || eq == 0) continue;
        setenv(l.substr(0, eq).c_str(), l.substr(eq + 1).c_str(), 1);
        LOGI("env: %s", l.c_str());
    }
    fclose(f);
}

// A NativeActivity can be destroyed and re-created inside one process, and that calls
// android_main a second time. Nothing here survives it: the EGL context, the OpenXR
// instance and session, the recompiled game and the threads it booted are all global and
// are built exactly once. The second call then deadlocks on the first call's leftovers,
// which is what a hung launch is -- from outside it shows up as two log_pump pipes and
// two threads parked in its read(fd, buf, 511).
//
// Unwinding all of that on the way out is not on offer: the game is a recompiled
// executable with no shutdown path, and the runtime around it was written to be set up
// once. So the process goes instead and the next launch gets a clean one, which is what
// the system expects by the time the activity is gone. The memory card is safe to leave
// this way -- it is rewritten at the end of every command that dirties it, so there is
// nothing buffered to lose.
[[noreturn]] static void app_exit(const char* why) {
    LOGI("exiting: %s", why);
    fflush(nullptr);
    plat_exit_now(0);
}

// The game reset the console (guest_reset): the app's own launch intent, started with its
// task cleared, after which guest_reset ends this process and Android starts the activity
// in a new one -- a clean console, as a power cycle gives. The process cannot run a second
// game itself (android_main refuses to be entered twice), hence the round trip.
static void relaunch(uint32_t, uint32_t) {
    JavaVM* vm = g_app->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) { LOGE("relaunch: cannot attach to the VM"); return; }
    jobject act = g_app->activity->clazz;
    jclass act_cls = env->GetObjectClass(act);
    jobject pm = env->CallObjectMethod(act, env->GetMethodID(act_cls, "getPackageManager", "()Landroid/content/pm/PackageManager;"));
    jobject pkg = env->CallObjectMethod(act, env->GetMethodID(act_cls, "getPackageName", "()Ljava/lang/String;"));
    jobject intent = env->CallObjectMethod(
        pm, env->GetMethodID(env->GetObjectClass(pm), "getLaunchIntentForPackage", "(Ljava/lang/String;)Landroid/content/Intent;"), pkg);
    if (env->ExceptionCheck() || !intent) {
        env->ExceptionClear();
        LOGE("relaunch: no launch intent");
        return;
    }
    const jint kNewTask = 0x10000000, kClearTask = 0x00008000;  // Intent.FLAG_ACTIVITY_*
    env->CallObjectMethod(intent, env->GetMethodID(env->GetObjectClass(intent), "addFlags", "(I)Landroid/content/Intent;"),
                          kNewTask | kClearTask);
    env->CallVoidMethod(act, env->GetMethodID(act_cls, "startActivity", "(Landroid/content/Intent;)V"), intent);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOGE("relaunch: startActivity failed");
        return;
    }
    LOGI("the game reset the console: starting it again");
}

void android_main(android_app* app) {
    // Belt and braces for the same thing: if the glue ever starts a second android_main
    // while the first is still in its loop, the process still only runs one.
    static bool entered;
    if (entered) app_exit("android_main re-entered; this process has already run a game");
    entered = true;
    g_app = app;

    pthread_t t;
    pthread_create(&t, nullptr, log_pump, nullptr);
    pthread_detach(t);
    app->onAppCmd = on_cmd;

    const std::string dir = app->activity->externalDataPath ? app->activity->externalDataPath : "";
    // The user's own image, pushed there by package-apk.ps1: an .iso or a .ciso.
    std::string iso = dir + "/game.iso";
    if (!plat_readable(iso.c_str())) iso = dir + "/game.ciso";
    const VrConfig& cfg = xrf::configure(dir, dir);

    if (!egl_init()) app_exit("EGL init failed");
    int glver = gl_load_with(gl_proc);
    if (glver < GCN_GL_VERSION_MIN) { LOGE("GL ES %d.%d too old", glver / 10, glver % 10); app_exit("GL too old"); }
    LOGI("GL %s / %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));

    // The loader has to be told about the VM and the activity before anything else.
    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
    if (XR_FAILED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                        (PFN_xrVoidFunction*)&xrInitializeLoaderKHR)) ||
        !xrInitializeLoaderKHR)
        app_exit("xrInitializeLoaderKHR unavailable");
    XrLoaderInitInfoAndroidKHR loader_init{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loader_init.applicationVM = app->activity->vm;
    loader_init.applicationContext = app->activity->clazz;
    if (XR_FAILED(xrInitializeLoaderKHR((const XrLoaderInitInfoBaseHeaderKHR*)&loader_init)))
        app_exit("xrInitializeLoaderKHR failed");

    static XrInstanceCreateInfoAndroidKHR android_info{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android_info.applicationVM = app->activity->vm;
    android_info.applicationActivity = app->activity->clazz;
    xrf::Platform plat;
    plat.extensions = {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME};
    plat.optional_extensions = {XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME};
    plat.session_created = session_created;
    g_thread_hints = cfg.thread_hints;
    plat.instance_next = &android_info;
    plat.graphics_binding = graphics_binding;
    plat.image_type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    // The renderer writes the game's own gamma-encoded colour, and a Quest takes GL_RGBA8
    // as linear and encodes it again, which showed the game too bright from the first
    // build to 2026-10-11 (docs/graphics.md, "The swapchain is sRGB"). An sRGB swapchain
    // written with the encode off (EXT_sRGB_write_control, in create_swapchains) stores
    // it as it is for the compositor to decode. GCN_XR_SRGB=0 takes the linear one, for
    // comparing the two in the headset.
    plat.formats = {GL_SRGB8_ALPHA8, GL_RGBA8};
    if (const char* e = getenv("GCN_XR_SRGB"); e && *e == '0') plat.formats = {GL_RGBA8};
    plat.gl_proc = gl_proc;
    plat.pump = pump;
    if (!xrf::init(plat)) app_exit("OpenXR setup failed");

    if (!plat_readable(iso.c_str())) {
        LOGE("no game image at %s/game.iso or game.ciso", dir.c_str());
        app_exit("no game image");
    }

    apply_env_file(dir);
    set_reset_handler(relaunch);
    mem_init();
    timing_init();
    input_script_init(read_script(dir).c_str());
    static const std::string shader_cache = dir + "/shaders.bin";
    gx::render_set_shader_cache(shader_cache.c_str());
    gx::render_set_shader_cache_deferred(true);
    gx::render_init(cfg.start_in_stereo ? cfg.stereo_scale : cfg.theater_scale);
    int pw, ph;
    xrf::panel_size(&pw, &ph);
    gx::render_set_window_size(pw, ph);
    if (!xrf::build_shaders()) app_exit("activity destroyed");

    // The app's own files directory is the only place it can write, and the memory card
    // has to land there or the game starts from a blank one every launch.
    static std::string save_dir = dir;
    if (!save_dir.empty()) g_save_dir = save_dir.c_str();

    // Every run records what the guest read from the controllers, into
    // <files>/inputs/<stamp>/ with a snapshot of the memory card, so that a route to
    // wherever something went wrong can be played back: on a desktop with
    // --replay=<dir> after an adb pull, or here with GCN_REPLAY=<dir> in gcn_env.txt
    // (relative to the files directory). GCN_NO_INPUT_LOG=1 turns the recording off.
    // See gcn-recomp/docs/diagnostics.md; the replay runs on a scratch copy of the
    // logged card, never the real one.
    static std::string replay_card;
    if (const char* r = getenv("GCN_REPLAY")) {
        const std::string replay_dir = r[0] == '/' ? std::string(r) : dir + "/" + r;
        if (!input_replay_load(replay_dir, replay_card)) LOGE("no replay at %s", replay_dir.c_str());
        else if (!replay_card.empty()) g_memcard_path = replay_card.c_str();
    }
    if (!getenv("GCN_NO_INPUT_LOG")) {
        char stamp[64];
        time_t now = time(nullptr);
        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
        input_log_start(dir + "/inputs/" + stamp, replay_card.empty() ? dir + "/memcard_a.raw" : replay_card);
    }

    // Before the guest runs, so the AI DMA has somewhere to go from its first block.
    // A device that will not open is not fatal: the game is still playable silently, and
    // the backend says why in the log.
    if (!audio_open()) LOGE("audio unavailable; continuing without sound");

    uint32_t entry = boot_load(iso.c_str());
    threads_start_boot(entry);
    LOGI("game started");

    xrf::run();
    app_exit(app->destroyRequested ? "activity destroyed" : "session ended");
}
