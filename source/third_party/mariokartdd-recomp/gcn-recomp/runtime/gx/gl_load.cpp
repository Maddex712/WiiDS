// Resolves the GL entry points through whatever the host uses to look them up. Kept
// free of SDL so the EGL-only Android frontend can link it too.
#include "gl.h"
#include <cstring>

int gl_load_with(GLLoadProc proc) {
#if defined(__APPLE__) && !defined(GCN_GL_ES)
    (void)proc;
    return 41;  // linked directly against the system framework, which is 4.1
#else
    int v;
#ifdef GCN_GL_ES
    v = gladLoadGLES2((GLADloadfunc)proc);
    // Below ES 3.2 the base-vertex draw is an extension, under its own suffixed name with
    // the same signature: ANGLE on Metal (GL ES 3.0) has both of these.
    if (v && !glDrawElementsBaseVertex) {
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n && !glDrawElementsBaseVertex; i++) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (e && !strcmp(e, "GL_OES_draw_elements_base_vertex"))
                glad_glDrawElementsBaseVertex = (PFNGLDRAWELEMENTSBASEVERTEXPROC)proc("glDrawElementsBaseVertexOES");
            else if (e && !strcmp(e, "GL_EXT_draw_elements_base_vertex"))
                glad_glDrawElementsBaseVertex = (PFNGLDRAWELEMENTSBASEVERTEXPROC)proc("glDrawElementsBaseVertexEXT");
        }
    }
#else
    v = gladLoadGL((GLADloadfunc)proc);
#endif
    if (!v) return 0;
    return GLAD_VERSION_MAJOR(v) * 10 + GLAD_VERSION_MINOR(v);
#endif
}

#if !defined(__APPLE__) && !defined(GCN_GL_ES)
#include <cstdio>
#include <map>
#include <string>

namespace {
using DebugProc = void(GLAD_API_PTR*)(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar*, const void*);
constexpr GLenum kDebugOutput = 0x92E0, kDebugOutputSync = 0x8242;
constexpr GLenum kTypeError = 0x824C, kSevHigh = 0x9146, kSevMedium = 0x9147;

void GLAD_API_PTR on_debug(GLenum, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* msg, const void*) {
    if (type != kTypeError && severity != kSevHigh && severity != kSevMedium) return;
    static std::map<GLuint, unsigned> seen;
    const unsigned n = ++seen[id];
    if (n == 1 || n == 10 || n == 100 || n == 1000 || n % 10000 == 0)
        fprintf(stderr, "[gl] %s%s (x%u)\n", type == kTypeError ? "error: " : "", msg, n);
}
}  // namespace

bool gl_debug_enable(GLLoadProc proc) {
    auto cb = (void(GLAD_API_PTR*)(DebugProc, const void*))proc("glDebugMessageCallback");
    if (!cb) return false;
    glEnable(kDebugOutput);
    glEnable(kDebugOutputSync);  // the message arrives inside the call that caused it
    cb(on_debug, nullptr);
    fprintf(stderr, "[gl] debug output on\n");
    return true;
}
#else
bool gl_debug_enable(GLLoadProc) { return false; }
#endif
