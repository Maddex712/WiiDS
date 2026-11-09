// Audio Interface: control register, streaming sample counter.
#include "../runtime.h"

#include <atomic>
#include <mutex>
static std::atomic<uint32_t> g_aicr, g_aivr, g_aiit;
static std::mutex g_cnt_mutex;
static uint32_t g_scnt_base;        // counter value at g_scnt_tick
static uint64_t g_scnt_tick;
static uint64_t g_irq_gen;          // invalidates stale AIIT events

enum { AICR_PSTAT = 1, AICR_AISFR = 2, AICR_AIINTMSK = 4, AICR_AIINT = 8, AICR_AIINTVLD = 16,
       AICR_SCRESET = 32, AICR_AIDFR = 64 };

uint32_t ai_dma_sample_rate() { return (g_aicr.load() & AICR_AIDFR) ? 32000 : 48000; }  // AIDFR: 1 = 32 kHz
static uint32_t stream_rate() { return (g_aicr.load() & AICR_AISFR) ? 48000 : 32000; }

// The streaming sample counter advances in real time while PSTAT is set.
static uint32_t sample_count_locked() {
    if (!(g_aicr.load() & AICR_PSTAT)) return g_scnt_base;
    return g_scnt_base + (uint32_t)((now_ticks() - g_scnt_tick) * stream_rate() / TB_FREQ);
}
static uint32_t sample_count() { std::lock_guard<std::mutex> lk(g_cnt_mutex); return sample_count_locked(); }
static void rebase_locked() { g_scnt_base = sample_count_locked(); g_scnt_tick = now_ticks(); }

uint32_t ai_volume_left() { return g_aivr.load() & 0xFF; }
uint32_t ai_volume_right() { return (g_aivr.load() >> 8) & 0xFF; }
bool ai_stream_playing() { return g_aicr.load() & AICR_PSTAT; }
uint32_t ai_stream_rate() { return stream_rate(); }
void ai_stream_advance(uint32_t) {}

static void ai_update_irq() {
    uint32_t cr = g_aicr.load();
    pi_set_interrupt(INT_AI, (cr & AICR_AIINT) && (cr & AICR_AIINTMSK));
}

// Schedule the AIIT interrupt for when the counter reaches the timing register.
static void schedule_aiit() {
    std::lock_guard<std::mutex> lk(g_cnt_mutex);
    uint64_t gen = ++g_irq_gen;
    uint32_t cr = g_aicr.load();
    if (!(cr & AICR_PSTAT) || (cr & AICR_AIINTVLD)) return;
    uint32_t cur = sample_count_locked(), target = g_aiit.load();
    uint32_t delta = target - cur;
    if (delta == 0 || delta > 0x80000000u) return;
    uint64_t ticks = (uint64_t)delta * TB_FREQ / stream_rate();
    event_schedule_in(ticks + 1, [gen] {
        if (gen != g_irq_gen) return;
        g_aicr.fetch_or(AICR_AIINT);
        ai_update_irq();
    });
}

void ai_init() {}

uint32_t ai_read32(uint32_t off) {
    switch (off) {
    case 0x00: return g_aicr;
    case 0x04: return g_aivr;
    case 0x08: return sample_count();
    case 0x0C: return g_aiit;
    }
    return 0;
}

void ai_write32(uint32_t off, uint32_t v) {
    switch (off) {
    case 0x00: {
        {
            std::lock_guard<std::mutex> lk(g_cnt_mutex);
            rebase_locked();
            uint32_t old = g_aicr.load();
            uint32_t nv = (v & ~(AICR_AIINT | AICR_SCRESET)) | (old & AICR_AIINT);
            if (v & AICR_AIINT) nv &= ~AICR_AIINT;  // write 1 to clear
            g_aicr = nv;
            if (v & AICR_SCRESET) g_scnt_base = 0;
            g_scnt_tick = now_ticks();
            LOG(LOG_AI, "AICR = %08X", nv);
        }
        ai_update_irq();
        schedule_aiit();
        break;
    }
    case 0x04: g_aivr = v; break;
    case 0x08: { std::lock_guard<std::mutex> lk(g_cnt_mutex); g_scnt_base = v; g_scnt_tick = now_ticks(); } schedule_aiit(); break;
    case 0x0C: g_aiit = v; schedule_aiit(); break;
    }
}
