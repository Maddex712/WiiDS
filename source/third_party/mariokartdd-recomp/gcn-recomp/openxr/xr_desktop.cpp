// PC VR: the OpenXR frontend on a desktop's GL context, through XR_KHR_opengl_enable, so it
// runs on whichever runtime the system has active -- SteamVR, Virtual Desktop, Meta's, or any
// other that offers OpenGL. It is the window main.cpp made, its context handed to the
// runtime; the window stays up and mirrors what the headset is shown.
//
// Files live where the desktop build keeps them: vr.txt in the working directory (beside
// rom/), and what the frontend writes -- view.txt, dumped frames -- in saves/.
#include "xr_desktop.h"
#include "xr_frontend.h"
#include "runtime.h"
#include "platform.h"
#include "record.h"
#include "gx/render_gl.h"
#include "gx/gl.h"

#include <SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>   // IUnknown, which openxr_platform.h names for Win32
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#define XR_USE_PLATFORM_WIN32
#endif
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdio>
#include <string>

uint32_t boot_load(const char* iso_path);
bool audio_open();
void audio_set_device(const char* name);

namespace {

XrDesktopHost g_host;
std::string g_audio_device;  // the headset's own output, where the runtime names one

void* gl_proc(const char* name) { return SDL_GL_GetProcAddress(name); }

#ifdef _WIN32
// Meta's runtime plays only the headset's audio endpoint into the headset; the Windows
// default device is somewhere else (speakers, a shaker), and is heard in the headset only
// while the runtime's own menu mirrors the desktop. XR_OCULUS_audio_device_guid names the
// endpoint by its WASAPI ID; SDL opens devices by their friendly name, so look that up.
std::string headset_audio_device(XrInstance instance) {
    PFN_xrGetAudioOutputDeviceGuidOculus get = nullptr;
    if (XR_FAILED(xrGetInstanceProcAddr(instance, "xrGetAudioOutputDeviceGuidOculus", (PFN_xrVoidFunction*)&get)) || !get)
        return {};
    wchar_t id[XR_MAX_AUDIO_DEVICE_STR_SIZE_OCULUS] = {};
    if (XR_FAILED(get(instance, id)) || !id[0]) return {};
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string name;
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IPropertyStore* props = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)) &&
        SUCCEEDED(en->GetDevice(id, &dev)) && SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) {
            char buf[512];
            if (WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, buf, sizeof(buf), nullptr, nullptr) > 0) name = buf;
        }
        PropVariantClear(&v);
    }
    if (props) props->Release();
    if (dev) dev->Release();
    if (en) en->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    if (name.empty()) fprintf(stderr, "[xr] the runtime named an audio device Windows does not know\n");
    return name;
}
#endif

#ifdef _WIN32
// KRVR's implicit layer (controller input for an Apple Vision Pro streamed through its
// CloudXR runtime) loads into every OpenXR app whatever the runtime, and on Meta's it leaves
// the menu button and both thumbstick clicks bound to nothing. It is of use only on its own
// runtime, so it is switched off for this process on any other. Its manifest names this
// variable; the loader reads it when the instance is created. DISABLE_KRVR_LAYER=0 (or
// any value) set beforehand is left alone.
void keep_foreign_layers_out() {
    if (getenv("DISABLE_KRVR_LAYER")) return;
    std::string runtime;
    if (const char* j = getenv("XR_RUNTIME_JSON")) runtime = j;
    else {
        char buf[1024];
        DWORD len = sizeof(buf);
        if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Khronos\\OpenXR\\1", "ActiveRuntime", RRF_RT_REG_SZ, nullptr,
                         buf, &len) == ERROR_SUCCESS)
            runtime = buf;
    }
    for (char& c : runtime) c = (char)tolower((unsigned char)c);
    if (runtime.find("krvr") == std::string::npos && runtime.find("cloudxr") == std::string::npos)
        _putenv_s("DISABLE_KRVR_LAYER", "1");
}
#endif

const void* graphics_binding(XrInstance instance, XrSystemId system) {
#ifdef _WIN32
    g_audio_device = headset_audio_device(instance);
#endif
    // Required before xrCreateSession. Unlike a headset's GL ES, a desktop runtime can
    // want a newer GL than the renderer asks for, so the answer is checked.
    PFN_xrGetOpenGLGraphicsRequirementsKHR getReq = nullptr;
    xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&getReq);
    if (!getReq) {
        fprintf(stderr, "[xr] error: the runtime has no xrGetOpenGLGraphicsRequirementsKHR\n");
        return nullptr;
    }
    XrGraphicsRequirementsOpenGLKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    if (XR_FAILED(getReq(instance, system, &req))) {
        fprintf(stderr, "[xr] error: xrGetOpenGLGraphicsRequirementsKHR failed\n");
        return nullptr;
    }
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    const XrVersion have = XR_MAKE_VERSION(major, minor, 0);
    if (have < req.minApiVersionSupported) {
        fprintf(stderr, "[xr] error: the runtime wants OpenGL %u.%u; this context is %d.%d\n",
                XR_VERSION_MAJOR(req.minApiVersionSupported), XR_VERSION_MINOR(req.minApiVersionSupported),
                major, minor);
        return nullptr;
    }
#ifdef _WIN32
    static XrGraphicsBindingOpenGLWin32KHR bind{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
    bind.hDC = wglGetCurrentDC();
    bind.hGLRC = wglGetCurrentContext();
    return &bind;
#else
    return nullptr;
#endif
}

// The window shows what the headset was just given: the left eye, or the panel. Cropped to
// the window's shape about the image's centre rather than squeezed into it.
void mirror(GLuint fbo, int w, int h) {
    record::frame(fbo, w, h);
    int dw, dh;
    SDL_GL_GetDrawableSize(g_host.window, &dw, &dh);
    if (dw <= 0 || dh <= 0) return;
    int sx0 = 0, sy0 = 0, sx1 = w, sy1 = h;
    if ((int64_t)w * dh > (int64_t)h * dw) {
        const int cw = (int)((int64_t)h * dw / dh);
        sx0 = (w - cw) / 2;
        sx1 = sx0 + cw;
    } else {
        const int ch = (int)((int64_t)w * dh / dw);
        sy0 = (h - ch) / 2;
        sy1 = sy0 + ch;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(sx0, sy0, sx1, sy1, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    SDL_GL_SwapWindow(g_host.window);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}  // namespace

int xr_desktop_main(const XrDesktopHost& host, const char* iso) {
    g_host = host;
    plat_make_dirs("saves");
    const VrConfig& cfg = xrf::configure(".", "saves");

    xrf::Platform plat;
#ifdef _WIN32
    plat.extensions = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    plat.optional_extensions = {XR_OCULUS_AUDIO_DEVICE_GUID_EXTENSION_NAME};
    keep_foreign_layers_out();
#endif
    plat.graphics_binding = graphics_binding;
    plat.image_type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    // The renderer writes the game's own gamma-encoded colour. A desktop runtime takes a
    // linear format at its word and encodes it again, which washes the picture out; an sRGB
    // swapchain written with GL_FRAMEBUFFER_SRGB off stores the colour as it is and is
    // decoded by the compositor, which is what the colour already is.
    plat.formats = {GL_SRGB8_ALPHA8, GL_RGBA8};
    if (const char* e = getenv("GCN_XR_SRGB"); e && *e == '0') plat.formats = {GL_RGBA8};
    plat.gl_proc = gl_proc;
    plat.pump = host.pump;
    plat.merge_input = host.merge_input;
    plat.mirror = mirror;
    // A movie is of what the viewer saw; the window follows it while one is being made.
    plat.mirror_view = record::active;
    if (!xrf::init(plat)) {
        fprintf(stderr,
                "Could not start VR. Check that a headset is connected and that the OpenXR runtime\n"
                "you want (SteamVR, Virtual Desktop, Meta Quest Link, ...) is set as the active one.\n");
        return 1;
    }

    gx::render_set_shader_cache_deferred(true);
    gx::render_init(cfg.start_in_stereo ? cfg.stereo_scale : cfg.theater_scale);
    int pw, ph;
    xrf::panel_size(&pw, &ph);
    gx::render_set_window_size(pw, ph);
    if (!xrf::build_shaders()) return 0;

    if (!g_audio_device.empty()) audio_set_device(g_audio_device.c_str());
    audio_open();
    uint32_t entry = boot_load(iso);
    threads_start_boot(entry);
    xrf::run();
    return 0;
}
