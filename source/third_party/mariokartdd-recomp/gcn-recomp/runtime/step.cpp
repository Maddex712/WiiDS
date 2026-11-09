// The step scale: running a game stepped per frame at another frame rate.
//
// A game that advances its simulation by per-frame constants runs at double speed when it
// is paced at twice its frame rate. The fix is at the instructions that do the stepping:
// the game project lists them in its steps.txt (which operand of each carries the value
// from the previous frame), the recompiler emits those instructions through the functions
// below (recomp/ppc.py, translate_step), and the game sets the scale: 0.5 when it runs at
// twice its native rate, 1 otherwise. An integer step (a counter) cannot be halved, so it
// is taken on every 1/scale-th frame instead: the game calls step_frame() once a frame.
//
// x += t becomes x += s*t; x *= k becomes x *= k^s; x = x*k + b keeps its fixed point
// b/(1-k) and approaches it at the rate k^s. None of this is exact for a non-linear step,
// and the two halves of a frame see a different order of updates than one whole one, so a
// scaled run drifts from the native one; how far is for the game's own comparison to say.
//
// Each site also names a power p of the scale (1 when the table leaves it out): what a
// step stands for is s^p. A rate is p = 1. A force that a Verlet integrator adds straight
// to a position (x += v + F, with v the displacement of the previous step) is p = 2, since
// the displacement carries over whole and the force acts over the square of the step. A
// rate the game derives from a position difference (a speed measured as x - x_prev) is in
// units of the step, and p = -1 on its store converts it back to the game's units
// wherever it is read.
//
// Each site passes its address, so that GCN_STEP_SKIP=<file> (hex addresses or lo-hi
// ranges, one per line) can leave listed sites unscaled, and GCN_STEP_ONLY=<file> all but
// the listed ones: that is how a site that breaks the game is bisected out of a list of
// hundreds without a rebuild. GCN_STEP_POW=<file> (lines of `<hex address> <power>`)
// overrides the power of the listed sites, for trying a reading of a site (a force as a
// rate, a rate as a force) before it goes into the table.
//
// A body the game integrates in N passes a frame, each pass a complete step (forces,
// dampings, constraints) of a fixed size, is not helped by scaling the sites inside the
// pass: that runs N passes of size s per paced frame, N/s a game frame, and the body's
// dampings and corrections act too often. It runs as the game's own when the pass count
// is N*s per paced frame and the passes keep their size, which the game arranges by
// patching the count and bracketing the loop with step_override(1) and
// step_override(NAN): while a finite override is set it replaces the scale at every site.
#include "runtime.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
double g_scale = 1.0;
double g_override = NAN;   // replaces g_scale while a game's override is in force
uint32_t g_frame = 0;

// Per code word: is this site scaled. Empty means all are.
std::vector<uint8_t> g_on;
void load_list(const char* path, bool on_listed) {
    g_on.assign((g_recomp_code_end - g_recomp_code_base) / 4 + 1, on_listed ? 0 : 1);
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "[step] cannot read %s\n", path); return; }
    char line[128];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        char* end;
        uint32_t lo = (uint32_t)strtoul(line, &end, 16), hi = lo;
        if (end == line) continue;
        if (*end == '-') hi = (uint32_t)strtoul(end + 1, nullptr, 16);
        for (uint32_t a = lo; a <= hi && a < g_recomp_code_end; a += 4)
            if (a >= g_recomp_code_base) { g_on[(a - g_recomp_code_base) / 4] = on_listed ? 1 : 0; n++; }
    }
    fclose(f);
    fprintf(stderr, "[step] %s: %d sites %s\n", path, n, on_listed ? "scaled, the rest not" : "left unscaled");
}
// Per code word: a power that replaces the site's own, or NaN. Empty means none.
std::vector<float> g_pow;
void load_powers(const char* path) {
    g_pow.assign((g_recomp_code_end - g_recomp_code_base) / 4 + 1, NAN);
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "[step] cannot read %s\n", path); return; }
    char line[128];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        char* end;
        uint32_t a = (uint32_t)strtoul(line, &end, 16);
        if (end == line || a < g_recomp_code_base || a >= g_recomp_code_end) continue;
        g_pow[(a - g_recomp_code_base) / 4] = (float)strtod(end, nullptr);
        n++;
    }
    fclose(f);
    fprintf(stderr, "[step] %s: %d sites with their own power\n", path, n);
}
const bool lists_loaded = [] {
    if (const char* e = getenv("GCN_STEP_SKIP")) load_list(e, false);
    if (const char* e = getenv("GCN_STEP_ONLY")) load_list(e, true);
    if (const char* e = getenv("GCN_STEP_POW")) load_powers(e);
    return true;
}();
inline double scale_at(uint32_t pc, double p) {
    if (!g_on.empty() && pc >= g_recomp_code_base && pc < g_recomp_code_end && !g_on[(pc - g_recomp_code_base) / 4]) return 1.0;
    if (!g_pow.empty() && pc >= g_recomp_code_base && pc < g_recomp_code_end) {
        float q = g_pow[(pc - g_recomp_code_base) / 4];
        if (q == q) p = q;
    }
    double s = g_override == g_override ? g_override : g_scale;
    if (s == 1.0 || p == 1.0) return s;
    if (p == 2.0) return s * s;
    if (p == -1.0) return 1.0 / s;
    return pow(s, p);
}
}

void step_set_scale(double s) { g_scale = s > 0.0 ? s : 1.0; }
double step_scale() { return g_scale; }
void step_override(double s) { g_override = s; }
void step_frame() { g_frame++; }

extern "C" double gcn_step_scale(uint32_t pc, double p) { return scale_at(pc, p); }

extern "C" uint32_t gcn_step_int(uint32_t pc, double p) {
    double s = scale_at(pc, p);
    if (s >= 1.0) return 1u;
    uint32_t period = (uint32_t)llround(1.0 / s);
    return (g_frame % period) == 0 ? 1u : 0u;
}

extern "C" double gcn_step_pow(uint32_t pc, double p, double k) {
    double s = scale_at(pc, p);
    if (s == 1.0 || !(k > 0.0)) return k;
    return pow(k, s);
}

extern "C" double gcn_step_damp(uint32_t pc, double p, double x, double k, double b) {
    double s = scale_at(pc, p);
    if (s == 1.0) return fma(x, k, b);
    if (k > 0.0 && k < 1.0) {
        double k2 = pow(k, s);
        return x * k2 + b * (1.0 - k2) / (1.0 - k);
    }
    return x * k + b * s;  // not an approach: scale the addend only
}
