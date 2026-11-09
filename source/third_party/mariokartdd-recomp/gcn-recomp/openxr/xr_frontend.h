// The OpenXR frontend: the game on a floating screen or in stereo around the viewer, with
// the controllers ours. One core serves every platform; a platform shell supplies the
// graphics binding, its own event loop and, on a desktop, a gamepad and a mirror window:
//
//   android/openxr_main.cpp  a standalone headset (EGL, GL ES)
//   openxr/xr_desktop.cpp    PC VR (WGL, desktop GL), entered from main.cpp with --vr
//
// The shell makes a GL context current, then calls configure, init, build_shaders and
// run in that order, booting the game between the last two. See the comment at the top of
// xr_frontend.cpp for how the view is put together.
#pragma once
#include "gx/gl.h"
#include "vr_config.h"
#include <openxr/openxr.h>
#include <string>
#include <vector>

struct PadState;

namespace xrf {

struct Platform {
    // Instance extensions the platform cannot do without -- its graphics binding's first.
    std::vector<const char*> extensions;
    // Instance extensions to enable where the runtime has them.
    std::vector<const char*> optional_extensions;
    // Chained into XrInstanceCreateInfo: Android's XrInstanceCreateInfoAndroidKHR.
    const void* instance_next = nullptr;
    // The graphics binding to chain into XrSessionCreateInfo, given the instance and
    // system. OpenXR requires the binding's xrGet*GraphicsRequirementsKHR to have been
    // called before xrCreateSession, so this is the place to call it. Null on failure.
    const void* (*graphics_binding)(XrInstance instance, XrSystemId system) = nullptr;
    // XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR or XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR. Both
    // structs are a header and a GL texture name, laid out alike.
    XrStructureType image_type = XR_TYPE_UNKNOWN;
    // Swapchain formats in order of preference; the first the runtime offers is taken.
    std::vector<int64_t> formats;
    // Where GL entry points come from, for extensions the generated loader lacks.
    GLLoadProc gl_proc = nullptr;
    // The platform's own events, between frames. False once the app should leave.
    bool (*pump)() = nullptr;
    // Controllers the platform has besides the headset's -- a desktop's gamepad and
    // keyboard -- merged into the pad the headset's controllers built.
    void (*merge_input)(PadState& p) = nullptr;
    // Shows what was just drawn somewhere besides the headset, a desktop window say:
    // `fbo` is readable, w x h, until this returns. Called only when there is a new image.
    void (*mirror)(GLuint fbo, int w, int h) = nullptr;
    // True while the mirror should show what the viewer sees rather than what the game
    // drew: in theater, the panel where the compositor places it, through the left eye as
    // the head moves, on black, redrawn every display frame. In stereo the eye already
    // is that. Unset, theater mirrors the panel's own image.
    bool (*mirror_view)() = nullptr;
    // Called once the session exists, on the thread that runs the frame loop: the
    // platform's own setup that needs one, such as Android's thread hints.
    void (*session_created)(XrInstance instance, XrSession session) = nullptr;
};

// The game's defaults with `config_dir`/vr.txt read over them, applied to the renderer.
// `state_dir` is where the frontend keeps what it writes: the viewer's choice of view
// (view.txt) and, with dump_every, the frames it dumps. `platform_defaults`, if given, is
// applied over the game's defaults and under vr.txt: what a platform's runtime makes of
// settings tuned on a Quest.
const VrConfig& configure(const std::string& config_dir, const std::string& state_dir,
                          void (*platform_defaults)(VrConfig&) = nullptr);

// The instance, session, swapchains and actions. The GL context must be current.
bool init(const Platform& p);

// The theater panel's size in texels, for gx::render_set_window_size. Valid after init.
void panel_size(int* w, int* h);

// Builds the shader cache (after gx::render_set_shader_cache_deferred and
// gx::render_init) a slice at a time, keeping the platform and the runtime answered
// between slices and a progress bar on the panel. False if the platform asked to leave.
bool build_shaders();

// The frame loop, until the platform or the runtime asks to leave.
void run();

}  // namespace xrf
