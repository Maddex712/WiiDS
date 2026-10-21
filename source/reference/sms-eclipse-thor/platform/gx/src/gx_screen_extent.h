// CPU screen bounds used to place 2D/HUD draws in a widened framebuffer.
#ifndef SMS_GX_SCREEN_EXTENT_H
#define SMS_GX_SCREEN_EXTENT_H
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gx {
struct ScreenExtent {
    float lo = 1e30f, hi = -1e30f;
    float sx, cx, p0, p1;
    bool ortho;
    const uint32_t* matrices;
    uint32_t lastMatrix = ~0u;
    float matrix[12];

    ScreenExtent(float sx_, float cx_, float p0_, float p1_, bool ortho_, const uint32_t* matrices_)
        : sx(sx_), cx(cx_), p0(p0_), p1(p1_), ortho(ortho_), matrices(matrices_) {}

    // Each cached run is traversed once. Matrix state is immutable during
    // the batch; reuse the last position matrix for consecutive vertices.
    void append(const uint8_t* vertices, uint32_t count, size_t stride, int matrixOffset, uint32_t defaultMatrix) {
        for (uint32_t v = 0; v < count; ++v, vertices += stride) {
            float pos[3];
            memcpy(pos, vertices, sizeof pos);
            const uint32_t idx = matrixOffset >= 0 ? (vertices[matrixOffset] & 63) : defaultMatrix;
            if (idx != lastMatrix) {
                memcpy(matrix, matrices + idx * 4, sizeof matrix);
                lastMatrix = idx;
            }
            const float x = matrix[0] * pos[0] + matrix[1] * pos[1] + matrix[2] * pos[2] + matrix[3];
            float X;
            if (ortho) X = cx + sx * (p0 * x + p1);
            else {
                const float z = matrix[8] * pos[0] + matrix[9] * pos[1] + matrix[10] * pos[2] + matrix[11];
                if (z > -1e-6f) continue;
                X = cx + sx * (p0 * x + p1 * z) / -z;
            }
            lo = std::min(lo, X);
            hi = std::max(hi, X);
        }
    }
};
}
#endif
