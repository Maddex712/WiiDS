// Guest threads, context switching and interrupt delivery.
//
// The SDK scheduler (SelectThreadToRun etc.) is recompiled unmodified. We HLE only
// OSSaveContext / OSLoadContext:
//   * Each guest execution stream runs on its own host thread; exactly one host
//     thread runs guest code at a time (a baton is handed over on switch).
//   * OSSaveContext(ctx) records a "binding" ctx -> (host thread, jmp_buf, CPU
//     snapshot). The jmp_buf was filled by setjmp at the call site (the recompiler
//     emits that for calls to OSSaveContext), so resuming = longjmp there with r3=1.
//   * Interrupt delivery at a safe point binds the current OSContext in the same way
//     (setjmp in irq_deliver) and runs the SDK's exception handler.
//   * OSLoadContext(ctx) resumes whichever host thread owns the binding (possibly
//     ourselves), or spawns a new host thread for a never-run context.
#include "runtime.h"
#include "mmu.h"
#include "platform.h"
#include "input_log.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>


enum BindKind { BIND_NONE, BIND_SAVED, BIND_EXC };

struct Binding {
    HostThread* h = nullptr;
    BindKind kind = BIND_NONE;
    jmp_buf jb;
    CPU snapshot;
    // What the save wrote into the context's SRR0 and r1. A context that no longer holds
    // them was re-initialised since (see binding_stale).
    uint32_t srr0 = 0, sp = 0;
};

struct HostThread {
    CPU cpu;
    int id;
    std::mutex m;
    std::condition_variable cv;
    bool run = false;
    Binding* resume = nullptr;   // set by whoever wakes us
    uint32_t start_ctx = 0;      // for fresh threads
    bool retired = false;        // its guest thread is gone: exit when woken
};

static thread_local HostThread* t_self;
static std::unordered_map<uint32_t, Binding> g_bindings;
static std::vector<HostThread*> g_threads;
static std::atomic<bool> g_quit{false};

HostThread* host_current() { return t_self; }
CPU* cpu_current() { return t_self ? &t_self->cpu : nullptr; }

// OSContext layout
enum { CTX_GPR = 0x000, CTX_CR = 0x080, CTX_LR = 0x084, CTX_CTR = 0x088, CTX_XER = 0x08C,
       CTX_FPR = 0x090, CTX_FPSCR = 0x194, CTX_SRR0 = 0x198, CTX_SRR1 = 0x19C,
       CTX_MODE = 0x1A0, CTX_STATE = 0x1A2, CTX_GQR = 0x1A4, CTX_PSF = 0x1C8 };
enum { OS_CONTEXT_STATE_EXC = 0x02 };

static void cpu_to_context(const CPU* c, uint32_t ctx, uint32_t srr0) {
    for (int i = 0; i < 32; i++) mem_w32(ctx + CTX_GPR + 4 * i, c->r[i]);
    mem_w32(ctx + CTX_CR, cpu_get_cr(c));
    mem_w32(ctx + CTX_LR, c->lr);
    mem_w32(ctx + CTX_CTR, c->ctr);
    mem_w32(ctx + CTX_XER, ((uint32_t)c->xer_so << 31) | ((uint32_t)c->xer_ov << 30) | ((uint32_t)c->xer_ca << 29));
    mem_w32(ctx + CTX_SRR0, srr0);
    mem_w32(ctx + CTX_SRR1, c->msr);
    for (int i = 0; i < 8; i++) mem_w32(ctx + CTX_GQR + 4 * i, c->gqr[i]);
}

static void context_to_cpu(CPU* c, uint32_t ctx) {
    for (int i = 0; i < 32; i++) c->r[i] = mem_r32(ctx + CTX_GPR + 4 * i);
    cpu_set_cr(c, mem_r32(ctx + CTX_CR));
    c->lr = mem_r32(ctx + CTX_LR);
    c->ctr = mem_r32(ctx + CTX_CTR);
    uint32_t xer = mem_r32(ctx + CTX_XER);
    c->xer_so = xer >> 31; c->xer_ov = (xer >> 30) & 1; c->xer_ca = (xer >> 29) & 1;
    for (int i = 0; i < 32; i++) {
        uint64_t v = ((uint64_t)mem_r32(ctx + CTX_FPR + 8 * i) << 32) | mem_r32(ctx + CTX_FPR + 8 * i + 4);
        c->f[i].u = v;
        uint64_t p = ((uint64_t)mem_r32(ctx + CTX_PSF + 8 * i) << 32) | mem_r32(ctx + CTX_PSF + 8 * i + 4);
        memcpy(&c->ps1[i], &p, 8);
    }
    c->fpscr = mem_r32(ctx + CTX_FPSCR);
    c->fprf_pending = 0;
    c->msr = mem_r32(ctx + CTX_SRR1);
    for (int i = 0; i < 8; i++) c->gqr[i] = mem_r32(ctx + CTX_GQR + 4 * i);
}

// ---------------------------------------------------------------------------
// Baton passing
// ---------------------------------------------------------------------------
static void* host_thread_main(void* arg);

static HostThread* new_host_thread() {
    HostThread* h = new HostThread();
    h->id = (int)g_threads.size();
    h->cpu.host = h;
    g_threads.push_back(h);
    return h;
}

// Guest code recurses deeply and the SDK gives its threads generous stacks, so give
// each host thread 64MB rather than the platform default.
static constexpr size_t HOST_STACK = 64 << 20;

static void spawn(HostThread* h) {
    if (!plat_thread_start(host_thread_main, h, HOST_STACK))
        fatal("could not start host thread %d", h->id);
}

// GCN_SWITCHLOG=1: how many times a frame the baton passed between host threads and how
// long each handoff took, from the wake to the woken thread running -- time the guest is
// stopped that neither the profile (CPU time) nor GCN_STALLS sees.
static const bool g_switchlog = getenv("GCN_SWITCHLOG") != nullptr;
static std::chrono::steady_clock::time_point g_switch_t0;
static uint64_t g_switches, g_switch_ns;
uint32_t gx_frames_submitted();
static void switch_report() {
    static uint32_t last_frame;
    const uint32_t f = gx_frames_submitted();
    if (f < last_frame + 60) return;
    const double frames = f - last_frame;
    fprintf(stderr, "[switch] f%u per frame over %.0f: %.1f handoffs, %.3f ms (%.1f us each)\n", f, frames,
            g_switches / frames, g_switch_ns / frames / 1e6, g_switches ? g_switch_ns / (double)g_switches / 1e3 : 0.0);
    g_switches = g_switch_ns = 0;
    last_frame = f;
}

static void wake(HostThread* h, Binding* b) {
    if (g_switchlog) g_switch_t0 = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(h->m);
    h->resume = b;
    h->run = true;
    h->cv.notify_one();
}

// Block the current host thread until someone hands us the baton. Returns the
// binding we should resume at (or nullptr for a fresh start).
static Binding* wait_for_baton(HostThread* self) {
    std::unique_lock<std::mutex> lk(self->m);
    self->cv.wait(lk, [&] { return self->run || g_quit.load() || self->retired; });
    if (g_quit || self->retired) { lk.unlock(); plat_thread_exit(); }
    return self->resume;
}

[[noreturn]] static void resume_binding(HostThread* self, Binding* b) {
    self->cpu = b->snapshot;
    self->cpu.host = self;
    if (b->kind == BIND_SAVED) self->cpu.r[3] = 1;
    b->kind = BIND_NONE;
    irq_request();  // re-evaluate pending interrupts after a switch
    GCN_LONGJMP(b->jb, 1);  // the mask-free variant off Windows; see compat.h
}

// Hand the baton to `target` (resuming at binding b) and block until we get it back.
[[noreturn]] static void switch_to(HostThread* self, HostThread* target, Binding* b) {
    {
        std::lock_guard<std::mutex> lk(self->m);
        self->run = false;
        self->resume = nullptr;
    }
    wake(target, b);
    Binding* mine = wait_for_baton(self);
    if (g_switchlog) {
        g_switches++;
        g_switch_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - g_switch_t0).count();
        switch_report();
    }
    if (!mine) fatal("thread %d woken without resume point", self->id);
    resume_binding(self, mine);
}

// A binding is a resume point for the context it was made for. The SDK saves a thread's
// context even as the thread exits (OSExitThread clears the context, then reschedules, and
// SelectThread saves the current thread's context before choosing the next), so a game
// that creates a new thread on an exited thread's OSThread -- OSCreateThread
// re-initialises the context with the new entry point and stack -- loads a context whose
// binding is the dead thread inside OSExitThread. Resuming that ran the old thread on
// past its exit and returned to whatever the new thread's stack held. The save's SRR0
// and r1 are in the context until something rewrites it, so a context that no longer
// holds them is a fresh one.
static void binding_stamp(Binding* b, uint32_t ctx) {
    b->srr0 = mem_r32(ctx + CTX_SRR0);
    b->sp = mem_r32(ctx + CTX_GPR + 4);
}
static bool binding_stale(const Binding* b, uint32_t ctx) {
    return mem_r32(ctx + CTX_SRR0) != b->srr0 || mem_r32(ctx + CTX_GPR + 4) != b->sp;
}

// The host thread of a guest thread that is gone: drop every resume point on it and let
// it exit, which frees its stack.
static void retire(HostThread* h) {
    for (auto& kv : g_bindings)
        if (kv.second.h == h) kv.second.kind = BIND_NONE;
    std::lock_guard<std::mutex> lk(h->m);
    h->retired = true;
    h->cv.notify_one();
}

static void* host_thread_main(void* arg) {
    HostThread* self = (HostThread*)arg;
    t_self = self;
    plat_thread_role(ThreadRole::GuestMain, "guest");
    Binding* b = wait_for_baton(self);
    if (b) fatal("fresh host thread %d asked to resume a binding", self->id);
    CPU* c = &self->cpu;
    uint32_t pc = mem_r32(self->start_ctx + CTX_SRR0);
    context_to_cpu(c, self->start_ctx);
    mem_w16(self->start_ctx + CTX_STATE, mem_r16(self->start_ctx + CTX_STATE) & ~OS_CONTEXT_STATE_EXC);
    LOG(LOG_THREAD, "host thread %d starting guest context %08X at %08X", self->id, self->start_ctx, pc);
    call_indirect(c, pc);
    for (;;) call_indirect(c, c->lr);  // thread function returned: continue at LR (OSExitThread)
    return nullptr;
}

// ---------------------------------------------------------------------------
// HLE: OSSaveContext / OSLoadContext
// ---------------------------------------------------------------------------
extern "C" jmp_buf* hle_context_jmpbuf(CPU* c) {
    // Called at an OSSaveContext call site before the call; r3 = context.
    return &g_bindings[c->r[3]].jb;
}

extern "C" void orig_OSSaveContext(CPU* c);
extern "C" void hle_OSSaveContext(CPU* c) {
    uint32_t ctx = c->r[3];
    orig_OSSaveContext(c);  // writes the context to guest memory, returns r3 = 0
    Binding& b = g_bindings[ctx];
    b.h = t_self;
    b.kind = BIND_SAVED;
    b.snapshot = *c;
    binding_stamp(&b, ctx);
    LOG(LOG_THREAD, "save ctx %08X on host %d", ctx, t_self->id);
}

extern "C" void hle_OSLoadContext(CPU* c) {
    uint32_t ctx = c->r[3];
    HostThread* self = t_self;
    // Clear the exception flag like the real OSLoadContext does.
    mem_w16(ctx + CTX_STATE, mem_r16(ctx + CTX_STATE) & ~OS_CONTEXT_STATE_EXC);
    auto it = g_bindings.find(ctx);
    if (it != g_bindings.end() && it->second.kind != BIND_NONE && binding_stale(&it->second, ctx)) {
        // The context was re-initialised since it was saved: a new thread on an old
        // thread's OSThread. The old one's host thread can never run again.
        HostThread* old = it->second.h;
        LOG(LOG_THREAD, "ctx %08X re-initialised since its save: host %d retired", ctx, old->id);
        if (old != self) retire(old);
        it->second.kind = BIND_NONE;
    }
    if (it != g_bindings.end() && it->second.kind != BIND_NONE) {
        Binding* b = &it->second;
        LOG(LOG_THREAD, "load ctx %08X (host %d -> %d)", ctx, self->id, b->h->id);
        if (b->h == self) resume_binding(self, b);
        switch_to(self, b->h, b);
    }
    // Never-run context: start a new host thread for it.
    HostThread* h = new_host_thread();
    h->start_ctx = ctx;
    LOG(LOG_THREAD, "load fresh ctx %08X -> new host thread %d", ctx, h->id);
    spawn(h);
    switch_to(self, h, nullptr);
}

// ---------------------------------------------------------------------------
// Decrementer
// ---------------------------------------------------------------------------
static uint64_t g_dec_base_tick;
static uint32_t g_dec_value;
std::atomic<uint64_t> g_dec_deadline{UINT64_MAX};
static bool g_dec_pending;

void dec_write(CPU* c, uint32_t v) {
    g_dec_base_tick = now_ticks();
    g_dec_value = v;
    g_dec_pending = false;
    // Exception fires when the decrementer passes from 0 to -1.
    g_dec_deadline = (v & 0x80000000u) ? UINT64_MAX : g_dec_base_tick + (uint64_t)v + 1;
    clock_update_limit();
}

uint32_t dec_read(CPU* c) {
    return g_dec_value - (uint32_t)(now_ticks() - g_dec_base_tick);
}

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------
extern std::atomic<uint32_t> g_pi_intsr, g_pi_intmr;
extern std::atomic<uint64_t> g_next_event_at;

static void irq_deliver(CPU* c, uint32_t exc) {
    uint32_t ctx = mem_r32(0x800000D4);  // OSCurrentContext
    if (!ctx || (ctx & 0x3FFFFFFF) >= RAM_SIZE) fatal("interrupt with bad current context %08X (exc %u)", ctx, exc);
    Binding* b = &g_bindings[ctx];
    b->h = t_self;
    b->kind = BIND_EXC;
    b->snapshot = *c;
    cpu_to_context(c, ctx, 0);
    binding_stamp(b, ctx);
    mem_w16(ctx + CTX_STATE, mem_r16(ctx + CTX_STATE) | OS_CONTEXT_STATE_EXC);
    if (GCN_SETJMP(b->jb) != 0) return;  // resumed by OSLoadContext(ctx)
    c->spr[26] = 0;          // SRR0
    c->spr[27] = c->msr;     // SRR1
    c->msr &= ~MSR_EE;
    c->r[3] = exc;
    c->r[4] = ctx;
    uint32_t handler = mem_r32(0x80003000 + exc * 4);
    call_indirect(c, handler);
    fatal("exception handler %08X for exception %u returned", handler, exc);
}

extern "C" void irq_poll(CPU* c) {
    if (g_quit) plat_thread_exit();
    debug_guest_check("interrupt poll");
    clock_pace();
    events_run_due();
    uint64_t now = now_ticks();
    // GCN_RESET_AFTER=<seconds>: the game resets the console (a restart) that far into
    // guest time, for trying a frontend's restart without playing to a reset.
    static const double reset_after = getenv("GCN_RESET_AFTER") ? atof(getenv("GCN_RESET_AFTER")) : 0.0;
    if (reset_after > 0.0 && now >= (uint64_t)(reset_after * TB_FREQ)) guest_reset(0, 0);
    if (g_dec_deadline.load() <= now) { g_dec_pending = true; g_dec_deadline = UINT64_MAX; }
    clock_update_limit();
    bool ext = (g_pi_intsr.load() & g_pi_intmr.load()) != 0;
    if (!ext && !g_dec_pending) return;
    if (!(c->msr & MSR_EE)) { irq_request(); return; }  // poll again at the next back-edge
    if (ext) { LOG(LOG_THREAD, "deliver external irq: intsr=%08X intmr=%08X", g_pi_intsr.load(), g_pi_intmr.load()); irq_deliver(c, 4); return; }
    g_dec_pending = false;
    irq_deliver(c, 8);
}

// Background ticker for the host clock: asks for a poll when a timed event or the
// decrementer is due. The virtual clock needs none; the back-edge count does it.
static void ticker_main() {
    while (!g_quit) {
        uint64_t now = now_ticks();
        if (g_next_event_at.load() <= now || g_dec_deadline.load() <= now) irq_request();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

void threads_start_boot(uint32_t entry) {
    if (!clock_is_virtual()) {
        static std::thread ticker(ticker_main);
        ticker.detach();
    }
    HostThread* h = new_host_thread();
    // Fake a context-free start: run entry directly on a host thread.
    struct Boot { static void* main(void* arg) {
        HostThread* self = (HostThread*)arg;
        t_self = self;
        plat_thread_role(ThreadRole::GuestMain, "guest");
        CPU* c = &self->cpu;
        c->msr = 0x00002032;  // FP, IR, DR, RI (EE off)
        call_indirect(c, self->start_ctx);
        LOG(LOG_THREAD, "boot entry returned");
        return nullptr;
    } };
    h->start_ctx = entry;
    if (!plat_thread_start(Boot::main, h, HOST_STACK))
        fatal("could not start the boot thread");
}

static GuestCheck g_guest_check;
void debug_set_guest_check(GuestCheck fn) { g_guest_check = fn; }

uint32_t gx_frames_submitted();
void debug_dump_threads();

static const uint32_t g_guard = getenv("GCN_GUARD") ? (uint32_t)strtoul(getenv("GCN_GUARD"), nullptr, 16) : 0;
static uint32_t g_guard_last;
static bool g_guard_have;

static void guard_check(const char* where) {
    if (!g_guard) return;
    const uint32_t v = mem_r32(g_guard);
    if (g_guard_have && v == g_guard_last) return;
    if (g_guard_have)
        fprintf(stderr, "[guard] %08X: %08X -> %08X at %s, frame %u, guest time %.4f s\n", g_guard, g_guard_last, v,
                where, (unsigned)gx_frames_submitted(), (double)now_ticks() / TB_FREQ);
    g_guard_last = v;
    g_guard_have = true;
}

void debug_guard_dma(const char* what, uint32_t addr, uint32_t len) {
    if (!g_guard) return;
    // Main memory is mirrored at 0x0, 0x8 and 0xC; the locked cache at 0xE is not RAM.
    const bool ram = (addr >> 28) == 0x0 || (addr >> 28) == 0x8 || (addr >> 28) == 0xC;
    const uint32_t a = addr & 0x01FFFFFFu, g = g_guard & 0x01FFFFFFu;
    if (ram && g + 4 > a && g < a + len)
        fprintf(stderr, "[guard] %08X is inside %s to %08X, %X bytes\n", g_guard, what, addr, len);
    guard_check(what);
}

void debug_guest_check(const char* where) {
    guard_check(where);
#ifdef GCN_GUEST_CHECKS
    if (!g_guest_check) return;
    const char* what = g_guest_check();
    if (!what) return;
    fprintf(stderr, "\nGUEST CHECK FAILED at %s, frame %u, guest time %.4f s:\n  %s\n", where,
            (unsigned)gx_frames_submitted(), (double)now_ticks() / TB_FREQ, what);
    input_log_end();
    debug_dump_threads();
    if (getenv("GCN_GUEST_CHECK_CONTINUE")) { g_guest_check = nullptr; return; }
    plat_exit_now(3);
#endif
}

void threads_request_quit() {
    g_quit = true;
    irq_request();
    for (auto* h : g_threads) { std::lock_guard<std::mutex> lk(h->m); h->cv.notify_all(); }
}

// ---------------------------------------------------------------------------
// Debug: dump guest thread state
// ---------------------------------------------------------------------------
const char* func_name(uint32_t addr);

void debug_dump_fifo();
void debug_print_counts();
void debug_dump_threads() {
    debug_dump_fifo();
    if (const char* e = getenv("GCN_PEEK")) {
        std::string s = e;
        size_t p = 0;
        while (p < s.size()) {
            size_t q = s.find(',', p);
            if (q == std::string::npos) q = s.size();
            uint32_t a = (uint32_t)strtoul(s.substr(p, q - p).c_str(), nullptr, 16);
            fprintf(stderr, "[peek] %08X = %08X\n", a, mem_r32(a));
            p = q + 1;
        }
    }
#ifdef GCN_WATCH
    debug_print_counts();
#endif
    for (auto* h : g_threads) {
        CPU* c = &h->cpu;
        fprintf(stderr, "=== host thread %d (run=%d) lr=%08X msr=%08X ctr=%08X\n", h->id, h->run, c->lr, c->msr, c->ctr);
        for (int i = 0; i < 32; i += 8)
            fprintf(stderr, "  r%-2d %08X %08X %08X %08X %08X %08X %08X %08X\n", i, c->r[i], c->r[i + 1], c->r[i + 2], c->r[i + 3],
                    c->r[i + 4], c->r[i + 5], c->r[i + 6], c->r[i + 7]);
#ifdef GCN_CALL_TRACE
        fprintf(stderr, "  guest call stack (innermost first):\n");
        uint32_t want = getenv("GCN_TRACE_DEPTH") ? (uint32_t)atoi(getenv("GCN_TRACE_DEPTH")) : 40;
        // The depth counts past the end of the array so that deep recursion still unwinds
        // correctly; anything above 256 was never recorded.
        uint32_t have = c->depth < 256 ? c->depth : 256;
        if (c->depth > 256)
            fprintf(stderr, "    (%u frames deeper than the stack records)\n", c->depth - 256);
        if (want > have) want = have;
        for (uint32_t i = 1; i <= want; i++) {
            uint32_t a = c->stack[have - i];
            fprintf(stderr, "    %08X %s\n", a, func_name(a));
        }
#else
        fprintf(stderr, "  (built without GCN_TRACE_CALLS, so there is no guest call stack)\n");
#endif
    }
}

#ifdef GCN_WATCH
// Debug build only: GCN_WATCH_ADDR=<hex guest address> reports every change to that word.
static std::vector<std::pair<uint32_t, uint64_t>> g_count_fns = [] {
    std::vector<std::pair<uint32_t, uint64_t>> v;
    if (const char* e = getenv("GCN_COUNT")) {
        std::string s = e;
        size_t p = 0;
        while (p < s.size()) {
            size_t q = s.find(',', p);
            if (q == std::string::npos) q = s.size();
            v.push_back({(uint32_t)strtoul(s.substr(p, q - p).c_str(), nullptr, 16), 0});
            p = q + 1;
        }
    }
    return v;
}();
void debug_print_counts() {
    for (auto& kv : g_count_fns) fprintf(stderr, "[count] %08X %s: %llu\n", kv.first, func_name(kv.first), (unsigned long long)kv.second);
}
// GCN_STORE_HIST=<from>-<to>: count how often each store in the recompiled code runs
// between those two presented frames, and print the table once the window has closed, one
// `[sthist] <pc> <count> <streak>` line per store site that ran. With the window set to a
// stretch of steady play, a count equal to the number of frames marks a store that runs
// once a frame, which is how a game project's static list of candidate per-frame steps is
// narrowed to the ones that run. The streak is how many of the frames the site ran in
// directly followed another frame it ran in: a per-frame step that is only active for
// part of the window (a countdown) still ran on consecutive frames, where a site that
// fires on events did not.
//
// GCN_STORE_HIST_PCS=<file> (hex addresses, one per line) adds, for every address that one
// of those stores wrote in the window, who else wrote it: `[stwr] <addr> <pc>:<count> ...`.
// That tells a value carried from frame to frame (its only writers are the steps) from a
// temporary recomputed every frame (something else assigns it first).
namespace gx { extern std::atomic<uint32_t> g_frames_submitted; }
namespace {
struct Writers {  // pc -> count
    std::unordered_map<uint32_t, uint32_t> n;
};
struct {
    uint32_t from = 0, to = 0;
    bool printed = false;
    std::vector<uint32_t> counts;
    std::vector<uint32_t> last_frame;  // per site: the last presented frame it ran in, +1
    std::vector<uint32_t> streak;      // per site: frames it ran in right after another
    std::vector<uint8_t> listed;  // per code word: is this pc in GCN_STORE_HIST_PCS
    std::unordered_map<uint32_t, Writers> writers;
} g_sthist = [] {
    decltype(g_sthist) h;
    if (const char* e = getenv("GCN_STORE_HIST")) {
        h.from = (uint32_t)strtoul(e, nullptr, 10);
        const char* dash = strchr(e, '-');
        h.to = dash ? (uint32_t)strtoul(dash + 1, nullptr, 10) : 0xFFFFFFFFu;
    }
    return h;
}();
void store_hist_init() {
    size_t words = (g_recomp_code_end - g_recomp_code_base) / 4 + 1;
    g_sthist.counts.resize(words);
    g_sthist.last_frame.resize(words);
    g_sthist.streak.resize(words);
    if (const char* path = getenv("GCN_STORE_HIST_PCS")) {
        g_sthist.listed.resize(words);
        if (FILE* f = fopen(path, "r")) {
            char line[64];
            while (fgets(line, sizeof(line), f)) {
                uint32_t pc = (uint32_t)strtoul(line, nullptr, 16);
                if (pc >= g_recomp_code_base && pc < g_recomp_code_end) g_sthist.listed[(pc - g_recomp_code_base) / 4] = 1;
            }
            fclose(f);
        }
    }
}
void store_hist_print() {
    fprintf(stderr, "[sthist] frames %u-%u\n", g_sthist.from, g_sthist.to);
    for (size_t i = 0; i < g_sthist.counts.size(); i++)
        if (g_sthist.counts[i]) fprintf(stderr, "[sthist] %08X %u %u\n", (uint32_t)(g_recomp_code_base + i * 4), g_sthist.counts[i], g_sthist.streak[i]);
    for (auto& kv : g_sthist.writers) {
        const Writers& w = kv.second;
        bool listed = false;
        for (auto& pn : w.n) listed |= g_sthist.listed[(pn.first - g_recomp_code_base) / 4] != 0;
        if (!listed) continue;
        fprintf(stderr, "[stwr] %08X", kv.first);
        for (auto& pn : w.n) fprintf(stderr, " %08X:%u", pn.first, pn.second);
        fputc('\n', stderr);
    }
    fprintf(stderr, "[sthist] end\n");
}
void store_hist(uint32_t pc, uint32_t ea) {
    uint32_t f = gx::g_frames_submitted.load(std::memory_order_relaxed);
    if (f < g_sthist.from) return;
    if (f >= g_sthist.to) {
        if (!g_sthist.printed) {
            g_sthist.printed = true;
            if (!g_sthist.counts.empty()) store_hist_print();
        }
        return;
    }
    if (g_sthist.counts.empty()) store_hist_init();
    if (pc < g_recomp_code_base || pc >= g_recomp_code_end) return;
    size_t i = (pc - g_recomp_code_base) / 4;
    g_sthist.counts[i]++;
    if (g_sthist.last_frame[i] != f + 1) {  // the first run in this frame
        if (g_sthist.last_frame[i] == f) g_sthist.streak[i]++;
        g_sthist.last_frame[i] = f + 1;
    }
    if (g_sthist.listed.empty()) return;
    g_sthist.writers[ea].n[pc]++;
}
}  // namespace
extern "C" void debug_watch_store(CPU* c, uint32_t pc, uint32_t ea) {
    if (g_sthist.to) store_hist(pc, ea);
    debug_watch_check(c, pc | 1u);
}
// GCN_READERS=<file> (hex guest addresses or lo-hi ranges, one per line): within the
// GCN_STORE_HIST window, which code reads those addresses. Printed with the histogram as
// `[readers] <addr> <pc>:<count> ...`, the readers' counterpart of [stwr]: a value whose
// unit a change of frame rate alters (a speed derived from a position difference) has to
// be converted wherever it is read, and this is the list of where.
namespace {
struct Readers {
    uint32_t lo, hi;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> by_addr;  // addr -> pc -> count
};
std::vector<Readers> g_readers = [] {
    std::vector<Readers> v;
    const char* path = getenv("GCN_READERS");
    if (!path) return v;
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "[readers] cannot read %s\n", path); return v; }
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char* end;
        uint32_t lo = (uint32_t)strtoul(line, &end, 16), hi = lo + 3;
        if (end == line) continue;
        if (*end == '-') hi = (uint32_t)strtoul(end + 1, nullptr, 16);
        v.push_back({lo, hi, {}});
    }
    fclose(f);
    return v;
}();
void readers_print() {
    for (auto& r : g_readers)
        for (auto& kv : r.by_addr) {
            fprintf(stderr, "[readers] %08X", kv.first);
            for (auto& pn : kv.second) fprintf(stderr, " %08X:%u", pn.first, pn.second);
            fputc('\n', stderr);
        }
}
}  // namespace
extern "C" void debug_watch_load(uint32_t pc, uint32_t ea) {
    if (g_readers.empty()) return;
    uint32_t f = gx::g_frames_submitted.load(std::memory_order_relaxed);
    if (f < g_sthist.from) return;
    if (f >= g_sthist.to) {
        static bool printed = false;
        if (!printed) { printed = true; readers_print(); }
        return;
    }
    for (auto& r : g_readers) {
        if (ea < r.lo || ea > r.hi) continue;
        r.by_addr[ea][pc]++;
        return;
    }
}
extern "C" void debug_watch_check(CPU* c, uint32_t fn) {
    for (auto& kv : g_count_fns) if (kv.first == fn) kv.second++;
    static uint32_t addr = [] { const char* e = getenv("GCN_WATCH_ADDR"); return e ? (uint32_t)strtoul(e, nullptr, 16) : 0u; }();
    static uint32_t last = 0xFFFFFFFF;
    if (!addr) return;
    // Peeked, so that a watch on a page-mapped address reads nothing until the game maps it
    // rather than faulting.
    uint32_t v;
    if (!vm_peek32(addr, &v)) return;
    if (v != last) {
        fprintf(stderr, "[watch] %08X: %08X -> %08X at %s %08X %s (lr %08X)\n", addr, last, v,
                (fn & 1) ? "store" : "entry", fn & ~1u, (fn & 1) ? "" : func_name(fn), c->lr);
        last = v;
    }
}
#endif

// Periodically report which game-code functions (below the SDK at 0x80100000) are active.
uint32_t gx_frames_submitted();
void debug_sampler_start() {
    std::thread([] {
        extern std::atomic<uint32_t> g_vi_retrace_count;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            for (auto* h : g_threads) {
                CPU* c = &h->cpu;
                // Walk the guest's own stack: the PowerPC ABI keeps the caller's frame
                // pointer at [sp] and its return address at [sp+4], so this is a real
                // guest backtrace read straight out of guest memory. It needs no
                // instrumentation at all, which is why it, rather than anything ENTER()
                // keeps, is what --sample reports.
                std::string bt, s;
                uint32_t seen[8]; int ns = 0;
                uint32_t sp = c->r[1];
                for (int d = 0; d < 24 && sp >= 0x80000000u && sp < 0x81800000u; d++) {
                    uint32_t prev = mem_r32(sp);
                    if (prev < 0x80000000u || prev >= 0x81800000u || prev <= sp) break;
                    uint32_t ret = mem_r32(prev + 4);
                    char buf[16]; snprintf(buf, sizeof(buf), " %08X", ret); bt += buf;
                    // Name the distinct ones that fall in the DOL's code.
                    if (ret >= g_recomp_code_base && ret < g_recomp_code_end && ns < 8) {
                        bool dup = false;
                        for (int k = 0; k < ns; k++) dup |= seen[k] == ret;
                        if (!dup) {
                            seen[ns++] = ret;
                            char b2[24]; snprintf(b2, sizeof(b2), " %08X", ret); s += b2;
                        }
                    }
                    sp = prev;
                }
                static uint32_t last_frames;
                uint32_t fr = gx_frames_submitted();
                fprintf(stderr, "[sample] vi=%u frames=%u (+%u) thread %d game fns:%s\n", g_vi_retrace_count.load(), fr, fr - last_frames, h->id, s.c_str());
                last_frames = fr;
                fprintf(stderr, "         guest stack:%s\n", bt.c_str());
            }
        }
    }).detach();
}
