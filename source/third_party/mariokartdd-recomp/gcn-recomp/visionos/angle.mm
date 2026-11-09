// See angle.h.
#include "angle.h"

#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <string>

#define LOGI(fmt, ...) fprintf(stderr, "[vos] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[vos] error: " fmt "\n", ##__VA_ARGS__)

namespace vos::angle {

PFNEGLGETERRORPROC GetError;
PFNEGLCREATEIMAGEKHRPROC CreateImageKHR;
PFNEGLCREATESYNCPROC CreateSync;
PFNEGLDESTROYSYNCPROC DestroySync;
PFNEGLQUERYDISPLAYATTRIBEXTPROC QueryDisplayAttribEXT;
PFNEGLQUERYDEVICEATTRIBEXTPROC QueryDeviceAttribEXT;
void (*EGLImageTargetTexture2DOES)(unsigned, void*);

namespace {
void* g_lib;
EGLDisplay g_display = EGL_NO_DISPLAY;
}  // namespace

void* proc(const char* name) {
    if (!g_lib) return nullptr;
    std::string exported;
    if (!strncmp(name, "egl", 3)) exported = std::string("EGL_") + (name + 3);
    else if (!strncmp(name, "gl", 2)) exported = std::string("GL_") + (name + 2);
    else exported = name;
    return dlsym(g_lib, exported.c_str());
}

bool load() {
    if (g_lib) return true;
    NSString* path = [NSBundle.mainBundle.privateFrameworksPath stringByAppendingPathComponent:@"libANGLE-shared.dylib"];
    g_lib = dlopen(path.UTF8String, RTLD_NOW | RTLD_LOCAL);
    if (!g_lib) {
        LOGE("cannot open ANGLE: %s", dlerror());
        return false;
    }
    GetError = (PFNEGLGETERRORPROC)proc("eglGetError");
    CreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)proc("eglCreateImageKHR");
    CreateSync = (PFNEGLCREATESYNCPROC)proc("eglCreateSync");
    DestroySync = (PFNEGLDESTROYSYNCPROC)proc("eglDestroySync");
    QueryDisplayAttribEXT = (PFNEGLQUERYDISPLAYATTRIBEXTPROC)proc("eglQueryDisplayAttribEXT");
    QueryDeviceAttribEXT = (PFNEGLQUERYDEVICEATTRIBEXTPROC)proc("eglQueryDeviceAttribEXT");
    EGLImageTargetTexture2DOES = (void (*)(unsigned, void*))proc("glEGLImageTargetTexture2DOES");
    if (!GetError || !CreateImageKHR || !CreateSync || !DestroySync || !EGLImageTargetTexture2DOES) {
        LOGE("ANGLE lacks an entry point this frontend needs");
        return false;
    }
    return true;
}

EGLDisplay display() { return g_display; }

bool create_context() {
    auto get_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)proc("eglGetPlatformDisplayEXT");
    auto initialize = (PFNEGLINITIALIZEPROC)proc("eglInitialize");
    auto bind_api = (PFNEGLBINDAPIPROC)proc("eglBindAPI");
    auto choose = (PFNEGLCHOOSECONFIGPROC)proc("eglChooseConfig");
    auto create = (PFNEGLCREATECONTEXTPROC)proc("eglCreateContext");
    auto pbuffer = (PFNEGLCREATEPBUFFERSURFACEPROC)proc("eglCreatePbufferSurface");
    auto make_current = (PFNEGLMAKECURRENTPROC)proc("eglMakeCurrent");
    auto query = (PFNEGLQUERYSTRINGPROC)proc("eglQueryString");
    if (!get_display || !initialize || !bind_api || !choose || !create || !pbuffer || !make_current) return false;

    const EGLint display_attr[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE};
    g_display = get_display(EGL_PLATFORM_ANGLE_ANGLE, (void*)EGL_DEFAULT_DISPLAY, display_attr);
    EGLint major = 0, minor = 0;
    if (g_display == EGL_NO_DISPLAY || !initialize(g_display, &major, &minor)) {
        LOGE("no ANGLE display on Metal (0x%x)", GetError());
        return false;
    }
    LOGI("EGL %d.%d, %s", major, minor, query ? query(g_display, EGL_VERSION) : "");
    bind_api(EGL_OPENGL_ES_API);
    const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE,
    };
    EGLConfig cfg;
    EGLint n = 0;
    if (!choose(g_display, cfg_attr, &cfg, 1, &n) || n < 1) {
        LOGE("no ES 3 config");
        return false;
    }
    const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    EGLContext ctx = create(g_display, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed (0x%x)", GetError());
        return false;
    }
    // Everything is drawn into the runtime's textures; the surface only has to exist.
    const EGLint pb_attr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface surf = pbuffer(g_display, cfg, pb_attr);
    if (!make_current(g_display, surf, surf, ctx)) {
        LOGE("eglMakeCurrent failed (0x%x)", GetError());
        return false;
    }
    return true;
}

}  // namespace vos::angle
