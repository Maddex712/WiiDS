// External Interface: IPL device (SRAM / RTC / UART). Memory cards: not present (yet).
#include "../runtime.h"
#include "../platform.h"
#include <ctime>
#include <memory>
#include "memcard.h"

static std::unique_ptr<MemCard> g_card_a;

struct ExiChannel {
    uint32_t csr, mar, len, cr, data;
};
static ExiChannel g_exi[3];

// ---- IPL device (channel 0, CS1) ----
static uint8_t g_sram[64];
static uint32_t g_ipl_addr;
static bool g_ipl_have_cmd;
static uint32_t g_ipl_pos;
static std::string g_uart_line;

static void sram_init() {
    memset(g_sram, 0, sizeof(g_sram));
    // counter_bias = 0, display_offset_h = 0, ntd = 0, lang = 0 (English), flags = stereo|...
    g_sram[0x13] = 0x2C;
    // OSSramEx: flashID[2][12] at 0x14 (zero), flashIDCheckSum[2] at 0x3A must equal ~sum(flashID).
    g_sram[0x3A] = 0xFF;
    g_sram[0x3B] = 0xFF;
    uint16_t sum = 0, inv = 0;
    for (int i = 0x0C; i < 0x14; i += 2) {
        uint16_t w = (uint16_t)((g_sram[i] << 8) | g_sram[i + 1]);
        sum += w;
        inv += (uint16_t)~w;
    }
    g_sram[0] = sum >> 8; g_sram[1] = (uint8_t)sum;
    g_sram[2] = inv >> 8; g_sram[3] = (uint8_t)inv;
}

// The RTC runs on guest time from a base the frontend sets (the input log's start time
// on a replay, the wall clock otherwise), so a replay sees the recording's calendar.
static time_t g_rtc_base;
void rtc_set_base(time_t utc) { g_rtc_base = utc; }

static uint32_t rtc_now() {
    // seconds since 2000-01-01 00:00:00 local time
    if (!g_rtc_base) g_rtc_base = time(nullptr);
    time_t t = g_rtc_base + (time_t)(now_ticks() / TB_FREQ);
    time_t local = t + plat_utc_offset_seconds();
    return (uint32_t)(local - 946684800);
}

static uint8_t ipl_transfer_byte(uint8_t in) {
    if (!g_ipl_have_cmd) {
        g_ipl_addr = (g_ipl_addr << 8) | in;
        if (++g_ipl_pos == 4) { g_ipl_have_cmd = true; g_ipl_pos = 0; }
        return 0;
    }
    bool write = g_ipl_addr & 0x80000000u;
    uint32_t addr = g_ipl_addr & 0x7FFFFFFFu;
    uint8_t out = 0;
    if ((addr & 0x7FFFFF00u) == 0x20000000u) {  // RTC
        uint32_t rtc = rtc_now();
        if (!write && g_ipl_pos < 4) out = (uint8_t)(rtc >> (24 - 8 * g_ipl_pos));
    } else if ((addr & 0x7FFFFF00u) == 0x20000100u) {  // SRAM
        uint32_t o = ((addr - 0x20000100u) >> 6) + g_ipl_pos;
        if (o < 64) {
            if (write) g_sram[o] = in; else out = g_sram[o];
        }
    } else if ((addr & 0x7FFFFF00u) == 0x20010000u) {  // UART
        if (write && g_ipl_pos > 0) {  // first byte after cmd is a length/pad byte on some SDKs
            if (in == '\n' || in == '\r') {
                if (!g_uart_line.empty()) LOG(LOG_OS, "UART: %s", g_uart_line.c_str());
                g_uart_line.clear();
            } else if (in) g_uart_line.push_back((char)in);
        }
    } else {
        // ROM (font etc.) - not provided
        if (g_ipl_pos == 0) LOG(LOG_EXI, "IPL ROM read at %08X (unsupported)", addr >> 6);
    }
    g_ipl_pos++;
    return out;
}

static void ipl_deselect() { g_ipl_have_cmd = false; g_ipl_pos = 0; g_ipl_addr = 0; }

// ---- generic ----
static int selected_device(int ch) {
    uint32_t cs = (g_exi[ch].csr >> 7) & 7;
    if (cs & 1) return 0;
    if (cs & 2) return 1;
    if (cs & 4) return 2;
    return -1;
}

static uint8_t device_byte(int ch, int dev, uint8_t in, bool reading) {
    if (ch == 0 && dev == 1) return ipl_transfer_byte(in);
    if (ch == 0 && dev == 0 && g_card_a) return g_card_a->transfer(in, reading);
    return 0xFF;
}

static void exi_update_irq() {
    bool irq = false;
    for (auto& c : g_exi) {
        if ((c.csr & 0x2) && (c.csr & 0x1)) irq = true;
        if ((c.csr & 0x8) && (c.csr & 0x4)) irq = true;
        if ((c.csr & 0x800) && (c.csr & 0x400)) irq = true;
    }
    pi_set_interrupt(INT_EXI, irq);
}

static void exi_transfer(int ch) {
    ExiChannel& c = g_exi[ch];
    int dev = selected_device(ch);
    uint32_t rw = (c.cr >> 2) & 3;
    if (c.cr & 2) {  // DMA
        uint8_t* p = phys_ptr(c.mar);
        for (uint32_t i = 0; i < c.len; i++) {
            uint8_t o = device_byte(ch, dev, rw == 1 ? p[i] : 0, rw != 1);
            if (rw == 0) p[i] = o;
        }
        c.mar += c.len;
        c.len = 0;
    } else {
        uint32_t n = ((c.cr >> 4) & 3) + 1;
        uint32_t result = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t in = (uint8_t)(c.data >> (24 - 8 * i));
            uint8_t o = device_byte(ch, dev, rw != 0 ? in : 0, rw != 1);
            result |= (uint32_t)o << (24 - 8 * i);
        }
        if (rw != 1) c.data = result;
    }
    c.cr &= ~1u;
    c.csr |= 0x8;  // TCINT
    exi_update_irq();
}

const char* g_save_dir;
// Set before boot to use a specific card file instead of <save dir>/memcard_a.raw. A replay
// points this at its scratch copy of the logged run's card.
const char* g_memcard_path;

void exi_init() {
    sram_init();
    const std::string path = g_memcard_path ? std::string(g_memcard_path)
                           : g_save_dir ? std::string(g_save_dir) + "/memcard_a.raw"
                                        : std::string("saves/memcard_a.raw");
    g_card_a = std::make_unique<MemCard>(path, 4);
}

uint32_t exi_read32(uint32_t off) {
    int ch = off / 0x14;
    if (ch > 2) return 0;
    ExiChannel& c = g_exi[ch];
    switch (off % 0x14) {
    case 0x00: {
        uint32_t v = c.csr & ~0x1000u;
        if (ch == 0 && g_card_a) v |= 0x1000;  // EXT: memory card inserted in slot A
        // EXT: device attached. Channel 0/1 memory cards: absent. Channel 2 (AD16): absent.
        return v;
    }
    case 0x04: return c.mar;
    case 0x08: return c.len;
    case 0x0C: return c.cr;
    case 0x10: return c.data;
    }
    return 0;
}

void exi_write32(uint32_t off, uint32_t v) {
    int ch = off / 0x14;
    if (ch > 2) return;
    ExiChannel& c = g_exi[ch];
    switch (off % 0x14) {
    case 0x00: {
        int old_dev = selected_device(ch);
        uint32_t clr = v & (0x2 | 0x8 | 0x800);
        uint32_t keep = c.csr & (0x2 | 0x8 | 0x800 | 0x2000);
        keep &= ~clr;
        c.csr = (v & ~(0x2u | 0x8u | 0x800u | 0x1000u)) | keep;
        int new_dev = selected_device(ch);
        if (old_dev != new_dev && ch == 0 && old_dev == 1) ipl_deselect();
        if (new_dev == 1 && ch == 0 && old_dev != 1) ipl_deselect();
        if (ch == 0 && g_card_a && old_dev != new_dev) {
            if (old_dev == 0 && g_card_a->deselect()) {
                // Card signals completion of erase/program via its EXI interrupt.
                event_schedule_in(TB_FREQ / 2000, [] { g_exi[0].csr |= 0x2; exi_update_irq(); });
            }
            if (new_dev == 0) g_card_a->select();
        }
        exi_update_irq();
        break;
    }
    case 0x04: c.mar = v & 0x03FFFFE0; break;
    case 0x08: c.len = v & 0x03FFFFE0; break;
    case 0x0C:
        if (!(ch == 0 && selected_device(ch) == 1))
            LOG(LOG_EXI, "EXI ch%d dev%d xfer cr=%02X data=%08X len=%X csr=%08X", ch, selected_device(ch), v & 0x3F, c.data, c.len, c.csr);
        c.cr = v & 0x3F;
        if (v & 1) exi_transfer(ch);
        break;
    case 0x10: c.data = v; break;
    }
}
