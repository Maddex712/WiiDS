// MMIO dispatch for 0xC8000000-0xCFFFFFFF (EFB + hardware registers) and the PI.
#include "../runtime.h"
#include "../platform.h"
#include <algorithm>
#include <atomic>

std::atomic<uint32_t> g_pi_intsr{0}, g_pi_intmr{0};
static uint32_t g_pi_fifo_base, g_pi_fifo_end, g_pi_fifo_wptr;

void pi_update() {
    if (g_pi_intsr.load() & g_pi_intmr.load()) irq_request();
}

void pi_set_interrupt(uint32_t cause, bool set) {
    // The CP's state is re-asserted at every gather-pipe drain, tens of thousands of times a
    // frame and nearly always unchanged; two atomic read-modify-writes each time were 6% of
    // the guest thread in a heavy scene. A pending cause keeps asking for its own poll
    // (irq_poll), so an unchanged one needs nothing here.
    const uint32_t cur = g_pi_intsr.load(std::memory_order_relaxed);
    if (set ? (cur & cause) == cause : (cur & cause) == 0) return;
    if (set) g_pi_intsr.fetch_or(cause);
    else g_pi_intsr.fetch_and(~cause);
    pi_update();
}

uint32_t pi_fifo_base() { return g_pi_fifo_base; }
uint32_t pi_fifo_end() { return g_pi_fifo_end; }
uint32_t& pi_fifo_wptr() { return g_pi_fifo_wptr; }

uint32_t pi_read32(uint32_t off) {
    switch (off) {
    case 0x00: return g_pi_intsr.load() | 0x10000;  // bit 16: reset switch not pressed
    case 0x04: return g_pi_intmr.load();
    case 0x0C: return g_pi_fifo_base;
    case 0x10: return g_pi_fifo_end;
    case 0x14: return g_pi_fifo_wptr;
    case 0x2C: return 0x246500B1;  // flipper revision
    default:
        LOG(LOG_HW, "PI read %02X", off);
        return 0;
    }
}

void gp_flush_partial();

void pi_write32(uint32_t off, uint32_t v) {
    // Repointing the CPU FIFO: anything still in the write-gather buffer belongs to the
    // old destination, so it goes out first. See gp_flush_partial.
    if (off == 0x0C || off == 0x10 || off == 0x14) {
        // GCN_GATHERLOG=1: every repointing of the pipe, with what was pending in it.
        static bool log = getenv("GCN_GATHERLOG") != nullptr;
        if (log) {
            extern uint32_t gx_frames_submitted();
            extern uint32_t gp_pending();
            fprintf(stderr, "[gather] frame %u: PI reg %02X <- %08X (base %08X end %08X wptr %08X, %u bytes pending)\n",
                    gx_frames_submitted(), off, v, g_pi_fifo_base, g_pi_fifo_end, g_pi_fifo_wptr, gp_pending());
        }
        gp_flush_partial();
    }
    switch (off) {
    case 0x00:  // writes acknowledge (clear) some causes
        g_pi_intsr.fetch_and(~(v & (INT_PI | INT_RSW | INT_DEBUG | INT_HSP)));
        pi_update();
        break;
    case 0x04: g_pi_intmr = v; pi_update(); break;
    case 0x0C: g_pi_fifo_base = v & 0x03FFFFE0; LOG(LOG_GX, "PI fifo base %08X", v); break;
    // Bit 26 is kept: GXRedirectWriteGatherPipe sets the end to 64 MB (0x04000000) so
    // that a stream into a game buffer never wraps, and masking it to 24 MB turned that
    // into an end of zero, which wrapped every line of the stream to base 0 instead of
    // the buffer. A game's CPU-skinned models arrived as whatever the buffer held.
    case 0x10: g_pi_fifo_end = v & 0x07FFFFE0; break;
    case 0x14: g_pi_fifo_wptr = v & 0x07FFFFE0; LOG(LOG_GX, "PI fifo wptr %08X", v); break;
    case 0x24: LOG(LOG_HW, "PI reset register write %08X", v); break;
    default: LOG(LOG_HW, "PI write %02X = %08X", off, v); break;
    }
}

// MI (memory interface): protection registers etc. Mostly ignored.
static uint16_t g_mi_regs[0x80];

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------
static inline bool is_efb(uint32_t a) { return (a & 0xFF000000u) == 0xC8000000u; }

extern "C" uint32_t mmio_read32(uint32_t a) {
    if (is_efb(a)) return efb_peek(a);
    uint32_t off = a & 0xFFFF;
    switch (off >> 12) {
    case 0x0: return ((uint32_t)cp_read16(off) << 16) | cp_read16(off + 2);
    case 0x1: return ((uint32_t)pe_read16(off & 0xFFF) << 16) | pe_read16((off & 0xFFF) + 2);
    case 0x2: return ((uint32_t)vi_read16(off & 0xFFF) << 16) | vi_read16((off & 0xFFF) + 2);
    case 0x3: return pi_read32(off & 0xFFF);
    case 0x4: return ((uint32_t)g_mi_regs[(off & 0xFF) >> 1] << 16) | g_mi_regs[((off & 0xFF) >> 1) + 1];
    case 0x5: return ((uint32_t)dsp_read16(off & 0xFFF) << 16) | dsp_read16((off & 0xFFF) + 2);
    case 0x6:
        if (off < 0x6400) return di_read32(off & 0x3FF);
        if (off < 0x6800) return si_read32(off & 0x3FF);
        if (off < 0x6C00) return exi_read32(off & 0x3FF);
        return ai_read32(off & 0x3FF);
    }
    LOG(LOG_HW, "unhandled MMIO read32 %08X", a);
    return 0;
}

extern "C" uint16_t mmio_read16(uint32_t a) {
    if (is_efb(a)) return (uint16_t)efb_peek(a);
    uint32_t off = a & 0xFFFF;
    switch (off >> 12) {
    case 0x0: return cp_read16(off);
    case 0x1: return pe_read16(off & 0xFFF);
    case 0x2: return vi_read16(off & 0xFFF);
    case 0x3: { uint32_t v = pi_read32(off & 0xFFC); return (off & 2) ? (uint16_t)v : (uint16_t)(v >> 16); }
    case 0x4: return g_mi_regs[(off & 0xFF) >> 1];
    case 0x5: return dsp_read16(off & 0xFFF);
    case 0x6: {
        uint32_t v = mmio_read32(a & ~3u);
        return (off & 2) ? (uint16_t)v : (uint16_t)(v >> 16);
    }
    }
    LOG(LOG_HW, "unhandled MMIO read16 %08X", a);
    return 0;
}

extern "C" uint8_t mmio_read8(uint32_t a) {
    uint32_t v = mmio_read32(a & ~3u);
    return (uint8_t)(v >> (24 - 8 * (a & 3)));
}

extern "C" void mmio_write32(uint32_t a, uint32_t v) {
    if (is_efb(a)) { efb_poke(a, v); return; }
    uint32_t off = a & 0xFFFF;
    switch (off >> 12) {
    case 0x0: cp_write16(off, v >> 16); cp_write16(off + 2, (uint16_t)v); return;
    case 0x1: pe_write16(off & 0xFFF, v >> 16); pe_write16((off & 0xFFF) + 2, (uint16_t)v); return;
    case 0x2: vi_write16(off & 0xFFF, v >> 16); vi_write16((off & 0xFFF) + 2, (uint16_t)v); return;
    case 0x3: pi_write32(off & 0xFFF, v); return;
    case 0x4: g_mi_regs[(off & 0xFF) >> 1] = v >> 16; g_mi_regs[((off & 0xFF) >> 1) + 1] = (uint16_t)v; return;
    case 0x5: dsp_write16(off & 0xFFF, v >> 16); dsp_write16((off & 0xFFF) + 2, (uint16_t)v); return;
    case 0x6:
        if (off < 0x6400) { di_write32(off & 0x3FF, v); return; }
        if (off < 0x6800) { si_write32(off & 0x3FF, v); return; }
        if (off < 0x6C00) { exi_write32(off & 0x3FF, v); return; }
        ai_write32(off & 0x3FF, v);
        return;
    case 0x8: gp_write32(v); return;
    }
    LOG(LOG_HW, "unhandled MMIO write32 %08X = %08X", a, v);
}

extern "C" void mmio_write16(uint32_t a, uint16_t v) {
    if (is_efb(a)) { efb_poke(a, v); return; }
    uint32_t off = a & 0xFFFF;
    switch (off >> 12) {
    case 0x0: cp_write16(off, v); return;
    case 0x1: pe_write16(off & 0xFFF, v); return;
    case 0x2: vi_write16(off & 0xFFF, v); return;
    case 0x4: g_mi_regs[(off & 0xFF) >> 1] = v; return;
    case 0x5: dsp_write16(off & 0xFFF, v); return;
    case 0x8: gp_write16(v); return;
    }
    LOG(LOG_HW, "unhandled MMIO write16 %08X = %04X", a, v);
}

extern "C" void mmio_write8(uint32_t a, uint8_t v) {
    uint32_t off = a & 0xFFFF;
    if ((off >> 12) == 0x8) { gp_write8(v); return; }
    LOG(LOG_HW, "unhandled MMIO write8 %08X = %02X", a, v);
}

// ---------------------------------------------------------------------------
// DMA bounds. See dma_fit() in runtime.h for why this exists.
// ---------------------------------------------------------------------------
uint32_t dma_fit(const char* engine, uint32_t pa, uint32_t len) {
    const uint32_t addr = pa & 0x01FFFFFF;   // the address phys_ptr() will actually use
    const uint32_t fit = addr >= RAM_SIZE ? 0u : std::min(len, RAM_SIZE - addr);
    if (fit == len) return fit;
    static int shown;
    char msg[160];
    snprintf(msg, sizeof(msg),
             "%s DMA runs past main memory: %08X + %X bytes, RAM ends at %X (%u bytes dropped)",
             engine, pa, len, RAM_SIZE, len - fit);
    if (shown == 0) {
        // The first one goes into the platform's crash record as well. If the guest does
        // not survive what this corrupted, the tombstone then names the engine that did
        // it -- which is the whole difficulty with this class of bug.
        plat_record_fatal(msg);
    }
    if (shown++ < 16) fprintf(stderr, "[dma] %s\n", msg);
    return fit;
}
