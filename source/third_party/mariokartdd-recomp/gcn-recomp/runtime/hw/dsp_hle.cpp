// High-level emulation of the DSP: boot ROM handshake, and which ucode the game booted:
// AX (ax.cpp), JAudio (jaudio.cpp) or the memory card unlock ucode.
#include "../runtime.h"
#include "dsp_hle.h"
#include "ax.h"
#include "jaudio.h"

enum : uint32_t {
    DSP_INIT = 0xDCD10000, DSP_RESUME = 0xDCD10001, DSP_YIELD = 0xDCD10002,
    DSP_DONE = 0xDCD10003, DSP_SYNC = 0xDCD10004, DSP_FRAME_END = 0xDCD10005,
    MAIL_RESUME = 0xCDD10000, MAIL_NEW_UCODE = 0xCDD10001, MAIL_RESET = 0xCDD10002,
    MAIL_CONTINUE = 0xCDD10003, MAIL_CMDLIST = 0xBABE0000,
};

enum class UCode { ROM, AX, JAudio, Card };
static uint32_t g_ax_iram_len, g_ax_init_vector;
static uint32_t g_jaudio_init_vector = ~0u;
static UCode g_ucode;

// ROM boot state
static int g_boot_state;
static uint32_t g_boot_iram_mm, g_boot_iram_addr, g_boot_iram_len, g_boot_dram_len, g_boot_start;
static uint32_t g_boot_key;

// AX state
static bool g_next_is_cmdlist;
static uint16_t g_cmdlist_size;
static bool g_upload_in_progress;

void dsp_hle_reset() {
    g_ucode = UCode::ROM;
    g_boot_state = 0;
    dsp_mail_clear();
    // The boot ROM announces itself.
    dsp_send_mail(0x8071FEED, false);
}

// A fingerprint of the ucode, logged so that one seen here can be told from another.
static uint32_t ucode_hash(uint32_t mm, uint32_t len) {
    uint32_t h = 0;
    const uint8_t* p = phys_ptr(mm);
    for (uint32_t i = 0; i < len; i++) { h ^= p[i]; h = (h << 3) | (h >> 29); }
    return h;
}

static void boot_ucode() {
    LOG(LOG_DSP, "boot ucode: iram mm=%08X dsp=%04X len=%X dram=%X start=%04X hash=%08X", g_boot_iram_mm, g_boot_iram_addr,
        g_boot_iram_len, g_boot_dram_len, g_boot_start, ucode_hash(g_boot_iram_mm, g_boot_iram_len));
    g_upload_in_progress = false;
    g_next_is_cmdlist = false;
    const uint32_t hash = ucode_hash(g_boot_iram_mm, g_boot_iram_len);
    if (jaudio_is_ucode(hash)) {
        // Booted again at another vector, it is resuming after another ucode had the DSP.
        if (g_jaudio_init_vector == ~0u) g_jaudio_init_vector = g_boot_start;
        g_ucode = UCode::JAudio;
        jaudio_boot(hash, g_boot_start != g_jaudio_init_vector);
        return;
    }
    // Otherwise the first ucode the game boots is AX; anything else of a different size, or
    // anything at all in a game that runs JAudio, is the memory card unlock ucode.
    if (!g_ax_iram_len && g_jaudio_init_vector == ~0u) { g_ax_iram_len = g_boot_iram_len; g_ax_init_vector = g_boot_start; }
    if (g_ax_iram_len && g_boot_iram_len == g_ax_iram_len) {
        bool resume = g_boot_start != g_ax_init_vector;
        g_ucode = UCode::AX;
        if (!resume) ax_init();
        dsp_send_mail(resume ? DSP_RESUME : DSP_INIT, true);
    } else {
        g_ucode = UCode::Card;
        dsp_send_mail(DSP_INIT, true);
    }
}

static void rom_mail(uint32_t mail) {
    // Pairs: 0x80F3xxxx key followed by a value.
    if (g_boot_state == 0) {
        if ((mail & 0xFFFF0000u) == 0x80F30000u) {
            g_boot_key = mail & 0xFFFF;
            g_boot_state = 1;
        } else {
            LOG(LOG_DSP, "ROM: unexpected mail %08X", mail);
        }
        return;
    }
    g_boot_state = 0;
    switch (g_boot_key) {
    case 0xA001: g_boot_iram_mm = mail; break;
    case 0xC002: g_boot_iram_addr = mail; break;
    case 0xA002: g_boot_iram_len = mail; break;
    case 0xB002: g_boot_dram_len = mail; break;
    case 0xD001: g_boot_start = mail; boot_ucode(); break;
    default: LOG(LOG_DSP, "ROM: unknown boot key %04X = %08X", g_boot_key, mail); break;
    }
}

static void ax_mail(uint32_t mail) {
    bool set_next = false;
    if (g_next_is_cmdlist) {
        ax_process_cmdlist(mail & 0x7FFFFFFF, g_cmdlist_size);
        dsp_send_mail(DSP_YIELD, true);
    } else if (g_upload_in_progress) {
        // Following mails describe a new ucode; treat like ROM boot pairs.
        rom_mail(mail);
    } else if (mail == MAIL_RESUME) {
        dsp_send_mail(DSP_RESUME, true);
    } else if (mail == MAIL_NEW_UCODE) {
        g_upload_in_progress = true;
    } else if (mail == MAIL_RESET) {
        dsp_hle_reset();
    } else if (mail == MAIL_CONTINUE) {
    } else if ((mail & 0xFFFF0000u) == MAIL_CMDLIST) {
        set_next = true;
        g_cmdlist_size = (uint16_t)mail;
    } else {
        LOG(LOG_DSP, "AX: unknown mail %08X", mail);
    }
    g_next_is_cmdlist = set_next;
}

void dsp_hle_mail(uint32_t mail) {
    switch (g_ucode) {
    case UCode::ROM: rom_mail(mail); break;
    case UCode::AX: ax_mail(mail); break;
    case UCode::JAudio:
        if (g_upload_in_progress) { rom_mail(mail); break; }
        switch (jaudio_mail(mail)) {
        case JAudioNext::Stay: break;
        case JAudioNext::NewUcode: g_upload_in_progress = true; break;
        case JAudioNext::Reset: dsp_hle_reset(); break;
        }
        break;
    case UCode::Card:
        // Card unlock: the real ucode computes a response; the card emulation doesn't
        // check it, so just report completion and fall back to the boot ROM.
        LOG(LOG_DSP, "card ucode mail %08X", mail);
        if (g_boot_state == 0 && mail == 0xFF000000u) { g_boot_state = 2; break; }
        g_boot_state = 0;
        g_ucode = UCode::ROM;
        dsp_send_mail(DSP_DONE, true);
        break;
    }
}

void dsp_hle_cpu_interrupt() {
    LOG(LOG_DSP, "CPU->DSP interrupt");
}
