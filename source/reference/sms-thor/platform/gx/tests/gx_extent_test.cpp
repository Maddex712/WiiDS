// Compare optimized bounds with the previous renderer, including mixed
// streamed/cached runs and indexed position matrices; no GL/ROM required.
#include "gx_screen_extent.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

struct Vertex { float p[3]; uint8_t matrix; uint8_t pad[3]; };
struct Fixture {
    std::vector<Vertex> streamed;
    std::vector<std::vector<Vertex>> runs;
    uint32_t matrices[256 + 12]{};
    float sx = 320, cx = 320, p0 = 0.003125f, p1 = -1;
    bool ortho = true, indexed = false;
    uint32_t defaultMatrix = 0;
};
struct Bounds { float lo = 1e30f, hi = -1e30f; };

// Frozen pre-optimization algorithm. In particular, locating each cached
// vertex rescans the run list from the beginning.
static Bounds previous(const Fixture& f) {
    Bounds b;
    size_t total = f.streamed.size();
    for (const auto& run : f.runs) total += run.size();
    for (size_t v = 0; v < total; ++v) {
        const Vertex* vx = nullptr;
        if (v < f.streamed.size()) vx = &f.streamed[v];
        else {
            size_t at = v - f.streamed.size();
            for (const auto& run : f.runs) {
                if (at < run.size()) { vx = &run[at]; break; }
                at -= run.size();
            }
        }
        float m[12];
        const uint32_t idx = f.indexed ? (vx->matrix & 63) : f.defaultMatrix;
        memcpy(m, f.matrices + idx * 4, sizeof m);
        const float x = m[0] * vx->p[0] + m[1] * vx->p[1] + m[2] * vx->p[2] + m[3];
        float X;
        if (f.ortho) X = f.cx + f.sx * (f.p0 * x + f.p1);
        else {
            const float z = m[8] * vx->p[0] + m[9] * vx->p[1] + m[10] * vx->p[2] + m[11];
            if (z > -1e-6f) continue;
            X = f.cx + f.sx * (f.p0 * x + f.p1 * z) / -z;
        }
        b.lo = std::min(b.lo, X); b.hi = std::max(b.hi, X);
    }
    return b;
}
static Bounds optimized(const Fixture& f) {
    gx::ScreenExtent e(f.sx, f.cx, f.p0, f.p1, f.ortho, f.matrices);
    const int offset = f.indexed ? offsetof(Vertex, matrix) : -1;
    e.append(reinterpret_cast<const uint8_t*>(f.streamed.data()), uint32_t(f.streamed.size()), sizeof(Vertex), offset, f.defaultMatrix);
    for (const auto& run : f.runs)
        e.append(reinterpret_cast<const uint8_t*>(run.data()), uint32_t(run.size()), sizeof(Vertex), offset, f.defaultMatrix);
    return {e.lo, e.hi};
}
static void check(const Fixture& f) {
    const Bounds a = previous(f), b = optimized(f);
    if (memcmp(&a, &b, sizeof a)) {
        fprintf(stderr, "Bounds changed: previous %g/%g optimized %g/%g\n", a.lo, a.hi, b.lo, b.hi);
        exit(1);
    }
}
static volatile float sink;
static double benchmark(const Fixture& f, Bounds (*scan)(const Fixture&)) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 2000; ++i) { Bounds b = scan(f); sink = b.lo + b.hi; }
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 2000;
}
int main() {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> value(-20, 20);
    for (int i = 0; i < 1500; ++i) {
        Fixture f;
        for (auto& word : f.matrices) { const float x = value(rng); memcpy(&word, &x, 4); }
        f.ortho = i & 1; f.indexed = i & 2; f.defaultMatrix = rng() & 63;
        f.sx = value(rng); f.cx = value(rng); f.p0 = value(rng); f.p1 = value(rng);
        auto fill = [&](std::vector<Vertex>& vertices) {
            vertices.resize(rng() % 30);
            for (auto& v : vertices) {
                for (auto& x : v.p) x = value(rng);
                v.matrix = uint8_t(rng());
            }
        };
        fill(f.streamed); f.runs.resize(rng() % 25);
        for (auto& run : f.runs) fill(run);
        check(f);
    }
    Fixture f;
    const float identity[12] = {1,0,0,0,0,1,0,0,0,0,1,0};
    memcpy(f.matrices, identity, sizeof identity);
    f.streamed = {{{0, 0, -1}, 0, {}}, {{640, 0, -1}, 0, {}}};
    check(f);
    const Bounds exact = optimized(f);
    if (exact.lo != 0 || exact.hi != 640) return 1;
    // Multi-run HUD/2D workload; numbers measure this CPU routine only, not
    // game FPS, GPU work or ARM64 device performance.
    for (int runs : {1, 32, 128, 512}) {
        f.runs.assign(runs, f.streamed);
        check(f);
        const double oldUs = benchmark(f, previous), newUs = benchmark(f, optimized);
        printf("Bounds CPU benchmark: %d cached runs, previous %.2f us, optimized %.2f us\n", runs, oldUs, newUs);
    }
    puts("PASS: 1500 mixed bounds fixtures bit-identical, exact full-width bounds preserved");
}
