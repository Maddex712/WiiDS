// GL_EXT_multisampled_render_to_texture, for antialiasing the VR eyes.
//
// On a tiled GPU -- every standalone headset -- this is the cheap way to multisample: the
// samples live only in on-chip tile memory and are averaged as each tile is written out,
// so the texture in memory stays single-sampled and the extra cost is mostly shading
// coverage at triangle edges rather than bandwidth. An ordinary multisampled renderbuffer
// plus a resolve blit would write and read every sample through memory instead.
//
// Not in the generated loader, so the two entry points are fetched here. Header-only so
// both the OpenXR frontend and the headless harness can use it.
#pragma once
#include "gl.h"
#include <cstring>

#ifdef GCN_GL_ES
namespace gx {

struct Msrtt {
    using TexFn = void (*)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);
    using RboFn = void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    TexFn framebuffer_texture_2d = nullptr;
    RboFn renderbuffer_storage = nullptr;
    int max_samples = 0;  // 0: unavailable
};

inline Msrtt msrtt_load(GLLoadProc proc) {
    Msrtt m;
    GLint n = 0;
    bool listed = false;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n && !listed; i++) {
        const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        listed = e && !strcmp(e, "GL_EXT_multisampled_render_to_texture");
    }
    if (!listed) return m;
    m.framebuffer_texture_2d = (Msrtt::TexFn)proc("glFramebufferTexture2DMultisampleEXT");
    m.renderbuffer_storage = (Msrtt::RboFn)proc("glRenderbufferStorageMultisampleEXT");
    if (!m.framebuffer_texture_2d || !m.renderbuffer_storage) return m;
    GLint max = 0;
    glGetIntegerv(0x8D57 /* GL_MAX_SAMPLES_EXT */, &max);
    m.max_samples = max;
    return m;
}

}  // namespace gx
#endif
