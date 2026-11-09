// Host audio: mixes the AI DMA stream (32 kHz, from the AX HLE) with DVD streamed
// music (48 kHz DTK ADPCM read from the disc) into 48 kHz stereo.
//
// Everything here is portable. The device that pulls on it is not, so opening it lives
// in a backend beside this file -- audio_sdl.cpp on the desktop, audio_aaudio.cpp on
// Android -- each of which supplies audio_open() and calls audio_render() from whatever
// callback its API hands it.
#include "runtime.h"
#include "hw/dtk.h"
#include <algorithm>
#include <mutex>
#include <vector>

uint32_t ai_volume_left();
uint32_t ai_volume_right();
bool ai_stream_playing();
uint32_t ai_stream_rate();
void ai_stream_advance(uint32_t samples);

static constexpr int OUT_RATE = 48000;
// Handed every block that goes to the device, when set: the movie recorder (record.h).
void (*g_audio_tap)(const int16_t* samples, int frames) = nullptr;
void audio_wav_capture(const int16_t* s, int frames);

// ---- AI DMA ring buffer (stereo L,R at 32 kHz) ----
static constexpr int DMA_RATE = 32000;
// How much to keep in hand. The guest delivers 5 ms blocks when it gets round to polling
// for them, and the device takes 10 ms at a gulp, so a ring allowed to sit near empty
// comes up short many times a second. Mostly the blocks are on time, but the guest
// cannot take an interrupt while it is inside a host-side GX drain, which can hold one
// up for 30-50 ms when a frame is heavy, nor while it waits at the end of a frame for a
// slow render stage to take the last one -- 40-60 ms at a time in a headset's heaviest
// scenes, which a 60 ms cushion did not ride through: each time it ran dry the sound
// stopped until the cushion was back, a dropout of 60 ms or more. GCN_AUDIO_CUSHION_MS
// sets it; read on first use, since the headset app sets its environment late.
static size_t ring_cushion() {
    static const size_t c = [] {
        const char* e = getenv("GCN_AUDIO_CUSHION_MS");
        const int ms = e ? atoi(e) : 100;
        return (size_t)DMA_RATE * std::clamp(ms, 10, 500) / 1000;
    }();
    return c;
}
static size_t ring_limit() { return ring_cushion() + DMA_RATE * 100 / 1000; }
static std::mutex g_ring_mutex;
static std::vector<int16_t> g_ring(DMA_RATE * 2);  // 1 second
static size_t g_ring_r, g_ring_w, g_ring_count;  // in frames
static double g_dma_pos;  // fractional read position for resampling
static bool g_ring_filling = true;  // silent until the cushion is (back) in hand
static double g_ring_avg;  // smoothed fill level, which the resampling rate steers by
static int g_stat_underruns;
static size_t g_stat_min_fill = SIZE_MAX;   // the lowest level a callback found, since asked

void audio_push_dma(const int16_t* samples_be, uint32_t frames) {
    std::lock_guard<std::mutex> lk(g_ring_mutex);
    size_t cap = g_ring.size() / 2;
    for (uint32_t i = 0; i < frames; i++) {
        // AX writes interleaved R, L (big endian)
        int16_t r = (int16_t)GCN_BSWAP16((uint16_t)samples_be[2 * i]);
        int16_t l = (int16_t)GCN_BSWAP16((uint16_t)samples_be[2 * i + 1]);
        if (g_ring_count == cap) { g_ring_r = (g_ring_r + 1) % cap; g_ring_count--; }  // drop oldest
        g_ring[2 * g_ring_w] = l;
        g_ring[2 * g_ring_w + 1] = r;
        g_ring_w = (g_ring_w + 1) % cap;
        g_ring_count++;
    }
}

// ---- DTK (DVD streaming ADPCM) ----
DtkState g_dtk;
// --no-music: the stream still runs, decodes and ends as it would, so the game sees
// what it expects of it; only its samples are left out of the mix.
static bool g_music_muted;
void audio_mute_music(bool muted) { g_music_muted = muted; }
std::mutex g_dtk_mutex;

static int16_t adp_decode(int32_t bits, int32_t q, int32_t& h1, int32_t& h2) {
    int32_t hist = 0;
    switch (q >> 4) {
    case 1: hist = h1 * 0x3c; break;
    case 2: hist = h1 * 0x73 - h2 * 0x34; break;
    case 3: hist = h1 * 0x62 - h2 * 0x37; break;
    }
    hist = std::clamp((hist + 0x20) >> 6, -0x200000, 0x1fffff);
    int32_t cur = (((int16_t)(bits << 12) >> (q & 0xf)) << 6) + hist;
    h2 = h1;
    h1 = cur;
    cur >>= 6;
    return (int16_t)std::clamp(cur, -0x8000, 0x7fff);
}

static std::vector<int16_t> g_dtk_buf;  // decoded stereo samples not yet consumed
static size_t g_dtk_buf_pos;
static double g_dtk_pos;  // fractional position for rate conversion

// Decode one 32-byte block (28 stereo samples). Caller holds g_dtk_mutex.
static bool dtk_decode_block() {
    if (!g_dtk.playing || g_dtk.len == 0) return false;
    if (g_dtk.cur >= g_dtk.start + g_dtk.len) {
        if (g_dtk.stop_at_end || g_dtk.next_len == 0) { g_dtk.playing = false; return false; }
        g_dtk.start = g_dtk.cur = g_dtk.next_start;
        g_dtk.len = g_dtk.next_len;
    }
    uint8_t blk[32];
    if (!iso_read(g_dtk.cur, blk, 32)) { g_dtk.playing = false; return false; }
    g_dtk.cur += 32;
    g_dtk_buf.erase(g_dtk_buf.begin(), g_dtk_buf.begin() + g_dtk_buf_pos);
    g_dtk_buf_pos = 0;
    for (int i = 0; i < 28; i++) {
        g_dtk_buf.push_back(adp_decode(blk[4 + i] & 0xF, blk[0], g_dtk.hist[0][0], g_dtk.hist[0][1]));
        g_dtk_buf.push_back(adp_decode(blk[4 + i] >> 4, blk[1], g_dtk.hist[1][0], g_dtk.hist[1][1]));
    }
    return true;
}

// For a frontend's stats: the underruns so far, and the lowest the ring has been, in
// milliseconds, since the last call (-1 if it has been refilling throughout).
void audio_ring_stats(int* underruns, int* min_fill_ms) {
    std::lock_guard<std::mutex> lk(g_ring_mutex);
    *underruns = g_stat_underruns;
    *min_fill_ms = g_stat_min_fill == SIZE_MAX ? -1 : (int)(g_stat_min_fill * 1000 / DMA_RATE);
    g_stat_min_fill = SIZE_MAX;
}

// ---- Mix one buffer ----
//
// Called from the backend's device callback, so it runs on an audio thread with a
// deadline: it takes two short locks and does no allocation beyond the DTK decode's
// amortised growth.
void audio_render(int16_t* out, int frames) {
    // DMA part: resample 32k -> 48k
    {
        std::lock_guard<std::mutex> lk(g_ring_mutex);
        size_t cap = g_ring.size() / 2;
        // Far too much buffered means this side stalled. Cut back to the cushion once,
        // rather than shaving the excess off a little at every callback.
        if (g_ring_count > ring_limit()) {
            size_t drop = g_ring_count - ring_cushion();
            g_ring_r = (g_ring_r + drop) % cap;
            g_ring_count -= drop;
        }
        if (g_ring_filling && g_ring_count >= ring_cushion()) {
            g_ring_filling = false;
            g_ring_avg = (double)g_ring_count;
            g_dma_pos = 0;
        }
        // The guest's clock and the device's are not the same clock, so the level drifts.
        // Resample up to half a percent fast or slow to walk it back to the cushion, which
        // cannot be heard, where running dry or overflowing every few minutes could.
        if (!g_ring_filling) g_stat_min_fill = std::min(g_stat_min_fill, g_ring_count);
        g_ring_avg += ((double)g_ring_count - g_ring_avg) * 0.02;
        double trim = std::clamp((g_ring_avg - ring_cushion()) / ring_cushion() * 0.01, -0.005, 0.005);
        const double step = (double)DMA_RATE / OUT_RATE * (1.0 + trim);
        static const bool stats = getenv("GCN_AXSTATS") != nullptr;
        static int calls;
        if (stats && ++calls % 400 == 0)
            fprintf(stderr, "[ring] fill=%zu avg=%.0f trim=%+.3f%% underruns=%d\n", g_ring_count, g_ring_avg, trim * 100, g_stat_underruns);
        for (int i = 0; i < frames; i++) {
            int32_t l = 0, r = 0;
            // Out of samples: stay silent until the cushion is back. Playing each block as
            // it lands would run dry again at once, and a stream of short gaps is a buzz.
            if (!g_ring_filling && g_ring_count < 2) { g_ring_filling = true; g_stat_underruns++; }
            if (!g_ring_filling) {
                size_t a = g_ring_r, b = (g_ring_r + 1) % cap;
                double f = g_dma_pos;
                l = (int32_t)(g_ring[2 * a] * (1 - f) + g_ring[2 * b] * f);
                r = (int32_t)(g_ring[2 * a + 1] * (1 - f) + g_ring[2 * b + 1] * f);
                g_dma_pos += step;
                while (g_dma_pos >= 1.0 && g_ring_count > 1) {
                    g_dma_pos -= 1.0;
                    g_ring_r = (g_ring_r + 1) % cap;
                    g_ring_count--;
                }
            }
            out[2 * i] = (int16_t)l;
            out[2 * i + 1] = (int16_t)r;
        }
    }
    // DTK streaming music
    if (ai_stream_playing()) {
        std::lock_guard<std::mutex> lk(g_dtk_mutex);
        const double step = (double)ai_stream_rate() / OUT_RATE;
        int32_t vl = (int32_t)ai_volume_left(), vr = (int32_t)ai_volume_right();
        if (g_music_muted) vl = vr = 0;
        static double counter_frac;
        counter_frac += frames * step;
        uint32_t elapsed = (uint32_t)counter_frac;
        counter_frac -= elapsed;
        for (int i = 0; i < frames; i++) {
            if (g_dtk_buf_pos + 2 > g_dtk_buf.size() && !dtk_decode_block()) break;
            int32_t l = g_dtk_buf[g_dtk_buf_pos], r = g_dtk_buf[g_dtk_buf_pos + 1];
            out[2 * i] = (int16_t)std::clamp(out[2 * i] + l * vl / 255, -32768, 32767);
            out[2 * i + 1] = (int16_t)std::clamp(out[2 * i + 1] + r * vr / 255, -32768, 32767);
            g_dtk_pos += step;
            while (g_dtk_pos >= 1.0) {
                g_dtk_pos -= 1.0;
                g_dtk_buf_pos += 2;
                if (g_dtk_buf_pos + 2 > g_dtk_buf.size() && !dtk_decode_block()) break;
            }
        }
        ai_stream_advance(elapsed);
    }
    audio_wav_capture(out, frames);
    if (g_audio_tap) g_audio_tap(out, frames);
}

static FILE* g_wav;
static uint32_t g_wav_bytes;
static void wav_header(FILE* f, uint32_t data_bytes) {
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); w32(36 + data_bytes); fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(OUT_RATE); w32(OUT_RATE * 4); w16(4); w16(16);
    fwrite("data", 1, 4, f); w32(data_bytes);
    fseek(f, 0, SEEK_END);
}
void audio_wav_capture(const int16_t* s, int frames) {
    if (!g_wav) return;
    fwrite(s, 4, frames, g_wav);
    g_wav_bytes += frames * 4;
    wav_header(g_wav, g_wav_bytes);
}

// GCN_WAV=<path> records exactly what went to the device. Opened here rather than in a
// backend because it is the mix that is being captured, not the device.
void audio_open_wav() {
    if (const char* p = getenv("GCN_WAV")) { g_wav = fopen(p, "wb"); if (g_wav) wav_header(g_wav, 0); }
}
