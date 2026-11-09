// High-level emulation of the AX audio ucode (the "old" 2001 GameCube variant):
// processes the command lists the AX library sends, mixes voices from ARAM and
// writes the final 5ms stereo block that the AI DMA plays.
#include "../runtime.h"
#include "ax.h"
#include <algorithm>
#include <initializer_list>
#include <string>

namespace {

constexpr int SPMS = 32;          // samples per millisecond (32 kHz)
constexpr int SPF = 5 * SPMS;     // samples per 5ms frame

int32_t g_left[SPF], g_right[SPF], g_surround[SPF];
int32_t g_auxa_l[SPF], g_auxa_r[SPF], g_auxa_s[SPF];
int32_t g_auxb_l[SPF], g_auxb_r[SPF], g_auxb_s[SPF];

// ---- parameter block (word indices into the big-endian u16 array) ----
enum {
    PB_NEXT_HI = 0, PB_NEXT_LO = 1, PB_SRC_TYPE = 4, PB_COEF_SELECT = 5, PB_MIXER_CONTROL = 6,
    PB_RUNNING = 7, PB_IS_STREAM = 8,
    PB_MIXER = 9,           // 18 words: (vol, delta) pairs for L, R, AuxA L/R, AuxB L/R, AuxB S, S, AuxA S
    PB_UPDATES = 34,        // num_updates[5], data_hi, data_lo
    PB_VOL = 50, PB_VOL_DELTA = 51,
    PB_LOOPING = 55, PB_FORMAT = 56, PB_LOOP_HI = 57, PB_LOOP_LO = 58, PB_END_HI = 59, PB_END_LO = 60,
    PB_CUR_HI = 61, PB_CUR_LO = 62,
    PB_COEFS = 63, PB_GAIN = 79, PB_PRED_SCALE = 80, PB_YN1 = 81, PB_YN2 = 82,
    PB_RATIO_HI = 83, PB_RATIO_LO = 84, PB_FRAC = 85, PB_LAST = 86,
    PB_LOOP_PRED_SCALE = 90, PB_LOOP_YN1 = 91, PB_LOOP_YN2 = 92,
    PB_WORDS = 93,
};

struct PB {
    uint16_t w[PB_WORDS];
    uint32_t get32(int i) const { return ((uint32_t)w[i] << 16) | w[i + 1]; }
    void set32(int i, uint32_t v) { w[i] = (uint16_t)(v >> 16); w[i + 1] = (uint16_t)v; }
};

void read_pb(uint32_t addr, PB& pb) {
    for (int i = 0; i < PB_WORDS; i++) pb.w[i] = mem_r16(addr + 2 * i);
}
void write_pb(uint32_t addr, const PB& pb) {
    for (int i = 0; i < PB_WORDS; i++) mem_w16(addr + 2 * i, pb.w[i]);
}

inline uint8_t aram_byte(uint32_t a) { return g_aram[a & (ARAM_SIZE - 1)]; }

// The DSP "accelerator": fetches the next sample for a voice, handling ADPCM
// decoding, the end of the sample data and looping.
struct Accel {
    PB& pb;
    uint32_t cur, end, loop;
    uint16_t fmt;
    bool stopped = false;

    explicit Accel(PB& p) : pb(p), cur(p.get32(PB_CUR_HI)), end(p.get32(PB_END_HI)), loop(p.get32(PB_LOOP_HI)), fmt(p.w[PB_FORMAT]) {}

    int16_t sample() {
        if (stopped) return 0;
        int32_t val = 0;
        switch (fmt) {
        case 0x00: {  // 4-bit DSP ADPCM, addresses in nibbles
            if ((cur & 15) == 0) {
                pb.w[PB_PRED_SCALE] = aram_byte(cur >> 1);
                cur += 2;
            }
            uint16_t ps = pb.w[PB_PRED_SCALE];
            int scale = 1 << (ps & 0xF);
            int ci = (ps >> 4) & 7;
            int32_t c1 = (int16_t)pb.w[PB_COEFS + ci * 2], c2 = (int16_t)pb.w[PB_COEFS + ci * 2 + 1];
            uint8_t b = aram_byte(cur >> 1);
            int t = (cur & 1) ? (b & 0xF) : (b >> 4);
            if (t >= 8) t -= 16;
            int32_t yn1 = (int16_t)pb.w[PB_YN1], yn2 = (int16_t)pb.w[PB_YN2];
            val = scale * t + ((0x400 + c1 * yn1 + c2 * yn2) >> 11);
            val = std::clamp(val, -0x7FFF, 0x7FFF);
            pb.w[PB_YN2] = pb.w[PB_YN1];
            pb.w[PB_YN1] = (uint16_t)val;
            cur++;
            break;
        }
        case 0x0A:  // PCM16, addresses in samples
            val = (int16_t)((aram_byte(cur * 2) << 8) | aram_byte(cur * 2 + 1));
            pb.w[PB_YN2] = pb.w[PB_YN1];
            pb.w[PB_YN1] = (uint16_t)val;
            cur++;
            break;
        case 0x19:  // PCM8
            val = (int16_t)(aram_byte(cur) << 8);
            pb.w[PB_YN2] = pb.w[PB_YN1];
            pb.w[PB_YN1] = (uint16_t)val;
            cur++;
            break;
        default:
            stopped = true;
            return 0;
        }
        if (cur > end) {
            if (pb.w[PB_LOOPING]) {
                cur = loop;
                pb.w[PB_PRED_SCALE] = pb.w[PB_LOOP_PRED_SCALE];
                if (!pb.w[PB_IS_STREAM]) {
                    pb.w[PB_YN1] = pb.w[PB_LOOP_YN1];
                    pb.w[PB_YN2] = pb.w[PB_LOOP_YN2];
                }
            } else {
                pb.w[PB_RUNNING] = 0;
                stopped = true;
            }
        }
        return (int16_t)val;
    }

    void finish() { pb.set32(PB_CUR_HI, cur); }
};

void mix_add(int32_t* out, const int16_t* in, int n, PB& pb, int vol_idx, bool ramp) {
    uint16_t vol = pb.w[vol_idx];
    uint16_t delta = ramp ? pb.w[vol_idx + 1] : 0;
    for (int i = 0; i < n; i++) {
        int32_t s = (int32_t)(((int64_t)in[i] * vol) >> 15);
        out[i] += std::clamp(s, -32767, 32767);
        vol += delta;
    }
    pb.w[vol_idx] = vol;
}

extern int g_stat_voices;
void process_voice(PB& pb, int offset) {
    if (!pb.w[PB_RUNNING]) return;
    if (offset == 0) g_stat_voices++;
    int16_t samples[SPMS];
    Accel acc(pb);
    // Sample rate conversion: linear interpolation, for the linear and polyphase modes alike.
    uint32_t ratio = pb.get32(PB_RATIO_HI);
    uint32_t pos = pb.w[PB_FRAC];
    int16_t* last = (int16_t*)&pb.w[PB_LAST];
    uint16_t src_type = pb.w[PB_SRC_TYPE];
    for (int i = 0; i < SPMS; i++) {
        int32_t frac = pos & 0xFFFF;
        if (src_type == 2) samples[i] = last[3];
        else samples[i] = (int16_t)(last[2] + (((last[3] - last[2]) * frac) >> 16));
        pos += ratio;
        while (pos >= 0x10000) {
            last[0] = last[1]; last[1] = last[2]; last[2] = last[3];
            last[3] = acc.sample();
            pos -= 0x10000;
        }
    }
    pb.w[PB_FRAC] = (uint16_t)pos;
    acc.finish();
    // Volume envelope
    for (int i = 0; i < SPMS; i++) {
        uint16_t v = pb.w[PB_VOL];
        samples[i] = (int16_t)std::clamp(((int32_t)samples[i] * v) >> 15, -32767, 32767);
        pb.w[PB_VOL] = (uint16_t)(v + (int16_t)pb.w[PB_VOL_DELTA]);
    }
    // Old (2001) ucode mixer control: main L/R always; bit0 AuxA, bit1 AuxB,
    // bit2 surround, bit3 volume ramps. Bit4 selects Dolby Pro Logic II mixing, where
    // the low bits mean something else.
    uint16_t mc = pb.w[PB_MIXER_CONTROL];
    bool ramp = mc & 8;
    bool auxa, auxb, surround;
    if (mc & 0x10) {
        auxb = (mc & 6) == 0;
        auxa = (mc & 7) == 1;
        surround = false;
    } else {
        auxa = mc & 1;
        auxb = mc & 2;
        surround = mc & 4;
    }
    mix_add(g_left + offset, samples, SPMS, pb, PB_MIXER + 0, ramp);
    mix_add(g_right + offset, samples, SPMS, pb, PB_MIXER + 2, ramp);
    if (auxa) {
        mix_add(g_auxa_l + offset, samples, SPMS, pb, PB_MIXER + 4, ramp);
        mix_add(g_auxa_r + offset, samples, SPMS, pb, PB_MIXER + 6, ramp);
        if (mc & 0x10) mix_add(g_auxa_s + offset, samples, SPMS, pb, PB_MIXER + 16, ramp);
    }
    if (auxb) {
        mix_add(g_auxb_l + offset, samples, SPMS, pb, PB_MIXER + 8, ramp);
        mix_add(g_auxb_r + offset, samples, SPMS, pb, PB_MIXER + 10, ramp);
    }
    if (surround) {
        mix_add(g_surround + offset, samples, SPMS, pb, PB_MIXER + 14, ramp);
        if (auxa) mix_add(g_auxa_s + offset, samples, SPMS, pb, PB_MIXER + 16, ramp);
        if (auxb) mix_add(g_auxb_s + offset, samples, SPMS, pb, PB_MIXER + 12, ramp);
    }
}

void process_pb_list(uint32_t pb_addr) {
    PB pb;
    int guard = 0;
    while (pb_addr && guard++ < 256) {
        read_pb(pb_addr, pb);
        // Per-millisecond parameter updates: (word index, value) pairs.
        uint32_t upd = pb.get32(PB_UPDATES + 5);
        for (int ms = 0; ms < 5; ms++) {
            uint16_t n = pb.w[PB_UPDATES + ms];
            for (int k = 0; k < n; k++) {
                uint16_t idx = mem_r16(upd), val = mem_r16(upd + 2);
                upd += 4;
                if (idx < PB_WORDS) pb.w[idx] = val;
            }
            process_voice(pb, ms * SPMS);
        }
        write_pb(pb_addr, pb);
        pb_addr = pb.get32(PB_NEXT_HI);
    }
}

void write_bufs(uint32_t addr, std::initializer_list<int32_t*> bufs) {
    for (int32_t* b : bufs)
        for (int i = 0; i < SPF; i++, addr += 4) mem_w32(addr, (uint32_t)b[i]);
}
void add_bufs(uint32_t addr, std::initializer_list<int32_t*> bufs) {
    for (int32_t* b : bufs)
        for (int i = 0; i < SPF; i++, addr += 4) b[i] += (int32_t)mem_r32(addr);
}
void set_bufs(uint32_t addr, std::initializer_list<int32_t*> bufs) {
    for (int32_t* b : bufs)
        for (int i = 0; i < SPF; i++, addr += 4) b[i] = (int32_t)mem_r32(addr);
}

// Replace the main buffers with one downloaded mono buffer: L = R = in (0x07), or
// L = -in, R = in (0x11); surround cleared either way.
void set_main_from(uint32_t addr, bool opposite) {
    for (int i = 0; i < SPF; i++, addr += 4) {
        int32_t v = (int32_t)mem_r32(addr);
        g_left[i] = opposite ? -v : v;
        g_right[i] = v;
        g_surround[i] = 0;
    }
}

// 0x01: add nine downloaded buffers (main L/R/S, AuxA L/R/S, AuxB L/R/S, all read from
// the same address in turn) scaled by a volume per group.
void download_and_mix(uint32_t addr, uint16_t vol_main, uint16_t vol_auxa, uint16_t vol_auxb) {
    int32_t* groups[3][3] = {{g_left, g_right, g_surround}, {g_auxa_l, g_auxa_r, g_auxa_s}, {g_auxb_l, g_auxb_r, g_auxb_s}};
    uint16_t vols[3] = {vol_main, vol_auxa, vol_auxb};
    for (int g = 0; g < 3; g++) {
        uint32_t p = addr;
        for (int b = 0; b < 3; b++)
            for (int i = 0; i < SPF; i++, p += 4) groups[g][b][i] += (int32_t)(((int64_t)(int32_t)mem_r32(p) * vols[g]) >> 15);
    }
}

void setup(uint32_t addr) {
    int32_t* bufs[9] = {g_left, g_right, g_surround, g_auxa_l, g_auxa_r, g_auxa_s, g_auxb_l, g_auxb_r, g_auxb_s};
    for (int b = 0; b < 9; b++) {
        int32_t init = (int32_t)(((uint32_t)mem_r16(addr) << 16) | mem_r16(addr + 2));
        int16_t delta = (int16_t)mem_r16(addr + 4);
        addr += 6;
        for (int i = 0; i < SPF; i++) { bufs[b][i] = init; init += delta; }
    }
}

int g_stat_voices, g_stat_frames, g_stat_peak;

void output(uint32_t surround_addr, uint32_t lr_addr) {
    for (int i = 0; i < SPF; i++) g_stat_peak = std::max(g_stat_peak, std::abs(g_left[i]));
    if (++g_stat_frames % 400 == 0 && getenv("GCN_AXSTATS")) {
        fprintf(stderr, "[ax] frames=%d voices(running, last frame)=%d peak=%d out=%08X\n", g_stat_frames, g_stat_voices, g_stat_peak, lr_addr);
        g_stat_peak = 0;
    }
    for (int i = 0; i < SPF; i++) mem_w32(surround_addr + 4 * i, (uint32_t)g_surround[i]);
    for (int i = 0; i < SPF; i++) {
        int32_t l = std::clamp(g_left[i], -32767, 32767), r = std::clamp(g_right[i], -32767, 32767);
        mem_w16(lr_addr + 4 * i, (uint16_t)r);       // interleaved R, L
        mem_w16(lr_addr + 4 * i + 2, (uint16_t)l);
    }
}

}  // namespace

void ax_init() {}

void ax_process_cmdlist(uint32_t addr, uint16_t size) {
    uint32_t pb_addr = 0;
    uint32_t p = addr, end = addr + size + 64;
    auto rd = [&]() { uint16_t v = mem_r16(p); p += 2; return v; };
    auto rd32 = [&]() { uint32_t hi = rd(); return (hi << 16) | rd(); };
    for (int guard = 0; guard < 256 && p < end; guard++) {
        uint16_t cmd = rd();
        switch (cmd) {
        case 0x00: setup(rd32()); break;
        case 0x01: { uint32_t a = rd32(); uint16_t vm = rd(), va = rd(), vb = rd(); download_and_mix(a, vm, va, vb); break; }
        case 0x02: pb_addr = rd32(); break;
        case 0x03: g_stat_voices = 0; process_pb_list(pb_addr); break;
        case 0x04: { uint32_t w = rd32(), r = rd32(); write_bufs(w, {g_auxa_l, g_auxa_r, g_auxa_s});
                     add_bufs(r, {g_left, g_right, g_surround}); break; }
        case 0x05: { uint32_t w = rd32(), r = rd32(); write_bufs(w, {g_auxb_l, g_auxb_r, g_auxb_s});
                     add_bufs(r, {g_left, g_right, g_surround}); break; }
        case 0x06: write_bufs(rd32(), {g_left, g_right, g_surround}); break;
        case 0x07: set_main_from(rd32(), false); break;
        case 0x08: p += 20; break;
        case 0x09: add_bufs(rd32(), {g_left, g_right, g_surround}); break;  // mix AuxB back in, nothing uploaded
        case 0x0A: case 0x0B: case 0x0C: break;
        case 0x0D: {  // continue in another command buffer: address, size
            uint32_t a = rd32();
            uint16_t sz = rd();
            p = a;
            end = a + sz + 64;
            break;
        }
        case 0x0E: { uint32_t s = rd32(), lr = rd32(); output(s, lr); break; }
        case 0x0F: return;
        case 0x10: {  // upload AuxB L/R, then download the CPU's version of them: add to main, keep as AuxB
            uint32_t up = rd32(), dl = rd32();
            write_bufs(up, {g_auxb_l, g_auxb_r});
            set_bufs(dl, {g_auxb_l, g_auxb_r});
            for (int i = 0; i < SPF; i++) { g_left[i] += g_auxb_l[i]; g_right[i] += g_auxb_r[i]; }
            break;
        }
        case 0x11: set_main_from(rd32(), true); break;
        case 0x12: p += 8; break;  // compressor: not in this ucode version
        case 0x13: {  // upload AuxA L/R/S and AuxB S; download and add main L, main R, AuxB L, AuxB R
            uint32_t auxa_up = rd32(), auxb_s_up = rd32(), main_l = rd32(), main_r = rd32(), auxb_l = rd32(), auxb_r = rd32();
            write_bufs(auxa_up, {g_auxa_l, g_auxa_r, g_auxa_s});
            write_bufs(auxb_s_up, {g_auxb_s});
            add_bufs(main_l, {g_left});
            add_bufs(main_r, {g_right});
            add_bufs(auxb_l, {g_auxb_l});
            add_bufs(auxb_r, {g_auxb_r});
            break;
        }
        default: {
            // Say so once, with the list itself, so a command this parser does not know
            // (or whose length it has wrong) can be identified.
            static int reported;
            if (reported++ < 2) {
                std::string words;
                for (uint32_t q = addr; q < addr + 2 * size && q < addr + 128; q += 2) {
                    char b[8]; snprintf(b, sizeof b, "%04X ", mem_r16(q)); words += b;
                }
                LOG(LOG_DSP, "AX: unknown command %04X at +%X in list %08X (%u words): %s", cmd, (unsigned)(p - 2 - addr), addr, size, words.c_str());
            }
            return;
        }
        }
    }
}
