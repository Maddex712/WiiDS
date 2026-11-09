// OpenGL include shim and render-target selection.
//
// The renderer targets two profiles from one source:
//
//   * desktop OpenGL 3.3 core  -- macOS, Windows, Linux
//   * OpenGL ES 3.2            -- Android / standalone headsets (GCN_GL_ES)
//   * OpenGL ES 3.0            -- ANGLE's, on visionOS (GCN_GL_ES and GCN_GL_ES30)
//
// Nothing here uses a feature newer than GL 3.3 / ES 3.0: samplers, VAOs, FBOs and
// explicit attribute locations are the whole requirement. Keeping the desktop floor at
// 3.3 also means mapping layers that stop there (Mesa's D3D12 driver, as shipped in
// Microsoft's OpenGL compatibility pack) can run it.
//
// macOS links the system framework directly; everywhere else the entry points are
// resolved at runtime by a generated glad loader -- see glad/, glad_es/ and gl_load.cpp.
#pragma once

#ifdef GCN_GL_ES

// GLSL ES has no default precision for float or int in a fragment shader, so it has to
// be stated or the shader fails to compile. Stays on one line per statement so the
// #version directive remains the first line.
#define GCN_GL_MAJOR 3
#ifdef GCN_GL_ES30
// ANGLE's Metal backend, which is what visionOS renders through, offers GL ES 3.0. What the
// renderer takes from 3.2 is optional there: glDrawElementsBaseVertex has a fallback in
// render_gl.cpp, and the extensions it asks for by name it does without when they are
// missing.
#define GCN_GL_MINOR 0
#define GCN_GLSL_VERSION "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#else
#define GCN_GL_MINOR 2
#define GCN_GLSL_VERSION "#version 320 es\nprecision highp float;\nprecision highp int;\n"
#endif
#include <glad/gles2.h>

#else

#define GCN_GL_MAJOR 3
#define GCN_GL_MINOR 3
#define GCN_GLSL_VERSION "#version 330 core\n"
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <glad/gl.h>
#endif

#endif

// Minimum acceptable version, encoded as major * 10 + minor, as gl_load() returns it.
#define GCN_GL_VERSION_MIN (GCN_GL_MAJOR * 10 + GCN_GL_MINOR)

// How the GL entry points are found: SDL_GL_GetProcAddress for the SDL frontend, or
// eglGetProcAddress for an EGL-only host such as a headless Android device.
using GLLoadProc = void* (*)(const char* name);

// Resolve the GL entry points for the current context. Must be called once after the
// context is made current, before any other gx:: call. Returns the version actually
// resolved as major * 10 + minor, or 0 if no usable GL was found.
//
// The result has to be checked against GCN_GL_VERSION_MIN: a legacy driver (Windows'
// "GDI Generic" 1.1, say) resolves successfully but leaves every GL 2.0+ entry point
// null, so calling into the renderer afterwards would dereference a null pointer.
int gl_load_with(GLLoadProc proc);

// GCN_GL_DEBUG: the driver's debug output (GL 4.3 / KHR_debug) on stderr -- errors and
// anything it rates high or medium, each distinct message counted rather than repeated.
// Needs a context created with the debug flag. False if the driver has no debug output.
bool gl_debug_enable(GLLoadProc proc);
