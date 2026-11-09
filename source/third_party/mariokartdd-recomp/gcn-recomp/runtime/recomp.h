/* Interface between recompiled guest code and the runtime. Must stay C. */
#pragma once
#include "compat.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(GCN_WATCH) || defined(GCN_TRACE_CALLS)
#define GCN_CALL_TRACE 1
#endif

typedef union { double d; uint64_t u; } FPR;

typedef struct CPU {
    uint32_t r[32];
    FPR f[32];         /* ps0 */
    double ps1[32];
    uint8_t cr[8];     /* each field: LT=8 GT=4 EQ=2 SO=1 */
    uint8_t xer_ca, xer_so, xer_ov, xer_bc;
    uint32_t lr, ctr, msr, fpscr;
    /* FPSCR[FPRF], the class of the last arithmetic result, kept lazily: an op that sets it
       records its result here, and FPSCR_READ works out the bits when something reads the
       FPSCR. pending: 0 none, 1 a double result, 2 a single one. */
    double fprf_v;
    uint8_t fprf_pending;
    uint32_t gqr[8];
    uint32_t spr[1024];
    void* host;        /* owning HostThread */
#ifdef GCN_CALL_TRACE
    uint32_t stack[256];  /* guest call stack, innermost at depth-1 */
    uint32_t depth;
#endif
} CPU;

/* Guest call stack. Every recompiled function opens with ENTER(its own address) and
   leaves through RET(), which pops.

   Nothing in the running game reads it: the only consumer is debug_dump_threads(), from
   the fault and interrupt handlers, and whatever is asking "which guest function did
   that?" at the time. So a release build leaves it out and both macros vanish, which also
   takes 1 KB off the CPU snapshot that every context switch copies.

   Not, as it turns out, because it is slow. Benchmarked on a Quest 3 at GCN_TIMESCALE=3,
   OFF/ON/OFF, the steady state was 40.60, 41.34 and 40.84 fps: the instrumented build
   measured *faster*, consistently, so a push and a pop per guest call sit below the noise
   floor and the couple of per cent between builds is code layout. It is out of release
   because it is diagnostic machinery that the running game has no use for, not to buy back
   frames -- do not go looking for them here.

   This replaces a ring of the last 256 functions *entered*. A ring cannot answer which
   function a piece of work came from: between a callee and its caller sit all the
   callee's siblings and any interrupt handler that ran, and two draws a few microseconds
   apart share almost the whole ring. A stack answers it exactly.

   The fields live in CPU, which OSSaveContext/OSLoadContext snapshot and restore beside
   the jmp_buf, so longjmping back into a parked guest thread brings the stack back with
   the C stack it belongs to. */
#ifdef GCN_CALL_TRACE
static inline void wr_push(CPU* c, uint32_t fn) {
    if (c->depth < 256) c->stack[c->depth] = fn;
    c->depth++;  /* counts past the end, so deep recursion still unwinds correctly */
}
static inline void wr_pop(CPU* c) {
    if (c->depth) c->depth--;
}
#endif

#ifdef GCN_WATCH
void debug_watch_check(CPU* c, uint32_t fn);
void debug_watch_store(CPU* c, uint32_t pc, uint32_t ea);
void debug_watch_load(uint32_t pc, uint32_t ea);
#define ENTER(addr) (wr_push(c, (addr)), debug_watch_check(c, (addr)))
#define WATCH_STORE(pc, ea) debug_watch_store(c, (pc), (ea))
#define WATCH_LOAD(pc, ea) debug_watch_load((pc), (ea))
#define EXIT() wr_pop(c)
#elif defined(GCN_TRACE_CALLS)
#define ENTER(addr) wr_push(c, (addr))
#define WATCH_STORE(pc, ea) ((void)0)
#define WATCH_LOAD(pc, ea) ((void)0)
#define EXIT() wr_pop(c)
#else
#define ENTER(addr) ((void)0)
#define WATCH_STORE(pc, ea) ((void)0)
#define WATCH_LOAD(pc, ea) ((void)0)
#define EXIT() ((void)0)
#endif

/* Guest heap tracing (runtime/heap_trace.cpp). A game's patches.txt splices these into
   OSAllocFromHeap's entry and both of its returns and into OSFreeToHeap's entry; the entry
   hook also takes the address of the SDK's HeapArray variable. */
void heap_trace_alloc(CPU* c, uint32_t heap_array_var);
void heap_trace_alloc_result(CPU* c, int ok);
void heap_trace_free(CPU* c);

/* The step scale (runtime/step.cpp): what a per-frame step listed in the game's steps.txt
   is scaled by, raised to the power p the table gives the site. */
double gcn_step_scale(uint32_t pc, double p);
uint32_t gcn_step_int(uint32_t pc, double p);
double gcn_step_pow(uint32_t pc, double p, double k);
double gcn_step_damp(uint32_t pc, double p, double x, double k, double b);

/* How a recompiled function returns. */
#define RET() do { EXIT(); return; } while (0)

#define MSR_EE 0x8000u

/* ---- memory ---- */
extern uint8_t* g_mem;             /* 1GB host reservation; guest addr & 0x3FFFFFFF */
/* The BATs cover 0x80000000 and 0xC0000000; 0x02000000-0x7FFFFFFF is reachable only
   through a page table the game set up, and goes through runtime/mmu.cpp. Below that is
   physical RAM, which the runtime's own DMA and the page table walk address directly.
   HOST_W is for a store: the page's Changed bit is what differs. */
uint32_t vm_translate(uint32_t ea, int write);
#define GCN_VM_LO 0x02000000u
static inline uint32_t guest_off(uint32_t a) {
    if (GCN_UNLIKELY(a - GCN_VM_LO < 0x80000000u - GCN_VM_LO)) return vm_translate(a, 0);
    return a & 0x3FFFFFFFu;
}
static inline uint32_t guest_off_w(uint32_t a) {
    if (GCN_UNLIKELY(a - GCN_VM_LO < 0x80000000u - GCN_VM_LO)) return vm_translate(a, 1);
    return a & 0x3FFFFFFFu;
}
/* Where guest memory is. Every guest load and store adds this to an offset, and as a
   variable it was loaded again for each of them: the generated code is compiled without
   strict aliasing, so any store might, for all the compiler knows, have changed g_mem.
   A platform that can map the reservation at a fixed address defines GCN_MEM_BASE, and
   the generated code then adds a constant instead (mem_init puts it there or stops). */
#ifdef GCN_MEM_BASE
#define GCN_MEM ((uint8_t*)(uintptr_t)GCN_MEM_BASE)
#else
#define GCN_MEM g_mem
#endif
#define HOST(a) (GCN_MEM + guest_off((uint32_t)(a)))
#define HOST_W(a) (GCN_MEM + guest_off_w((uint32_t)(a)))
#define IS_MMIO(a) ((uint32_t)((a) - 0xC8000000u) < 0x08000000u)

uint32_t mmio_read32(uint32_t a);
uint16_t mmio_read16(uint32_t a);
uint8_t mmio_read8(uint32_t a);
void mmio_write32(uint32_t a, uint32_t v);
void mmio_write16(uint32_t a, uint16_t v);
void mmio_write8(uint32_t a, uint8_t v);

#define LIKELY(x) GCN_LIKELY(x)
#define UNLIKELY(x) GCN_UNLIKELY(x)

static inline uint32_t LD32(uint32_t a) {
    if (UNLIKELY(IS_MMIO(a))) return mmio_read32(a);
    uint32_t v; memcpy(&v, HOST(a), 4); return GCN_BSWAP32(v);
}
static inline uint32_t LD16(uint32_t a) {
    if (UNLIKELY(IS_MMIO(a))) return mmio_read16(a);
    uint16_t v; memcpy(&v, HOST(a), 2); return GCN_BSWAP16(v);
}
static inline uint32_t LDS16(uint32_t a) { return (uint32_t)(int32_t)(int16_t)LD16(a); }
static inline uint32_t LD8(uint32_t a) {
    if (UNLIKELY(IS_MMIO(a))) return mmio_read8(a);
    return *HOST(a);
}
static inline uint32_t LD32BR(uint32_t a) { return GCN_BSWAP32(LD32(a)); }
static inline uint32_t LD16BR(uint32_t a) { return GCN_BSWAP16((uint16_t)LD16(a)); }
static inline uint64_t LD64(uint32_t a) {
    if (UNLIKELY(IS_MMIO(a))) return ((uint64_t)mmio_read32(a) << 32) | mmio_read32(a + 4);
    uint64_t v; memcpy(&v, HOST(a), 8); return GCN_BSWAP64(v);
}
/* The write-gather pipe. A game's CPU-skinning loops can store every vertex they produce
   through it, and taking each of those stores through mmio_write32's register decode was a
   measurable part of a frame.
   A 32-bit store to the pipe's own address appends to the line buffer here, and a full
   line goes to gp_flush_line(), exactly as gp_write32 does it; mirrors of the address and
   call-tracing builds (which probe each word) keep the general path. */
extern uint8_t g_gp_buf[64];
extern uint32_t g_gp_len;
void gp_flush_line(void);
static inline int gp_store32(uint32_t a, uint32_t v) {
#ifndef GCN_CALL_TRACE
    if ((a & 0xFFFFF000u) == 0xCC008000u) {
        v = GCN_BSWAP32(v);
        memcpy(g_gp_buf + g_gp_len, &v, 4);
        g_gp_len += 4;
        if (g_gp_len >= 32) gp_flush_line();
        return 1;
    }
#else
    (void)a; (void)v;
#endif
    return 0;
}
/* The same for the narrower stores a game makes into the pipe: a draw's opcode and count,
   and its vertices' indices, which go in as bytes and halfwords. */
static inline int gp_store16(uint32_t a, uint32_t v) {
    if ((a & 0xFFFFF000u) == 0xCC008000u) {
        g_gp_buf[g_gp_len] = (uint8_t)(v >> 8);
        g_gp_buf[g_gp_len + 1] = (uint8_t)v;
        g_gp_len += 2;
        if (g_gp_len >= 32) gp_flush_line();
        return 1;
    }
    return 0;
}
static inline int gp_store8(uint32_t a, uint32_t v) {
    if ((a & 0xFFFFF000u) == 0xCC008000u) {
        g_gp_buf[g_gp_len++] = (uint8_t)v;
        if (g_gp_len >= 32) gp_flush_line();
        return 1;
    }
    return 0;
}

static inline void ST32(uint32_t a, uint32_t v) {
    if (UNLIKELY(IS_MMIO(a))) { if (gp_store32(a, v)) return; mmio_write32(a, v); return; }
    v = GCN_BSWAP32(v); memcpy(HOST_W(a), &v, 4);
}
static inline void ST16(uint32_t a, uint32_t v) {
    if (UNLIKELY(IS_MMIO(a))) { if (gp_store16(a, v)) return; mmio_write16(a, (uint16_t)v); return; }
    uint16_t h = GCN_BSWAP16((uint16_t)v); memcpy(HOST_W(a), &h, 2);
}
static inline void ST8(uint32_t a, uint32_t v) {
    if (UNLIKELY(IS_MMIO(a))) { if (gp_store8(a, v)) return; mmio_write8(a, (uint8_t)v); return; }
    *HOST_W(a) = (uint8_t)v;
}
static inline void ST32BR(uint32_t a, uint32_t v) { ST32(a, GCN_BSWAP32(v)); }
static inline void ST16BR(uint32_t a, uint32_t v) { ST16(a, GCN_BSWAP16((uint16_t)v)); }
static inline void ST64(uint32_t a, uint64_t v) {
    if (UNLIKELY(IS_MMIO(a))) { mmio_write32(a, (uint32_t)(v >> 32)); mmio_write32(a + 4, (uint32_t)v); return; }
    v = GCN_BSWAP64(v); memcpy(HOST_W(a), &v, 8);
}
static inline double LDF32(uint32_t a) {
    uint32_t v = LD32(a); float f; memcpy(&f, &v, 4); return (double)f;
}
static inline void STF32(uint32_t a, double d) {
    float f = (float)d; uint32_t v; memcpy(&v, &f, 4); ST32(a, v);
}

/* ---- condition register / xer ---- */
#define CRB(n) ((c->cr[(n) >> 2] >> (3 - ((n) & 3))) & 1)
#define SET_CRB(n, v) do { int _s = 3 - ((n) & 3); \
    c->cr[(n) >> 2] = (uint8_t)((c->cr[(n) >> 2] & ~(1 << _s)) | ((!!(v)) << _s)); } while (0)
#define CMPS(a, b) ((uint8_t)(((a) < (b) ? 8 : (a) > (b) ? 4 : 2) | c->xer_so))
#define CMPU(a, b) ((uint8_t)(((uint32_t)(a) < (uint32_t)(b) ? 8 : (uint32_t)(a) > (uint32_t)(b) ? 4 : 2) | c->xer_so))
#define SET_CR0(v) (c->cr[0] = CMPS((int32_t)(v), 0))
#define SET_OV(x) do { c->xer_ov = (uint8_t)(x); c->xer_so |= c->xer_ov; } while (0)
#define ROTL(x, n) (((uint32_t)(x) << (n)) | ((uint32_t)(x) >> ((32 - (n)) & 31)))

static inline uint32_t cpu_get_cr(const CPU* c) {
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint32_t)c->cr[i] << (28 - 4 * i);
    return v;
}
static inline void cpu_set_cr(CPU* c, uint32_t v) {
    for (int i = 0; i < 8; i++) c->cr[i] = (v >> (28 - 4 * i)) & 0xF;
}
#define GET_CR() cpu_get_cr(c)
#define SET_CR(v) cpu_set_cr(c, (v))
#define GET_XER() (((uint32_t)c->xer_so << 31) | ((uint32_t)c->xer_ov << 30) | ((uint32_t)c->xer_ca << 29) | c->xer_bc)
#define SET_XER(v) do { uint32_t _v = (v); c->xer_so = _v >> 31; c->xer_ov = (_v >> 30) & 1; \
    c->xer_ca = (_v >> 29) & 1; c->xer_bc = _v & 0x7F; } while (0)

static inline uint8_t fcmp(double a, double b) {
    if (a < b) return 8;
    if (a > b) return 4;
    if (a == b) return 2;
    return 1;
}
#define FCMP(a, b) fcmp((a), (b))

static inline int32_t FCTIWZ(double d) {
    if (d != d) return INT32_MIN;
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return (int32_t)d;
}
static inline int32_t FCTIW(double d) {
    if (d != d) return INT32_MIN;
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return (int32_t)nearbyint(d);
}

/* FPSCR[FPRF] (bits 15-19: C FL FG FE FU) for a result, as the 750 sets it: a code that
   tells NaN, infinities, normals, denormals and zeros of each sign apart. A single-
   precision result is denormal by the single format's range. Code that classifies a value
   by rounding it and reading the FPSCR back (frsp; mcrfs) depends on it. */
static inline uint32_t FPRF_OF(double v, int single) {
    if (v != v) return 0x11;
    const int neg = signbit(v) != 0;
    if (isinf(v)) return neg ? 0x09 : 0x05;
    if (v == 0.0) return neg ? 0x12 : 0x02;
    const double a = fabs(v);
    const int denorm = single ? a < 1.17549435e-38 : a < 2.2250738585072014e-308;
    if (denorm) return neg ? 0x18 : 0x14;
    return neg ? 0x08 : 0x04;
}
static inline uint32_t FPSCR_READ(CPU* c) {
    if (c->fprf_pending) {
        c->fpscr = (c->fpscr & ~0x1F000u) | (FPRF_OF(c->fprf_v, c->fprf_pending == 2) << 12);
        c->fprf_pending = 0;
    }
    return c->fpscr;
}

/* ---- runtime services called from generated code ---- */
typedef void (*RecompFn)(CPU*);
typedef struct { uint32_t addr; RecompFn fn; const char* name; } RecompFunc;
extern const RecompFunc g_recomp_funcs[];
extern const uint32_t g_recomp_func_count;
/* The DOL's code sections: every function the table can hold lies in [base, end). */
extern const uint32_t g_recomp_code_base, g_recomp_code_end;
/* Code the game loads from its data files at run time, recompiled at canonical addresses
   and bound to where the game put it on first call; see runtime/modules.cpp. */
typedef struct { uint32_t canon, size, entry, code_end; const uint8_t* bytes; const char* name; } RecompModule;
extern const RecompModule g_recomp_modules[];
extern const uint32_t g_recomp_module_count;
uint32_t modules_canon(uint32_t a);
/* Relocatable modules (OSLink), recompiled at canonical addresses and run wherever the
   game links them; see runtime/rels.cpp. A relocated immediate is computed from the base
   OSLink gave its target section, as the linker would have written it. */
#define REL_MAX_SECTIONS 32
#define REL_MAX_MODULES 256
typedef struct { uint32_t index, canon, size, exec; } RecompRelSection;
typedef struct { uint32_t id; const char* name; uint32_t count; RecompRelSection sections[REL_MAX_SECTIONS]; } RecompRel;
extern const RecompRel g_recomp_rels[];
extern const uint32_t g_recomp_rel_count;
extern uint32_t g_rel_base[];  /* [module * REL_MAX_SECTIONS + section]: where it is now;
                                  REL_MAX_MODULES modules */
#define REL_V(m, s, a) (g_rel_base[(m) * REL_MAX_SECTIONS + (s)] + (uint32_t)(a))
#define REL_LO(m, s, a) ((uint16_t)REL_V(m, s, a))
#define REL_HI(m, s, a) ((uint16_t)(REL_V(m, s, a) >> 16))
#define REL_HA(m, s, a) ((uint16_t)((REL_V(m, s, a) + 0x8000u) >> 16))
uint32_t rels_canon(uint32_t a);
/* Where a module's section is now: the address OSLink gave it, 0 while the module (by its
   REL id) is not linked. For a game's own code that reads a module's data. */
uint32_t rels_section_address(uint32_t module_id, uint32_t section);
void rels_linking(uint32_t module_info);
void rels_linked(uint32_t module_info);
void rels_unlinked(uint32_t module_info);

void call_indirect(CPU* c, uint32_t addr);
void unimpl(CPU* c, uint32_t pc, uint32_t inst);
void hle_sc(CPU* c, uint32_t pc);
void hle_trap(CPU* c, uint32_t pc, uint32_t inst);
void hle_rfi(CPU* c, uint32_t pc);
void hle_mtmsr(CPU* c, uint32_t v);
uint32_t hle_mfspr(CPU* c, uint32_t spr);
void hle_mtspr(CPU* c, uint32_t spr, uint32_t v);
void hle_set_fpscr(CPU* c, uint32_t v);
void hle_dcbz(CPU* c, uint32_t ea);
void hle_dcbz_l(CPU* c, uint32_t ea);
void hle_dcbi(CPU* c, uint32_t ea);
/* Segment registers and the TLB, for a game that maps memory through the Gekko's page
   tables; see runtime/mmu.cpp. */
void hle_mtsr(CPU* c, uint32_t sr, uint32_t v);
uint32_t hle_mfsr(CPU* c, uint32_t sr);
void hle_tlbie(CPU* c, uint32_t ea);
void hle_lswi(CPU* c, uint32_t ea, int rd, int n);
void hle_stswi(CPU* c, uint32_t ea, int rs, int n);
/* Paired-single quantised loads and stores. The GQR picks a format; the plain float one
   (types 0-3) needs no scaling and is by far the most common, so it is handled here,
   inline in the generated code, and only the integer formats call out. They were a tenth
   of the guest thread's time as calls, mostly spent computing a scale factor the float
   format never uses. */
void psq_load_quantised(CPU* c, uint32_t ea, int frd, int w, int gqr);
void psq_store_quantised(CPU* c, uint32_t ea, int frs, int w, int gqr);
static inline void psq_load(CPU* c, uint32_t ea, int frd, int w, int gqr) {
    if (LIKELY(((c->gqr[gqr] >> 16) & 7) < 4)) {
        c->f[frd].d = LDF32(ea);
        c->ps1[frd] = w ? 1.0 : LDF32(ea + 4);
        return;
    }
    psq_load_quantised(c, ea, frd, w, gqr);
}
static inline void psq_store(CPU* c, uint32_t ea, int frs, int w, int gqr) {
    if (LIKELY((c->gqr[gqr] & 7) < 4)) {
        STF32(ea, c->f[frs].d);
        if (!w) STF32(ea + 4, c->ps1[frs]);
        return;
    }
    psq_store_quantised(c, ea, frs, w, gqr);
}
jmp_buf* hle_context_jmpbuf(CPU* c);

/* The guest clock. Every backward branch in recompiled code passes through IRQ_CHECK,
   which counts it; with the virtual clock (the default) guest time *is* that count, so a
   run that executes the same instructions reads the same time base, and its timed events
   -- retrace, DVD completion, decrementer -- land on the same instructions. g_vlimit is the
   count at which the next event is due (or 0 when the runtime wants attention), so the
   hot path is one increment and one compare. See clock_* in cpu.cpp. */
extern volatile uint64_t g_vcount;
extern volatile uint64_t g_vlimit;
void irq_poll(CPU* c);
#define IRQ_CHECK() do { if (UNLIKELY(++g_vcount >= g_vlimit)) irq_poll(c); } while (0)
/* The OS idle spin (tables/idle.txt): nothing will change until the next event, so the
   runtime may skip guest time forward to it, and sleep the host meanwhile. */
void idle_hint(CPU* c);
#define IDLE_CHECK() idle_hint(c)

#ifdef __cplusplus
}
#endif
