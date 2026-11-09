// High-level emulation of the JAudio synthesizer ucode, the DSP program JSystem's audio
// library runs in place of AX: its mail protocol and its synthesizer.
//
// The protocol, as the CPU side drives it:
//
// - At boot the ucode sends DSP_INIT (interrupting) and then a handshake mail, F3551111,
//   which the library's task-init callback waits for.
// - A command arrives as a count mail (1..0x7FFF) followed by that many words. The first
//   word's top byte is the opcode, with bit 7 set; the rest are its arguments. Each command
//   is acknowledged with DSP_SYNC (interrupting) and F355xxxx, xxxx being the top half of
//   the command word: the library matches that against the command it is waiting for.
// - Opcode 1 tells the ucode how many voices a frame has (the low half of the word).
// - Opcode 2 renders frames: their count in bits 16-23, the output volume in the low half,
//   then the left and right output buffers. It is not acknowledged. Voices are rendered as
//   the CPU allows them: a zero mail and then a sync mail, (group << 16) | flags, releases
//   voices 16*group..16*group+15, a set bit (bit 15 first) for each voice to mix. When a
//   frame's voices are all done the ucode sends DSP_SYNC and F355FF00, on which the CPU
//   sends the next frame's syncs; after the last frame, DSP_FRAME_END, on which the CPU
//   answers CDD10003, and only then are further commands carried out.
// - CDD10001 hands the DSP to another ucode (the boot ROM mails that load it follow),
//   CDD10002 resets to the boot ROM, CDD10000 halts.
//
// An earlier build of the ucode speaks an earlier protocol, with the same synthesizer:
//
// - At boot it sends one mail, which the library polls for.
// - A command is its words alone, with no count mail: the opcode says how many follow
//   (0: none, a wait; 1: four, as above; 2: two, the output buffers; 0xB: one, the memory
//   card unlock's parameters). Each is answered by one mail, not interrupting, which the
//   library polls for: any mail but 88881357.
// - Opcode 2 only sets up the frame. Each zero mail then releases one subframe, every
//   voice in it, and the ucode answers with 88881357 and the DSP interrupt; the library
//   counts the interrupts, and after the last subframe sends the next frame's opcode 2.
//
// The synthesizer. Opcode 1's words are the voice parameter blocks (0x180 bytes each), the
// resampling filter (64 phases of 4 taps, then four 64-sample oscillator patterns and a
// quarter sine), the 16 ADPCM coefficient pairs and the four effect lines. A frame is
// 0x50 samples at 32 kHz: each released voice fetches samples from ARAM (4-bit or 2-bit
// ADPCM in 9- or 5-byte blocks of 16, PCM8, PCM16) or runs an oscillator, is resampled at
// its pitch (4.12), and is mixed into buses -- either through its six mixer slots (bus,
// target and current volume, 1.15, ramped across the frame) or, when its auto mixer is on,
// placed by the ucode into the four front/rear quadrants by pan and depth. The effect
// lines delay their send buses by a ring of frames in main memory and mix them back
// through an 8-tap filter. The front pair, at the command's output volume (4.12), is the
// frame's output. The ucode keeps its place in the block (next ARAM address, samples
// left, the resampler's fraction), and the CPU reads and moves those to stream music
// through a ring in ARAM. GCN_JAUDIO_WAV=<path> records the output at 32 kHz;
// GCN_JAUDIO_STATS=1 prints levels and what the game uses; GCN_JAUDIO_VOICES=1 each voice as
// it starts, and where a streamed voice is in its ring.
#include "../runtime.h"
#include "dsp_hle.h"
#include "jaudio.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

namespace {

enum : uint32_t {
    DSP_INIT = 0xDCD10000, DSP_RESUME = 0xDCD10001, DSP_SYNC = 0xDCD10004, DSP_FRAME_END = 0xDCD10005,
    HANDSHAKE = 0xF3551111, ACK = 0xF3550000, FRAME_DONE = 0xF355FF00,
};

// Fingerprints (dsp_hle.cpp's ucode_hash) of the builds of this ucode that speak the
// protocol above, and of the earlier build that speaks the earlier one.
constexpr uint32_t kHashes[] = {0x2FCDF1EC};
constexpr uint32_t kEarlyHashes[] = {0x42F64AC4};
constexpr uint32_t EARLY_HALT = 0x88881357;
bool g_early;

enum class State { Waiting, Command, Sync, Halted };
State g_state;
std::vector<uint32_t> g_batch;
uint32_t g_batch_len;
std::deque<std::vector<uint32_t>> g_pending;
bool g_can_execute;

void ack(uint32_t cmd) {
    if (g_early) {
        dsp_send_mail(ACK | (cmd >> 16), false);
        return;
    }
    dsp_send_mail(DSP_SYNC, true);
    dsp_send_mail(ACK | (cmd >> 16), false);
}

// Rendering.
bool g_rendering;
uint32_t g_frames, g_frame;
uint32_t g_voices_per_frame, g_voice, g_released;
uint16_t g_sync_flags[16];
uint32_t g_out_left, g_out_right;
uint16_t g_out_volume;
bool g_frame_open;

// What opcode 1 handed over: the voice parameter blocks, the resampling filter (and the
// oscillator patterns after it), the ADPCM coefficients and the effect lines.
uint32_t g_pb_base, g_fx_base;
int16_t g_res_coef[64][4];
int16_t g_patterns[4][64];
int16_t g_adpcm_coef[16][2];
int16_t g_sine[128];  // a quarter period, for the auto mixer's pan law

constexpr int FRAME = 0x50;      // samples a frame, at 32 kHz
constexpr int PB_BYTES = 0x180;  // one voice's parameter block
constexpr int MAX_VOICES = 256;

// The mix buses, named by the DSP data-memory address of the buffer each one is. A voice's
// six mixer slots each name one and give it a volume.
enum Bus { LEFT, RIGHT, FX0, FX1, FX2, FX3, BUS_0CA0, BUS_0F40, BUS_0FA0, BUS_0B00, BUS_09A0, NUM_BUSES };
constexpr uint16_t kBusAddr[NUM_BUSES] = {0x0D00, 0x0D60, 0x0DC0, 0x0E20, 0x0E80, 0x0EE0,
                                          0x0CA0, 0x0F40, 0x0FA0, 0x0B00, 0x09A0};
int32_t g_bus[NUM_BUSES][FRAME];
uint32_t g_bus_used;  // which buses a voice has mixed into since boot, for the log

int bus_index(uint16_t addr) {
    for (int i = 0; i < NUM_BUSES; i++)
        if (kBusAddr[i] == addr) return i;
    return -1;
}

// A parameter block, as big-endian halfwords in guest memory.
struct PB {
    uint32_t addr;
    uint16_t w[PB_BYTES / 2];
    void load(uint32_t a) {
        addr = a;
        for (int i = 0; i < PB_BYTES / 2; i++) w[i] = mem_r16(a + 2 * i);
    }
    uint16_t u16(uint32_t off) const { return w[off / 2]; }
    int16_t s16(uint32_t off) const { return (int16_t)w[off / 2]; }
    uint32_t u32(uint32_t off) const { return (uint32_t)w[off / 2] << 16 | w[off / 2 + 1]; }
    void put16(uint32_t off, uint16_t v) { w[off / 2] = v; mem_w16(addr + off, v); }
    void put32(uint32_t off, uint32_t v) { put16(off, (uint16_t)(v >> 16)); put16(off + 2, (uint16_t)v); }
};

// The parameter block's fields (byte offsets), as JSystem's DSP interface names and uses
// them; the ucode reads the CPU's and keeps its own in the same block.
enum : uint32_t {
    PB_ACTIVE = 0x00, PB_FINISHED = 0x02, PB_PITCH = 0x04, PB_RESET = 0x08, PB_PAUSE = 0x0C,
    PB_MIXERS = 0x10,  // six of {bus, target volume, current volume, flags}
    // The auto mixer: pan (high byte) and rear (low byte), each 0..127; the effect send in
    // the high byte; the current and target volume; and whether it is on.
    PB_AM_PAN = 0x50, PB_AM_FX = 0x52, PB_AM_VOLUME = 0x54, PB_AM_TARGET = 0x56, PB_AM_ON = 0x58,
    PB_POS_FRAC = 0x60, PB_BLOCK_SAMPLES = 0x64, PB_START = 0x68,
    PB_LOOP_LEFT = 0x6C,  // ucode: (high half) samples left before the loop point
    PB_CUR_ADDR = 0x70,   // ucode: the next byte of sample data to fetch, in ARAM
    PB_REMAINING = 0x74,  // ucode: samples left to fetch before the end or the loop point
    PB_SOURCE = 0x100, PB_LOOP = 0x102, PB_LOOP_YN1 = 0x104, PB_LOOP_YN2 = 0x106,
    PB_FORCE_STOP = 0x10A, PB_LOOP_START = 0x110, PB_LOOP_END = 0x114, PB_BASE = 0x118,
};

// Where the sample data comes from (PB_SOURCE). Wave sources hold the number of bytes a
// block of PB_BLOCK_SAMPLES samples takes; the oscillators are small numbers of their own.
enum : uint16_t {
    SRC_SQUARE = 0, SRC_SAW = 1, SRC_SQUARE_25 = 3, SRC_PATTERN_1 = 4, SRC_ADPCM2 = 5,
    SRC_PATTERN_0 = 7, SRC_PCM8 = 8, SRC_ADPCM4 = 9, SRC_PATTERN_0_STEP = 10, SRC_PATTERN_2 = 11,
    SRC_PATTERN_3 = 12, SRC_PCM16 = 16,
    // The earlier build's streamed music: PCM16 from a ring in main memory that the CPU
    // refills. PB_START's high half is the ucode's place in the ring; PB_LOOP_LEFT's high
    // half is how many samples are left before it wraps, which is what the CPU reads to
    // tell which part of the ring is playing and so may not be refilled (left at 0, every
    // part looked free: the CPU decoded the whole stream into the ring during its first
    // lap and the voice played the same 1.4 s of ring over and over); the ring is
    // PB_LOOP_END's high half samples long; PB_REMAINING counts down the stream's samples.
    SRC_DIRECT_PCM16 = 0x21,
};

// What the ucode keeps for a voice between frames beyond what the block holds: the ADPCM
// predictor and the decoded block in progress, and the resampler's last input samples.
struct Voice {
    int16_t yn1, yn2;
    int16_t block[16];
    int block_pos, block_len;
    int16_t hist[4];
    uint32_t osc_pos;
};
Voice g_voices[MAX_VOICES];

int16_t clamp16(int32_t v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

// One ADPCM block: a header byte (scale in the high nibble, coefficient pair in the low)
// and sixteen 4-bit or 2-bit residuals.
void decode_adpcm(Voice& v, uint32_t aram, int bytes) {
    auto byte = [&](uint32_t i) { return g_aram[(aram + i) & (ARAM_SIZE - 1)]; };
    const uint8_t hdr = byte(0);
    const int32_t scale = 1 << (hdr >> 4);
    const int16_t* c = g_adpcm_coef[hdr & 15];
    for (int i = 0; i < 16; i++) {
        int32_t r;
        if (bytes == 9) {
            int n = (byte(1 + i / 2) >> ((i & 1) ? 0 : 4)) & 15;
            r = (n >= 8 ? n - 16 : n) << 11;
        } else {
            int n = (byte(1 + i / 4) >> (6 - 2 * (i & 3))) & 3;
            r = (n >= 2 ? n - 4 : n) << 13;
        }
        int16_t s = clamp16((scale * r + c[0] * v.yn1 + c[1] * v.yn2) >> 11);
        v.yn2 = v.yn1;
        v.yn1 = s;
        v.block[i] = s;
    }
}

// The next sample of a wave voice, at its own rate. Returns false once a voice that does
// not loop has run out.
bool next_sample(PB& pb, Voice& v, int16_t& out) {
    const uint16_t src = pb.u16(PB_SOURCE);
    const bool adpcm = src == SRC_ADPCM4 || src == SRC_ADPCM2;
    if (src == SRC_DIRECT_PCM16) {
        const uint32_t remaining = pb.u32(PB_REMAINING);
        if (remaining == 0) return false;
        const uint32_t ring = pb.u32(PB_LOOP_END) >> 16;
        uint32_t pos = pb.u32(PB_START) >> 16;
        if (ring && pos >= ring) pos = 0;
        out = (int16_t)mem_r16(0x80000000u | ((pb.u32(PB_BASE) + 2 * pos) & 0x01FFFFFF));
        pos++;
        if (ring && pos >= ring) pos = 0;
        pb.put32(PB_START, pos << 16);
        pb.put32(PB_LOOP_LEFT, (ring ? ring - pos : 0) << 16);
        pb.put32(PB_REMAINING, remaining - 1);
        return true;
    }
    if (v.block_pos >= v.block_len) {
        uint32_t remaining = pb.u32(PB_REMAINING);
        uint32_t skip = 0;
        if (remaining == 0) {
            if (!pb.u16(PB_LOOP)) return false;
            // Back to the loop point: the block that holds it, with the predictor the
            // block carries for it, skipping into the block as far as the loop starts.
            const uint32_t start = pb.u32(PB_LOOP_START);
            const uint32_t per = adpcm ? 16 : 1;
            pb.put32(PB_CUR_ADDR, pb.u32(PB_BASE) + start / per * (adpcm ? src : src / 8));
            remaining = pb.u32(PB_LOOP_END) - start + start % per;
            skip = start % per;
            if (adpcm) {
                v.yn1 = pb.s16(PB_LOOP_YN1);
                v.yn2 = pb.s16(PB_LOOP_YN2);
            }
            if (remaining <= skip) return false;
        }
        const uint32_t a = pb.u32(PB_CUR_ADDR);
        uint32_t n;
        if (adpcm) {
            decode_adpcm(v, a, src);
            n = remaining < 16 ? remaining : 16;
            pb.put32(PB_CUR_ADDR, a + src);
        } else if (src == SRC_PCM16) {
            const uint8_t* p = g_aram + (a & (ARAM_SIZE - 2));
            v.block[0] = (int16_t)(p[0] << 8 | p[1]);
            n = 1;
            pb.put32(PB_CUR_ADDR, a + 2);
        } else {  // PCM8
            v.block[0] = (int16_t)((int8_t)g_aram[a & (ARAM_SIZE - 1)] << 8);
            n = 1;
            pb.put32(PB_CUR_ADDR, a + 1);
        }
        pb.put32(PB_REMAINING, remaining - n);
        v.block_pos = (int)skip;
        v.block_len = (int)n;
    }
    out = v.block[v.block_pos++];
    return true;
}

// Fills one frame of a voice at the output rate. Returns false when a voice that does not
// loop ran out during it (the rest of the frame is silence).
bool voice_samples(PB& pb, Voice& v, int16_t* out) {
    const uint16_t src = pb.u16(PB_SOURCE);
    const uint32_t ratio = pb.u16(PB_PITCH);
    if (src == SRC_ADPCM4 || src == SRC_ADPCM2 || src == SRC_PCM8 || src == SRC_PCM16 || src == SRC_DIRECT_PCM16) {
        // Resampled with the ucode's own filter: four taps in 64 phases, interpolating
        // between the second and third of the last four input samples.
        uint32_t pos = pb.u16(PB_POS_FRAC) & 0xFFF;
        const uint32_t need = (pos + ratio * FRAME) >> 12;
        static int16_t in[4 + FRAME * 16];
        memcpy(in, v.hist, sizeof(v.hist));
        bool alive = true;
        for (uint32_t i = 0; i < need; i++) {
            int16_t s = 0;
            if (alive && !next_sample(pb, v, s)) alive = false;
            in[4 + i] = s;
        }
        uint32_t j = 0;
        for (int i = 0; i < FRAME; i++) {
            const int16_t* c = g_res_coef[(pos >> 6) & 63];
            int32_t acc = 0;
            for (int k = 0; k < 4; k++) acc += c[k] * in[j + k];
            out[i] = clamp16(acc >> 15);
            pos += ratio;
            j += pos >> 12;
            pos &= 0xFFF;
        }
        memcpy(v.hist, in + need, sizeof(v.hist));
        pb.put16(PB_POS_FRAC, (uint16_t)pos);
        return alive;
    }
    // Oscillators: one period is 64 steps of the ratio, at the output rate.
    for (int i = 0; i < FRAME; i++) {
        const uint32_t ph = (v.osc_pos >> 12) & 63;
        int16_t s = 0;
        switch (src) {
        case SRC_SQUARE: s = ph < 32 ? 0x4000 : -0x4000; break;
        case SRC_SQUARE_25: s = ph < 16 ? 0x4000 : -0x4000; break;
        case SRC_SAW: s = (int16_t)((ph << 10) - 0x8000); break;
        case SRC_PATTERN_0: case SRC_PATTERN_0_STEP: s = g_patterns[0][ph]; break;
        case SRC_PATTERN_1: s = g_patterns[1][ph]; break;
        case SRC_PATTERN_2: s = g_patterns[2][ph]; break;
        case SRC_PATTERN_3: s = g_patterns[3][ph]; break;
        default: break;
        }
        out[i] = s;
        v.osc_pos += ratio;
    }
    return true;
}

// GCN_JAUDIO_STATS=1 prints, once a second, the loudest mixer volume, the voices mixed and
// the loudest bus sample before the output volume.
const bool g_stats = getenv("GCN_JAUDIO_STATS") != nullptr;
// GCN_JAUDIO_VOICES=1 prints each voice as it starts: its source, pitch, where its samples
// are and how it loops, and its first two mixer slots.
const bool g_voice_log = getenv("GCN_JAUDIO_VOICES") != nullptr;
int32_t g_stat_vmax, g_stat_bmax;
uint32_t g_stat_mixes, g_stat_frames;
uint32_t g_stat_srcs, g_stat_filters;

void render_voice(uint32_t index) {
    if (!g_pb_base || index >= MAX_VOICES) return;
    PB pb;
    pb.load(g_pb_base + index * PB_BYTES);
    if (!pb.u16(PB_ACTIVE) || pb.u16(PB_FINISHED)) return;
    Voice& v = g_voices[index];
    if (pb.u16(PB_RESET)) {
        // A voice just started: fetching begins at the block's base address (moved on
        // already for an ADPCM wave started part way in; PCM skips PB_START samples).
        memset(&v, 0, sizeof(v));
        const uint16_t src = pb.u16(PB_SOURCE);
        if (g_voice_log)
            fprintf(stderr, "[jaudio] voice %2u start: src %02X pitch %04X base %08X start %08X end %08X loop %u@%08X am %u fx %04X mix %04X@%04X %04X@%04X\n",
                    index, src, pb.u16(PB_PITCH), pb.u32(PB_BASE), pb.u32(PB_START), pb.u32(PB_LOOP_END), pb.u16(PB_LOOP),
                    pb.u32(PB_LOOP_START), pb.u16(PB_AM_ON), pb.u16(PB_AM_FX), pb.u16(PB_MIXERS), pb.u16(PB_MIXERS + 2),
                    pb.u16(PB_MIXERS + 8), pb.u16(PB_MIXERS + 10));
        if (src == SRC_DIRECT_PCM16) {
            // The CPU set the stream's length itself; the ring starts at its beginning.
            pb.put32(PB_START, 0);
            pb.put32(PB_LOOP_LEFT, pb.u32(PB_LOOP_END) & 0xFFFF0000u);
        } else {
            uint32_t base = pb.u32(PB_BASE), end = pb.u32(PB_LOOP_END);
            if (src == SRC_PCM16 || src == SRC_PCM8) {
                const uint32_t skip = pb.u32(PB_START);
                base += skip * (src / 8);
                end = end > skip ? end - skip : 0;
            }
            pb.put32(PB_CUR_ADDR, base);
            pb.put32(PB_REMAINING, end);
        }
        pb.put16(PB_POS_FRAC, 0);
        pb.put16(PB_RESET, 0);
    }
    if (g_voice_log && pb.u16(PB_SOURCE) == SRC_DIRECT_PCM16) {
        static uint32_t n[MAX_VOICES];
        if (n[index]++ % 200 == 0) {
            const uint32_t ring = pb.u32(PB_LOOP_END) >> 16, base = pb.u32(PB_BASE);
            uint32_t sum[4] = {};
            for (uint32_t i = 0; i < ring; i++) sum[i * 4 / ring] = sum[i * 4 / ring] * 31 + mem_r16(0x80000000u | ((base + 2 * i) & 0x01FFFFFF));
            fprintf(stderr, "[jaudio] voice %2u stream: pos %05X remaining %08X paused %u ring sums %08X %08X %08X %08X\n", index,
                    pb.u32(PB_START) >> 16, pb.u32(PB_REMAINING), pb.u16(PB_PAUSE), sum[0], sum[1], sum[2], sum[3]);
        }
    }
    if (pb.u16(PB_PAUSE)) return;
    if (g_stats) {
        g_stat_srcs |= 1u << (pb.u16(PB_SOURCE) & 31);
        g_stat_filters |= 1u << (pb.u16(0x108) & 31) | (pb.u16(0x108) & 0x20 ? 0x80000000u : 0);
    }
    int16_t samples[FRAME];
    const bool alive = voice_samples(pb, v, samples);
    const bool stopping = pb.u16(PB_FORCE_STOP) != 0;
    auto mix = [&](int b, int32_t cur, int32_t target) {
        g_bus_used |= 1u << b;
        if (g_stats) { g_stat_vmax = std::max<int32_t>(g_stat_vmax, target); g_stat_mixes++; }
        // Volumes are 1.15; a change ramps across the frame.
        for (int i = 0; i < FRAME; i++) {
            const int32_t vol = cur + (target - cur) * i / FRAME;
            g_bus[b][i] += samples[i] * vol >> 15;
        }
    };
    if (pb.u16(PB_AM_ON)) {
        // The auto mixer (the library's surround mode, where it sets no mixer volumes and
        // the slots hold whatever was on its stack): the ucode places the voice itself,
        // pan across and rear front-to-back with a sine law, into the four quadrants, and
        // sends the effect share to the effect buses the voice is connected to.
        const uint16_t pos = pb.u16(PB_AM_PAN);
        const int x = (pos >> 8) & 0x7F, y = pos & 0x7F;
        const int32_t right = g_sine[x], left = g_sine[x ^ 0x7F], back = g_sine[y], front = g_sine[y ^ 0x7F];
        const int32_t cur = pb.u16(PB_AM_VOLUME), target = stopping ? 0 : pb.u16(PB_AM_TARGET);
        const struct { Bus bus; int32_t gain; } quad[4] = {
            {LEFT, left * front >> 15}, {BUS_0F40, left * back >> 15}, {RIGHT, right * front >> 15}, {BUS_0CA0, right * back >> 15}};
        for (const auto& q : quad)
            if (q.gain) mix(q.bus, q.gain * cur >> 15, q.gain * target >> 15);
        // The effect lines are a stereo pair of sends; a voice names the ones it uses.
        const int32_t fx = (pb.u16(PB_AM_FX) >> 8) & 0x7F;
        if (fx) {
            for (int m = 0; m < 6; m++) {
                const int b = bus_index(pb.u16(PB_MIXERS + 8 * m));
                if (b >= FX0 && b <= FX3) mix(b, cur * fx / 127 * 0x5A82 >> 15, target * fx / 127 * 0x5A82 >> 15);
            }
        }
        pb.put16(PB_AM_VOLUME, (uint16_t)target);
        if (!alive || stopping) pb.put16(PB_FINISHED, 1);
        return;
    }
    for (int m = 0; m < 6; m++) {
        const uint32_t o = PB_MIXERS + 8 * m;
        const uint16_t bus = pb.u16(o);
        if (!bus) continue;
        const int32_t target = stopping ? 0 : pb.s16(o + 2), cur = pb.s16(o + 4);
        const int b = bus_index(bus);
        if (b >= 0) mix(b, cur, target);
        pb.put16(o + 4, (uint16_t)target);
    }
    if (!alive || stopping) pb.put16(PB_FINISHED, 1);
}

// The effect lines (reverb, echo): each delays its input bus by a ring of frames in main
// memory and mixes it back, through an 8-tap filter, into up to two buses at a volume.
struct FxLine {
    int16_t last[8];
    uint32_t frame;
};
FxLine g_fx[4];

struct FxPB {
    uint16_t enabled, frames;
    uint32_t buffer;
    uint16_t dest[2], volume[2];
    int16_t coef[8];
    bool load(int i) {
        if (!g_fx_base) return false;
        const uint32_t a = g_fx_base + i * 0x20;
        enabled = mem_r16(a);
        frames = mem_r16(a + 2);
        buffer = mem_r32(a + 4);
        dest[0] = mem_r16(a + 8);
        volume[0] = mem_r16(a + 0xA);
        dest[1] = mem_r16(a + 0xC);
        volume[1] = mem_r16(a + 0xE);
        for (int k = 0; k < 8; k++) coef[k] = (int16_t)mem_r16(a + 0x10 + 2 * k);
        return enabled && frames && buffer;
    }
};

// GCN_JAUDIO_NOFX=1 leaves the effect lines out, to tell their contribution apart.
const bool g_no_fx = getenv("GCN_JAUDIO_NOFX") != nullptr;

void fx_return() {
    if (g_no_fx) return;
    for (int i = 0; i < 4; i++) {
        FxPB fx;
        if (!fx.load(i)) continue;
        FxLine& l = g_fx[i];
        l.frame %= fx.frames;
        const uint32_t a = fx.buffer + l.frame * FRAME * 2;
        int16_t buf[8 + FRAME];
        memcpy(buf, l.last, sizeof(l.last));
        for (int k = 0; k < FRAME; k++) buf[8 + k] = (int16_t)mem_r16(a + 2 * k);
        memcpy(l.last, buf + FRAME, sizeof(l.last));
        int16_t filtered[FRAME];
        for (int k = 0; k < FRAME; k++) {
            int32_t acc = 0;
            for (int t = 0; t < 8; t++) acc += buf[k + t] * fx.coef[t];
            filtered[k] = clamp16(acc >> 15);
        }
        for (int d = 0; d < 2; d++) {
            const int b = bus_index(fx.dest[d]);
            if (b < 0) continue;
            for (int k = 0; k < FRAME; k++) g_bus[b][k] += filtered[k] * (int16_t)fx.volume[d] >> 15;
        }
    }
}

void fx_send() {
    for (int i = 0; i < 4; i++) {
        FxPB fx;
        if (!fx.load(i)) continue;
        FxLine& l = g_fx[i];
        const uint32_t a = fx.buffer + (l.frame % fx.frames) * FRAME * 2;
        for (int k = 0; k < FRAME; k++) mem_w16(a + 2 * k, (uint16_t)clamp16(g_bus[FX0 + i][k]));
        l.frame = (l.frame + 1) % fx.frames;
    }
}

FILE* g_wav;
uint32_t g_wav_bytes;

void wav_write(const int16_t* lr, int frames) {
    static bool opened;
    if (!opened) {
        opened = true;
        if (const char* p = getenv("GCN_JAUDIO_WAV")) g_wav = fopen(p, "wb");
    }
    if (!g_wav) return;
    g_wav_bytes += frames * 4;
    auto hdr = [&] {
        uint32_t rate = 32000, v;
        fseek(g_wav, 0, SEEK_SET);
        fwrite("RIFF", 1, 4, g_wav); v = 36 + g_wav_bytes; fwrite(&v, 4, 1, g_wav);
        fwrite("WAVEfmt ", 1, 8, g_wav); v = 16; fwrite(&v, 4, 1, g_wav);
        uint16_t f[2] = {1, 2}; fwrite(f, 2, 2, g_wav);
        fwrite(&rate, 4, 1, g_wav); v = rate * 4; fwrite(&v, 4, 1, g_wav);
        uint16_t g[2] = {4, 16}; fwrite(g, 2, 2, g_wav);
        fwrite("data", 1, 4, g_wav); fwrite(&g_wav_bytes, 4, 1, g_wav);
        fseek(g_wav, 0, SEEK_END);
    };
    if (g_wav_bytes == (uint32_t)frames * 4) hdr();
    fwrite(lr, 4, frames, g_wav);
    hdr();
}

void open_frame() {
    memset(g_bus, 0, sizeof(g_bus));
    fx_return();
    g_frame_open = true;
}

void finish_frame() {
    fx_send();
    // The front pair is the output. The rear pair (0x0F40 left, 0x0CA0 right) is folded
    // into it a little quieter rather than matrix-encoded, which a stereo listener would
    // only hear as phase smear; 0x0FA0, which the library keeps in stereo, goes to both.
    int16_t lr[FRAME * 2];
    if (g_stats) {
        for (int i = 0; i < FRAME; i++)
            g_stat_bmax = std::max({g_stat_bmax, std::abs(g_bus[LEFT][i]), std::abs(g_bus[RIGHT][i])});
        if (++g_stat_frames == 400) {
            fprintf(stderr, "[jaudio] vol max %04X, %u mixes/frame, bus peak %d, out vol %04X, sources %08X, filters %08X\n",
                    g_stat_vmax, g_stat_mixes / 400, g_stat_bmax, g_out_volume, g_stat_srcs, g_stat_filters);
            for (int i = 0; i < 4; i++) {
                FxPB fx;
                if (fx.load(i))
                    fprintf(stderr, "[jaudio]   fx%d: %u frames at %08X -> %04X@%04X %04X@%04X coef %d %d %d %d %d %d %d %d\n", i, fx.frames,
                            fx.buffer, fx.dest[0], fx.volume[0], fx.dest[1], fx.volume[1], fx.coef[0], fx.coef[1], fx.coef[2],
                            fx.coef[3], fx.coef[4], fx.coef[5], fx.coef[6], fx.coef[7]);
            }
            g_stat_vmax = g_stat_bmax = 0;
            g_stat_mixes = g_stat_frames = 0;
        }
    }
    for (int i = 0; i < FRAME; i++) {
        const int32_t c = g_bus[BUS_0FA0][i] * 0x5A82 >> 15;
        const int32_t l = g_bus[LEFT][i] + c + (g_bus[BUS_0F40][i] * 0x5A82 >> 15);
        const int32_t r = g_bus[RIGHT][i] + c + (g_bus[BUS_0CA0][i] * 0x5A82 >> 15);
        lr[2 * i] = clamp16(l * g_out_volume >> 12);
        lr[2 * i + 1] = clamp16(r * g_out_volume >> 12);
    }
    const uint32_t bytes = g_out_right > g_out_left ? (g_out_right - g_out_left) / g_frames : 0;
    for (int side = 0; side < 2; side++) {
        const uint32_t a = (side ? g_out_right : g_out_left) + g_frame * bytes;
        const uint32_t n = dma_fit("jaudio output", a, bytes) / 2;
        for (uint32_t i = 0; i < n && i < FRAME; i++) mem_w16(0x80000000u | a + 2 * i, (uint16_t)lr[2 * i + side]);
    }
    wav_write(lr, FRAME);
    g_frame_open = false;
    static uint32_t logged;
    if (g_bus_used != logged) {
        logged = g_bus_used;
        LOG(LOG_DSP, "jaudio: buses in use %03X", g_bus_used);
    }
}

// Renders as far as the released voices allow.
void render() {
    while (g_frame < g_frames) {
        if (!g_frame_open) open_frame();
        while (g_voice < g_voices_per_frame) {
            if (g_voice >= g_released) return;
            const uint16_t flags = g_sync_flags[(g_voice >> 4) & 15];
            if ((flags >> (15 - (g_voice & 15))) & 1) render_voice(g_voice);
            g_voice++;
        }
        finish_frame();
        g_frame++;
        g_voice = 0;
        g_released = 0;
        dsp_send_mail(DSP_SYNC, true);
        dsp_send_mail(FRAME_DONE, false);
    }
    g_rendering = false;
    g_can_execute = false;
    dsp_send_mail(DSP_FRAME_END, true);
}

// The earlier protocol's release: one subframe, every voice.
void render_subframe() {
    if (!g_rendering || g_frame >= g_frames) {
        LOG(LOG_DSP, "jaudio: subframe released outside a frame");
        return;
    }
    open_frame();
    for (uint32_t v = 0; v < g_voices_per_frame; v++) render_voice(v);
    finish_frame();
    if (++g_frame == g_frames) g_rendering = false;
    dsp_send_mail(EARLY_HALT, true);
}

void execute(const std::vector<uint32_t>& cmd) {
    uint32_t op = (cmd[0] >> 24) & 0x7F;
    auto arg = [&](size_t i) { return i < cmd.size() ? cmd[i] : 0u; };
    switch (op) {
    case 0x01: {
        // The voices a frame has, their parameter blocks, the resampling filter (with the
        // oscillators' patterns after it), the ADPCM coefficients and the effect lines.
        g_voices_per_frame = cmd[0] & 0xFFFF;
        g_pb_base = arg(1);
        const uint32_t res = arg(2), coef = arg(3);
        g_fx_base = arg(4);
        for (int i = 0; i < 64 * 4; i++) g_res_coef[i / 4][i % 4] = (int16_t)mem_r16(res + 2 * i);
        for (int i = 0; i < 4 * 64; i++) g_patterns[i / 64][i % 64] = (int16_t)mem_r16(res + 0x200 + 2 * i);
        for (int i = 0; i < 32; i++) g_adpcm_coef[i / 2][i % 2] = (int16_t)mem_r16(coef + 2 * i);
        for (int i = 0; i < 128; i++) g_sine[i] = (int16_t)mem_r16(res + 0x400 + 2 * i);
        memset(g_voices, 0, sizeof(g_voices));
        memset(g_fx, 0, sizeof(g_fx));
        LOG(LOG_DSP, "jaudio: %u voices at %08X, filter %08X, ADPCM %08X, effects %08X", g_voices_per_frame, g_pb_base, res,
            coef, g_fx_base);
        ack(cmd[0]);
        break;
    }
    case 0x02:
        g_frames = (cmd[0] >> 16) & 0xFF;
        g_out_volume = (uint16_t)cmd[0];
        g_out_left = arg(1) & 0x01FFFFFF;
        g_out_right = arg(2) & 0x01FFFFFF;
        g_frame = g_voice = g_released = 0;
        g_frame_open = false;
        g_rendering = true;
        if (g_early) ack(cmd[0]);
        else render();
        break;
    case 0x00:  // the earlier protocol's wait
    case 0x0B:  // the earlier protocol's memory card unlock: the card does not check the answer
        ack(cmd[0]);
        break;
    default:
        LOG(LOG_DSP, "jaudio: command %02X (%zu words) not implemented", op, cmd.size());
        ack(cmd[0]);
        break;
    }
}

void run_pending() {
    while (g_can_execute && !g_rendering && !g_pending.empty()) {
        std::vector<uint32_t> cmd = std::move(g_pending.front());
        g_pending.pop_front();
        execute(cmd);
    }
}

}  // namespace

bool jaudio_is_ucode(uint32_t hash) {
    for (uint32_t h : kHashes)
        if (h == hash) return true;
    for (uint32_t h : kEarlyHashes)
        if (h == hash) return true;
    return false;
}

void jaudio_boot(uint32_t hash, bool resume) {
    g_early = false;
    for (uint32_t h : kEarlyHashes)
        if (h == hash) g_early = true;
    g_state = State::Waiting;
    if (g_early) {
        g_rendering = false;
        dsp_send_mail(DSP_INIT, false);
        return;
    }
    if (resume) {
        dsp_send_mail(DSP_RESUME, true);
        return;
    }
    g_pending.clear();
    g_can_execute = true;
    g_rendering = false;
    dsp_send_mail(DSP_INIT, true);
    dsp_send_mail(HANDSHAKE, false);
}

namespace {

// The earlier protocol: how many words follow each opcode.
int early_args(uint32_t op) {
    switch (op) {
    case 0x00: return 0;
    case 0x01: return 4;
    case 0x02: return 2;
    case 0x0B: return 1;
    default: return -1;
    }
}

JAudioNext early_mail(uint32_t mail) {
    if (g_state == State::Command) {
        g_batch.push_back(mail);
        if (g_batch.size() == g_batch_len) {
            g_state = State::Waiting;
            execute(g_batch);
        }
        return JAudioNext::Stay;
    }
    if (mail == 0) {
        render_subframe();
        return JAudioNext::Stay;
    }
    if ((mail >> 16) == 0xCDD1) {
        switch (mail & 0xFFFF) {
        case 1: g_state = State::Halted; return JAudioNext::NewUcode;
        case 2: g_state = State::Halted; return JAudioNext::Reset;
        default: return JAudioNext::Stay;
        }
    }
    const int n = (mail & 0x80000000u) ? early_args((mail >> 24) & 0x7F) : -1;
    if (n < 0) {
        LOG(LOG_DSP, "jaudio: unexpected mail %08X", mail);
        return JAudioNext::Stay;
    }
    g_batch.assign(1, mail);
    g_batch_len = (uint32_t)n + 1;
    if (n == 0) execute(g_batch);
    else g_state = State::Command;
    return JAudioNext::Stay;
}

}  // namespace

JAudioNext jaudio_mail(uint32_t mail) {
    if (g_early && g_state != State::Halted) return early_mail(mail);
    switch (g_state) {
    case State::Halted:
        LOG(LOG_DSP, "jaudio: mail %08X while halted", mail);
        return JAudioNext::Stay;
    case State::Command:
        g_batch.push_back(mail);
        if (g_batch.size() == g_batch_len) {
            g_pending.push_back(std::move(g_batch));
            g_batch.clear();
            g_state = State::Waiting;
            run_pending();
        }
        return JAudioNext::Stay;
    case State::Sync: {
        g_state = State::Waiting;
        uint32_t group = (mail >> 16) & 0xF;
        g_sync_flags[group] = (uint16_t)mail;
        g_released = (group + 1) * 16;
        if (g_rendering) render();
        return JAudioNext::Stay;
    }
    case State::Waiting:
        break;
    }
    if (mail & 0x80000000u) {
        if ((mail >> 16) != 0xCDD1) {
            LOG(LOG_DSP, "jaudio: unexpected mail %08X", mail);
            return JAudioNext::Stay;
        }
        switch (mail & 0xFFFF) {
        case 1: g_state = State::Halted; return JAudioNext::NewUcode;
        case 2: g_state = State::Halted; return JAudioNext::Reset;
        case 3: g_can_execute = true; run_pending(); return JAudioNext::Stay;
        default: g_state = State::Halted; return JAudioNext::Stay;
        }
    }
    if (mail == 0) {
        g_state = State::Sync;
    } else {
        g_batch.clear();
        g_batch_len = mail & 0x7FFF;
        g_state = State::Command;
    }
    return JAudioNext::Stay;
}
