// GX front end (runs on the guest thread): vertex decoding, XF (transform, lighting,
// texgen) on the CPU, primitive assembly, pixel-state snapshots and EFB copies.
#include "../runtime.h"
#include "../platform.h"
#include "gx.h"
#include "render.h"
#include "texture.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <unordered_set>

namespace gx {

std::atomic<uint32_t> g_frames_submitted{0};
static std::unique_ptr<Batch> g_batch;
static uint32_t g_tev_reg[4][2], g_tev_konst[4][2];

// GCN_FRAMETIME=1 prints one line per presented frame with where the guest thread's time
// went: the interval since the last present, how much of it was the GX front end (vertex
// decode, transform and lighting, texture hashing and decoding), the batch's shape, and
// how full the submission queue was -- a full queue means the guest was waiting on the
// renderer, not the other way round. The renderer prints its own line per batch.
static bool frametime_on() {  // on first use: the app sets its environment after static init
    static const bool on = getenv("GCN_FRAMETIME") != nullptr;
    return on;
}
static double g_fe_ms, g_tex_ms;
static uint32_t g_fe_draws;
static std::chrono::steady_clock::time_point g_last_present;
static inline double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
size_t queue_depth();

// ---------------------------------------------------------------------------
// Submission queue
// ---------------------------------------------------------------------------
static std::mutex g_q_mutex;
static std::condition_variable g_q_cv;
static std::deque<std::unique_ptr<Batch>> g_queue;

// A batch holds every vertex of a frame -- around 10 MB during a race -- so the queue
// has to be a hard bound, not a hint. If the renderer is slower than the guest (a
// mobile GPU, say) an advisory wait that queues anyway grows this by tens of megabytes
// a second; on a Quest 3 that reached 3.8 GB and the app was killed by lowmemorykiller.
// It is also latency: when the renderer is the slow stage the queue stays full, and every
// frame shown is as many frames old as it holds, so it holds one (see kFeMaxFrames in
// fifo.cpp).
static constexpr size_t kMaxQueuedBatches = 1;

void submit_batch(std::unique_ptr<Batch> b) {
    std::unique_lock<std::mutex> lk(g_q_mutex);
    StallTimer stall(Stall::RenderFull);
    // Wait for room, which throttles the guest to the renderer's rate. The timeout is
    // only a safety valve: if nothing is consuming at all (no window, a stalled render
    // thread) the guest must not block forever, so drop the oldest frame instead.
    if (!g_q_cv.wait_for(lk, std::chrono::seconds(2),
                         [] { return g_queue.size() < kMaxQueuedBatches; })) {
        if (!g_queue.empty()) g_queue.pop_front();
    }
    g_queue.push_back(std::move(b));
    g_q_cv.notify_all();
}

size_t queue_depth() {
    std::lock_guard<std::mutex> lk(g_q_mutex);
    return g_queue.size();
}

// Frames the renderer has taken off the queue, for GCN_STALLS's latency figure.
static std::atomic<uint32_t> g_frames_taken{0};
uint32_t frames_taken() { return g_frames_taken.load(std::memory_order_relaxed); }

std::unique_ptr<Batch> take_batch(int timeout_ms) {
    std::unique_lock<std::mutex> lk(g_q_mutex);
    if (!g_q_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [] { return !g_queue.empty(); })) return nullptr;
    auto b = std::move(g_queue.front());
    g_queue.pop_front();
    g_frames_taken.fetch_add(1, std::memory_order_relaxed);
    g_q_cv.notify_all();
    return b;
}

// The storage pool; see Batch in render.h. Bounded so that a frontend that lets batches
// pile up somewhere does not keep that many frames' worth of capacity alive.
// Never destroyed: batches are recycled from static destructors at exit, and the pool
// must still be there when they are.
static std::mutex& pool_mutex() { static auto* m = new std::mutex; return *m; }
static std::vector<Batch>& pool() { static auto* p = new std::vector<Batch>; return *p; }
static constexpr size_t kMaxPooled = 12;

Batch::~Batch() {
    if (pooled) return;
    Batch b;
    b.pooled = true;
    b.cmds.swap(cmds);
    b.verts.swap(verts);
    b.packed.swap(packed);
    b.indices.swap(indices);
    b.states.swap(states);
    b.mtxs.swap(mtxs);
    b.new_textures.swap(new_textures);
    b.dead_textures.swap(dead_textures);
    // Emptied now, on the thread that is done with it: the decoded textures go with it.
    b.cmds.clear();
    b.verts.clear();
    b.packed.clear();
    b.indices.clear();
    b.states.clear();
    b.mtxs.clear();
    b.new_textures.clear();
    b.dead_textures.clear();
    std::lock_guard<std::mutex> lk(pool_mutex());
    if (pool().size() < kMaxPooled) pool().push_back(std::move(b));
}

// set_view_turn's latest, set and read on the guest thread.
static float g_view_turn[4] = {0.0f, 0.0f, 0.0f, 1.0f};

static Batch& batch() {
    if (!g_batch) {
        g_batch = std::make_unique<Batch>();
        std::lock_guard<std::mutex> lk(pool_mutex());
        if (!pool().empty()) {
            *g_batch = std::move(pool().back());
            pool().pop_back();
            g_batch->pooled = false;
        }
        memcpy(g_batch->view_turn, g_view_turn, sizeof(g_view_turn));
    }
    return *g_batch;
}

// set_view_turn, where the command stream has got to it (fifo.cpp): into the batch being
// built as well as every one after it.
void view_turn_reached(const float q[4]) {
    memcpy(g_view_turn, q, sizeof(g_view_turn));
    if (g_batch) memcpy(g_batch->view_turn, q, sizeof(g_view_turn));
}

// set_draw_tag, where the command stream has got to it: the states taken from here on
// carry it (PixelState::tag).
static uint8_t g_draw_tag;
void draw_tag_reached(uint8_t tag) {
    if (tag == g_draw_tag) return;
    g_draw_tag = tag;
    g_state.pixel_dirty = true;
}

// GCN_MTXLOG=<n> dumps the nth presented frame's draws; see draw_impl.
static const uint32_t g_mtxlog = getenv("GCN_MTXLOG") ? (uint32_t)atoi(getenv("GCN_MTXLOG")) : 0;
static uint32_t g_mtx_draw;
// Whether this is the frame GCN_MTXLOG dumps.
static bool mtxlog_frame() { return g_mtxlog && g_frame_counter == g_mtxlog; }

static void submit_for_transform(std::unique_ptr<Batch> b, std::unique_ptr<struct Pending> pd);
static void recycle_pending(std::unique_ptr<struct Pending> p);
static void pack_all(Batch& b);
static bool xf_sync();
static std::unique_ptr<struct Pending> g_pending;

static void flush_batch() {
    if (g_batch && !g_batch->cmds.empty()) {
        texture_take_dead(g_batch->dead_textures);
        // Transformed already (see xf_sync), or by the worker before the renderer sees it.
        // A batch with nothing to transform -- a copy alone, which a game that clears
        // strips of the EFB with copies makes between its passes -- still goes through the
        // worker's queue when it is in use: handed to the renderer directly, it overtook
        // the pass before it, still being transformed, and cleared its strip a pass early.
        if (xf_sync()) {
            pack_all(*g_batch);
            submit_batch(std::move(g_batch));
        } else if (!g_pending) {
            pack_all(*g_batch);
            submit_for_transform(std::move(g_batch), nullptr);
        }
        else submit_for_transform(std::move(g_batch), std::move(g_pending));
    }
    g_batch.reset();
    if (g_pending) recycle_pending(std::move(g_pending));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static inline float xf_f(uint32_t i) {
    uint32_t v = g_state.xf_mem[i & 0x7FF];
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static inline float xfr_f(uint32_t reg) {
    uint32_t v = g_state.xf_regs[reg & 0xFF];
    float f;
    memcpy(&f, &v, 4);
    return f;
}
// The transform reads XF state from snapshots taken when each draw arrived, not from
// g_state, because it runs after the frame is submitted and on other threads: see XfDraw.
// Each region's snapshot is indexed from the region's first word.
// A region's snapshot is a page table: one offset into the frame's snapshot words per
// sixteen-word block, each block copied only when it changed (see State::xf_block_gen).
struct XfView {
    const uint32_t* base;
    const uint32_t* pt[XF_REGIONS];
};
static thread_local XfView t_xf;
static inline uint32_t tw(XfRegion region, uint32_t i) {
    return t_xf.base[t_xf.pt[region][i >> 4] + (i & 15)];
}
static inline float tf(XfRegion region, uint32_t i) {
    const uint32_t w = tw(region, i);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static inline float read_comp(const uint8_t*& p, uint32_t fmt, float scale) {
    switch (fmt) {
    case 0: return (float)(*p++) * scale;
    case 1: return (float)(int8_t)(*p++) * scale;
    case 2: { float v = (float)rd16(p) * scale; p += 2; return v; }
    case 3: { float v = (float)(int16_t)rd16(p) * scale; p += 2; return v; }
    case 4: { uint32_t u = rd32(p); p += 4; float f; memcpy(&f, &u, 4); return f; }
    default: return 0;
    }
}
static inline uint32_t comp_size(uint32_t fmt) { static const uint32_t s[8] = {1, 1, 2, 2, 4, 0, 0, 0}; return s[fmt & 7]; }

// color as 0xRRGGBBAA
static uint32_t read_color(const uint8_t* p, uint32_t fmt) {
    switch (fmt) {
    case 0: { uint16_t v = rd16(p); uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
              return (((r << 3) | (r >> 2)) << 24) | (((g << 2) | (g >> 4)) << 16) | (((b << 3) | (b >> 2)) << 8) | 0xFF; }
    case 1: case 2: return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | 0xFF;
    case 3: { uint16_t v = rd16(p);
              return ((((v >> 12) & 15) * 17) << 24) | ((((v >> 8) & 15) * 17) << 16) | ((((v >> 4) & 15) * 17) << 8) | ((v & 15) * 17); }
    case 4: { uint32_t v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
              auto e = [](uint32_t x) { return (x << 2) | (x >> 4); };
              return (e((v >> 18) & 63) << 24) | (e((v >> 12) & 63) << 16) | (e((v >> 6) & 63) << 8) | e(v & 63); }
    case 5: return rd32(p);
    }
    return 0xFFFFFFFF;
}
static uint32_t color_size(uint32_t fmt) { static const uint32_t s[8] = {2, 3, 4, 2, 3, 4, 4, 4}; return s[fmt & 7]; }

struct InVertex {
    // Left uninitialised on purpose, as GpuVertex is: the frame's input vertices are grown
    // with resize() and every field is written by decode_vertex straight after.
    InVertex() {}
    uint32_t pos_idx;             // the position's array index, for GCN_VTXLOG
    float pos[3];
    float nrm[3], bin[3], tan[3];
    uint32_t col[2];
    float tex[8][2];
    uint8_t pnmtx;
    uint8_t texmtx[8];
};

// Reads `N` components of one format into floats, exactly as read_comp would, one
// specialisation per format and count. Which one an attribute uses is fixed by the vertex
// layout, so it is chosen once per layout (make_layout) rather than switched on for every
// component of every vertex -- the decode is the largest thing left on the guest thread.
using CompReader = void (*)(const uint8_t* q, float* dst, float scale);
template <uint32_t FMT>
static inline float read_one(const uint8_t* q, float scale) {
    if constexpr (FMT == 0) return (float)q[0] * scale;
    else if constexpr (FMT == 1) return (float)(int8_t)q[0] * scale;
    else if constexpr (FMT == 2) { uint16_t v; memcpy(&v, q, 2); return (float)GCN_BSWAP16(v) * scale; }
    else if constexpr (FMT == 3) { uint16_t v; memcpy(&v, q, 2); return (float)(int16_t)GCN_BSWAP16(v) * scale; }
    else if constexpr (FMT == 4) { uint32_t v; memcpy(&v, q, 4); v = GCN_BSWAP32(v); float f; memcpy(&f, &v, 4); return f; }
    else return 0.0f;
}
template <uint32_t FMT, int N>
static void read_n(const uint8_t* q, float* dst, float scale) {
    constexpr uint32_t sz = FMT == 4 ? 4 : FMT >= 2 && FMT <= 3 ? 2 : FMT <= 1 ? 1 : 0;
    for (int i = 0; i < N; i++) dst[i] = read_one<FMT>(q + i * sz, scale);
}
template <int N>
static CompReader reader_for(uint32_t fmt) {
    switch (fmt) {
    case 0: return read_n<0, N>;
    case 1: return read_n<1, N>;
    case 2: return read_n<2, N>;
    case 3: return read_n<3, N>;
    case 4: return read_n<4, N>;
    default: return read_n<5, N>;
    }
}
static CompReader reader_for_count(uint32_t fmt, uint32_t n) {
    return n == 1 ? reader_for<1>(fmt) : n == 2 ? reader_for<2>(fmt) : reader_for<3>(fmt);
}

struct Layout {
    uint32_t pnmtx, texmtx;       // presence bits
    uint32_t pos_desc, pos_fmt, pos_cnt; float pos_scale;
    uint32_t nrm_desc, nrm_fmt; bool nbt, idx3; float nrm_scale;
    uint32_t col_desc[2], col_fmt[2];
    uint32_t tex_desc[8], tex_fmt[8], tex_cnt[8]; float tex_scale[8];
    // Resolved from the above by make_layout: see CompReader.
    CompReader pos_rd, nrm_rd, tex_rd[8];
    uint32_t pos_size, nrm_csz, col_size[2], tex_size[8];
    uint32_t tex_used;            // bit t: texcoord t present
};

static Layout make_layout(int vat) {
    const uint32_t* cp = g_state.cp;
    uint32_t lo = cp[0x50], hi = cp[0x60], a = cp[0x70 + vat], b = cp[0x80 + vat], c = cp[0x90 + vat];
    Layout L{};
    L.pnmtx = lo & 1;
    L.texmtx = (lo >> 1) & 0xFF;
    L.pos_desc = (lo >> 9) & 3; L.pos_cnt = (a & 1) ? 3 : 2; L.pos_fmt = (a >> 1) & 7;
    L.pos_scale = 1.0f / (float)(1u << ((a >> 4) & 31));
    L.nrm_desc = (lo >> 11) & 3; L.nbt = (a >> 9) & 1; L.nrm_fmt = (a >> 10) & 7; L.idx3 = (a >> 31) & 1;
    L.nrm_scale = (L.nrm_fmt == 1 || L.nrm_fmt == 0) ? 1.0f / 64 : (L.nrm_fmt == 4 ? 1.0f : 1.0f / 16384);
    L.col_desc[0] = (lo >> 13) & 3; L.col_fmt[0] = (a >> 14) & 7;
    L.col_desc[1] = (lo >> 15) & 3; L.col_fmt[1] = (a >> 18) & 7;
    // GCN_VATLOG=1: each vertex layout as it is built (VCD and VAT), for reading a draw's
    // vertex data against the format it was decoded with.
    static const bool vatlog = getenv("GCN_VATLOG") != nullptr;
    if (vatlog)
        fprintf(stderr, "[vat] vat%d vcd=%08X/%08X A=%08X B=%08X C=%08X pos desc=%u cnt=%u fmt=%u nrm desc=%u fmt=%u col0 desc=%u fmt=%u col1 desc=%u fmt=%u\n", vat, lo, hi, a, b, c,
                L.pos_desc, L.pos_cnt, L.pos_fmt, L.nrm_desc, L.nrm_fmt, L.col_desc[0], L.col_fmt[0], L.col_desc[1], L.col_fmt[1]);
    uint32_t tc[8] = {(a >> 21) & 1, b & 1, (b >> 9) & 1, (b >> 18) & 1, (b >> 27) & 1, (c >> 5) & 1, (c >> 14) & 1, (c >> 23) & 1};
    uint32_t tf[8] = {(a >> 22) & 7, (b >> 1) & 7, (b >> 10) & 7, (b >> 19) & 7, (b >> 28) & 7, (c >> 6) & 7, (c >> 15) & 7, (c >> 24) & 7};
    uint32_t ts[8] = {(a >> 25) & 31, (b >> 4) & 31, (b >> 13) & 31, (b >> 22) & 31, c & 31, (c >> 9) & 31, (c >> 18) & 31, (c >> 27) & 31};
    for (int i = 0; i < 8; i++) {
        L.tex_desc[i] = (hi >> (2 * i)) & 3;
        L.tex_cnt[i] = tc[i] ? 2 : 1;
        L.tex_fmt[i] = tf[i];
        L.tex_scale[i] = 1.0f / (float)(1u << ts[i]);
    }
    L.pos_rd = reader_for_count(L.pos_fmt, L.pos_cnt);
    L.pos_size = L.pos_cnt * comp_size(L.pos_fmt);
    L.nrm_rd = reader_for_count(L.nrm_fmt, 3);
    L.nrm_csz = comp_size(L.nrm_fmt);
    for (int ci = 0; ci < 2; ci++) L.col_size[ci] = color_size(L.col_fmt[ci]);
    L.tex_used = 0;
    for (int i = 0; i < 8; i++) {
        L.tex_rd[i] = reader_for_count(L.tex_fmt[i], L.tex_cnt[i]);
        L.tex_size[i] = L.tex_cnt[i] * comp_size(L.tex_fmt[i]);
        if (L.tex_desc[i]) L.tex_used |= 1u << i;
    }
    return L;
}

// Layouts come from the CP registers only, which the game sets per model rather than
// per draw, so they are cached against State::cp_gen.
static Layout g_layouts[8];
static uint32_t g_layouts_valid, g_layouts_gen;

static const Layout& layout_for(int vat) {
    if (g_layouts_gen != g_state.cp_gen) {
        g_layouts_valid = 0;
        g_layouts_gen = g_state.cp_gen;
    }
    if (!(g_layouts_valid & (1u << vat))) {
        g_layouts[vat] = make_layout(vat);
        g_layouts_valid |= 1u << vat;
    }
    return g_layouts[vat];
}

// Resolve an attribute pointer: direct data or indexed array element.
static const uint8_t* attr_ptr(const uint8_t*& p, uint32_t desc, int array, uint32_t direct_size, uint32_t* idx_out = nullptr) {
    if (desc == 1) { const uint8_t* r = p; p += direct_size; if (idx_out) *idx_out = ~0u; return r; }
    uint32_t idx;
    if (desc == 2) idx = *p++;
    else { idx = rd16(p); p += 2; }
    if (idx_out) *idx_out = idx;
    uint32_t base = g_state.cp[0xA0 + array], stride = g_state.cp[0xB0 + array];
    return phys_ptr(base + idx * stride);
}

// The matrix indices a vertex that does not carry its own falls back to. XF registers,
// so they belong to the per-draw plan rather than to the CP-derived Layout.
static uint8_t g_defpnmtx, g_deftex[8];

// The normal, binormal and tangent the transform unit keeps from the last vertex that
// carried them, as Dolphin has it, for a vertex without them. They were left
// uninitialised, and an emboss texgen offset its coordinate by whatever the memory held --
// bright streaks of a bump-mapped hull's own texture crawling over it.
// Saved at the end of each draw (draw_impl) rather than after every vertex: a draw's layout
// either carries them, and none of its vertices reads these, or does not, and none writes
// them. Copied per vertex, the copy read back floats the reader had just stored one at a
// time, which a core cannot forward to a wider load; waiting for them to land was a tenth
// of the front end's time in a scene of skinned models.
static float g_last_nrm[3] = {0, 0, 1}, g_last_bin[3] = {0, 0, 0}, g_last_tan[3] = {0, 0, 0};

static void decode_vertex(const Layout& L, const uint8_t*& p, InVertex& v) {
    v.pnmtx = L.pnmtx ? *p++ : g_defpnmtx;
    memcpy(v.texmtx, g_deftex, sizeof(v.texmtx));
    if (L.texmtx)
        for (int i = 0; i < 8; i++)
            if (L.texmtx & (1u << i)) v.texmtx[i] = *p++;
    v.pos_idx = ~0u;
    if (L.pos_desc) {
        const uint8_t* q = attr_ptr(p, L.pos_desc, 0, L.pos_size, &v.pos_idx);
        v.pos[2] = 0.0f;
        L.pos_rd(q, v.pos, L.pos_scale);
    }
    // A vertex without a normal, or with a normal but no binormal and tangent, has the
    // last ones loaded: see g_last_nrm.
    if (!L.nrm_desc) memcpy(v.nrm, g_last_nrm, sizeof(v.nrm));
    if (!L.nrm_desc || !L.nbt) { memcpy(v.bin, g_last_bin, sizeof(v.bin)); memcpy(v.tan, g_last_tan, sizeof(v.tan)); }
    if (L.nrm_desc) {
        const uint32_t csz = L.nrm_csz;
        if (L.nbt && L.idx3 && L.nrm_desc != 1) {
            float* dst[3] = {v.nrm, v.bin, v.tan};
            for (int k = 0; k < 3; k++) {
                const uint8_t* q = attr_ptr(p, L.nrm_desc, 1, 0);
                L.nrm_rd(q + k * 3 * csz, dst[k], L.nrm_scale);
            }
        } else {
            const uint8_t* q = attr_ptr(p, L.nrm_desc, 1, (L.nbt ? 9 : 3) * csz);
            L.nrm_rd(q, v.nrm, L.nrm_scale);
            if (L.nbt) {
                L.nrm_rd(q + 3 * csz, v.bin, L.nrm_scale);
                L.nrm_rd(q + 6 * csz, v.tan, L.nrm_scale);
            }
        }
    }
    for (int ci = 0; ci < 2; ci++) {
        if (!L.col_desc[ci]) { v.col[ci] = 0xFFFFFFFF; continue; }
        const uint8_t* q = attr_ptr(p, L.col_desc[ci], 2 + ci, L.col_size[ci]);
        v.col[ci] = read_color(q, L.col_fmt[ci]);
    }
    // Texcoords absent from the layout are zero; each present one is read in one call.
    memset(v.tex, 0, sizeof(v.tex));
    for (uint32_t used = L.tex_used; used; used &= used - 1) {
        const int t = __builtin_ctz(used);
        const uint8_t* q = attr_ptr(p, L.tex_desc[t], 4 + t, L.tex_size[t]);
        L.tex_rd[t](q, v.tex[t], L.tex_scale[t]);
    }
}

// ---------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------
struct Vec3 { float x, y, z; };
static inline Vec3 v3(float x, float y, float z) { return {x, y, z}; }
static inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
// One division and three multiplies rather than three divisions. This is the hottest
// floating-point operation in the front end -- once for the normal and once per light
// per vertex -- and a division is not pipelined on the cores this runs on.
//
// The reciprocal costs up to an ulp against dividing three times. The result is a
// direction fed to the lighting, whose output is quantised to eight bits per channel
// before it leaves this file, so the difference cannot reach a pixel; if an accuracy
// question ever lands here, this is the line to put back.
static inline Vec3 normalize(Vec3 a) {
    float l = sqrtf(dot(a, a));
    if (!(l > 0)) return a;
    const float inv = 1.0f / l;
    return Vec3{a.x * inv, a.y * inv, a.z * inv};
}

// Eight-bit channel to float, by table. Four divisions per unpack and up to eight
// unpacks per vertex made this one of the most expensive things in the transform, and a
// table of the 256 possible results is exactly equal to the division it replaces.
static const float kU8toF[256] = {
#define GCN_U8_ROW(n) (n) / 255.0f, (n + 1) / 255.0f, (n + 2) / 255.0f, (n + 3) / 255.0f,                      (n + 4) / 255.0f, (n + 5) / 255.0f, (n + 6) / 255.0f, (n + 7) / 255.0f
#define GCN_U8_ROW32(n) GCN_U8_ROW(n), GCN_U8_ROW(n + 8), GCN_U8_ROW(n + 16), GCN_U8_ROW(n + 24)
    GCN_U8_ROW32(0),   GCN_U8_ROW32(32),  GCN_U8_ROW32(64),  GCN_U8_ROW32(96),
    GCN_U8_ROW32(128), GCN_U8_ROW32(160), GCN_U8_ROW32(192), GCN_U8_ROW32(224),
#undef GCN_U8_ROW32
#undef GCN_U8_ROW
};

static inline void unpack_rgba(uint32_t c, float out[4]) {
    out[0] = kU8toF[(c >> 24) & 0xFF]; out[1] = kU8toF[(c >> 16) & 0xFF];
    out[2] = kU8toF[(c >> 8) & 0xFF];  out[3] = kU8toF[c & 0xFF];
}

// A light's parameters, read out of XF memory once per draw rather than once per vertex
// per light. Twelve unpacked floats, a colour unpack and a normalise is a great deal of
// work to repeat for every vertex when none of it changes within a draw, and this game
// lights most of what it draws.
struct LightParams {
    float col[4];
    Vec3 pos, dir, cosatt, distatt, distatt_n;
};
static thread_local LightParams g_lights[8];
static thread_local uint32_t g_lights_loaded;   // which of g_lights are valid for the current draw

static void load_light(int i) {
    const uint32_t base = i * 0x10;
    LightParams& L = g_lights[i];
    unpack_rgba(tw(XF_LIGHT, base + 3), L.col);
    L.pos = v3(tf(XF_LIGHT, base + 10), tf(XF_LIGHT, base + 11), tf(XF_LIGHT, base + 12));
    L.dir = v3(tf(XF_LIGHT, base + 13), tf(XF_LIGHT, base + 14), tf(XF_LIGHT, base + 15));
    L.cosatt = v3(tf(XF_LIGHT, base + 4), tf(XF_LIGHT, base + 5), tf(XF_LIGHT, base + 6));
    L.distatt = v3(tf(XF_LIGHT, base + 7), tf(XF_LIGHT, base + 8), tf(XF_LIGHT, base + 9));
    L.distatt_n = normalize(L.distatt);
}

// Compute light accumulation for one channel component set (rgb or alpha).
static void light_channel(uint32_t ctrl, Vec3 pos, Vec3 nrm, float lacc[4], bool alpha) {
    uint32_t mask = ((ctrl >> 2) & 0xF) | (((ctrl >> 11) & 0xF) << 4);
    uint32_t diff = (ctrl >> 7) & 3;
    uint32_t attn = (ctrl >> 9) & 3;
    for (int i = 0; i < 8; i++) {
        if (!(mask & (1u << i))) continue;
        if (!(g_lights_loaded & (1u << i))) {
            load_light(i);
            g_lights_loaded |= 1u << i;
        }
        const LightParams& L = g_lights[i];
        const float* col = L.col;
        const Vec3 lpos = L.pos, ldir_param = L.dir, cosatt = L.cosatt, distatt = L.distatt;
        Vec3 ldir;
        float a = 1.0f;
        if (attn == 1) {  // specular
            ldir = normalize(sub(lpos, pos));  // GXInitSpecularDir puts the light "far away" along -dir
            float nd = dot(nrm, ldir);
            float h = nd >= 0 ? std::max(0.0f, dot(nrm, ldir_param)) : 0.0f;
            Vec3 da = diff == 0 ? distatt : L.distatt_n;
            float num = std::max(0.0f, cosatt.x + cosatt.y * h + cosatt.z * h * h);
            float den = da.x + da.y * h + da.z * h * h;
            a = den != 0 ? num / den : 0.0f;
        } else if (attn == 3) {  // spot
            Vec3 d = sub(lpos, pos);
            float dist2 = dot(d, d), dist = sqrtf(dist2);
            ldir = dist > 0 ? Vec3{d.x / dist, d.y / dist, d.z / dist} : d;
            float c = std::max(0.0f, dot(ldir, ldir_param));
            float num = std::max(0.0f, cosatt.x + cosatt.y * c + cosatt.z * c * c);
            float den = distatt.x + distatt.y * dist + distatt.z * dist2;
            a = den != 0 ? num / den : 0.0f;
        } else {
            ldir = normalize(sub(lpos, pos));
        }
        float d = 1.0f;
        if (diff == 1) d = dot(ldir, nrm);
        else if (diff == 2) d = std::max(0.0f, dot(ldir, nrm));
        float f = a * d;
        if (alpha) lacc[3] += f * col[3];
        else { lacc[0] += f * col[0]; lacc[1] += f * col[1]; lacc[2] += f * col[2]; }
    }
}

// One colour channel's registers, unpacked once per draw. The material and ambient
// registers were unpacked four times per vertex between them, and all four readings are
// of the same two registers.
struct ChanParams {
    uint32_t cctrl, actrl;
    float mat[4], amb[4];
};
static thread_local ChanParams g_chan[2];

static void load_chan(int chan) {
    ChanParams& C = g_chan[chan];
    C.cctrl = tw(XF_REGS, 0x0E + chan);
    C.actrl = tw(XF_REGS, 0x10 + chan);
    unpack_rgba(tw(XF_REGS, 0x0C + chan), C.mat);
    unpack_rgba(tw(XF_REGS, 0x0A + chan), C.amb);
}

static uint32_t compute_color(int chan, const InVertex& v, Vec3 pos, Vec3 nrm) {
    const ChanParams& C = g_chan[chan];
    const uint32_t cctrl = C.cctrl, actrl = C.actrl;
    // The vertex's own colour, needed only where one of the four selectors asks for it.
    float vcol[4];
    if ((cctrl | actrl) & 0x41) unpack_rgba(v.col[chan], vcol);
    const float* mat = (cctrl & 1) ? vcol : C.mat;
    const float* amb = (cctrl & 0x40) ? vcol : C.amb;
    const float* amat = (actrl & 1) ? vcol : C.mat;
    const float* aamb = (actrl & 0x40) ? vcol : C.amb;
    float out[4];
    // color
    float rgb[3] = {mat[0], mat[1], mat[2]};
    if (cctrl & 2) {
        float lacc[4] = {amb[0], amb[1], amb[2], 0};
        light_channel(cctrl, pos, nrm, lacc, false);
        for (int k = 0; k < 3; k++) rgb[k] = mat[k] * std::clamp(lacc[k], 0.0f, 1.0f);
    }
    // alpha
    float al = amat[3];
    if (actrl & 2) {
        float lacc[4] = {0, 0, 0, aamb[3]};
        light_channel(actrl, pos, nrm, lacc, true);
        al = amat[3] * std::clamp(lacc[3], 0.0f, 1.0f);
    }
    out[0] = rgb[0]; out[1] = rgb[1]; out[2] = rgb[2]; out[3] = al;
    uint32_t r = 0;
    for (int k = 0; k < 4; k++) r = (r << 8) | (uint32_t)lrintf(std::clamp(out[k], 0.0f, 1.0f) * 255.0f);
    return r;
}

// ---------------------------------------------------------------------------
// Transform one vertex
// ---------------------------------------------------------------------------

// What the transform reads that cannot change within a draw: the channel and texgen
// counts, and each texgen's bit-packed description. A draw here averages under eight
// vertices, so unpacking these per vertex was a large share of the transform.
struct GenParams {
    uint32_t proj, form, type, row, srcrow, light, post, postnorm;
};
// Whether any texgen embosses, which is the only thing that uses the transformed
// binormal and tangent. Without one, normalising them per vertex -- two square roots
// and six divisions -- produced values nothing read.
static thread_local bool g_need_tangents;
static thread_local GenParams g_gen[8];
static thread_local uint32_t g_numcol, g_ntex;
static thread_local bool g_dualtex;

// The position and normal matrices for the pnmtx last seen, so a draw whose vertices all
// index one matrix -- every draw in this game -- reads it out of XF memory once. Reset
// per draw, since a matrix may be loaded between draws.
static thread_local uint32_t g_mtx_cached;
static thread_local float g_posmtx[12], g_nrmmtx[9];

// The texture matrices the same way: a regular texgen read its twelve words through the
// snapshot's page table for every vertex, two dependent loads each, and with two texgens
// on nearly every vertex that was a quarter of the transform's memory traffic for a
// matrix the whole draw shares. Kept per texgen against the index the vertex names, and
// dropped with the position matrix when the matrix memory's snapshot changes. The post
// matrices are named by the plan rather than the vertex, so they are kept with it, and
// dropped when the post-matrix memory's snapshot changes.
static thread_local uint8_t g_texmtx_idx[8];    // 0xFF: nothing cached for that texgen
static thread_local float g_texmtx[8][12];
static thread_local uint32_t g_post_loaded;     // bit t: g_postmtx[t] holds texgen t's
static thread_local float g_postmtx[8][12];

static void forget_matrices() {
    g_mtx_cached = 0xFFFFFFFFu;
    memset(g_texmtx_idx, 0xFF, sizeof(g_texmtx_idx));
}

static void load_matrices(uint8_t pnmtx) {
    const uint32_t m = (pnmtx & 63) * 4;
    for (int i = 0; i < 12; i++) g_posmtx[i] = tf(XF_MTX, m + i);
    const uint32_t n = (pnmtx & 31) * 3;
    for (int i = 0; i < 9; i++) g_nrmmtx[i] = tf(XF_NRM, n + i);
    g_mtx_cached = pnmtx;
}

static inline const float* texgen_matrix(uint32_t t, uint8_t texmtx) {
    const uint8_t idx = texmtx & 63;
    if (g_texmtx_idx[t] != idx) {
        const uint32_t tm = (uint32_t)idx * 4;
        for (int i = 0; i < 12; i++) g_texmtx[t][i] = tf(XF_MTX, tm + i);
        g_texmtx_idx[t] = idx;
    }
    return g_texmtx[t];
}

static inline const float* post_matrix(uint32_t t, uint32_t post) {
    if (!(g_post_loaded & (1u << t))) {
        const uint32_t ptm = post * 4;
        for (int i = 0; i < 12; i++) g_postmtx[t][i] = tf(XF_POST, ptm + i);
        g_post_loaded |= 1u << t;
    }
    return g_postmtx[t];
}

// The matrix indices a vertex falls back to are needed when it is decoded, on the guest
// thread, from the live registers. Called once per draw, before decoding.
static void load_decode_defaults() {
    const uint32_t mi0 = g_state.xf_regs[0x18], mi1 = g_state.xf_regs[0x19];
    g_defpnmtx = (uint8_t)(mi0 & 63);
    g_deftex[0] = (uint8_t)((mi0 >> 6) & 63);  g_deftex[1] = (uint8_t)((mi0 >> 12) & 63);
    g_deftex[2] = (uint8_t)((mi0 >> 18) & 63); g_deftex[3] = (uint8_t)((mi0 >> 24) & 63);
    g_deftex[4] = (uint8_t)(mi1 & 63);         g_deftex[5] = (uint8_t)((mi1 >> 6) & 63);
    g_deftex[6] = (uint8_t)((mi1 >> 12) & 63); g_deftex[7] = (uint8_t)((mi1 >> 18) & 63);
}

// Called once per draw, before the vertex loop, with t_xf pointing at the draw's
// snapshots.
static void load_xf_plan() {
    g_numcol = tw(XF_REGS, 0x09) & 3;
    g_ntex = tw(XF_REGS, 0x3F) & 15;
    g_dualtex = tw(XF_REGS, 0x12) & 1;
    g_need_tangents = false;
    for (uint32_t t = 0; t < g_ntex && t < 8; t++) {
        const uint32_t info = tw(XF_REGS, 0x40 + t);
        const uint32_t post = tw(XF_REGS, 0x50 + t);
        GenParams& G = g_gen[t];
        G.proj = (info >> 1) & 1;
        G.form = (info >> 2) & 1;
        G.type = (info >> 4) & 7;
        G.row = (info >> 7) & 31;
        G.srcrow = (info >> 12) & 7;
        G.light = (info >> 15) & 7;
        G.post = post & 63;
        G.postnorm = (post >> 8) & 1;
        if (G.type == 1) g_need_tangents = true;   // emboss
    }
    load_chan(0);
    load_chan(1);
    g_lights_loaded = 0;
    g_post_loaded = 0;
    forget_matrices();
}

static uint32_t g_draw_seq;  // front-end draws so far, stamped into each vertex
static thread_local uint32_t t_draw_seq;  // the draw being transformed

static void transform_vertex(const InVertex& v, GpuVertex& o, bool has_nrm) {
    o.src_idx = v.pos_idx;
    o.src_draw = t_draw_seq;
    if (v.pnmtx != g_mtx_cached) load_matrices(v.pnmtx);
    const float* pm = g_posmtx;
    const float px = v.pos[0], py = v.pos[1], pz = v.pos[2];
    Vec3 pos = v3(pm[0] * px + pm[1] * py + pm[2] * pz + pm[3],
                  pm[4] * px + pm[5] * py + pm[6] * pz + pm[7],
                  pm[8] * px + pm[9] * py + pm[10] * pz + pm[11]);
    o.pos[0] = pos.x; o.pos[1] = pos.y; o.pos[2] = pos.z;
    const float* nm = g_nrmmtx;
    auto nmul = [&](const float* a) {
        return normalize(v3(nm[0] * a[0] + nm[1] * a[1] + nm[2] * a[2],
                            nm[3] * a[0] + nm[4] * a[1] + nm[5] * a[2],
                            nm[6] * a[0] + nm[7] * a[1] + nm[8] * a[2]));
    };
    Vec3 nrm = has_nrm ? nmul(v.nrm) : v3(0, 0, 1);
    // Only an emboss texgen reads these; see g_need_tangents. Not normalised: GX does not,
    // and a game scales them so that the light's direction along them is the bump's offset
    // in texture coordinates. Normalised, the offset was a sizeable fraction of the
    // texture, and the second, shifted sample of the hull showed its panel lines elsewhere.
    Vec3 bin = v3(0, 0, 0), tan = v3(0, 0, 0);
    if (g_need_tangents) {
        auto mul = [&](const float* a) {
            return v3(nm[0] * a[0] + nm[1] * a[1] + nm[2] * a[2], nm[3] * a[0] + nm[4] * a[1] + nm[5] * a[2],
                      nm[6] * a[0] + nm[7] * a[1] + nm[8] * a[2]);
        };
        bin = mul(v.bin);
        tan = mul(v.tan);
    }

    const uint32_t numcol = g_numcol;
    for (int ch = 0; ch < 2; ch++) {
        uint32_t c = ch < (int)numcol ? compute_color(ch, v, pos, nrm) : 0xFFFFFFFF;
        o.col[ch][0] = c >> 24; o.col[ch][1] = c >> 16; o.col[ch][2] = c >> 8; o.col[ch][3] = (uint8_t)c;
    }

    const uint32_t ntex = g_ntex;
    const bool dualtex = g_dualtex;
    for (uint32_t t = 0; t < 8; t++) {
        if (t >= ntex) { o.tex[t][0] = o.tex[t][1] = 0; o.tex[t][2] = 1; continue; }
        const GenParams& G = g_gen[t];
        const uint32_t proj = G.proj, form = G.form, type = G.type, row = G.row;
        float src[4] = {0, 0, 1, 1};
        switch (row) {
        case 0: src[0] = v.pos[0]; src[1] = v.pos[1]; src[2] = v.pos[2]; break;
        case 1: src[0] = v.nrm[0]; src[1] = v.nrm[1]; src[2] = v.nrm[2]; break;
        case 2: break;  // colors (used by color texgens)
        case 3: src[0] = v.bin[0]; src[1] = v.bin[1]; src[2] = v.bin[2]; break;  // binormal T
        case 4: src[0] = v.tan[0]; src[1] = v.tan[1]; src[2] = v.tan[2]; break;  // binormal B
        default: if (row >= 5 && row <= 12) { src[0] = v.tex[row - 5][0]; src[1] = v.tex[row - 5][1]; } break;
        }
        if (form == 0) src[2] = 1.0f;  // AB11
        float s = 0, tt = 0, q = 1;
        if (type == 0) {  // regular
            const float* M = texgen_matrix(t, v.texmtx[t]);
            s = M[0] * src[0] + M[1] * src[1] + M[2] * src[2] + M[3];
            tt = M[4] * src[0] + M[5] * src[1] + M[6] * src[2] + M[7];
            if (proj) q = M[8] * src[0] + M[9] * src[1] + M[10] * src[2] + M[11];
        } else if (type == 1) {  // emboss
            const uint32_t srcrow = G.srcrow, light = G.light;
            if (!(g_lights_loaded & (1u << light))) {
                load_light((int)light);
                g_lights_loaded |= 1u << light;
            }
            Vec3 ldir = normalize(sub(g_lights[light].pos, pos));
            s = o.tex[srcrow][0] + dot(ldir, tan);
            tt = o.tex[srcrow][1] + dot(ldir, bin);
            q = o.tex[srcrow][2];
        } else {  // color0/color1 -> s,t
            int ch = type == 2 ? 0 : 1;
            s = kU8toF[o.col[ch][0]];
            tt = kU8toF[o.col[ch][1]];
        }
        if (dualtex && type == 0) {
            const float* P = post_matrix(t, G.post);
            float in[3] = {s, tt, q};
            if (G.postnorm) {
                float l = sqrtf(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
                if (l > 0) { in[0] /= l; in[1] /= l; in[2] /= l; }
            }
            s = P[0] * in[0] + P[1] * in[1] + P[2] * in[2] + P[3];
            tt = P[4] * in[0] + P[5] * in[1] + P[6] * in[2] + P[7];
            q = P[8] * in[0] + P[9] * in[1] + P[10] * in[2] + P[11];
        }
        o.tex[t][0] = s; o.tex[t][1] = tt; o.tex[t][2] = q;
    }
}

// ---------------------------------------------------------------------------
// Pixel state snapshot
// ---------------------------------------------------------------------------
static void resolve_texture(PixelState& st, int map) {
    const uint32_t* bp = g_state.bp;
    uint32_t base = map < 4 ? 0x80 + map : 0xA0 + (map - 4);
    uint32_t mode0 = bp[base], mode1 = bp[base + 4], img0 = bp[base + 8], img3 = bp[base + 0x14], tlut = bp[base + 0x18];
    TexParams p{};
    p.addr = (img3 & 0x1FFFFF) << 5;
    p.width = (img0 & 0x3FF) + 1;
    p.height = ((img0 >> 10) & 0x3FF) + 1;
    p.fmt = (img0 >> 20) & 0xF;
    bool mips = (mode0 >> 5) & 3;
    uint32_t levels = 1;
    if (mips) {
        uint32_t maxlod = ((mode1 >> 8) & 0xFF) / 16;
        uint32_t w = p.width, h = p.height;
        while (levels <= maxlod && (w > 1 || h > 1)) { w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1; levels++; }
    }
    p.levels = levels;
    p.tlut_off = (tlut & 0x3FF) << 9;
    p.tlut_fmt = (tlut >> 10) & 3;
    const auto t0 = frametime_on() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    TexLookup r = texture_lookup(p, batch().new_textures);
    if (frametime_on()) g_tex_ms += ms_since(t0);
    st.tex_id[map] = r.id;
    st.tex_is_efb[map] = r.efb;
    st.tex_efb_fmt[map] = (uint8_t)r.efb_fmt;
    st.tex_pal[map] = r.pal_id;
    st.tex_pal_bits[map] = (uint8_t)r.pal_bits;
}

static PixelState g_last_state;
static bool g_have_last_state;
static uint32_t g_last_state_idx;

// Whether position matrix `pnmtx` leaves the view-space Y axis alone -- it may turn the
// object about that axis, but not tilt it -- which is how a thing the game has placed in
// front of the camera is told from world geometry.
//
// Translation is not tested: the countdown rig is placed at (0, 30, -160), and something
// held a fixed distance in front of the camera is as much a part of the HUD as something
// at the origin.
//
// The rotation used to have to be the identity, which is true of the rig only once it has
// finished arriving. It spins in about the vertical axis over ten frames -- cos running
// 0.833 to 1 while it descends -- and for those ten frames it was read as world geometry,
// drawn with the world's pitch taken out of it, and then snapped onto the HUD frame the
// moment the spin stopped. Allowing the turn is what makes it arrive the way it does on a
// television.
//
// Nothing in the world passes this. A frame's worth of matrices at the start of a race --
// 6,464 of them over sixteen frames -- holds three that leave the Y axis alone: the two
// 2D layers at the origin, and the rig. World geometry cannot, because its matrix carries
// the camera, and this game's chase camera is pitched some 23 degrees down at all times.
//
// The matrix is taken from the draw's first vertex. A draw whose vertices indexed
// different position matrices would be judged by that one; none in this game does.
static bool pos_matrix_is_view_space(uint8_t pnmtx) {
    // GCN_VS_YAW=0 goes back to demanding the identity rotation, which is what this did
    // before it was taught to allow a turn. For telling apart "the rig is misplaced" from
    // "something else was reclassified by the wider test".
    static const bool allow_yaw = !(getenv("GCN_VS_YAW") && atoi(getenv("GCN_VS_YAW")) == 0);
    const uint32_t m = (pnmtx & 63) * 4;
    if (!allow_yaw) {
        for (int i = 0; i < 9; i++) {
            const float want = (i % 4 == 0) ? 1.0f : 0.0f;
            if (fabsf(xf_f(m + (i / 3) * 4 + (i % 3)) - want) > 1e-4f) return false;
        }
        return true;
    }
    const float m00 = xf_f(m + 0), m01 = xf_f(m + 1), m02 = xf_f(m + 2);
    const float m10 = xf_f(m + 4), m11 = xf_f(m + 5), m12 = xf_f(m + 6);
    const float m21 = xf_f(m + 9);
    // The Y axis is untouched: no tilt in, and none out.
    if (fabsf(m01) > 1e-4f || fabsf(m10) > 1e-4f || fabsf(m12) > 1e-4f ||
        fabsf(m21) > 1e-4f || fabsf(m11 - 1.0f) > 1e-4f)
        return false;
    // And what is left is a rotation rather than a scale or a skew that happens to keep
    // the Y axis, which no draw in this game produces but which would be read as a HUD
    // element if it ever did.
    return fabsf(m00 * m00 + m02 * m02 - 1.0f) < 1e-3f;
}

// `view_space` is pos_matrix_is_view_space() for the draw: the game put these vertices in
// view space itself instead of placing them in the world. See PixelState.
static uint32_t snapshot_state(bool view_space) {
    // Nothing the state is built from has changed since the last draw, so the answer is
    // the last answer. This is the common case by a wide margin -- see State::pixel_dirty.
    if (g_have_last_state && !g_state.pixel_dirty && g_last_state.view_space == view_space)
        return g_last_state_idx;
    g_state.pixel_dirty = false;
    PixelState st;
    memset(&st, 0, sizeof(st));
    st.view_space = view_space;
    memcpy(st.bp, g_state.bp, sizeof(st.bp));
    memcpy(st.tev_reg, g_tev_reg, sizeof(g_tev_reg));
    memcpy(st.tev_konst, g_tev_konst, sizeof(g_tev_konst));
    for (int i = 0; i < 6; i++) st.proj[i] = xfr_f(0x20 + i);
    st.proj[6] = (float)g_state.xf_regs[0x26];
    for (int i = 0; i < 6; i++) st.viewport[i] = xfr_f(0x1A + i);
    st.num_texgens = g_state.xf_regs[0x3F] & 15;
    st.num_colors = g_state.xf_regs[0x09] & 3;
    st.clip_disable = g_state.xf_regs[0x05] & 1;
    st.tag = g_draw_tag;
    // Textures referenced by TEV stages and indirect stages.
    uint32_t genmode = g_state.bp[0x00];
    uint32_t nstages = ((genmode >> 10) & 15) + 1;
    uint32_t used = 0;
    for (uint32_t s = 0; s < nstages; s++) {
        uint32_t order = g_state.bp[0x28 + s / 2] >> ((s & 1) * 12);
        if (order & 0x40) used |= 1u << (order & 7);
    }
    uint32_t nind = (genmode >> 16) & 7;
    for (uint32_t i = 0; i < nind; i++) used |= 1u << ((g_state.bp[0x27] >> (6 * i)) & 7);
    for (int m = 0; m < 8; m++)
        if (used & (1u << m)) resolve_texture(st, m);
    if (g_have_last_state && memcmp(&st, &g_last_state, sizeof(st)) == 0) return g_last_state_idx;
    g_last_state = st;
    g_have_last_state = true;
    g_last_state_idx = (uint32_t)batch().states.size();
    batch().states.push_back(st);
    return g_last_state_idx;
}

// ---------------------------------------------------------------------------
// Deferred transform
//
// Decoding a vertex reads guest memory -- the arrays its indices point into -- which the
// game may change as soon as the draw is issued, so it happens when the draw arrives, on
// the guest thread. Transforming and lighting it reads only the decoded vertex and XF
// state, and that XF state is small and changes rarely within a frame. So each draw
// records its decoded vertices and which snapshot of each XF region it saw (a region is
// copied only when it has changed since the last draw), and the transform runs after the
// frame is submitted, on worker threads, while the guest gets on with the next frame. The
// output is the same either way.
//
// GCN_XF_SYNC=1 transforms each draw as it arrives instead, which the diagnostics that
// read transformed vertices on the guest thread (GCN_TRACE_FRAME, GCN_GXSTATS,
// GCN_MTXLOG) also select. GCN_XF_THREADS sets how many threads share a frame (default 2).
// ---------------------------------------------------------------------------
struct XfDraw {
    uint32_t first_in, first_out, count, seq;
    uint32_t snap[XF_REGIONS];   // offsets into Pending::snap
    bool has_nrm;
    uint32_t cmd;                // the Batch::cmds entry drawing it; ~0 if none
    uint8_t col_desc[2], col_fmt[2];   // how each colour was read, for GCN_LIGHTLOG
};

struct Pending {
    std::vector<InVertex> in;
    std::vector<XfDraw> draws;
    std::vector<uint32_t> snap;
    // Each region's latest page table, and the generation it was built at; each block's
    // latest copy, and the generation that was. ~0 is a generation never seen.
    uint32_t snap_gen[XF_REGIONS];
    uint32_t snap_off[XF_REGIONS];
    uint32_t blk_gen[XF_REGIONS][17];
    uint32_t blk_off[XF_REGIONS][17];
    Pending() { clear(); }
    void clear() {
        in.clear();
        draws.clear();
        snap.clear();
        memset(snap_gen, 0xFF, sizeof(snap_gen));
        memset(blk_gen, 0xFF, sizeof(blk_gen));
    }
};

static bool xf_sync() {
    static const bool sync = getenv("GCN_XF_SYNC") || getenv("GCN_TRACE_FRAME") ||
                             getenv("GCN_GXSTATS") || getenv("GCN_MTXLOG");
    return sync;
}

// Pendings are recycled with their capacity, as batches are.
static std::mutex g_pend_pool_mutex;
static std::vector<std::unique_ptr<Pending>> g_pend_pool;

static Pending& pending() {
    if (!g_pending) {
        std::lock_guard<std::mutex> lk(g_pend_pool_mutex);
        if (!g_pend_pool.empty()) {
            g_pending = std::move(g_pend_pool.back());
            g_pend_pool.pop_back();
        } else {
            g_pending = std::make_unique<Pending>();
        }
    }
    return *g_pending;
}

static void recycle_pending(std::unique_ptr<Pending> p) {
    p->clear();
    std::lock_guard<std::mutex> lk(g_pend_pool_mutex);
    if (g_pend_pool.size() < 4) g_pend_pool.push_back(std::move(p));
}

// Where vertex `v` of a command goes in Batch::packed, and at what stride; null for a
// command with no draw to pack.
static uint8_t* packed_dest(Batch& b, uint32_t cmd, uint32_t v, uint32_t& stride) {
    if (cmd == ~0u || b.packed.empty()) return nullptr;
    const Cmd& c = b.cmds[cmd];
    stride = packed_stride(draw_ntex(b.states[c.state]));
    return b.packed.data() + (size_t)((int64_t)c.base_vertex + v) * stride;
}

// Lays out Batch::packed (see there): a region per texture coordinate count, each draw's
// vertices in it in the order they were drawn, and each command's base_vertex. Packing
// itself is done as the vertices are transformed, while they are still in cache.
static void layout_packed(Batch& b) {
    size_t count[9] = {};
    for (const Cmd& c : b.cmds)
        if (c.type == CmdType::Draw) count[draw_ntex(b.states[c.state])] += c.vcount;
    size_t bytes = 0, next[9];
    for (uint32_t n = 0; n < 9; n++) {
        const uint32_t s = packed_stride(n);
        bytes = (bytes + s - 1) / s * s;
        next[n] = bytes / s;  // the region's first vertex, in vertices of its own stride
        bytes += count[n] * s;
    }
    for (Cmd& c : b.cmds) {
        if (c.type != CmdType::Draw) continue;
        size_t& at = next[draw_ntex(b.states[c.state])];
        c.base_vertex = (int32_t)((int64_t)at - c.vfirst);
        at += c.vcount;
    }
    b.packed.resize(bytes);
}

// GX's texture offsets for lines and points (GXEnableTexOffsets with GXSetLineWidth's or
// GXSetPointSize's offset): the rasteriser widens a line into a quad and runs a texture
// coordinate's s from its value to value + 1/n across the width (SU_LPSIZE bits 16-18 for
// lines and 19-21 for points give n as 16, 8, 4, 2 or 1; s and t both, for a point),
// per coordinate enabled in its SU_SSIZE register (bit 18 lines, 19 points). A game draws
// antialiased lines this way, from a texture whose alpha is a profile across u that is
// clear at both edges. GL draws a line one pixel wide with no coordinate across it, so
// each line samples the middle of its range instead -- with s at the edge, as it was, every
// such line drew fully transparent.
static void line_point_tex_offset(const Batch& b, uint32_t cmd, uint8_t* dst, uint32_t stride, uint32_t n) {
    const Cmd& c = b.cmds[cmd];
    if (c.prim != 1 && c.prim != 2) return;
    const PixelState& st = b.states[c.state];
    static const float kOffset[8] = {0.0f, 1.0f / 16, 1.0f / 8, 1.0f / 4, 1.0f / 2, 1.0f, 1.0f, 1.0f};
    const uint32_t lp = st.bp[0x22];
    const float off = kOffset[c.prim == 1 ? (lp >> 16) & 7 : (lp >> 19) & 7] * 0.5f;
    if (off == 0.0f) return;
    const uint32_t ntex = draw_ntex(st), bit = c.prim == 1 ? 18 : 19;
    for (uint32_t t = 0; t < ntex; t++) {
        if (!((st.bp[0x30 + 2 * t] >> bit) & 1)) continue;
        uint8_t* p = dst + kPackedHead + 12 * t;
        for (uint32_t i = 0; i < n; i++, p += stride) {
            float tc[3];
            memcpy(tc, p, sizeof(tc));
            tc[0] += off * tc[2];
            if (c.prim == 2) tc[1] += off * tc[2];
            memcpy(p, tc, sizeof(tc));
        }
    }
}

// For a batch whose vertices were transformed as they arrived (xf_sync): packs them all.
static void pack_all(Batch& b) {
    layout_packed(b);
    for (size_t i = 0; i < b.cmds.size(); i++) {
        if (b.cmds[i].type != CmdType::Draw) continue;
        const Cmd& c = b.cmds[i];
        uint32_t stride;
        uint8_t* dst = packed_dest(b, (uint32_t)i, c.vfirst, stride);
        uint8_t* const first = dst;
        for (uint32_t v = c.vfirst; v < c.vfirst + c.vcount; v++, dst += stride) memcpy(dst, &b.verts[v], stride);
        line_point_tex_offset(b, (uint32_t)i, first, stride, c.vcount);
    }
}

// The snapshot offsets the current plan was loaded from (see transform_draw). A frame's
// draws are mostly runs of small primitives under one set of registers -- a room's
// display lists are thousands of four-vertex fans -- so the plan, the cached matrices and
// the lights are each kept until the snapshot they came from changes. Forgotten at the
// start of each frame's share (transform_range), since snapshots are reused.
static thread_local bool t_plan_valid;
static thread_local uint32_t t_plan_snap[XF_REGIONS];

// GCN_XFSTATS=1: what the transform is asked for, summed over 64 frames and printed per
// frame -- vertices, how many carry a normal, how many have a lit colour or alpha channel,
// light evaluations (a vertex times each light its channels sum), texgens, texgens with a
// post matrix, emboss texgens. Which of this a scene needs decides what the transform's
// time goes on, and every figure is a function of the draw's plan and its vertex count,
// so nothing is added per vertex.
struct XfStats { uint64_t verts, nrm, lit, alit, lights, texgens, post, emboss; };
static bool xfstats_on() {  // on first use: the app sets its environment after static init
    static const bool on = getenv("GCN_XFSTATS") != nullptr;
    return on;
}
static thread_local XfStats t_stats;
static std::mutex g_stats_mutex;
static XfStats g_stats;

static void count_draw_stats(const XfDraw& d) {
    XfStats& S = t_stats;
    const uint64_t n = d.count;
    S.verts += n;
    if (d.has_nrm) S.nrm += n;
    auto nlights = [](uint32_t ctrl) {
        return (uint64_t)__builtin_popcount(((ctrl >> 2) & 0xF) | (((ctrl >> 11) & 0xF) << 4));
    };
    for (uint32_t ch = 0; ch < g_numcol; ch++) {
        if (g_chan[ch].cctrl & 2) { S.lit += n; S.lights += n * nlights(g_chan[ch].cctrl); }
        if (g_chan[ch].actrl & 2) { S.alit += n; S.lights += n * nlights(g_chan[ch].actrl); }
    }
    S.texgens += n * g_ntex;
    for (uint32_t t = 0; t < g_ntex && t < 8; t++) {
        if (g_gen[t].type == 1) S.emboss += n;
        else if (g_gen[t].type == 0 && g_dualtex) S.post += n;
    }
}

static void flush_draw_stats() {
    std::lock_guard<std::mutex> lk(g_stats_mutex);
    g_stats.verts += t_stats.verts; g_stats.nrm += t_stats.nrm; g_stats.lit += t_stats.lit;
    g_stats.alit += t_stats.alit; g_stats.lights += t_stats.lights; g_stats.texgens += t_stats.texgens;
    g_stats.post += t_stats.post; g_stats.emboss += t_stats.emboss;
    t_stats = XfStats{};
}

static void transform_draw(const Pending& pd, const XfDraw& d, Batch& b) {
    t_xf.base = pd.snap.data();
    for (int r = 0; r < XF_REGIONS; r++) t_xf.pt[r] = pd.snap.data() + d.snap[r];
    t_draw_seq = d.seq;
    if (!t_plan_valid || d.snap[XF_REGS] != t_plan_snap[XF_REGS]) {
        load_xf_plan();
    } else {
        if (d.snap[XF_MTX] != t_plan_snap[XF_MTX] || d.snap[XF_NRM] != t_plan_snap[XF_NRM]) forget_matrices();
        if (d.snap[XF_POST] != t_plan_snap[XF_POST]) g_post_loaded = 0;
        if (d.snap[XF_LIGHT] != t_plan_snap[XF_LIGHT]) g_lights_loaded = 0;
    }
    {
    // GCN_LIGHTLOG=<min verts>: the channel and light registers of each lit draw of at
    // least that many vertices, once per distinct setup, for seeing why a channel lights
    // the way it does. A draw is named by its vertex count, which is what the draw log
    // shows for it.
    static const int lightlog = getenv("GCN_LIGHTLOG") ? atoi(getenv("GCN_LIGHTLOG")) : 0;
    if (lightlog && (int)d.count >= lightlog) {
        // A setup is the channel controls, materials, ambients and light colours and
        // attenuations; a light's position and direction move with the camera every
        // frame and are not part of what makes two setups differ.
        uint64_t h = g_numcol;
        auto mix = [&](uint32_t w) { h = (h ^ w) * 0x100000001B3ull; };
        for (int c = 0; c < 2; c++) {
            mix(g_chan[c].cctrl); mix(g_chan[c].actrl); mix(tw(XF_REGS, 0x0C + c)); mix(tw(XF_REGS, 0x0A + c));
            uint32_t ctrl = g_chan[c].cctrl | g_chan[c].actrl;
            uint32_t mask = ((ctrl >> 2) & 0xF) | (((ctrl >> 11) & 0xF) << 4);
            for (int i = 0; i < 8; i++)
                if (mask & (1u << i))
                    for (int w = 3; w < 10; w++) mix(tw(XF_LIGHT, i * 0x10 + w));
        }
        static std::mutex seen_mutex;
        static std::unordered_set<uint64_t> seen;
        bool fresh;
        {
            std::lock_guard<std::mutex> lk(seen_mutex);
            fresh = seen.insert(h).second;
        }
        if (fresh) {
            const InVertex& v0 = pd.in[d.first_in];
            // The raw colours of the first few vertices, and how many distinct alphas the
            // draw's vertices carry between them: a fade that should be smooth and comes
            // out stepped is told from its source here.
            std::unordered_set<uint32_t> alphas;
            for (uint32_t i = 0; i < d.count; i++) alphas.insert(pd.in[d.first_in + i].col[0] & 0xFF);
            fprintf(stderr, "[light]   raw col0:");
            for (uint32_t i = 0; i < d.count && i < 8; i++) fprintf(stderr, " %08X", pd.in[d.first_in + i].col[0]);
            // The colour arrays are read live, so they are the draw's own only under
            // GCN_XF_SYNC=1.
            fprintf(stderr, "  distinct alphas=%zu  col0 desc=%u fmt=%u col1 desc=%u fmt=%u arrays %08X/%u %08X/%u\n", alphas.size(),
                    d.col_desc[0], d.col_fmt[0], d.col_desc[1], d.col_fmt[1],
                    g_state.cp[0xA2], g_state.cp[0xB2], g_state.cp[0xA3], g_state.cp[0xB3]);
            fprintf(stderr, "[light] draw of %u verts (numcol %u, v0 col %08X %08X): chan0 cctrl=%04X actrl=%04X mat=%08X amb=%08X | chan1 cctrl=%04X actrl=%04X mat=%08X amb=%08X\n", d.count,
                    g_numcol, v0.col[0], v0.col[1], g_chan[0].cctrl, g_chan[0].actrl, tw(XF_REGS, 0x0C), tw(XF_REGS, 0x0A), g_chan[1].cctrl, g_chan[1].actrl, tw(XF_REGS, 0x0D), tw(XF_REGS, 0x0B));
            for (int c = 0; c < 2; c++) {
                uint32_t ctrl = g_chan[c].cctrl | g_chan[c].actrl;
                uint32_t mask = ((ctrl >> 2) & 0xF) | (((ctrl >> 11) & 0xF) << 4);
                for (int i = 0; i < 8; i++) {
                    if (!(mask & (1u << i))) continue;
                    load_light(i);
                    const LightParams& L = g_lights[i];
                    fprintf(stderr, "[light]   chan%d light%d col=%08X pos=%.1f,%.1f,%.1f dir=%.2f,%.2f,%.2f cos=%.2f,%.2f,%.2f dist=%.2f,%.2f,%.2f\n", c, i, tw(XF_LIGHT, i * 0x10 + 3),
                            L.pos.x, L.pos.y, L.pos.z, L.dir.x, L.dir.y, L.dir.z, L.cosatt.x, L.cosatt.y, L.cosatt.z, L.distatt.x, L.distatt.y, L.distatt.z);
                }
            }
        }
    }
    }
    t_plan_valid = true;
    memcpy(t_plan_snap, d.snap, sizeof(t_plan_snap));
    if (xfstats_on()) count_draw_stats(d);
    const InVertex* in = pd.in.data() + d.first_in;
    GpuVertex* out = b.verts.data() + d.first_out;
    uint32_t stride = 0;
    uint8_t* dst = packed_dest(b, d.cmd, d.first_out, stride);
    if (!dst) {
        for (uint32_t i = 0; i < d.count; i++) transform_vertex(in[i], out[i], d.has_nrm);
        return;
    }
    uint8_t* const first = dst;
    for (uint32_t i = 0; i < d.count; i++, dst += stride) {
        transform_vertex(in[i], out[i], d.has_nrm);
        memcpy(dst, &out[i], stride);
    }
    line_point_tex_offset(b, d.cmd, first, stride, d.count);
}

static void transform_range(const Pending& pd, size_t lo, size_t hi, Batch& b) {
    t_plan_valid = false;
    for (size_t i = lo; i < hi; i++) transform_draw(pd, pd.draws[i], b);
    if (xfstats_on()) flush_draw_stats();
}

// A frame's draws, handed out a chunk at a time to whichever of the worker and its helpers
// is free. They were split into equal shares of vertices, one per thread, but a vertex's
// cost varies severalfold with its lighting and texgens: in a scene of lit, skinned models
// beside plain ones one share took two and a half times the other, and the thread with the
// light share sat idle while the frame waited on the heavy one.
struct XfShare {
    const Pending* pd;
    Batch* batch;
    size_t n;
    std::atomic<size_t> next{0};
};
static void transform_share(XfShare& s) {
    constexpr size_t kChunk = 128;   // draws: most are 4-8 vertices, a few thousands
    for (;;) {
        const size_t lo = s.next.fetch_add(kChunk, std::memory_order_relaxed);
        if (lo >= s.n) return;
        transform_range(*s.pd, lo, std::min(s.n, lo + kChunk), *s.batch);
    }
}

// Helpers that take a share of each frame alongside the worker.
struct XfHelper {
    std::thread thread;
    std::mutex m;
    std::condition_variable cv;
    XfShare* share = nullptr;
    bool busy = false;
};
static std::vector<std::unique_ptr<XfHelper>> g_helpers;

static void helper_main(XfHelper* h) {
    plat_thread_role(ThreadRole::RenderWorker, "gx-transform");
    for (;;) {
        std::unique_lock<std::mutex> lk(h->m);
        h->cv.wait(lk, [h] { return h->busy && h->share; });
        XfShare* share = h->share;
        lk.unlock();
        transform_share(*share);
        lk.lock();
        h->share = nullptr;
        h->busy = false;
        h->cv.notify_all();
    }
}

static void transform_pending(const Pending& pd, Batch& b) {
    static const int threads = [] {
        const char* e = getenv("GCN_XF_THREADS");
        const int n = e ? atoi(e) : 2;
        return n < 1 ? 1 : n > 8 ? 8 : n;
    }();
    while ((int)g_helpers.size() < threads - 1) {
        auto h = std::make_unique<XfHelper>();
        h->thread = std::thread(helper_main, h.get());
        h->thread.detach();
        g_helpers.push_back(std::move(h));
    }
    const size_t n = pd.draws.size();
    layout_packed(b);
    if (threads == 1 || n < 64) {
        transform_range(pd, 0, n, b);
        return;
    }
    XfShare share;
    share.pd = &pd;
    share.batch = &b;
    share.n = n;
    for (int k = 1; k < threads; k++) {
        XfHelper* h = g_helpers[k - 1].get();
        std::lock_guard<std::mutex> lk(h->m);
        h->share = &share;
        h->busy = true;
        h->cv.notify_all();
    }
    transform_share(share);
    for (int k = 1; k < threads; k++) {
        XfHelper* h = g_helpers[k - 1].get();
        std::unique_lock<std::mutex> lk(h->m);
        h->cv.wait(lk, [h] { return !h->busy; });
    }
}

// The worker: transforms each submitted frame, then hands it to the renderer, in order.
struct XfJob {
    std::unique_ptr<Batch> batch;
    std::unique_ptr<Pending> pending;
};
static std::mutex g_xf_mutex;
static std::condition_variable g_xf_cv;
static std::deque<XfJob> g_xf_jobs;
static constexpr size_t kMaxXfJobs = 1;

static void xf_worker() {
    plat_thread_role(ThreadRole::RenderWorker, "gx-transform");
    for (;;) {
        XfJob job;
        {
            std::unique_lock<std::mutex> lk(g_xf_mutex);
            g_xf_cv.wait(lk, [] { return !g_xf_jobs.empty(); });
            job = std::move(g_xf_jobs.front());
        }
        if (!job.pending) {  // nothing to transform; queued to keep its place (flush_batch)
            submit_batch(std::move(job.batch));
            std::lock_guard<std::mutex> lk(g_xf_mutex);
            g_xf_jobs.pop_front();
            g_xf_cv.notify_all();
            continue;
        }
        const auto t0 = std::chrono::steady_clock::now();
        transform_pending(*job.pending, *job.batch);
        if (frametime_on())
            fprintf(stderr, "[xf] %zu draws, %zu vertices transformed in %.2fms\n",
                    job.pending->draws.size(), job.pending->in.size(), ms_since(t0));
        if (xfstats_on()) {
            static uint32_t frames;
            if (++frames % 64 == 0) {
                std::lock_guard<std::mutex> lk(g_stats_mutex);
                const XfStats& S = g_stats;
                const double k = 1.0 / 64;
                fprintf(stderr, "[xfstats] per frame: verts %.0f (nrm %.0f lit %.0f alit %.0f) light evals %.0f  texgens %.0f (post %.0f emboss %.0f)\n",
                        S.verts * k, S.nrm * k, S.lit * k, S.alit * k, S.lights * k, S.texgens * k, S.post * k, S.emboss * k);
                g_stats = XfStats{};
            }
        }
        recycle_pending(std::move(job.pending));
        submit_batch(std::move(job.batch));
        {
            std::lock_guard<std::mutex> lk(g_xf_mutex);
            g_xf_jobs.pop_front();
            g_xf_cv.notify_all();
        }
    }
}

// Hands a finished frame to the worker, waiting while it has kMaxXfJobs in hand: like the
// render queue, a bound and not a hint.
static void submit_for_transform(std::unique_ptr<Batch> b, std::unique_ptr<Pending> pd) {
    static const bool started = [] {
        std::thread(xf_worker).detach();
        return true;
    }();
    (void)started;
    std::unique_lock<std::mutex> lk(g_xf_mutex);
    StallTimer stall(Stall::XfFull);
    g_xf_cv.wait(lk, [] { return g_xf_jobs.size() < kMaxXfJobs; });
    g_xf_jobs.push_back({std::move(b), std::move(pd)});
    g_xf_cv.notify_all();
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

static void draw_impl(const DrawCall& dc);

void renderer_draw(const DrawCall& dc) {
    if (!frametime_on()) { draw_impl(dc); return; }
    const auto t0 = std::chrono::steady_clock::now();
    draw_impl(dc);
    g_fe_ms += ms_since(t0);
    g_fe_draws++;
}

static void draw_impl(const DrawCall& dc) {
    if (dc.count == 0) return;
    const Layout& L = layout_for(dc.vat);
    load_decode_defaults();
    const uint8_t* p = dc.data;
    g_draw_seq++;
    Pending& pd = pending();
    const uint32_t first_in = (uint32_t)pd.in.size();
    pd.in.resize(first_in + dc.count);
    InVertex* const g_in = pd.in.data() + first_in;
    for (uint32_t i = 0; i < dc.count; i++) decode_vertex(L, p, g_in[i]);
    if (L.nrm_desc) {
        const InVertex& last = g_in[dc.count - 1];
        memcpy(g_last_nrm, last.nrm, sizeof(g_last_nrm));
        if (L.nbt) { memcpy(g_last_bin, last.bin, sizeof(g_last_bin)); memcpy(g_last_tan, last.tan, sizeof(g_last_tan)); }
    }
    // GCN_VTXLOG=<frame> prints each draw of that frame as the front end sees it: the
    // primitive, vertex layout, the first vertices' raw indices and positions, and the
    // position matrix each of them selects -- what the renderer's draw log cannot show,
    // since it only sees the transformed result.
    static const uint32_t vtxlog = getenv("GCN_VTXLOG") ? (uint32_t)atoi(getenv("GCN_VTXLOG")) : 0;
    if (vtxlog && g_frame_counter == vtxlog) {
        static uint32_t n;
        fprintf(stderr, "[vtx] %u prim=%02X vat=%u count=%u pnmtx=%u pos(desc=%u fmt=%u cnt=%u scale=%g) nrm(desc=%u) tex0(desc=%u) base0=%08X stride0=%u tex0addr=%08X\n",
                n++, dc.prim, dc.vat, dc.count, L.pnmtx, L.pos_desc, L.pos_fmt, L.pos_cnt, L.pos_scale, L.nrm_desc,
                L.tex_desc[0], g_state.cp[0xA0], g_state.cp[0xB0], (g_state.bp[0x94] & 0x1FFFFF) << 5);
        {
            // The first bytes of the position array, so a vertex buffer full of nonsense
            // can be told from an index pointing at the wrong one.
            const uint8_t* q = phys_ptr(g_state.cp[0xA0]);
            fprintf(stderr, "      posarray:");
            for (int i = 0; i < 32; i++) fprintf(stderr, "%s%02X", (i % 4) ? "" : " ", q[i]);
            fprintf(stderr, "\n");
        }
        // Near-duplicate positions within the draw: two vertices closer than 0.05 units
        // but not identical. Printed with their raw bytes, so a pair the model really
        // holds can be told from one the decode produced.
        for (uint32_t i = 0; i < dc.count; i++)
            for (uint32_t j = i + 1; j < dc.count; j++) {
                const float dx = g_in[i].pos[0] - g_in[j].pos[0], dy = g_in[i].pos[1] - g_in[j].pos[1], dz = g_in[i].pos[2] - g_in[j].pos[2];
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.0f && d2 < 0.05f * 0.05f && L.pos_desc >= 2 && g_in[i].pos_idx != g_in[j].pos_idx) {
                    const uint8_t* a = phys_ptr(g_state.cp[0xA0] + g_in[i].pos_idx * g_state.cp[0xB0]);
                    const uint8_t* bq = phys_ptr(g_state.cp[0xA0] + g_in[j].pos_idx * g_state.cp[0xB0]);
                    fprintf(stderr, "      neardup v%u(idx %u) v%u(idx %u) d=%.4f raw %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X | %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
                            i, g_in[i].pos_idx, j, g_in[j].pos_idx, sqrtf(d2),
                            a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11],
                            bq[0], bq[1], bq[2], bq[3], bq[4], bq[5], bq[6], bq[7], bq[8], bq[9], bq[10], bq[11]);
                }
            }
        uint32_t seen = 0xFFFFFFFF;
        for (uint32_t i = 0; i < dc.count && i < 12; i++) {
            const InVertex& v = g_in[i];
            fprintf(stderr, "      v%u pnmtx=%u idx=%u pos=%.2f,%.2f,%.2f\n", i, v.pnmtx, v.pos_idx, v.pos[0], v.pos[1], v.pos[2]);
            if (v.pnmtx != seen) {
                seen = v.pnmtx;
                const uint32_t m = (v.pnmtx & 63) * 4;
                fprintf(stderr, "      mtx%u | %.3f %.3f %.3f %.2f | %.3f %.3f %.3f %.2f | %.3f %.3f %.3f %.2f\n", v.pnmtx,
                        xf_f(m), xf_f(m + 1), xf_f(m + 2), xf_f(m + 3), xf_f(m + 4), xf_f(m + 5), xf_f(m + 6), xf_f(m + 7),
                        xf_f(m + 8), xf_f(m + 9), xf_f(m + 10), xf_f(m + 11));
            }
        }
    }
    // Transformed straight into the batch, each vertex once; the primitive's shape is
    // expressed by the indices below.
    Batch& b = batch();
    const uint32_t base = (uint32_t)b.verts.size();
    b.verts.resize(base + dc.count);
    XfDraw d;
    d.first_in = first_in;
    d.first_out = base;
    d.count = dc.count;
    d.seq = g_draw_seq;
    d.has_nrm = L.nrm_desc != 0;
    for (int c = 0; c < 2; c++) { d.col_desc[c] = (uint8_t)L.col_desc[c]; d.col_fmt[c] = (uint8_t)L.col_fmt[c]; }
    d.cmd = ~0u;
    for (int r = 0; r < XF_REGIONS; r++) {
        if (pd.snap_gen[r] != g_state.xf_gen[r]) {
            const uint32_t* src = (r == XF_REGS ? g_state.xf_regs : g_state.xf_mem) + kXfBase[r];
            const uint32_t* gens = g_state.xf_block_gen + (r == XF_REGS ? kXfRegBlocks : 0) + kXfBase[r] / 16;
            const uint32_t nb = kXfLen[r] / 16;
            for (uint32_t k = 0; k < nb; k++) {
                if (pd.blk_gen[r][k] == gens[k]) continue;
                pd.blk_off[r][k] = (uint32_t)pd.snap.size();
                pd.snap.insert(pd.snap.end(), src + 16 * k, src + 16 * k + 16);
                pd.blk_gen[r][k] = gens[k];
            }
            pd.snap_off[r] = (uint32_t)pd.snap.size();
            pd.snap.insert(pd.snap.end(), pd.blk_off[r], pd.blk_off[r] + nb);
            pd.snap_gen[r] = g_state.xf_gen[r];
        }
        d.snap[r] = pd.snap_off[r];
    }
    pd.draws.push_back(d);
    if (xf_sync()) {
        t_plan_valid = false;  // snapshots restart every frame; no carrying a plan over
        transform_draw(pd, d, b);
    }
    GpuVertex* out = b.verts.data() + base;

    // GCN_MTXLOG=<n> prints the position matrix and projection of every draw in the nth
    // presented frame. The timebase is wall-clock driven, so frame N is a slightly
    // different moment in every run; pick a frame well inside a steady scene.
    // Whether the draw's position matrix places it in view space, and which entry of the
    // batch's matrix list it is: both functions of the matrix alone, so they are kept while
    // neither the matrix index nor XF's matrix memory has changed -- the common case, since
    // a model's draws follow one another under one matrix.
    static uint32_t c_gen = 0, c_pnmtx = ~0u, c_mtx = 0;
    static size_t c_mtxs_size = ~(size_t)0;
    static bool c_view_space = false;
    const uint32_t pnmtx0 = g_in[0].pnmtx;
    const bool mtx_cached = c_gen == g_state.xf_gen[XF_MTX] && c_pnmtx == pnmtx0 && c_mtxs_size == b.mtxs.size();
    if (!mtx_cached) c_view_space = pos_matrix_is_view_space(pnmtx0);
    uint32_t state = snapshot_state(c_view_space);
    if (g_mtxlog) {
        // GCN_WATCH=a,b,c prints those guest addresses every frame. Sparse snapshots cannot
        // tell a steady flag from one that blinks, so this samples every frame.
        static const char* watch = getenv("GCN_WATCH");
        static uint32_t watch_last_frame = ~0u;
        if (watch && g_frame_counter != watch_last_frame) {
            watch_last_frame = g_frame_counter;
            fprintf(stderr, "[watch] f%u", g_frame_counter);
            for (const char* s = watch; s && *s;) {
                const uint32_t a = (uint32_t)strtoul(s, nullptr, 0);
                fprintf(stderr, " %#x=%08x", a, mem_r32(a));
                const char* c = strchr(s, ',');
                s = c ? c + 1 : nullptr;
            }
            fprintf(stderr, "\n");
        }
        if (g_frame_counter == g_mtxlog) g_mtx_draw = 0;
    }
    if (mtxlog_frame()) {
        const uint32_t m = (g_in[0].pnmtx & 63) * 4;
        const PixelState& ps = b.states[state];
        fprintf(stderr, "[mtx] %3u n=%4u pnmtx=%2u st=%u | %8.3f %8.3f %8.3f %10.2f | %8.3f %8.3f %8.3f %10.2f"
                " | %8.3f %8.3f %8.3f %10.2f | out0=%.1f,%.1f,%.1f |",
                g_mtx_draw, dc.count, g_in[0].pnmtx & 63, state,
                xf_f(m + 0), xf_f(m + 1), xf_f(m + 2), xf_f(m + 3),
                xf_f(m + 4), xf_f(m + 5), xf_f(m + 6), xf_f(m + 7),
                xf_f(m + 8), xf_f(m + 9), xf_f(m + 10), xf_f(m + 11),
                out[0].pos[0], out[0].pos[1], out[0].pos[2]);
        // The textures too: a model is recognised by what it is textured with, so this
        // is what ties a draw in one pass to the same model drawn in another.
        for (int i = 0; i < 8; i++)
            if (ps.tex_id[i]) fprintf(stderr, " t%d=%u%s", i, ps.tex_id[i], ps.tex_is_efb[i] ? "*" : "");
        fprintf(stderr, "\n");
        fprintf(stderr, "[prj] %3u type=%d %9.4f %9.4f %9.4f %9.4f %9.4f %9.4f\n", g_mtx_draw,
                (int)g_state.xf_regs[0x26], xfr_f(0x20), xfr_f(0x21), xfr_f(0x22),
                xfr_f(0x23), xfr_f(0x24), xfr_f(0x25));
        g_mtx_draw++;
    }

    // GCN_PNMLOG=a-b lists the position matrices a frame actually uses, one line each time
    // the matrix changes, over a window of frames. What it is for: an object the game
    // places in front of the camera is told apart from world geometry by its rotation
    // being identity, so anything camera-attached that *animates* is misread as world
    // geometry for as long as it moves. This is how to see that happen.
    static int pnm_lo = -1, pnm_hi = -1;
    static bool pnm_parsed = false;
    if (!pnm_parsed) {
        pnm_parsed = true;
        if (const char* r = getenv("GCN_PNMLOG")) {
            pnm_lo = atoi(r);
            const char* dash = strchr(r, '-');
            pnm_hi = dash ? atoi(dash + 1) : pnm_lo;
        }
    }
    if (pnm_lo >= 0 && (int)g_frame_counter >= pnm_lo && (int)g_frame_counter <= pnm_hi &&
        dc.count) {
        const uint32_t m = (g_in[0].pnmtx & 63) * 4;
        static float last[12];
        static uint32_t last_frame = ~0u;
        float cur[12];
        for (int i = 0; i < 12; i++) cur[i] = xf_f(m + i);
        if (g_frame_counter != last_frame || memcmp(cur, last, sizeof(cur)) != 0) {
            memcpy(last, cur, sizeof(cur));
            last_frame = g_frame_counter;
            fprintf(stderr, "[pnm] f%u n=%4u id=%2u ident=%d | %7.3f %7.3f %7.3f %9.2f |"
                            " %7.3f %7.3f %7.3f %9.2f | %7.3f %7.3f %7.3f %9.2f\n",
                    g_frame_counter, dc.count, g_in[0].pnmtx & 63,
                    (int)pos_matrix_is_view_space(g_in[0].pnmtx), cur[0], cur[1], cur[2], cur[3],
                    cur[4], cur[5], cur[6], cur[7], cur[8], cur[9], cur[10], cur[11]);
        }
    }
    // The draw's position matrix, from its first vertex -- see pos_matrix_is_view_space
    // for why that one stands for the draw. Appended only when it differs from the last
    // one appended, so the draws of one rigid object share an index.
    uint32_t mtx;
    if (mtx_cached) {
        mtx = c_mtx;
    } else {
        mtx = (uint32_t)b.mtxs.size() / 12;
        const uint32_t m = (g_in[0].pnmtx & 63) * 4;
        float cur[12];
        for (int i = 0; i < 12; i++) cur[i] = xf_f(m + i);
        if (mtx > 0 && memcmp(cur, &b.mtxs[(mtx - 1) * 12], sizeof(cur)) == 0) {
            mtx--;
        } else {
            b.mtxs.insert(b.mtxs.end(), cur, cur + 12);
        }
        c_gen = g_state.xf_gen[XF_MTX];
        c_pnmtx = pnmtx0;
        c_mtx = mtx;
        c_mtxs_size = b.mtxs.size();
    }
    uint32_t first = (uint32_t)b.indices.size();
    uint8_t prim = 0;
    // Sized once and written through a pointer: a push_back per index, with its capacity
    // check, was a measurable part of a frame of twenty thousand small draws.
    uint32_t nidx = 0;
    switch (dc.prim) {
    case PRIM_QUADS: case PRIM_QUADS2: nidx = dc.count / 4 * 6; break;
    case PRIM_TRIANGLES: nidx = dc.count / 3 * 3; break;
    case PRIM_TRISTRIP: case PRIM_TRIFAN: nidx = dc.count >= 3 ? (dc.count - 2) * 3 : 0; break;
    case PRIM_LINES: nidx = dc.count / 2 * 2; break;
    case PRIM_LINESTRIP: nidx = dc.count >= 2 ? (dc.count - 1) * 2 : 0; break;
    case PRIM_POINTS: nidx = dc.count; break;
    }
    b.indices.resize(first + nidx);
    uint32_t* ip = b.indices.data() + first;
    auto push = [&](uint32_t i) { *ip++ = base + i; };
    switch (dc.prim) {
    case PRIM_QUADS: case PRIM_QUADS2:
        for (uint32_t i = 0; i + 3 < dc.count; i += 4) { push(i); push(i + 1); push(i + 2); push(i); push(i + 2); push(i + 3); }
        break;
    case PRIM_TRIANGLES:
        for (uint32_t i = 0; i + 2 < dc.count; i += 3) { push(i); push(i + 1); push(i + 2); }
        break;
    case PRIM_TRISTRIP:
        for (uint32_t i = 0; i + 2 < dc.count; i++) {
            if (i & 1) { push(i + 1); push(i); push(i + 2); }
            else { push(i); push(i + 1); push(i + 2); }
        }
        break;
    case PRIM_TRIFAN:
        for (uint32_t i = 1; i + 1 < dc.count; i++) { push(0); push(i); push(i + 1); }
        break;
    case PRIM_LINES:
        prim = 1;
        for (uint32_t i = 0; i + 1 < dc.count; i += 2) { push(i); push(i + 1); }
        break;
    case PRIM_LINESTRIP:
        prim = 1;
        for (uint32_t i = 0; i + 1 < dc.count; i++) { push(i); push(i + 1); }
        break;
    case PRIM_POINTS:
        prim = 2;
        for (uint32_t i = 0; i < dc.count; i++) push(i);
        break;
    }
    uint32_t count = (uint32_t)(ip - (b.indices.data() + first));
    b.indices.resize(first + count);
    if (!count) return;
    // Merge with the previous draw when state and primitive type match.
    if (!b.cmds.empty()) {
        Cmd& last = b.cmds.back();
        if (last.type == CmdType::Draw && last.state == state && last.prim == prim &&
            last.mtx == mtx && last.first + last.count == first && last.vfirst + last.vcount == base) {
            last.count += count;
            last.vcount += dc.count;
            pd.draws.back().cmd = (uint32_t)b.cmds.size() - 1;
            return;
        }
    }
    Cmd c{};
    c.type = CmdType::Draw;
    c.prim = prim;
    c.state = state;
    c.first = first;
    c.count = count;
    c.mtx = mtx;
    c.vfirst = base;
    c.vcount = dc.count;
    pd.draws.back().cmd = (uint32_t)b.cmds.size();
    b.cmds.push_back(c);
}

// ---------------------------------------------------------------------------
// BP side effects
// ---------------------------------------------------------------------------
void renderer_bp_write(uint32_t reg, uint32_t v) {
    if (reg >= 0xE0 && reg <= 0xE7) {
        uint32_t r = (reg - 0xE0) / 2, half = (reg - 0xE0) & 1;
        if ((v >> 23) & 1) g_tev_konst[r][half] = v;
        else g_tev_reg[r][half] = v;
        return;
    }
    if (reg == 0x65) {  // load TLUT
        uint32_t src = (g_state.bp[0x64] & 0x1FFFFF) << 5;
        uint32_t tmem = (v & 0x3FF) << 9;
        uint32_t count = ((v >> 10) & 0x7FF) * 16 * 2;
        tlut_load(src, tmem, count);
        return;
    }
}

// Whether a display copy finishes a frame, which is where a batch ends. A game whose EFB
// cannot hold a whole frame renders it in parts and copies each to its place in the XFB
// (render_gl.cpp assembles them, see Xfb there); ending a batch at every part handed a
// frontend several batches per game frame, and one that takes a batch per display frame
// ran the game at a fraction of its speed, while an eye drew each part as a frame of its
// own. So the rows each XFB has had written are tracked here as the renderer tracks them,
// and only the copy that completes one, or a copy of the whole frame, ends the batch.
// Copies to an address that never fills up (strips to a scratch buffer) then simply ride
// along in the next batch.
namespace {
struct XfbRows {
    uint32_t base, stride, h, n;
    std::vector<uint8_t> written;
};
std::vector<XfbRows> g_xfb_rows;
}  // namespace

static bool display_copy_ends_frame(const EfbCopyCmd& cc) {
    const uint32_t h = vi_display_height();
    if (!h || cc.dst_h >= h) return true;
    const uint32_t stride = cc.dst_w * 2;
    XfbRows* x = nullptr;
    for (auto& r : g_xfb_rows)
        if (r.stride == stride && r.h == h && cc.dest >= r.base && cc.dest < r.base + stride * h) x = &r;
    if (!x) {
        g_xfb_rows.push_back({cc.dest, stride, h, 0, std::vector<uint8_t>(h, 0)});
        if (g_xfb_rows.size() > 8) g_xfb_rows.erase(g_xfb_rows.begin());
        x = &g_xfb_rows.back();
    }
    const uint32_t row0 = (cc.dest - x->base) / stride;
    const uint32_t rows = std::min(cc.dst_h, h - row0);
    for (uint32_t r = row0; r < row0 + rows; r++)
        if (!x->written[r]) { x->written[r] = 1; x->n++; }
    if (x->n < h) return false;
    std::fill(x->written.begin(), x->written.end(), 0);
    x->n = 0;
    return true;
}

// Copies a game's CPU reads (set_copy_writeback in gx.h). The front end lists each as it
// meets it, with the display copies made before it; the renderer hands over its bytes as it
// makes it (copy_writeback_done); and the guest thread lands them in RAM at the display
// copy kWritebackLag after the copy's own frame's (copy_writebacks_land).
//
// It used to be done at once: the front end sent the batch so far to the renderer and waited
// for the bytes, so that the guest's next draw-done found them. But a frontend draws a batch
// as a frame, so each such copy cut the frame in two, and every piece became a frame of its
// own: a headset showed the frame before the copy, then the frame after it, each in a display
// frame of its own, and a game copying three buffers a frame flickered through four pieces at
// a quarter of its speed, the eyes never seeing the frame whole. Landing them later keeps the
// frame whole and never waits in practice: two display copies on, the renderer has had a
// frame's time to make them. The guest thread lands them at the same guest instruction in
// a replay, so replays stay exact.
struct Writeback {
    uint32_t ticket, frame, dest, w, h, format;
    bool ready = false;
    std::vector<uint8_t> bytes;
};
static std::mutex g_wb_mutex;
static std::condition_variable g_wb_cv;
static std::deque<Writeback> g_wb;
static uint32_t g_wb_ticket;
static constexpr uint32_t kWritebackLag = 2;

std::atomic<bool> g_renderer_writes_back{false};
static CopyWritebackFilter g_copy_writeback;
void set_copy_writeback(CopyWritebackFilter filter) { g_copy_writeback = filter; }

void copy_writeback_done(uint32_t ticket, std::vector<uint8_t>&& bytes) {
    std::lock_guard<std::mutex> lk(g_wb_mutex);
    for (auto& w : g_wb)
        if (w.ticket == ticket) {
            w.bytes = std::move(bytes);
            w.ready = true;
            break;
        }
    g_wb_cv.notify_all();
}

// On the guest thread, at the display copy that makes `frames` of them. A copy made after
// the display copy numbered f (its Writeback::frame) belongs to frame f + 1, and lands now
// if f + 1 + kWritebackLag <= frames. The filter is asked again first: in the frames since,
// the game may have let the memory go -- left the room whose buffers they were, say -- and
// the bytes are then dropped rather than written over whatever has it now.
void copy_writebacks_land(uint32_t frames) {
    std::unique_lock<std::mutex> lk(g_wb_mutex);
    while (!g_wb.empty() && g_wb.front().frame + 1 + kWritebackLag <= frames) {
        Writeback& w = g_wb.front();
        if (!w.ready) {
            StallTimer stall(Stall::Finish);
            if (!g_wb_cv.wait_for(lk, std::chrono::seconds(2), [&w] { return w.ready; })) {
                fprintf(stderr, "[copy] write-back of %08X not done after 2 s; going on without it\n", w.dest);
                g_wb.pop_front();
                continue;
            }
        }
        const uint32_t phys = w.dest & 0x01FFFFFF;
        const bool land = !w.bytes.empty() && phys + w.bytes.size() <= RAM_SIZE && g_copy_writeback &&
                          g_copy_writeback(w.dest, w.w, w.h, w.format, false);
        if (land) {
            memcpy(phys_ptr(w.dest), w.bytes.data(), w.bytes.size());
            debug_guard_dma("EFB copy write-back", w.dest, (uint32_t)w.bytes.size());
        }
        // GCN_COPYLOG: each write-back as it lands, or is dropped, with how many of its bytes
        // are not zero (an id buffer with nothing in it is all zero).
        static const bool copylog = getenv("GCN_COPYLOG") != nullptr;
        if (copylog) {
            size_t nz = 0;
            for (uint8_t b : w.bytes) nz += b != 0;
            fprintf(stderr, "[copy] write-back of %08X from frame %u %s at %u: %zu bytes, %zu not zero\n", w.dest,
                    w.frame + 1, land ? "landed" : "dropped", frames, w.bytes.size(), nz);
        }
        g_wb.pop_front();
    }
}

void renderer_efb_copy(uint32_t dest_addr, bool /*unused*/) {
    const uint32_t* bp = g_state.bp;
    uint32_t v = bp[0x52];
    EfbCopyCmd cc{};
    cc.src_x = bp[0x49] & 0x3FF;
    cc.src_y = (bp[0x49] >> 10) & 0x3FF;
    cc.src_w = (bp[0x4A] & 0x3FF) + 1;
    cc.src_h = ((bp[0x4A] >> 10) & 0x3FF) + 1;
    cc.to_xfb = (v >> 14) & 1;
    cc.dest = dest_addr;
    bool half = (v >> 9) & 1;
    cc.dst_w = half ? cc.src_w / 2 : cc.src_w;
    cc.dst_h = half ? cc.src_h / 2 : cc.src_h;
    uint32_t tf = (v >> 3) & 0xF;
    cc.format = tf / 2 + (tf & 1) * 8;
    if ((v >> 15) & 1) cc.format |= 0x10;
    cc.depth = (bp[0x43] & 7) == 3;
    cc.clear = (v >> 11) & 1;
    cc.clear_color = (bp[0x41] >> 3) & 1;
    cc.clear_alpha = (bp[0x41] >> 4) & 1;
    cc.clear_z = (bp[0x40] >> 4) & 1;
    cc.clear_rgba = ((bp[0x4F] & 0xFF) << 24) | ((bp[0x50] >> 8 & 0xFF) << 16) | ((bp[0x50] & 0xFF) << 8) | ((bp[0x4F] >> 8) & 0xFF);
    cc.clear_z_value = bp[0x51] & 0xFFFFFF;
    // Logged after the fields are filled in; it used to print cc.clear before it was
    // assigned, so every copy appeared not to clear.
    static const bool copylog = getenv("GCN_COPYLOG") != nullptr;
    if (copylog) {
        // How many draw commands the batch holds since its last copy: what this copy can show.
        uint32_t draws = 0;
        const Batch& cb = batch();
        for (size_t i = cb.cmds.size(); i-- > 0 && cb.cmds[i].type == CmdType::Draw;) draws++;
        fprintf(stderr, "[copy] f%u xfb=%d dest=%08X dst=%ux%u src=%ux%u@%u,%u fmt=%X clear=%d "
                "half=%d pe=%06X pixfmt=%u depth=%d draws=%u seq=%u\n", g_frame_counter, cc.to_xfb, dest_addr, cc.dst_w, cc.dst_h,
                cc.src_w, cc.src_h, cc.src_x, cc.src_y, cc.format, cc.clear, (int)half, v, bp[0x43] & 7, (int)cc.depth, draws, (uint32_t)g_draw_seq);
    }
    if (!cc.to_xfb) cc.tex_id = texture_register_efb_copy(dest_addr, cc.dst_w, cc.dst_h, cc.format);
    // In the GCN_MTXLOG dump, so the draws it lists can be read against the pass
    // boundaries: which were drawn into an off-screen target and which into the scene.
    if (mtxlog_frame())
        fprintf(stderr, "[cpy] after draw %u: xfb=%d tex=%u dst=%ux%u src=%ux%u@%u,%u clear=%d\n",
                g_mtx_draw, cc.to_xfb, cc.tex_id, cc.dst_w, cc.dst_h, cc.src_w, cc.src_h,
                cc.src_x, cc.src_y, cc.clear);
    // For display copies the presentation must see the EFB before the copy's clear.
    if (cc.to_xfb) {
        Cmd pc{};
        pc.type = CmdType::Present;
        pc.copy = cc;
        batch().cmds.push_back(pc);
    }
    cc.writeback = !cc.to_xfb && g_copy_writeback && g_renderer_writes_back.load() &&
                   g_copy_writeback(dest_addr, cc.dst_w, cc.dst_h, cc.format, cc.clear);
    Cmd c{};
    c.type = CmdType::EfbCopy;
    c.copy = cc;
    if (cc.writeback) {
        std::lock_guard<std::mutex> lk(g_wb_mutex);
        c.copy.writeback_ticket = ++g_wb_ticket;
        Writeback w;
        w.ticket = g_wb_ticket;
        w.frame = g_frame_counter;
        w.dest = dest_addr;
        w.w = cc.dst_w;
        w.h = cc.dst_h;
        w.format = cc.format;
        g_wb.push_back(std::move(w));
    }
    batch().cmds.push_back(c);
    static int trace_frame = getenv("GCN_TRACE_FRAME") ? atoi(getenv("GCN_TRACE_FRAME")) : -1;
    if ((int)g_frame_counter == trace_frame) {
        Batch& bb = batch();
        fprintf(stderr, "[trace] frame %u copy xfb=%d fmt=%X src %ux%u@%u,%u dst %ux%u\n", g_frame_counter, cc.to_xfb, cc.format, cc.src_w, cc.src_h,
                cc.src_x, cc.src_y, cc.dst_w, cc.dst_h);
        for (size_t i = 0; i < bb.cmds.size(); i++) {
            const Cmd& cmd = bb.cmds[i];
            if (cmd.type != CmdType::Draw) { fprintf(stderr, "  [%zu] copy/present\n", i); continue; }
            const PixelState& ps = bb.states[cmd.state];
            uint32_t ns = ((ps.bp[0] >> 10) & 15) + 1;
            fprintf(stderr, "  [%zu] draw n=%u stages=%u tex0=%u%s blend=%06X z=%06X pixfmt=%X order0=%06X c0=%06X a0=%06X c1=%06X a1=%06X kc0=%08X/%08X\n", i, cmd.count, ns,
                    ps.tex_id[0], ps.tex_is_efb[0] ? "(efb)" : "", ps.bp[0x41], ps.bp[0x40], ps.bp[0x43], ps.bp[0x28], ps.bp[0xC0], ps.bp[0xC1], ps.bp[0xC2], ps.bp[0xC3],
                    ps.tev_konst[0][0], ps.tev_konst[0][1]);
            for (uint32_t st = 0; st < ns; st++)
                fprintf(stderr, "      st%u order=%03X cenv=%06X aenv=%06X ksel=%06X\n", st, (ps.bp[0x28 + st / 2] >> ((st & 1) * 12)) & 0xFFF,
                        ps.bp[0xC0 + 2 * st], ps.bp[0xC1 + 2 * st], ps.bp[0xF6 + st / 2]);
            for (int m = 0; m < 8; m++)
                if (ps.tex_id[m]) fprintf(stderr, "      map%d id=%u img0=%06X img3=%06X\n", m, ps.tex_id[m], ps.bp[(m < 4 ? 0x88 + m : 0xA8 + m - 4)], ps.bp[(m < 4 ? 0x94 + m : 0xB4 + m - 4)]);
            for (int m = 0; m < 3; m++) {
                uint32_t a = (ps.bp[0x94 + m] & 0x1FFFFF) << 5;
                const uint8_t* q = phys_ptr(a);
                uint64_t sum = 0; uint32_t mn = 255, mx = 0;
                for (int i = 0; i < 64000; i++) { sum += q[i]; mn = std::min<uint32_t>(mn, q[i]); mx = std::max<uint32_t>(mx, q[i]); }
                fprintf(stderr, "      plane%d @%08X avg=%llu min=%u max=%u\n", m, a, (unsigned long long)(sum / 64000), mn, mx);
            }
            fprintf(stderr, "      num_texgens=%u (bp genmode %u) xf texgen0=%08X texgen1=%08X\n", ps.num_texgens, ps.bp[0] & 15,
                    g_state.xf_regs[0x40], g_state.xf_regs[0x41]);
            for (uint32_t i = cmd.first; i < cmd.first + 2 && i < bb.indices.size(); i++) {
                const uint32_t v = bb.indices[i];
                fprintf(stderr, "      v%u pos=(%g,%g,%g) col0=%02X%02X%02X%02X col1=%02X%02X%02X%02X\n", v,
                        bb.verts[v].pos[0], bb.verts[v].pos[1], bb.verts[v].pos[2],
                        bb.verts[v].col[0][0], bb.verts[v].col[0][1], bb.verts[v].col[0][2], bb.verts[v].col[0][3],
                        bb.verts[v].col[1][0], bb.verts[v].col[1][1], bb.verts[v].col[1][2], bb.verts[v].col[1][3]);
                for (int tc = 0; tc < 8; tc++)
                    fprintf(stderr, "        tex%d=(%g,%g,%g)\n", tc, bb.verts[v].tex[tc][0],
                            bb.verts[v].tex[tc][1], bb.verts[v].tex[tc][2]);
            }
            for (int r = 0; r < 4; r++)
                fprintf(stderr, "      reg%d %06X %06X konst%d %06X %06X\n", r, ps.tev_reg[r][0], ps.tev_reg[r][1], r, ps.tev_konst[r][0], ps.tev_konst[r][1]);
        }
    }
    if (cc.to_xfb && getenv("GCN_GXSTATS")) {
        Batch& bb = batch();
        uint32_t draws = 0;
        for (auto& cmd : bb.cmds) draws += cmd.type == CmdType::Draw;
        fprintf(stderr, "[gx] frame %u: %u draws, %zu verts, %zu states, %zu new tex, copy %ux%u@%u,%u\n", g_frame_counter, draws,
                bb.verts.size(), bb.states.size(), bb.new_textures.size(), cc.src_w, cc.src_h, cc.src_x, cc.src_y);
        if (!bb.states.empty()) {
            const PixelState& ps = bb.states.back();
            fprintf(stderr, "     genmode %06X order %06X cenv %06X aenv %06X tex0 id %u img0 %06X img3 %06X mode0 %06X blend %06X z %06X\n",
                    ps.bp[0], ps.bp[0x28], ps.bp[0xC0], ps.bp[0xC1], ps.tex_id[0], ps.bp[0x88], ps.bp[0x94], ps.bp[0x80], ps.bp[0x41], ps.bp[0x40]);
            fprintf(stderr, "     proj %g %g %g %g %g %g type %g  vp %g %g %g %g %g %g\n", ps.proj[0], ps.proj[1], ps.proj[2], ps.proj[3],
                    ps.proj[4], ps.proj[5], ps.proj[6], ps.viewport[0], ps.viewport[1], ps.viewport[2], ps.viewport[3], ps.viewport[4], ps.viewport[5]);
        }
        for (size_t i = 0; i < bb.verts.size() && i < 3; i++) {
            const GpuVertex& gv = bb.verts[i];
            fprintf(stderr, "     v%zu pos %g %g %g col %02X%02X%02X%02X tex0 %g %g %g\n", i, gv.pos[0], gv.pos[1], gv.pos[2], gv.col[0][0], gv.col[0][1],
                    gv.col[0][2], gv.col[0][3], gv.tex[0][0], gv.tex[0][1], gv.tex[0][2]);
        }
    }
    if (cc.to_xfb) {
        if (frametime_on()) {
            const Batch& bb = batch();
            size_t decoded = 0;
            for (auto& t : bb.new_textures)
                for (auto& l : t->levels) decoded += l.size() * 4;
            const auto now = std::chrono::steady_clock::now();
            const double interval = g_last_present.time_since_epoch().count() ? ms_since(g_last_present) : 0.0;
            fprintf(stderr, "[ft] f%u guest %6.2fms  fe %6.2fms (tex %5.2f)  draws %4u/%4zu  verts %6zu  idx %6zu  states %4zu  newtex %3zu (%zuKB)  queue %zu\n",
                    g_frame_counter, interval, g_fe_ms, g_tex_ms, g_fe_draws, bb.cmds.size(), bb.verts.size(), bb.indices.size(),
                    bb.states.size(), bb.new_textures.size(), decoded / 1024, queue_depth());
            g_last_present = now;
            g_fe_ms = g_tex_ms = 0;
            g_fe_draws = 0;
        }
        g_frame_counter++;
        texture_evict();
        // With the front end on its own thread these are the guest's business, and the
        // skim does them where the guest issued the copy: see SkimSync::Frame in fifo.cpp.
        if (!g_fe_threaded) {
            g_frames_submitted++;
            copy_writebacks_land(g_frames_submitted.load());
            debug_guest_check("frame");
            heap_trace_frame(g_frames_submitted.load());
        }
        if (display_copy_ends_frame(cc)) {
            g_have_last_state = false;
            flush_batch();
        }
    }
}

}  // namespace gx

uint32_t gx_frames_submitted() { return gx::g_frames_submitted.load(); }
