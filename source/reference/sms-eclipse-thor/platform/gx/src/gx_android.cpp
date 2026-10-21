// GLES adapters for desktop-only GL entry points used by the GX backend.
// Logic operations use an RGBA8 destination snapshot per draw. Exact
// numeric pixel metrics use a shader storage counter rather than GLES's
// boolean occlusion queries, which cannot represent the game's counters.
#include "gl_funcs.h"
#include "gx_internal.h"
#include "gx_android.h"
#include <cstring>
#include <array>
#include <unordered_map>
#include <vector>

namespace gx { namespace android {
namespace {
void* (*proc)(const char*);
template <typename T> T entry(const char* name) { return reinterpret_cast<T>(proc(name)); }
PFNGLENABLEPROC enable;
PFNGLDISABLEPROC disable;
PFNGLGETINTEGERVPROC getInteger;
PFNGLUSEPROGRAMPROC useProgram;
PFNGLDELETEPROGRAMPROC deleteProgram;
PFNGLLINKPROGRAMPROC linkProgram;
PFNGLPROGRAMBINARYPROC programBinary;
PFNGLDRAWELEMENTSBASEVERTEXPROC drawBase;
PFNGLBEGINQUERYPROC beginQuery;
PFNGLENDQUERYPROC endQuery;
PFNGLGETQUERYOBJECTUIVPROC queryResult;
PFNGLDELETEQUERIESPROC deleteQueries;
PFNGLSAMPLERPARAMETERFPROC samplerParameter;
PFNGLBINDSAMPLERPROC bindSampler;
PFNGLCOLORMASKPROC colorMask;
bool clipDistance, depthClamp, logicEnabled, metricOn;
const char* geometryExtension;  // the geometry shader extension, or null
bool colourWrites = true;  // any of glColorMask's four
GLenum logic = GL_COPY;
GLuint program, logicTexture;
GLuint rebasedIndices;
float pointSize = 1;
int textureW, textureH;
std::unordered_map<GLuint, GLuint> counters;
std::unordered_map<GLuint, float> samplerBias;
GLuint unitSampler[8] = {};
struct UniformState {
    GLint logicOp, logicDest, point, lod;
    GLint lastLogic = -100;
    GLfloat lastPoint = 0;
    std::array<GLfloat, 8> lastLod{};
    bool destWritten = false, pointWritten = false, lodWritten = false;
};
std::unordered_map<GLuint, UniformState> uniformStates;

bool has(const char* name) {
    GLint n = 0;
    getInteger(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        auto e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
        if (e && !strcmp(e, name)) return true;
    }
    return false;
}
bool unsupported(GLenum cap) {
    return cap == GL_COLOR_LOGIC_OP || cap == GL_PROGRAM_POINT_SIZE ||
           (cap == GL_DEPTH_CLAMP && !depthClamp) ||
           ((cap == GL_CLIP_DISTANCE0 || cap == GL_CLIP_DISTANCE1) && !clipDistance);
}
void uniforms() {
    if (!program) return;
    // Metric draws can switch programs for every triangle. Uniform lookup is
    // a synchronous driver round trip on some Android drivers and emulators;
    // cache locations and the values retained by each linked program.
    auto entry = uniformStates.try_emplace(program);
    UniformState& u = entry.first->second;
    if (entry.second) {
        u.logicOp = glGetUniformLocation(program, "u_smsLogicOp");
        u.logicDest = glGetUniformLocation(program, "u_smsLogicDest");
        u.point = glGetUniformLocation(program, "u_smsPointSize");
        u.lod = glGetUniformLocation(program, "u_smsLodBias");
    }
    const GLint operation = logicEnabled ? GLint(logic - GL_CLEAR) : -1;
    if (u.logicOp >= 0 && operation != u.lastLogic) {
        glUniform1i(u.logicOp, operation); u.lastLogic = operation;
    }
    if (u.logicDest >= 0 && !u.destWritten) {
        glUniform1i(u.logicDest, 8); u.destWritten = true;
    }
    if (u.point >= 0 && (!u.pointWritten || pointSize != u.lastPoint)) {
        glUniform1f(u.point, pointSize); u.lastPoint = pointSize; u.pointWritten = true;
    }
    if (u.lod >= 0) {
        std::array<GLfloat, 8> values{};
        for (int i = 0; i < 8; ++i) {
            auto bias = samplerBias.find(unitSampler[i]);
            if (bias != samplerBias.end()) values[i] = bias->second;
        }
        if (!u.lodWritten || values != u.lastLod) {
            glUniform1fv(u.lod, 8, values.data()); u.lastLod = values; u.lodWritten = true;
        }
    }
}
void deleteProgramAdapter(GLuint p) { uniformStates.erase(p); deleteProgram(p); }
void linkProgramAdapter(GLuint p) { uniformStates.erase(p); linkProgram(p); }
void programBinaryAdapter(GLuint p, GLenum format, const void* binary, GLsizei size) {
    uniformStates.erase(p); programBinary(p, format, binary, size);
}
void samplerAdapter(GLuint sampler, GLenum name, GLfloat value) {
    if (name == GL_TEXTURE_LOD_BIAS) { samplerBias[sampler] = value; uniforms(); }
    else samplerParameter(sampler, name, value);
}
void bindSamplerAdapter(GLuint unit, GLuint sampler) {
    bindSampler(unit, sampler);
    if (unit < 8) { unitSampler[unit] = sampler; uniforms(); }
}
void enableAdapter(GLenum cap) {
    if (cap == GL_COLOR_LOGIC_OP) { logicEnabled = true; uniforms(); }
    if (!unsupported(cap)) enable(cap);
}
void disableAdapter(GLenum cap) {
    if (cap == GL_COLOR_LOGIC_OP) { logicEnabled = false; uniforms(); }
    if (!unsupported(cap)) disable(cap);
}
void integerAdapter(GLenum name, GLint* out) {
    if (name == GL_COLOR_LOGIC_OP) *out = logicEnabled;
    else if (unsupported(name)) *out = 0;
    else getInteger(name, out);
}
void programAdapter(GLuint p) { useProgram(p); program = p; uniforms(); }
void logicAdapter(GLenum op) { logic = op; uniforms(); }
void colorMaskAdapter(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    colourWrites = r || g || b || a;
    colorMask(r, g, b, a);
}
void pointAdapter(GLfloat size) { pointSize = size; uniforms(); }
void clearDepthAdapter(GLdouble depth) { entry<PFNGLCLEARDEPTHFPROC>("glClearDepthf")(GLfloat(depth)); }
void depthRangeAdapter(GLdouble lo, GLdouble hi) { entry<PFNGLDEPTHRANGEFPROC>("glDepthRangef")(GLfloat(lo), GLfloat(hi)); }
void fragLocationAdapter(GLuint, GLuint, const GLchar*) {} // shaders declare location 0

// eglGetProcAddress may return a desktop entry point even on an ES 3.1
// context where base-vertex drawing is unavailable. Rebase the index stream
// for that context; ES 3.2 and extension-capable handhelds use the native call.
void rebaseDraw(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint base) {
    auto draw = entry<PFNGLDRAWELEMENTSPROC>("glDrawElements");
    if (!base) { draw(mode, count, type, indices); return; }
    GLint buffer = 0;
    getInteger(GL_ELEMENT_ARRAY_BUFFER_BINDING, &buffer);
    const size_t bytes = type == GL_UNSIGNED_INT ? 4 : type == GL_UNSIGNED_SHORT ? 2 : 1;
    auto src = static_cast<const uint8_t*>(glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER,
        GLintptr(reinterpret_cast<uintptr_t>(indices)), GLsizeiptr(size_t(count) * bytes), GL_MAP_READ_BIT));
    if (!src) fatal("Android base-vertex index stream could not be read");
    std::vector<uint32_t> rebased(size_t(count), 0);
    for (GLsizei i = 0; i < count; ++i) {
        uint32_t value = bytes == 4 ? reinterpret_cast<const uint32_t*>(src)[i]
            : bytes == 2 ? reinterpret_cast<const uint16_t*>(src)[i] : src[i];
        rebased[size_t(i)] = value + base;
    }
    glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
    if (!rebasedIndices) glGenBuffers(1, &rebasedIndices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rebasedIndices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(rebased.size() * sizeof(uint32_t)), rebased.data(), GL_STREAM_DRAW);
    draw(mode, count, GL_UNSIGNED_INT, nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GLuint(buffer));
}

void snapshotDestination() {
    GLint viewport[4], active;
    getInteger(GL_VIEWPORT, viewport);
    getInteger(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE8);
    if (!logicTexture) glGenTextures(1, &logicTexture);
    glBindTexture(GL_TEXTURE_2D, logicTexture);
    if (textureW != viewport[2] || textureH != viewport[3]) {
        textureW = viewport[2]; textureH = viewport[3];
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, textureW, textureH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    entry<PFNGLCOPYTEXSUBIMAGE2DPROC>("glCopyTexSubImage2D")(GL_TEXTURE_2D, 0, 0, 0, 0, 0, textureW, textureH);
    glActiveTexture(GLenum(active));
}
// Whether a draw needs the destination snapshot: a logic operation that reads
// the destination, on a draw that writes colour. Mario's electric shock
// (an Electrokoopa's, say) draws his body twice with XOR and colour writes
// off, for depth, then twice with them on.
bool needsDestination() {
    return logicEnabled && colourWrites && logic != GL_COPY && logic != GL_CLEAR && logic != GL_SET &&
           logic != GL_COPY_INVERTED;
}
// One snapshot per draw, not per primitive: primitives of one draw that
// overlap each other see the destination from before the draw. A snapshot per
// triangle copied the whole 2x EFB a few thousand times a frame for the
// shock, which held a Snapdragon 8 Gen 3 at about 1 FPS while it lasted.
void drawAdapter(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint base) {
    if (needsDestination()) snapshotDestination();
    drawBase(mode, count, type, indices, base);
}
void multiDrawAdapter(GLenum mode, const GLsizei* counts, GLenum type, const void* const* indices, GLsizei n, const GLint* bases) {
    if (needsDestination()) snapshotDestination();
    for (GLsizei i = 0; i < n; ++i) drawBase(mode, counts[i], type, indices[i], bases[i]);
}

void beginAdapter(GLenum target, GLuint q) {
    if (target != GL_SAMPLES_PASSED) return beginQuery(target, q);
    GLuint& buffer = counters[q];
    if (!buffer) glGenBuffers(1, &buffer);
    const GLuint zero = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof zero, &zero, GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
    metricOn = true;
}
void endAdapter(GLenum target) {
    if (target != GL_SAMPLES_PASSED) return endQuery(target);
    metricOn = false;
}
void queryAdapter(GLuint q, GLenum name, GLuint* result) {
    auto c = counters.find(q);
    if (c == counters.end()) return queryResult(q, name, result);
    if (name == GL_QUERY_RESULT_AVAILABLE) { *result = GL_TRUE; return; }
    entry<PFNGLMEMORYBARRIERPROC>("glMemoryBarrier")(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, c->second);
    auto value = static_cast<const GLuint*>(glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), GL_MAP_READ_BIT));
    if (!value) fatal("Android pixel metric counter could not be read");
    *result = *value;
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
}
void deleteAdapter(GLsizei n, const GLuint* queries) {
    for (GLsizei i = 0; i < n; ++i) {
        auto c = counters.find(queries[i]);
        if (c != counters.end()) { glDeleteBuffers(1, &c->second); counters.erase(c); }
    }
    deleteQueries(n, queries);
}
void replace(std::string& s, const std::string& from, const std::string& to) {
    for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) s.replace(at, from.size(), to);
}
}

bool metricActive() { return metricOn; }
bool geometryShaders() { return geometryExtension != nullptr; }
void memoryBarrier(unsigned bits) { entry<PFNGLMEMORYBARRIERPROC>("glMemoryBarrier")(GLbitfield(bits)); }
bool needsRebasedIndices() { return drawBase == rebaseDraw; }
IndexRange::IndexRange(size_t sourceOffset, int count, int sourceBase, unsigned buffer, const uint32_t* src)
    : offset(sourceOffset), base(sourceBase) {
    if (drawBase != rebaseDraw || !base || count <= 0) return;
    if (!src) fatal("Android base-vertex range has no CPU indices");
    std::vector<uint32_t> indices(size_t(count), 0);
    for (int i = 0; i < count; ++i) indices[size_t(i)] = src[i] + base;
    if (!rebasedIndices) glGenBuffers(1, &rebasedIndices);
    originalBuffer = buffer;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rebasedIndices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * 4), indices.data(), GL_STREAM_DRAW);
    offset = 0;
    base = 0;
    rebased = true;
}
IndexRange::~IndexRange() {
    if (rebased) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, originalBuffer);
}
bool desktopEntry(const char* name) {
    return !strcmp(name, "glLogicOp") || !strcmp(name, "glPointSize") ||
           !strcmp(name, "glClearDepth") || !strcmp(name, "glDepthRange") ||
           !strcmp(name, "glBindFragDataLocation") || !strcmp(name, "glMultiDrawElementsBaseVertex") ||
           !strcmp(name, "glDrawElementsBaseVertex");
}

void install(void* (*getProc)(const char*)) {
    proc = getProc;
    enable = glEnable; disable = glDisable; getInteger = glGetIntegerv;
    useProgram = glUseProgram; drawBase = glDrawElementsBaseVertex;
    deleteProgram = glDeleteProgram; linkProgram = glLinkProgram; programBinary = glProgramBinary;
    samplerParameter = glSamplerParameterf; bindSampler = glBindSampler;
    colorMask = glColorMask;
    GLint major = 0, minor = 0;
    getInteger(GL_MAJOR_VERSION, &major); getInteger(GL_MINOR_VERSION, &minor);
    if (has("GL_OES_draw_elements_base_vertex")) drawBase = entry<PFNGLDRAWELEMENTSBASEVERTEXPROC>("glDrawElementsBaseVertexOES");
    else if (has("GL_EXT_draw_elements_base_vertex")) drawBase = entry<PFNGLDRAWELEMENTSBASEVERTEXPROC>("glDrawElementsBaseVertexEXT");
    else if (major == 3 && minor < 2) drawBase = rebaseDraw;
    if (!drawBase) fatal("Android base-vertex drawing is unavailable");
    logmsg("Android GLES %d.%d: base vertex %s", major, minor, drawBase == rebaseDraw ? "rebased indices" : "native");
    beginQuery = glBeginQuery; endQuery = glEndQuery;
    queryResult = glGetQueryObjectuiv; deleteQueries = glDeleteQueries;
    clipDistance = has("GL_EXT_clip_cull_distance");
    depthClamp = has("GL_EXT_depth_clamp");
    // Shaders stay GLSL ES 3.10, as every shader of a program must have the
    // same version, so a geometry shader needs the extension even on 3.2.
    geometryExtension = has("GL_EXT_geometry_shader") ? "GL_EXT_geometry_shader"
                        : has("GL_OES_geometry_shader") ? "GL_OES_geometry_shader" : nullptr;
    gx::gl::gx_glEnable = enableAdapter; gx::gl::gx_glDisable = disableAdapter;
    gx::gl::gx_glGetIntegerv = integerAdapter; gx::gl::gx_glUseProgram = programAdapter;
    gx::gl::gx_glDeleteProgram = deleteProgramAdapter; gx::gl::gx_glLinkProgram = linkProgramAdapter;
    if (programBinary) gx::gl::gx_glProgramBinary = programBinaryAdapter;
    gx::gl::gx_glSamplerParameterf = samplerAdapter; gx::gl::gx_glBindSampler = bindSamplerAdapter;
    gx::gl::gx_glLogicOp = logicAdapter; gx::gl::gx_glPointSize = pointAdapter;
    gx::gl::gx_glColorMask = colorMaskAdapter;
    gx::gl::gx_glClearDepth = clearDepthAdapter; gx::gl::gx_glDepthRange = depthRangeAdapter;
    gx::gl::gx_glBindFragDataLocation = fragLocationAdapter;
    gx::gl::gx_glMultiDrawElementsBaseVertex = multiDrawAdapter;
    gx::gl::gx_glDrawElementsBaseVertex = drawAdapter;
    gx::gl::gx_glBeginQuery = beginAdapter; gx::gl::gx_glEndQuery = endAdapter;
    gx::gl::gx_glGetQueryObjectuiv = queryAdapter; gx::gl::gx_glDeleteQueries = deleteAdapter;
    if (drawBase == rebaseDraw || !has("GL_EXT_buffer_storage")) gx::gl::gx_glBufferStorage = nullptr;
    else gx::gl::gx_glBufferStorage = entry<PFNGLBUFFERSTORAGEPROC>("glBufferStorageEXT");
    logmsg("Android GLES: clip distances %s, depth clamp %s, geometry shaders %s", clipDistance ? "yes" : "no",
           depthClamp ? "yes" : "no", geometryExtension ? "yes" : "no");
}

std::string shaderSource(const std::string& source, unsigned type) {
    std::string s = source;
    const bool gxClip = s.find("gl_ClipDistance[0] = clip.z") != std::string::npos;
    std::string header = "#version 310 es\n";
    if (type == GL_GEOMETRY_SHADER && geometryExtension)
        header += std::string("#extension ") + geometryExtension + " : require\n";
    if ((type == GL_VERTEX_SHADER || type == GL_GEOMETRY_SHADER) && clipDistance &&
        s.find("gl_ClipDistance") != std::string::npos)
        header += "#extension GL_EXT_clip_cull_distance : require\n";
    header += "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\nprecision highp usampler2D;\nprecision highp isampler2D;\n";
    // EXT_clip_cull_distance predeclares this built-in array. Our constant
    // indices [0] and [1] size it implicitly, as the extension permits. An
    // explicit standalone redeclaration is rejected by Adreno 750 drivers.
    // Keep the extension and writes; do not disable the game's clip planes.
    replace(s, "#version 330 core\n", header);
    replace(s, "#extension GL_ARB_shader_image_load_store : require\n", "");
    if (type == GL_GEOMETRY_SHADER) {
        // without clip distances the vertex shader writes none to pass on
        if (!clipDistance)
            for (size_t at; (at = s.find("  gl_ClipDistance[")) != std::string::npos;)
                s.erase(at, s.find('\n', at) + 1 - at);
    } else if (type == GL_VERTEX_SHADER) {
        if (!clipDistance) {
            replace(s, "  gl_ClipDistance[0] = clip.z + clip.w;\n  gl_ClipDistance[1] = -clip.z;\n", "");
            replace(s, "  gl_ClipDistance[0] = 1.0;\n  gl_ClipDistance[1] = 1.0;\n", "");
        }
        if (!depthClamp && gxClip) {
            replace(s, "  gl_Position.w = clip.w;\n", "  gl_Position.w = clip.w;\n  if (clip.w > 0.0) gl_Position.z = clamp(gl_Position.z, -clip.w, clip.w);\n");
        }
        auto at = s.find("void main()");
        if (at != std::string::npos) {
            s.insert(at, "uniform float u_smsPointSize;\n");
            replace(s, "void main() {", "void main() { gl_PointSize = u_smsPointSize;");
        }
    } else {
        replace(s, "out vec4 o_color;", "layout(location=0) out vec4 o_color;");
        if (s.find("u_tevreg") != std::string::npos) {
            std::string sampling = "uniform float u_smsLodBias[8];\n";
            for (int unit = 0; unit < 8; ++unit) {
                const std::string index = std::to_string(unit);
                replace(s, "texture(u_tex[" + index + "], ", "smsSample" + index + "(");
                sampling += "vec4 smsSample" + index + "(vec2 uv) { return texture(u_tex[" + index + "], uv, u_smsLodBias[" + index + "]); }\n";
            }
            auto at = s.find("void main()");
            s.insert(at, sampling);
            at = s.find("void main()");
            s.insert(at, R"(
uniform int u_smsLogicOp;
uniform sampler2D u_smsLogicDest;
vec4 smsLogic(vec4 color) {
  if (u_smsLogicOp < 0 || u_smsLogicOp == 3) return color;
  uvec4 a = uvec4(round(clamp(color, 0.0, 1.0) * 255.0));
  if (u_smsLogicOp == 0) return vec4(0.0);
  if (u_smsLogicOp == 15) return vec4(1.0);
  if (u_smsLogicOp == 12) return vec4(~a & uvec4(255)) / 255.0;
  uvec4 b = uvec4(round(texelFetch(u_smsLogicDest, ivec2(gl_FragCoord.xy), 0) * 255.0));
  uvec4 r;
  switch(u_smsLogicOp) {
    case 0: r=uvec4(0); break; case 1: r=a&b; break; case 2: r=a&~b; break;
    case 4: r=~a&b; break; case 5: r=b; break; case 6: r=a^b; break;
    case 7: r=a|b; break; case 8: r=~(a|b); break; case 9: r=~(a^b); break;
    case 10: r=~b; break; case 11: r=a|~b; break; case 12: r=~a; break;
    case 13: r=~a|b; break; case 14: r=~(a&b); break; default: r=uvec4(255); break;
  }
  return vec4(r & uvec4(255)) / 255.0;
}
)");
            replace(s, "o_color = color;", "o_color = smsLogic(color);");
        }
    }
    return s;
}
}}
