// Serial Interface: GameCube controllers.
#include "../runtime.h"
#include "../input_log.h"
#include "pad.h"
#include <mutex>

PadState g_pads[4];
static std::mutex g_pad_mutex;

void pad_set_state(int chan, const PadState& s) {
    std::lock_guard<std::mutex> lk(g_pad_mutex);
    g_pads[chan] = s;
}
static PadState pad_get(int chan) {
    // Replay substitutes the logged state for the live one; recording sees what the
    // guest actually read, whichever that was.
    PadState p;
    {
        std::lock_guard<std::mutex> lk(g_pad_mutex);
        p = g_pads[chan];
    }
    input_pad_poll(chan, p);
    return p;
}

static uint32_t g_outbuf[4], g_poll, g_comcsr, g_sisr, g_exilk;
static uint8_t g_iobuf[128];
static bool g_busy;

enum { CSR_TSTART = 1u, CSR_RDSTINTMSK = 1u << 27, CSR_RDSTINT = 1u << 28, CSR_COMERR = 1u << 29,
       CSR_TCINTMSK = 1u << 30, CSR_TCINT = 1u << 31 };

static inline uint32_t sisr_shift(int chan) { return 24 - 8 * chan; }
enum { SR_NOREP = 0x08, SR_RDST = 0x20 };

bool pad_motor(int chan) {
    const uint32_t out = g_outbuf[chan];
    return (out >> 16) == 0x40 && (out & 3) == 1;
}

static void si_update_irq() {
    bool rdst = false;
    for (int i = 0; i < 4; i++) if ((g_sisr >> sisr_shift(i)) & SR_RDST) rdst = true;
    if (rdst) g_comcsr |= CSR_RDSTINT; else g_comcsr &= ~CSR_RDSTINT;
    bool irq = ((g_comcsr & CSR_TCINT) && (g_comcsr & CSR_TCINTMSK)) ||
               ((g_comcsr & CSR_RDSTINT) && (g_comcsr & CSR_RDSTINTMSK));
    pi_set_interrupt(INT_SI, irq);
}

static void make_poll_response(int chan, uint32_t& hi, uint32_t& lo) {
    PadState p = pad_get(chan);
    hi = ((uint32_t)(p.buttons & 0x1F7F) << 16) | 0x00800000u | ((uint32_t)p.stick_x << 8) | p.stick_y;
    uint32_t mode = (g_outbuf[chan] >> 8) & 7;
    switch (mode) {
    default:
    case 3: lo = ((uint32_t)p.cstick_x << 24) | ((uint32_t)p.cstick_y << 16) | ((uint32_t)p.trig_l << 8) | p.trig_r; break;
    case 0: case 5: case 6: case 7:
        lo = ((uint32_t)p.cstick_x << 24) | ((uint32_t)p.cstick_y << 16) | ((p.trig_l & 0xF0u) << 8) | ((p.trig_r & 0xF0u) << 4); break;
    case 1: lo = ((uint32_t)(p.cstick_x & 0xF0) << 24) | ((uint32_t)(p.cstick_y & 0xF0) << 20) | ((uint32_t)p.trig_l << 8) | p.trig_r; break;
    case 2: lo = ((uint32_t)(p.cstick_x & 0xF0) << 24) | ((uint32_t)(p.cstick_y & 0xF0) << 20) | ((uint32_t)(p.trig_l & 0xF0) << 12) | ((uint32_t)(p.trig_r & 0xF0) << 8); break;
    case 4: lo = ((uint32_t)p.cstick_x << 24) | ((uint32_t)p.cstick_y << 16); break;
    }
}

static void si_transfer_complete() {
    g_busy = false;
    int chan = (g_comcsr >> 1) & 3;
    uint32_t outlen = (g_comcsr >> 16) & 0x7F; if (!outlen) outlen = 128;
    uint32_t inlen = (g_comcsr >> 8) & 0x7F; if (!inlen) inlen = 128;
    uint8_t cmd = g_iobuf[0];
    PadState p = pad_get(chan);
    g_comcsr &= ~CSR_TSTART;
    g_comcsr |= CSR_TCINT;
    if (!p.connected) {
        g_comcsr |= CSR_COMERR;
        g_sisr |= SR_NOREP << sisr_shift(chan);
    } else {
        g_comcsr &= ~CSR_COMERR;
        uint8_t resp[128] = {};
        switch (cmd) {
        case 0x00: case 0xFF:  // reset / get type
            resp[0] = 0x09; resp[1] = 0x00; resp[2] = 0x00;
            break;
        case 0x41: case 0x42:  // origin / calibrate
            resp[0] = 0x00; resp[1] = 0x80;
            resp[2] = 0x80; resp[3] = 0x80; resp[4] = 0x80; resp[5] = 0x80;
            resp[6] = 0x00; resp[7] = 0x00; resp[8] = 0x00; resp[9] = 0x00;
            break;
        case 0x40: {  // direct poll
            uint32_t hi, lo;
            make_poll_response(chan, hi, lo);
            for (int i = 0; i < 4; i++) { resp[i] = hi >> (24 - 8 * i); resp[4 + i] = lo >> (24 - 8 * i); }
            break;
        }
        default:
            LOG(LOG_SI, "unknown SI command %02X (out %u in %u)", cmd, outlen, inlen);
            break;
        }
        memcpy(g_iobuf, resp, inlen);
    }
    si_update_irq();
}

// Called every VI field: latch polled controller data.
void si_poll_tick() {
    uint32_t en = (g_poll >> 4) & 0xF;
    for (int chan = 0; chan < 4; chan++) {
        if (!(en & (0x8 >> chan))) continue;
        if (pad_get(chan).connected) {
            g_sisr |= SR_RDST << sisr_shift(chan);
            g_sisr &= ~(SR_NOREP << sisr_shift(chan));
        } else {
            g_sisr |= SR_NOREP << sisr_shift(chan);
        }
    }
    si_update_irq();
}

void si_init() {
    g_pads[0].connected = true;
    struct T { static void tick() { si_poll_tick(); event_schedule_in(TB_FREQ / 120, tick); } };
    event_schedule_in(TB_FREQ / 120, T::tick);
}

uint32_t si_read32(uint32_t off) {
    if (off >= 0x80) {
        uint32_t o = off - 0x80;
        return ((uint32_t)g_iobuf[o] << 24) | ((uint32_t)g_iobuf[o + 1] << 16) | ((uint32_t)g_iobuf[o + 2] << 8) | g_iobuf[o + 3];
    }
    if (off < 0x30) {
        int chan = off / 12, reg = (off % 12) / 4;
        if (reg == 0) return g_outbuf[chan];
        uint32_t hi, lo;
        make_poll_response(chan, hi, lo);
        if (!pad_get(chan).connected) { hi = 0x80000000u; lo = 0; }
        if (reg == 1) return hi;
        // reading the low word clears RDST
        g_sisr &= ~(SR_RDST << sisr_shift(chan));
        si_update_irq();
        return lo;
    }
    switch (off) {
    case 0x30: return g_poll;
    case 0x34: return g_comcsr;
    case 0x38: return g_sisr;
    case 0x3C: return g_exilk;
    }
    return 0;
}

void si_write32(uint32_t off, uint32_t v) {
    if (off >= 0x80) {
        uint32_t o = off - 0x80;
        g_iobuf[o] = v >> 24; g_iobuf[o + 1] = v >> 16; g_iobuf[o + 2] = v >> 8; g_iobuf[o + 3] = v;
        return;
    }
    if (off < 0x30) {
        int chan = off / 12, reg = (off % 12) / 4;
        if (reg == 0) g_outbuf[chan] = v;
        return;
    }
    switch (off) {
    case 0x30: g_poll = v; break;
    case 0x34: {
        uint32_t keep = g_comcsr & (CSR_TCINT | CSR_RDSTINT | CSR_COMERR);
        if (v & CSR_TCINT) keep &= ~CSR_TCINT;
        g_comcsr = (v & ~(CSR_TCINT | CSR_RDSTINT | CSR_COMERR)) | keep;
        si_update_irq();
        if ((v & CSR_TSTART) && !g_busy) {
            g_busy = true;
            event_schedule_in(TB_FREQ / 20000, si_transfer_complete);
        }
        break;
    }
    case 0x38:
        // writing 1 to error bits clears them; bit 31 (WR) copies outbufs
        g_sisr &= ~(v & 0x0F0F0F0Fu);
        break;
    case 0x3C: g_exilk = v; break;
    }
}
