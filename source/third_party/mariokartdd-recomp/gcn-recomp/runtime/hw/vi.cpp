// Video Interface: retrace interrupts and framebuffer registers.
#include "../runtime.h"
#include <algorithm>
#include <atomic>

static uint16_t g_vi[0x80];
static uint64_t g_frame_start;
static constexpr uint64_t FIELD_TICKS = (uint64_t)(TB_FREQ / 59.94);
std::atomic<uint32_t> g_vi_retrace_count{0};
std::atomic<uint32_t> g_vi_xfb_addr{0};

static void vi_update_irq() {
    bool any = false;
    for (int i = 0; i < 4; i++) {
        uint16_t hi = g_vi[(0x30 + 4 * i) >> 1];
        if ((hi & 0x8000) && (hi & 0x1000)) any = true;
    }
    pi_set_interrupt(INT_VI, any);
}

// Which field of an interlaced frame is being scanned out: 0 the first, 1 the second.
static int g_field;

static bool vi_progressive() { return (g_vi[0x02 >> 1] >> 2) & 1; }

static void vi_retrace() {
    g_frame_start = now_ticks();
    g_field = vi_progressive() ? 0 : g_field ^ 1;
    g_vi_retrace_count++;
    uint32_t tfbl = ((uint32_t)g_vi[0x1C >> 1] << 16) | g_vi[0x1E >> 1];
    g_vi_xfb_addr = (tfbl & 0x10000000) ? ((tfbl & 0xFFFFFF) << 5) : (tfbl & 0xFFFFFF);
    // Raise display interrupts 0 and 1 if enabled.
    for (int i = 0; i < 2; i++) {
        uint16_t& hi = g_vi[(0x30 + 4 * i) >> 1];
        if (hi & 0x1000) hi |= 0x8000;
    }
    vi_update_irq();
    event_schedule(g_frame_start + FIELD_TICKS, vi_retrace);
}

void vi_init() {
    event_schedule_in(FIELD_TICKS, vi_retrace);
}

// The lines the VI scans out: ACV per field, two fields unless progressive (DCR.NIN).
uint32_t vi_display_height() {
    uint32_t acv = (g_vi[0x00 >> 1] >> 4) & 0x3FF;
    return vi_progressive() ? acv : acv * 2;
}

// Where the beam is, as the VI counts it: (vertical << 16) | horizontal, both from 1. A
// line is two half-lines of HLW (HTR0) horizontal counts each, and a field is 525 half-lines
// (524 progressive). An interlaced frame counts its lines through both fields, 1..525, so
// the second field starts half way along line 263 -- where the SDK puts the display
// interrupt that marks it, and how VIGetNextField tells the fields apart. Counting each
// field from line 1 with the horizontal count fixed at 1 made every field the first, and
// a movie player that waits for the second to start never did.
static uint32_t beam_position() {
    uint32_t hlw = g_vi[0x06 >> 1] & 0x1FF;
    if (!hlw) hlw = 429;
    uint64_t per_field = (uint64_t)(vi_progressive() ? 524 : 525) * hlw;
    uint64_t t = std::min<uint64_t>(now_ticks() - g_frame_start, FIELD_TICKS - 1);
    uint64_t pos = g_field * per_field + t * per_field / FIELD_TICKS;
    return (uint32_t)((pos / (2 * hlw) + 1) << 16 | (pos % (2 * hlw) + 1));
}

uint16_t vi_read16(uint32_t off) {
    off &= 0xFF;
    switch (off) {
    case 0x2C: return (uint16_t)(beam_position() >> 16);  // DPV: vertical count, 1-based
    case 0x2E: return (uint16_t)beam_position();          // DPH: horizontal count, 1-based
    case 0x6E: return 0;  // DTV status: no component cable
    default: return g_vi[off >> 1];
    }
}

void vi_write16(uint32_t off, uint16_t v) {
    off &= 0xFF;
    if (getenv("GCN_VILOG") && off < 0x40 && g_vi[off >> 1] != v) fprintf(stderr, "[vi] reg %02X = %04X\n", off, v);
    g_vi[off >> 1] = v;
    if (off >= 0x30 && off < 0x40) vi_update_irq();
}
