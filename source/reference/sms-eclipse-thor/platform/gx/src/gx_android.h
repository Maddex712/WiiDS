#pragma once
#include <string>
#include <cstddef>
#include <cstdint>

namespace gx { namespace android {
#ifdef __ANDROID__
void install(void* (*getProc)(const char*));
bool desktopEntry(const char* name);
std::string shaderSource(const std::string& source, unsigned type);
bool metricActive();
bool needsRebasedIndices();
// Geometry shaders (GL_EXT/OES_geometry_shader): GX's wide lines need them.
bool geometryShaders();
void memoryBarrier(unsigned bits);
// Rebase an entire range once when GLES lacks base-vertex draws. Keep the
// temporary element buffer bound for its ordered colour/depth/metric passes.
struct IndexRange {
    size_t offset;
    int base;
    unsigned originalBuffer = 0;
    bool rebased = false;
    IndexRange(size_t offset, int count, int base, unsigned buffer, const uint32_t* indices);
    ~IndexRange();
};
#else
inline bool desktopEntry(const char*) { return false; }
inline std::string shaderSource(const std::string& source, unsigned) { return source; }
inline bool geometryShaders() { return true; }
#endif
}}
