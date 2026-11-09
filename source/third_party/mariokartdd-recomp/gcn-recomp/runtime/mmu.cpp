// The Gekko's page tables.
//
// A GameCube game reaches memory through the BATs the IPL sets up -- 0x80000000 and
// 0xC0000000 onto the 24 MB of RAM -- and that is what HOST() in recomp.h does: mask the
// guest address onto one host reservation. Some games also program the MMU proper: a page
// table (SDR1), segment registers, and a window below 0x80000000, which no BAT covers,
// whose pages they map onto physical pages themselves, page in from the DVD or ARAM on a
// DSI, and evict again. An access from 0x02000000 to 0x7FFFFFFF takes the slow path here
// (below that is physical RAM, which the runtime addresses directly):
//
//   * A software TLB, one entry per guest page of the window, direct-mapped and tagged by
//     page number. A hit is a few instructions. A miss reads the game's page table with the
//     750's hashed lookup. The PTE's R bit is set on a hit, C on a write; a page whose PTE
//     allows writes but whose C bit is clear is entered read-only until the first write
//     misses on it, so a game that reads C to decide whether an evicted page needs writing
//     back sees what the hardware would have set.
//   * No PTE, or one whose protection the access violates: the hardware would take a DSI,
//     and so does the guest, right here on the guest thread. DAR, DSISR, SRR0 and SRR1 are
//     set as the hardware sets them and the vector at 0x300 runs. That vector is code the
//     game assembled at boot, which the recompiler never saw, so it is interpreted
//     instruction by instruction until it branches into a function the recompiler did see,
//     which runs natively; when the handler's rfi comes back, the lookup is made again.
//     SRR0 is the link register rather than a pc: the generated code has no pc to resume
//     at, and hle_rfi treats "rfi to the link register" as a return, which is exactly the
//     end of the exception.
//   * tlbie drops the TLB entry, so a page the game remaps or evicts is looked up afresh.
//
// Aliasing the host pages instead, so that the fast path served the window too, was the
// first design, and it cannot work: the host's page is 16 KB on Apple silicon and the
// guest's is 4 KB, so one guest page cannot be mapped on its own. The check on every
// access is a compare and a never-taken branch for an address in RAM.
#include "mmu.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "recomp.h"
#include "runtime.h"

static uint32_t g_sdr1;
static uint32_t g_sr[16];

enum { SPR_DSISR = 18, SPR_DAR = 19, SPR_SRR0 = 26, SPR_SRR1 = 27 };

// The TLB: page number in the high word; physical page, valid (1) and writable (2) in the
// low word. One 64-bit word per entry so that a reader on another thread sees one entry
// or the other, never half of each.
static const uint32_t TLB_ENTRIES = 4096;
static std::atomic<uint64_t> g_tlb[TLB_ENTRIES];

static void tlb_flush() {
    for (auto& e : g_tlb) e.store(0, std::memory_order_relaxed);
}

void mmu_set_sdr1(uint32_t v) { g_sdr1 = v; tlb_flush(); }
extern "C" void hle_mtsr(CPU*, uint32_t sr, uint32_t v) { g_sr[sr & 15] = v; tlb_flush(); }
extern "C" uint32_t hle_mfsr(CPU*, uint32_t sr) { return g_sr[sr & 15]; }
extern "C" void hle_tlbie(CPU*, uint32_t ea) {
    g_tlb[(ea >> 12) & (TLB_ENTRIES - 1)].store(0, std::memory_order_relaxed);
}

// The hashed page table lookup, as the 750 does it: primary hash, then secondary.
struct Pte { uint32_t addr, w0, w1; };

static bool pte_find(uint32_t ea, Pte* out) {
    uint32_t sr = g_sr[ea >> 28];
    if (sr & 0x80000000u) return false;  // T: a direct-store segment, which nothing uses
    uint32_t vsid = sr & 0xFFFFFF, api = (ea >> 22) & 0x3F, pidx = (ea >> 12) & 0xFFFF;
    uint32_t hash = (vsid & 0x7FFFF) ^ pidx;
    uint32_t htaborg = g_sdr1 & 0xFFFF0000u, htabmask = g_sdr1 & 0x1FF;
    for (uint32_t h = 0; h < 2; h++) {
        uint32_t hh = h ? ~hash : hash;
        uint32_t pteg = htaborg | (((hh >> 10) & htabmask) << 16) | ((hh & 0x3FF) << 6);
        if (pteg + 64 > RAM_SIZE) return false;
        for (uint32_t i = 0; i < 8; i++) {
            uint32_t a = pteg + i * 8;
            uint32_t w0 = mem_r32(a);
            if ((w0 & 0x80000000u) && ((w0 >> 7) & 0xFFFFFF) == vsid && ((w0 >> 6) & 1) == h && (w0 & 0x3F) == api) {
                out->addr = a;
                out->w0 = w0;
                out->w1 = mem_r32(a + 4);
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Delivering a DSI: the exception vector, interpreted
// ---------------------------------------------------------------------------
static inline int32_t sx16(uint32_t i) { return (int16_t)i; }
static inline int32_t sx26(uint32_t i) { return ((int32_t)(i << 6)) >> 6; }
static inline int32_t sx16b(uint32_t i) { return (int16_t)(i & 0xFFFC); }

static bool bc_taken(CPU* c, uint32_t i) {
    uint32_t bo = (i >> 21) & 31, bi = (i >> 16) & 31;
    bool ctr_ok = true;
    if (!(bo & 4)) { c->ctr--; ctr_ok = ((bo & 2) != 0) == (c->ctr == 0); }
    bool cond_ok = (bo & 16) || ((((c->cr[bi >> 2] >> (3 - (bi & 3))) & 1) != 0) == ((bo & 8) != 0));
    return ctr_ok && cond_ok;
}

static void vector_run(CPU* c, uint32_t pc) {
    uint32_t return_pc = 0;  // where a bl/blrl into native code comes back to; 0 for a plain branch
    for (int steps = 0; steps < 10000; steps++) {
        if (recomp_is_function(pc | 0x80000000u)) {
            call_indirect(c, pc | 0x80000000u);
            if (!return_pc) return;
            pc = return_pc;
            return_pc = 0;
            continue;
        }
        if ((pc & 0x3FFFFFFFu) >= 0x4000) fatal("exception vector left low memory at %08X", pc);
        uint32_t i = mem_r32(pc), op = i >> 26;
        uint32_t d = (i >> 21) & 31, a = (i >> 16) & 31, b = (i >> 11) & 31;
        uint32_t next = pc + 4;
        switch (op) {
        case 10: c->cr[(i >> 23) & 7] = CMPU(c->r[a], i & 0xFFFF); break;
        case 11: c->cr[(i >> 23) & 7] = CMPS((int32_t)c->r[a], sx16(i)); break;
        case 14: c->r[d] = (a ? c->r[a] : 0) + (uint32_t)sx16(i); break;
        case 15: c->r[d] = (a ? c->r[a] : 0) + (i << 16); break;
        case 16:  // bc
            if (bc_taken(c, i)) {
                uint32_t t = (i & 2) ? (uint32_t)sx16b(i) : pc + (uint32_t)sx16b(i);
                if (i & 1) { c->lr = next; return_pc = next; }
                next = t;
            }
            break;
        case 18: {  // b
            uint32_t t = (i & 2) ? (uint32_t)(sx26(i) & ~3) : pc + (uint32_t)(sx26(i) & ~3);
            if (i & 1) { c->lr = next; return_pc = next; }
            next = t;
            break;
        }
        case 19: {
            uint32_t xo = (i >> 1) & 0x3FF;
            if (xo == 16) {  // bclr
                if (bc_taken(c, i)) { uint32_t t = c->lr & ~3u; if (i & 1) { c->lr = next; return_pc = next; } next = t; }
            } else if (xo == 528) {  // bcctr
                if (bc_taken(c, i)) { uint32_t t = c->ctr & ~3u; if (i & 1) { c->lr = next; return_pc = next; } next = t; }
            } else if (xo == 50) {  // rfi: the exception is over
                c->msr = c->spr[SPR_SRR1];
                return;
            } else if (xo == 150) {  // isync
            } else {
                fatal("exception vector at %08X: unsupported op 19 instruction %08X", pc, i);
            }
            break;
        }
        case 21: {  // rlwinm
            uint32_t sh = b, mb = (i >> 6) & 31, me = (i >> 1) & 31;
            uint32_t r = ROTL(c->r[d], sh);
            uint32_t mask = (mb <= me) ? ((0xFFFFFFFFu >> mb) & (0xFFFFFFFFu << (31 - me)))
                                       : ((0xFFFFFFFFu >> mb) | (0xFFFFFFFFu << (31 - me)));
            c->r[a] = r & mask;
            if (i & 1) c->cr[0] = CMPS((int32_t)c->r[a], 0);
            break;
        }
        case 24: c->r[a] = c->r[d] | (i & 0xFFFF); break;
        case 25: c->r[a] = c->r[d] | (i << 16); break;
        case 28: c->r[a] = c->r[d] & (i & 0xFFFF); c->cr[0] = CMPS((int32_t)c->r[a], 0); break;
        case 29: c->r[a] = c->r[d] & (i << 16); c->cr[0] = CMPS((int32_t)c->r[a], 0); break;
        case 31: {
            uint32_t xo = (i >> 1) & 0x3FF;
            uint32_t spr = ((i >> 16) & 31) | (((i >> 11) & 31) << 5);
            switch (xo) {
            case 0: c->cr[(i >> 23) & 7] = CMPS((int32_t)c->r[a], (int32_t)c->r[b]); break;
            case 32: c->cr[(i >> 23) & 7] = CMPU(c->r[a], c->r[b]); break;
            case 19: c->r[d] = GET_CR(); break;
            case 144: {  // mtcrf
                uint32_t fxm = (i >> 12) & 0xFF, v = c->r[d];
                for (int f = 0; f < 8; f++)
                    if (fxm & (0x80 >> f)) c->cr[f] = (v >> (28 - 4 * f)) & 15;
                break;
            }
            case 83: c->r[d] = c->msr; break;
            case 146: c->msr = c->r[d]; break;
            case 339: c->r[d] = hle_mfspr(c, spr); break;
            case 467: hle_mtspr(c, spr, c->r[d]); break;
            case 28: c->r[a] = c->r[d] & c->r[b]; if (i & 1) c->cr[0] = CMPS((int32_t)c->r[a], 0); break;
            case 444: c->r[a] = c->r[d] | c->r[b]; if (i & 1) c->cr[0] = CMPS((int32_t)c->r[a], 0); break;
            case 316: c->r[a] = c->r[d] ^ c->r[b]; if (i & 1) c->cr[0] = CMPS((int32_t)c->r[a], 0); break;
            case 23: c->r[d] = LD32((a ? c->r[a] : 0) + c->r[b]); break;
            case 151: ST32((a ? c->r[a] : 0) + c->r[b], c->r[d]); break;
            case 54: case 86: case 246: case 278: case 598: case 854: case 982: case 306: case 566: break;  // cache and sync
            default: fatal("exception vector at %08X: unsupported op 31 instruction %08X", pc, i);
            }
            break;
        }
        case 32: c->r[d] = LD32((a ? c->r[a] : 0) + (uint32_t)sx16(i)); break;
        case 36: ST32((a ? c->r[a] : 0) + (uint32_t)sx16(i), c->r[d]); break;
        default: fatal("exception vector at %08X: unsupported instruction %08X", pc, i);
        }
        pc = next;
    }
    fatal("exception vector did not finish");
}

// MSR bits an exception clears: EE PR FP FE0 SE BE FE1 IR DR.
static const uint32_t MSR_EXC_CLEAR = 0x8000 | 0x4000 | 0x2000 | 0x800 | 0x400 | 0x200 | 0x100 | 0x20 | 0x10;

static void deliver_dsi(uint32_t ea, uint32_t dsisr) {
    CPU* c = cpu_current();
    if (!c) fatal("MMU: no page for %08X (DSISR %08X) on a runtime thread, which cannot take the game's fault", ea, dsisr);
    LOG(LOG_OS, "DSI at %08X dsisr=%08X lr=%08X", ea, dsisr, c->lr);
    c->spr[SPR_DAR] = ea;
    c->spr[SPR_DSISR] = dsisr;
    c->spr[SPR_SRR0] = c->lr;
    c->spr[SPR_SRR1] = c->msr;
    c->msr &= ~MSR_EXC_CLEAR;
    vector_run(c, 0x80000300);
}

extern "C" uint32_t vm_translate(uint32_t ea, int write) {
    uint32_t pn = ea >> 12;
    std::atomic<uint64_t>& slot = g_tlb[pn & (TLB_ENTRIES - 1)];
    for (int tries = 0; tries < 16; tries++) {
        uint64_t e = slot.load(std::memory_order_relaxed);
        if ((uint32_t)(e >> 32) == pn && (e & 1) && (!write || (e & 2))) return ((uint32_t)e & 0xFFFFF000u) | (ea & 0xFFF);
        if (!g_sdr1) fatal("MMU: access to %08X with no page table", ea);
        Pte p;
        if (!pte_find(ea, &p)) { deliver_dsi(ea, 0x40000000u | (write ? 0x02000000u : 0)); continue; }
        uint32_t pp = p.w1 & 3;
        bool key = (g_sr[ea >> 28] >> 30) & 1;  // Ks: the supervisor key, since the guest runs privileged
        bool rd_ok = key ? pp != 0 : true;
        bool wr_ok = key ? pp == 2 : pp != 3;
        if ((write && !wr_ok) || (!write && !rd_ok)) { deliver_dsi(ea, 0x08000000u | (write ? 0x02000000u : 0)); continue; }
        uint32_t phys = p.w1 & 0xFFFFF000u;
        if (phys + 0x1000 > RAM_SIZE) fatal("MMU: PTE for %08X names physical page %08X outside RAM", ea, phys);
        uint32_t w1 = p.w1 | 0x100;  // R
        if (write) w1 |= 0x80;       // C
        if (w1 != p.w1) mem_w32(p.addr + 4, w1);
        uint64_t entry = ((uint64_t)pn << 32) | phys | 1 | ((wr_ok && (w1 & 0x80)) ? 2 : 0);
        slot.store(entry, std::memory_order_relaxed);
        return phys | (ea & 0xFFF);
    }
    fatal("MMU: the game's handler did not map %08X", ea);
}

bool vm_peek32(uint32_t ea, uint32_t* out) {
    if (ea & 3) return false;
    if (ea >= 0x80000000u || ea < GCN_VM_LO) {
        if ((ea & 0x3FFFFFFFu) + 4 > RAM_SIZE) return false;
        *out = mem_r32(ea);
        return true;
    }
    uint32_t phys;
    const uint64_t e = g_tlb[(ea >> 12) & (TLB_ENTRIES - 1)].load(std::memory_order_relaxed);
    if ((uint32_t)(e >> 32) == (ea >> 12) && (e & 1)) {
        phys = ((uint32_t)e & 0xFFFFF000u) | (ea & 0xFFF);
    } else {
        Pte p;
        if (!g_sdr1 || !pte_find(ea, &p)) return false;
        phys = (p.w1 & 0xFFFFF000u) | (ea & 0xFFF);
    }
    if (phys + 4 > RAM_SIZE) return false;
    *out = mem_r32(0x80000000u | phys);
    return true;
}
