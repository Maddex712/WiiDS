// Memory, CPU helper routines, timing and event scheduling.
#include "runtime.h"
#include "platform.h"
#include "mmu.h"
#include "modules.h"
#include "input_log.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>
#include <cstdlib>
#include <cmath>

uint8_t* g_mem;
uint8_t* g_aram;
// Off by default: DVD, whose reads happen every frame and drown the rest of the console.
// `--log=dvd` or `--log-all` turns them on.
bool g_log_enabled[LOG_COUNT] = {true, true, false, false, false, false, false, false, false, false, false};

static std::mutex g_log_mutex;
static const char* kLogNames[LOG_COUNT] = {"CPU", "OS", "HW", "DVD", "GX", "VI", "SI", "EXI", "DSP", "AI", "THR"};

// "exi,dsp" enables those categories (names as printed, case-insensitive); "all" enables
// every one. Returns false if a name is unknown.
bool log_enable_list(const char* list) {
    std::string name;
    for (const char* p = list;; p++) {
        if (*p && *p != ',') { name.push_back((char)toupper((unsigned char)*p)); continue; }
        bool found = false;
        if (name == "ALL") { for (auto& e : g_log_enabled) e = true; found = true; }
        for (int i = 0; i < LOG_COUNT && !found; i++)
            if (name == kLogNames[i]) { g_log_enabled[i] = true; found = true; }
        if (!found) return false;
        name.clear();
        if (!*p) return true;
    }
}

void log_msg(LogCat cat, const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_log_mutex);
    fprintf(stderr, "[%s] ", kLogNames[cat]);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void fatal(const char* fmt, ...) {
    // Formatted once rather than printed piecewise, because the text has to reach two
    // places: stderr, and the platform's own crash record. On Android stderr is a pipe
    // into a logcat ring that the app's tracing can lap in under a minute, so a message
    // that went only there can be gone before anyone comes to read it.
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    plat_record_fatal(msg);
    fprintf(stderr, "FATAL: %s\n", msg);
    CPU* c = cpu_current();
    if (c) {
        fprintf(stderr, "  lr=%08X ctr=%08X msr=%08X\n", c->lr, c->ctr, c->msr);
        for (int i = 0; i < 32; i += 4)
            fprintf(stderr, "  r%-2d %08X %08X %08X %08X\n", i, c->r[i], c->r[i + 1], c->r[i + 2], c->r[i + 3]);
    }
    plat_backtrace_print();
    fflush(stderr);
    abort();
}

// ---------------------------------------------------------------------------
// Memory: reserve 1GB, commit RAM at 0 and locked cache at 0x20000000.
// ---------------------------------------------------------------------------
void mem_init() {
    const size_t reserve = 0x40000000;
#ifdef GCN_MEM_BASE
    // See GCN_MEM in recomp.h: the generated code was compiled for this address.
    void* p = plat_reserve(reserve, (void*)(uintptr_t)GCN_MEM_BASE);
    if (!p) fatal("could not reserve guest memory at %llX", (unsigned long long)GCN_MEM_BASE);
#else
    void* p = plat_reserve(reserve);
    if (!p) fatal("mem reserve failed");
#endif
    g_mem = (uint8_t*)p;
    // Plus a guard of zeros past the end: a read that runs a little beyond RAM (a
    // paired-single load at the top of the arena) gets nothing on the hardware and zeros
    // in Dolphin, not a fault. A megabyte, short of where a page table's window begins.
    if (!plat_commit(g_mem, RAM_SIZE + 0x100000)) fatal("mem commit failed");
    if (!plat_commit(g_mem + LC_BASE, LC_SIZE)) fatal("lc commit failed");
    g_aram = (uint8_t*)calloc(1, ARAM_SIZE);
}

// ---------------------------------------------------------------------------
// Indirect calls
// ---------------------------------------------------------------------------
// One slot per instruction word, in 1 MB buckets allocated where there is code: the
// recompiled code may sit in several far-apart ranges (a second image a game maps
// elsewhere, modules at canonical addresses), and one flat table across them would be
// most of a gigabyte.
static RecompFn* g_fn_buckets[4096];

static RecompFn fn_lookup(uint32_t addr) {
    RecompFn* b = g_fn_buckets[addr >> 20];
    return b ? b[(addr & 0xFFFFF) >> 2] : nullptr;
}

const char* func_name(uint32_t addr) {
    for (uint32_t i = 0; i < g_recomp_func_count; i++)
        if (g_recomp_funcs[i].addr == addr) return g_recomp_funcs[i].name;
    return "?";
}

// The function `addr` falls inside, rather than the one it starts. A backtrace is made of
// return addresses, which point into the middle of their function and so match nothing in
// func_name(); the nearest start at or below is what names them. `start` takes that start,
// so a caller can print the offset. Linear because this runs on failure paths only.
const char* func_containing(uint32_t addr, uint32_t* start) {
    uint32_t best = 0;
    const char* name = nullptr;
    for (uint32_t i = 0; i < g_recomp_func_count; i++) {
        uint32_t a = g_recomp_funcs[i].addr;
        if (a <= addr && a >= best) { best = a; name = g_recomp_funcs[i].name; }
    }
    if (start) *start = best;
    return name ? name : "?";
}

static void build_fn_table() {
    for (uint32_t i = 0; i < g_recomp_func_count; i++) {
        uint32_t a = g_recomp_funcs[i].addr;
        RecompFn*& b = g_fn_buckets[a >> 20];
        if (!b) b = (RecompFn*)calloc(0x40000, sizeof(RecompFn));
        b[(a & 0xFFFFF) >> 2] = g_recomp_funcs[i].fn;
    }
    modules_init();
}

bool recomp_is_function(uint32_t addr) {
    return !(addr & 3) && fn_lookup(addr) != nullptr;
}

extern "C" void call_indirect(CPU* c, uint32_t addr) {
    if (!(addr & 3)) {
        RecompFn fn = fn_lookup(addr);
        if (fn) { fn(c); return; }
        // In a module the game loaded: at its canonical address, once bound.
        uint32_t canon = modules_canon(addr);
        if (canon == addr && modules_bind(addr)) canon = modules_canon(addr);
        if (canon != addr && (fn = fn_lookup(canon))) { fn(c); return; }
        // In a relocatable module, where the game linked it.
        canon = rels_canon(addr);
        if (canon != addr && (fn = fn_lookup(canon))) { fn(c); return; }
    }
    // GCN_RAM_DUMP=<path>: the guest RAM at this failure, for finding out what the code at
    // `addr` is and where it came from.
    if (const char* path = getenv("GCN_RAM_DUMP")) {
        if (FILE* f = fopen(path, "wb")) { fwrite(g_mem, 1, RAM_SIZE, f); fclose(f); }
    }
    fatal("call_indirect: no function at %08X (lr=%08X)", addr, c->lr);
}

extern "C" void unimpl(CPU* c, uint32_t pc, uint32_t inst) {
    fatal("unimplemented instruction %08X at %08X", inst, pc);
}
extern "C" void hle_sc(CPU* c, uint32_t pc) {}  // PPCSync etc.
extern "C" void hle_trap(CPU* c, uint32_t pc, uint32_t inst) {
    // tw/twi: evaluate TO field
    uint32_t to = (inst >> 21) & 31, a = c->r[(inst >> 16) & 31];
    uint32_t b = (inst >> 26) == 3 ? (uint32_t)(int32_t)(int16_t)inst : c->r[(inst >> 11) & 31];
    int32_t sa = (int32_t)a, sb = (int32_t)b;
    bool t = ((to & 16) && sa < sb) || ((to & 8) && sa > sb) || ((to & 4) && a == b) ||
             ((to & 2) && a < b) || ((to & 1) && a > b);
    if (t) fatal("trap at %08X", pc);
}
// rfi: continue at SRR0 with MSR = SRR1. The SDK uses it for two things, neither an
// exception return (exceptions are delivered and returned by the runtime, see threads.cpp):
//   * "return with a new MSR": mtsrr0 lr; mtsrr1 ...; rfi. SRR0 is the link register, so
//     this is a blr. The generated code RETs right after this call.
//   * "jump with a new MSR": __OSInitMemoryProtection's RealMode puts a *physical* code
//     address in SRR0 and rfi's to it, to run the BAT setup with translation off. Run as a
//     call: the target itself ends in the first idiom, which brings control back here, and
//     the RET after this call then returns to RealMode's caller -- which is where the
//     target's own rfi was aiming.
extern "C" void hle_rfi(CPU* c, uint32_t pc) {
    const uint32_t target = c->spr[26], msr = c->spr[27];
    c->msr = msr;
    if (target == c->lr) return;
    call_indirect(c, target | 0x80000000u);
}

extern "C" void hle_mtmsr(CPU* c, uint32_t v) { c->msr = v; }
extern "C" void hle_set_fpscr(CPU* c, uint32_t v) {
    c->fpscr = v;
    c->fprf_pending = 0;  // an explicit write supersedes the last result's class
    // rounding mode bits 30-31 (RN); only nearest is honoured for now
}

enum { SPR_DEC = 22, SPR_SDR1 = 25, SPR_SRR0 = 26, SPR_SRR1 = 27, SPR_TBL_R = 268, SPR_TBU_R = 269,
       SPR_TBL_W = 284, SPR_TBU_W = 285, SPR_HID2 = 920, SPR_WPAR = 921, SPR_DMAU = 922, SPR_DMAL = 923 };

static int64_t g_tb_offset;

// Registers that configure the processor itself rather than describe what a thread is doing:
// the HIDs, the gather pipe's and the locked cache's, the BATs and SDR1, the L2 and the
// performance monitor. There is one of each on the console, shared by every thread, and the
// OS does not save them with a context. Each guest thread has a CPU of its own here, so
// these are kept apart from it: a decoder that enabled the locked cache on one thread and
// checked HID2 for it on another found it disabled.
static uint32_t g_hw_spr[1024];
static bool is_hw_spr(uint32_t spr) {
    return spr == 25 || (spr >= 528 && spr <= 543) || (spr >= 920 && spr <= 923) || (spr >= 936 && spr <= 958) ||
           spr == 1008 || spr == 1009 || (spr >= 1013 && spr <= 1019);
}
static uint32_t& spr_ref(CPU* c, uint32_t spr) {
    spr &= 1023;
    return is_hw_spr(spr) ? g_hw_spr[spr] : c->spr[spr];
}

void dec_write(CPU* c, uint32_t v);
uint32_t dec_read(CPU* c);

void gp_reset();  // gx/fifo.cpp

extern "C" uint32_t hle_mfspr(CPU* c, uint32_t spr) {
    switch (spr) {
    case SPR_WPAR:
        // Bit 0 reports a non-empty gather buffer; the SDK spins on it before it moves
        // the FIFO. It always reads as empty here: full lines go out as they fill, and
        // what is left is padding the SDK's next WPAR write discards (gp_reset).
        return spr_ref(c, spr) & ~1u;
    case SPR_TBL_R: return (uint32_t)(now_ticks() + g_tb_offset);
    case SPR_TBU_R: return (uint32_t)((now_ticks() + g_tb_offset) >> 32);
    case SPR_DEC: return dec_read(c);
    // HID2 reads back as written, except DMAQL (bits 24..27): the locked-cache DMA queue
    // is always empty here, since hle_mtspr runs each transfer at once. The top four bits
    // are LSQE, WPE, PSE and LCE, and the game reads them: the THP video decoder checks
    // LCE (0x10000000) before it will decode a frame, and returns an error if it is clear,
    // which is what blanked the title and menu videos.
    case SPR_HID2: return spr_ref(c, spr) & ~0x0F000000u;
    default: return spr_ref(c, spr);
    }
}

extern "C" void hle_mtspr(CPU* c, uint32_t spr, uint32_t v) {
    switch (spr) {
    case SPR_TBL_W: {
        uint64_t cur = now_ticks() + g_tb_offset;
        uint64_t nv = (cur & 0xFFFFFFFF00000000ull) | v;
        g_tb_offset += (int64_t)(nv - cur);
        return;
    }
    case SPR_TBU_W: {
        uint64_t cur = now_ticks() + g_tb_offset;
        uint64_t nv = (cur & 0xFFFFFFFFull) | ((uint64_t)v << 32);
        g_tb_offset += (int64_t)(nv - cur);
        return;
    }
    case SPR_DEC: dec_write(c, v); return;
    case SPR_SDR1: spr_ref(c, spr) = v; mmu_set_sdr1(v); return;
    case SPR_WPAR:
        // Writing WPAR sets the gather address and empties the gather buffer. The SDK
        // writes it only after padding the pipe with zero bytes and waiting for the
        // buffer-not-empty bit to clear, on both sides of a redirect; what is left in the
        // buffer then is padding that must not reach memory. See gp_reset.
        gp_reset();
        spr_ref(c, spr) = v;
        return;
    case SPR_DMAL: {
        spr_ref(c, spr) = v;
        if (v & 2) {  // trigger locked-cache DMA
            uint32_t dmau = spr_ref(c, SPR_DMAU);
            uint32_t len = ((dmau & 0x1F) << 2) | ((v >> 2) & 3);
            if (len == 0) len = 128;
            uint32_t mem = dmau & ~0x1Fu, lc = v & ~0x1Fu;
            if (v & 0x10) memcpy(HOST_W(lc), HOST(mem), len * 32);   // load: mem -> LC
            else { memcpy(HOST_W(mem), HOST(lc), len * 32); debug_guard_dma("locked-cache DMA", mem, len * 32); debug_guest_check("after locked-cache DMA"); }  // store: LC -> mem
            spr_ref(c, spr) &= ~2u;
        }
        return;
    }
    default: spr_ref(c, spr) = v; return;
    }
}

extern "C" void hle_dcbz(CPU* c, uint32_t ea) { memset(HOST_W(ea & ~31u), 0, 32); debug_guard_dma("dcbz", ea & ~31u, 32); }
extern "C" void hle_dcbz_l(CPU* c, uint32_t ea) { memset(HOST_W(ea & ~31u), 0, 32); debug_guard_dma("dcbz_l", ea & ~31u, 32); }
extern "C" void hle_dcbi(CPU* c, uint32_t ea) {}

extern "C" void hle_lswi(CPU* c, uint32_t ea, int rd, int n) {
    int r = (rd - 1) & 31;
    for (int i = 0; i < n; i++) {
        if ((i & 3) == 0) { r = (r + 1) & 31; c->r[r] = 0; }
        c->r[r] |= LD8(ea + i) << (24 - 8 * (i & 3));
    }
}
extern "C" void hle_stswi(CPU* c, uint32_t ea, int rs, int n) {
    int r = rs, shift = 24;
    for (int i = 0; i < n; i++) {
        ST8(ea + i, (c->r[r] >> shift) & 0xFF);
        if (shift == 0) { r = (r + 1) & 31; shift = 24; } else shift -= 8;
    }
}

// ---------------------------------------------------------------------------
// Paired-single quantized load/store
// ---------------------------------------------------------------------------
// 2^n for the six-bit signed scale a GQR holds, by table: ldexpf is a libm call, and it
// was being made for every element of every quantised load and store.
static const float* pow2_table() {
    static float t[128];
    static bool init = [] {
        for (int n = -64; n < 64; n++) t[n + 64] = ldexpf(1.0f, n);
        return true;
    }();
    (void)init;
    return t;
}
static inline float pow2(int n) { return pow2_table()[n + 64]; }

static inline double dequant(uint32_t type, int scale, uint32_t ea, int idx) {
    float s = pow2(-scale);
    switch (type) {
    case 4: return (double)((float)(uint8_t)LD8(ea + idx) * s);
    case 5: return (double)((float)(uint16_t)LD16(ea + idx * 2) * s);
    case 6: return (double)((float)(int8_t)LD8(ea + idx) * s);
    case 7: return (double)((float)(int16_t)LD16(ea + idx * 2) * s);
    default: return LDF32(ea + idx * 4);
    }
}

extern "C" void psq_load_quantised(CPU* c, uint32_t ea, int frd, int w, int gqr) {
    uint32_t g = c->gqr[gqr];
    uint32_t type = (g >> 16) & 7;
    int scale = (int)((g >> 24) & 0x3F);
    if (scale & 0x20) scale -= 64;
    double p0 = dequant(type, scale, ea, 0);
    double p1 = w ? 1.0 : dequant(type, scale, ea, 1);
    c->f[frd].d = p0;
    c->ps1[frd] = p1;
}

static inline void quant_store(uint32_t type, int scale, uint32_t ea, int idx, double v) {
    float f = (float)v * pow2(scale);
    auto clampi = [](float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); };
    switch (type) {
    case 4: ST8(ea + idx, (uint8_t)(int)clampi(f, 0, 255)); break;
    case 5: ST16(ea + idx * 2, (uint16_t)(int)clampi(f, 0, 65535)); break;
    case 6: ST8(ea + idx, (uint8_t)(int8_t)(int)clampi(f, -128, 127)); break;
    case 7: ST16(ea + idx * 2, (uint16_t)(int16_t)(int)clampi(f, -32768, 32767)); break;
    default: STF32(ea + idx * 4, v); break;
    }
}

extern "C" void psq_store_quantised(CPU* c, uint32_t ea, int frs, int w, int gqr) {
    uint32_t g = c->gqr[gqr];
    uint32_t type = g & 7;
    int scale = (int)((g >> 8) & 0x3F);
    if (scale & 0x20) scale -= 64;
    quant_store(type, scale, ea, 0, c->f[frs].d);
    if (!w) quant_store(type, scale, ea, 1, c->ps1[frs]);
}

// ---------------------------------------------------------------------------
// Timing and events
// ---------------------------------------------------------------------------
static std::chrono::steady_clock::time_point g_t0;

// Diagnostic only: GCN_TIMESCALE=N makes the emulated timebase advance N times faster,
// so the game tries to run at N x real time. Useful for finding out how much CPU
// headroom a machine actually has -- a host that is merely keeping up at 1.0 and one
// with room to spare both report the game's capped 30 fps otherwise. It skews every
// other emulated timing (DVD, audio, retrace), so it is not a correctness path.
static double g_time_scale = 1.0;

// Virtual clock: guest time is the back-edge count times this. The value sets how much
// work the guest can do per 60 Hz frame before it sees the retrace arrive late: with
// g_ticks_per_edge ticks per loop iteration, a frame holds FIELD_TICKS / g_ticks_per_edge
// iterations. It is a constant, not a measurement, because determinism is the point: the
// same route must count the same ticks on every machine. Measured with GCN_CLOCKLOG
// (2026-10-08): a game's ordinary play ran 45-60k back-edges of work per frame and its
// heaviest cinematic peaked near 85k, so at 4 ticks per edge that cinematic took half a
// field and at 2 a quarter. 2 leaves room for scenes heavier than those; the real Gekko,
// at a few cycles per short loop iteration, is in the same range.
//
// GCN_CPU_SCALE=N (or clock_set_cpu_scale) runs the guest on a CPU N times as fast as
// that: the ticks per edge are divided by N, so a frame of the same work takes 1/N of the
// virtual time, which is how a game that paces itself on the retrace can be made to run
// at a frame rate its own hardware could not reach. Integer-ish: 2 is one tick per edge,
// which is as fast as the virtual clock resolves.
static constexpr uint64_t kTicksPerEdge = 2;
static uint64_t g_ticks_per_edge = kTicksPerEdge;
static bool g_virtual_clock = true;
volatile uint64_t g_vcount;
volatile uint64_t g_vlimit = UINT64_MAX;
static int64_t g_pace_offset_ns;  // virtual time the host gave up on catching up with
// The clock's base: virtual time is g_vbase_ticks plus the edges since g_vbase_count at
// the current rate. A change of rate (clock_set_cpu_scale) moves the base to the present
// so that the time already elapsed keeps its value. Scaling the whole count instead, as
// this did at first, rescaled the elapsed time too: switching a game from 30 to 60 fps a
// few minutes in halved the virtual clock, every pending timer became minutes away, and
// the paced game froze for as long; switching back doubled it and the pacer slept the
// same again (the Quest, 2026-10-10). An unpaced harness run never showed it, since its
// idle jumps cross any gap for free.
static uint64_t g_vbase_ticks, g_vbase_count;

bool clock_is_virtual() { return g_virtual_clock; }
void clock_set_scale(double s) { g_time_scale = s; }
double clock_scale() { return g_time_scale; }
static inline uint64_t virtual_ticks() { return g_vbase_ticks + (g_vcount - g_vbase_count) * g_ticks_per_edge; }
void clock_set_cpu_scale(double s) {
    if (s <= 0) return;
    g_vbase_ticks = virtual_ticks();
    g_vbase_count = g_vcount;
    g_ticks_per_edge = std::max<uint64_t>(1, (uint64_t)((double)kTicksPerEdge / s + 0.5));
    clock_update_limit();  // the pending limit was counted at the old rate
}
double clock_cpu_scale() { return (double)kTicksPerEdge / (double)g_ticks_per_edge; }

void timing_init() {
    g_t0 = std::chrono::steady_clock::now();
    if (const char* e = getenv("GCN_CLOCK")) g_virtual_clock = strcmp(e, "host") != 0;
    if (const char* e = getenv("GCN_TIMESCALE")) {
        double s = atof(e);
        if (s >= 0) g_time_scale = s;
    }
    if (const char* e = getenv("GCN_CPU_SCALE")) clock_set_cpu_scale(atof(e));
    build_fn_table();
}

uint64_t host_ns() {
    auto d = std::chrono::steady_clock::now() - g_t0;
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}

uint64_t now_ticks() {
    if (g_virtual_clock) return virtual_ticks();
    return (uint64_t)((double)host_ns() * (TB_FREQ / 1e9) * (g_time_scale > 0 ? g_time_scale : 1.0));
}

void irq_request() { g_vlimit = 0; }

// The guest has nothing to run until the next event. On the virtual clock that event is
// a known count away, so jump straight to it; the spin's iterations would have been
// nothing but clock. On the host clock, give the CPU up for a moment instead of spinning.
static uint64_t g_vskipped;
static uint64_t g_jumps, g_jump_edges;   // the clock's catch-up jumps; see clock_pace
extern "C" void idle_hint(CPU* c) {
    if (g_virtual_clock) {
        uint64_t limit = g_vlimit, cur = g_vcount;
        if (limit != UINT64_MAX && cur < limit) { g_vskipped += limit - cur; g_vcount = limit; }
        else g_vcount = cur + 1;
    } else {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    irq_poll(c);
}

extern std::atomic<uint64_t> g_dec_deadline;
extern std::atomic<uint64_t> g_next_event_at;

void clock_update_limit() {
    if (!g_virtual_clock) { g_vlimit = UINT64_MAX; return; }  // the ticker asks for polls
    uint64_t at = std::min(g_next_event_at.load(), g_dec_deadline.load());
    uint64_t limit = at == UINT64_MAX ? UINT64_MAX
                   : at <= g_vbase_ticks ? g_vbase_count
                   : g_vbase_count + (at - g_vbase_ticks + g_ticks_per_edge - 1) / g_ticks_per_edge;
    // A replay's next recorded jump is made from a poll at its count (clock_pace).
    g_vlimit = std::min(limit, input_replay_next_jump_at());
}

uint32_t gx_frames_submitted();
extern uint64_t g_aid_frames_delivered;   // hw/dsp.cpp

// GCN_CLOCKLOG=1: once per virtual second, how the guest's time relates to the host's and
// how much the guest executed per presented frame. The edges/frame figure is what
// g_ticks_per_edge is tuned against.
static void clock_log() {
    static bool on = getenv("GCN_CLOCKLOG") != nullptr;
    if (!on) return;
    static uint64_t next_at, last_edges, last_frames, last_audio;
    uint64_t vt = now_ticks();
    if (vt < next_at) return;
    next_at = vt + TB_FREQ;
    const uint64_t audio = g_aid_frames_delivered;
    uint64_t edges = g_vcount - g_vskipped, frames = gx_frames_submitted(), host = host_ns();
    fprintf(stderr, "[clock] virtual %6.1fs host %6.1fs  frames %u (+%u)  work edges/frame %.0fk (%.0f%% of a field)  jumps %u (%.0f ms)\n",
            (double)vt / TB_FREQ, (double)host / 1e9, (unsigned)frames, (unsigned)(frames - last_frames),
            frames > last_frames ? (double)(edges - last_edges) / (frames - last_frames) / 1000.0 : 0.0,
            frames > last_frames ? (double)(edges - last_edges) / (frames - last_frames) * g_ticks_per_edge * 100.0 / (TB_FREQ / 59.94) : 0.0,
            (unsigned)g_jumps, (double)g_jump_edges * g_ticks_per_edge * 1000.0 / TB_FREQ);
    // The sound the AI DMA delivered in that second, which should be its sample rate.
    fprintf(stderr, "[clock] audio %llu frames\n", (unsigned long long)(audio - last_audio));
    last_edges = edges; last_frames = frames; last_audio = audio;
}

// Catching up. A host that cannot keep real time -- a heavy room on a headset -- left
// virtual time behind the wall clock. The game saw nothing wrong: every frame was a
// frame, so it ran at full speed in slow motion, and the sound, delivered a virtual DMA
// block at a time, came up short of the device's real seconds -- the dropouts. The
// hardware would have dropped frames instead, its DSP keeping real time and the game's
// own step growing with the frame. So in play, when virtual time has fallen more than a
// few milliseconds behind the host's, it jumps to it: the game's next reading of the
// time base sees the frame it really took, the AI DMA's blocks come due together and are
// delivered at speed (aid_start_block), the retrace is simply late once, and the sound
// carries on. Each jump is written to the input log with the back-edge count it was
// made at, and a replay makes exactly those jumps at exactly those counts and none of
// its own, so a replay is still exact. GCN_CATCHUP=0 keeps the slow-motion behaviour. A
// host more than kForgiveNs behind is re-anchored as it always was: a stall that size
// is a load or a shader build, not a frame to make up.
static const bool g_catchup = !(getenv("GCN_CATCHUP") && atoi(getenv("GCN_CATCHUP")) == 0);
static constexpr int64_t kCatchupNs = 4000000;
static constexpr int64_t kForgiveNs = 100000000;

static void clock_jump(uint64_t edges) {
    g_vcount += edges;
    g_vskipped += edges;
    g_jumps++;
    g_jump_edges += edges;
}

// Hold virtual time to real time. Called from irq_poll, i.e. whenever an event is due,
// which is at least every SI poll (1/120 s). When the host falls behind, the shortfall is
// caught up (above) or, under GCN_CATCHUP=0 or in a replay with no jump recorded here,
// forgiven: the game then slows down instead of fast-forwarding later.
void clock_pace() {
    clock_log();
    if (!g_virtual_clock) return;
    if (const uint64_t edges = input_replay_take_jump(g_vcount)) clock_jump(edges);
    if (g_time_scale <= 0) return;
    int64_t virt_ns = (int64_t)((double)now_ticks() * (1e9 / TB_FREQ) / g_time_scale);
    int64_t ahead = virt_ns - (int64_t)host_ns() - g_pace_offset_ns;
    if (ahead > 300000) {
        if (ahead > 50000000) ahead = 50000000;
        std::this_thread::sleep_for(std::chrono::nanoseconds(ahead));
    } else if (ahead < -kForgiveNs) {
        g_pace_offset_ns += ahead;  // too far behind: re-anchor
    } else if (ahead < -kCatchupNs && g_catchup && !input_replay_active()) {
        const double ticks = (double)-ahead * (TB_FREQ / 1e9) * g_time_scale;
        const uint64_t edges = (uint64_t)((ticks + g_ticks_per_edge - 1) / g_ticks_per_edge);
        input_log_jump(g_vcount, edges);
        clock_jump(edges);
    }
}

struct Event {
    uint64_t at;
    uint64_t seq;
    EventFn fn;
    bool operator>(const Event& o) const { return at != o.at ? at > o.at : seq > o.seq; }
};
static std::mutex g_ev_mutex;
static std::priority_queue<Event, std::vector<Event>, std::greater<Event>> g_events;
static uint64_t g_ev_seq;
std::atomic<uint64_t> g_next_event_at{UINT64_MAX};  // declared above

void event_schedule(uint64_t at, EventFn fn) {
    {
        std::lock_guard<std::mutex> lk(g_ev_mutex);
        g_events.push(Event{at, g_ev_seq++, std::move(fn)});
        g_next_event_at = g_events.top().at;
    }
    clock_update_limit();
}
void event_schedule_in(uint64_t delta, EventFn fn) { event_schedule(now_ticks() + delta, std::move(fn)); }

void events_run_due() {
    uint64_t now = now_ticks();
    for (;;) {
        EventFn fn;
        {
            std::lock_guard<std::mutex> lk(g_ev_mutex);
            if (g_events.empty() || g_events.top().at > now) {
                g_next_event_at = g_events.empty() ? UINT64_MAX : g_events.top().at;
                return;
            }
            fn = std::move(const_cast<Event&>(g_events.top()).fn);
            g_events.pop();
        }
        fn();
    }
}
