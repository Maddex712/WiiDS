// ANGLE, the GL ES the renderer runs on in visionOS (tools/build-angle-visionos.sh says why
// and how it is built). WebKit's build exports its entry points under prefixed names --
// EGL_Initialize for eglInitialize, GL_DrawArrays for glDrawArrays -- so nothing is linked
// by name: the library is opened from the app's Frameworks directory and each entry point
// is looked up, the GL ones through proc() by the glad loader, the EGL ones here.
#pragma once
#define EGL_EGL_PROTOTYPES 0
#define EGL_NO_PLATFORM_SPECIFIC_TYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>

namespace vos::angle {

// Opens the library and resolves what is below. False, having said why, if it cannot.
bool load();

// A GL or EGL entry point by its standard name (glFoo, eglFoo), for gl_load_with.
void* proc(const char* name);

// A display on ANGLE's Metal backend with a GL ES 3 context current on the calling thread.
bool create_context();
EGLDisplay display();

extern PFNEGLGETERRORPROC GetError;
extern PFNEGLCREATEIMAGEKHRPROC CreateImageKHR;
extern PFNEGLCREATESYNCPROC CreateSync;
extern PFNEGLDESTROYSYNCPROC DestroySync;
extern PFNEGLQUERYDISPLAYATTRIBEXTPROC QueryDisplayAttribEXT;
extern PFNEGLQUERYDEVICEATTRIBEXTPROC QueryDeviceAttribEXT;
// glEGLImageTargetTexture2DOES, typed without GL's headers.
extern void (*EGLImageTargetTexture2DOES)(unsigned target, void* image);

}  // namespace vos::angle
