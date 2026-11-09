// The Vision Pro's shell around the OpenXR frontend (openxr/xr_frontend.h): GL ES through
// ANGLE (angle.h), the log, and the order the game is brought up in. The OpenXR it drives is the one in
// xr_runtime.mm. The app itself -- the launcher window and the immersive space whose layer
// this renders into -- is App.swift, which calls gcn_vos_run on a thread of its own.
//
// Everything the app reads and writes is in its Documents folder, which the Files app shows
// (and `xcrun devicectl device copy to --domain-type appDataContainer` reaches): the disc
// image (any .iso or .ciso), vr.txt, gcn_env.txt and gcn_input.txt as on the Quest, the
// memory card, the shader cache, the input logs, and log.txt, the last run's output.
#include "runtime.h"
#include "platform.h"
#include "input_script.h"
#include "input_log.h"
#include "xr_frontend.h"
#include "vos_runtime.h"
#include "angle.h"
#include "bridge.h"

#include "gx/render_gl.h"
#include "gx/gl.h"

#import <Foundation/Foundation.h>
#include <os/log.h>

#include <pthread.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <string>

uint32_t boot_load(const char* iso_path);
bool audio_open();

#define LOGI(fmt, ...) fprintf(stderr, "[vos] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[vos] error: " fmt "\n", ##__VA_ARGS__)

namespace {

std::string documents_dir() {
    NSArray* dirs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    return dirs.count ? std::string([dirs[0] UTF8String]) : std::string();
}

// printf and fprintf go nowhere in an app. They are piped into the unified log (Console.app,
// or `log stream` against the device) and copied to <Documents>/log.txt, which outlives the
// run for when nothing was watching.
FILE* g_log_file;
void* log_pump(void* arg) {
    const int fd = (int)(intptr_t)arg;
    static os_log_t log = os_log_create("com.gcn-recomp", "game");
    char buf[1024];
    std::string line;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (g_log_file) {
            fwrite(buf, 1, (size_t)n, g_log_file);
            fflush(g_log_file);
        }
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                os_log(log, "%{public}s", line.c_str());
                line.clear();
            } else {
                line += buf[i];
            }
        }
    }
    return nullptr;
}

void start_log(const std::string& dir) {
    g_log_file = fopen((dir + "/log.txt").c_str(), "w");
    int fds[2];
    if (pipe(fds) != 0) return;
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    pthread_t t;
    pthread_create(&t, nullptr, log_pump, (void*)(intptr_t)fds[0]);
    pthread_detach(t);
}

void* gl_proc(const char* name) { return vos::angle::proc(name); }

std::string read_script(const std::string& dir) {
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

// The runtime's GCN_* diagnostics are environment variables, and an app started from the
// home view has none: <Documents>/gcn_env.txt, one KEY=VALUE per line, stands in for it,
// as it does on the Quest.
void apply_env_file(const std::string& dir) {
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

[[noreturn]] void app_exit(const char* why) {
    LOGI("exiting: %s", why);
    fflush(nullptr);
    plat_exit_now(0);
}

bool pump() { return vos::layer_alive(); }

// The graphics binding is ANGLE's context, which the runtime finds for itself; it only
// needs something to point at.
const void* graphics_binding(XrInstance, XrSystemId) {
    static int binding;
    return &binding;
}

}  // namespace

const char* gcn_vos_app_label(void) { return GCN_APP_LABEL; }

const char* gcn_vos_find_image(void) {
    static std::string found;
    const std::string dir = documents_dir();
    found = plat_find_file(dir.c_str(), ".iso");
    if (found.empty()) found = plat_find_file(dir.c_str(), ".ciso");
    return found.c_str();
}

const char* gcn_vos_check_image(void) {
    static std::string why;
    const std::string path = gcn_vos_find_image();
    if (path.empty()) return why = "There is no disc image (.iso or .ciso) in the app.", why.c_str();
    FILE* f = fopen(path.c_str(), "rb");
    // A CISO keeps its first stored block, where the disc header is, right after its own
    // 0x8000-byte header.
    char id[6] = {};
    char magic[4] = {};
    bool ok = f && fread(magic, 1, 4, f) == 4;
    if (ok) ok = fseek(f, memcmp(magic, "CISO", 4) == 0 ? 0x8000 : 0, SEEK_SET) == 0 && fread(id, 1, 6, f) == 6;
    if (f) fclose(f);
    if (!ok) return why = "The disc image cannot be read.", why.c_str();
    if (memcmp(id, GCN_DISC_ID, 6) != 0)
        return why = std::string("That is not the right disc: this app plays ") + GCN_DISC_ID + ", and the image is " +
                     std::string(id, 6) + ".",
               why.c_str();
    return why = "", why.c_str();
}

// What the Quest's numbers mean here, over the game's defaults and under vr.txt. The
// recommended eye is the foveated texture's size (xr_runtime.mm), which an eye at
// eye_scale 1 already fills on average; a racing game held 60 there through a full
// championship. And ANGLE's timer queries add up each command buffer's span on the GPU,
// which overlap and include the compositor's time: in that championship the eyes "took" over 17 ms a frame
// in 107 seconds that still ran at 60, so a budget acted on them halved the eyes for
// nothing.
static void vos_defaults(VrConfig& c) {
    c.eye_scale = 1.0f;
    c.gpu_budget_ms = 0.0f;
}

// The game runs once per process, as on the Quest: the recompiled executable has no
// shutdown path. Leaving the immersive space ends the app, and the next launch is a clean
// console. A console reset (the game over screen does one) ends it too: visionOS gives an
// app no way to start itself again, so the viewer opens it again from the home view.
void gcn_vos_run(void* layer_renderer) {
    static bool entered;
    if (entered) app_exit("the game has already run in this process");
    entered = true;

    const std::string dir = documents_dir();
    start_log(dir);
    LOGI("%s on visionOS; documents at %s", GCN_APP_LABEL, dir.c_str());
    const std::string iso = gcn_vos_find_image();
    if (iso.empty()) app_exit("no .iso or .ciso in Documents");

    const VrConfig& cfg = xrf::configure(dir, dir, vos_defaults);

    if (!vos::angle::load() || !vos::angle::create_context()) app_exit("no GL ES 3 context");
    const int glver = gl_load_with(gl_proc);
    if (glver < GCN_GL_VERSION_MIN) app_exit("GL ES too old");
    LOGI("GL %s / %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));

    vos::set_layer_renderer(layer_renderer);
    vos::set_render_quality(cfg.get("render_quality", 1.0f));
    vos::start_controllers();

    xrf::Platform plat;
    plat.graphics_binding = graphics_binding;
    plat.image_type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    // The runtime's images are BGRA8 Metal textures that it composites as sRGB; to the
    // core they are linear RGBA8, which it draws the game's own colour into unchanged.
    plat.formats = {GL_RGBA8};
    plat.gl_proc = gl_proc;
    plat.pump = pump;
    plat.merge_input = vos::merge_gamepad;
    if (!xrf::init(plat)) app_exit("OpenXR setup failed");

    apply_env_file(dir);
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
    if (!xrf::build_shaders()) app_exit("the immersive space closed");

    static std::string save_dir = dir;
    g_save_dir = save_dir.c_str();

    // Every run records its input into <Documents>/inputs/<stamp>/, as on the Quest; see
    // gcn-recomp/docs/diagnostics.md. GCN_REPLAY in gcn_env.txt plays one back.
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

    if (!audio_open()) LOGE("audio unavailable; continuing without sound");

    uint32_t entry = boot_load(iso.c_str());
    threads_start_boot(entry);
    LOGI("game started");

    xrf::run();
    app_exit("the immersive space closed");
}
