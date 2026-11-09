// The movie recorder (record.h).
//
// Video: each frame is blitted into a framebuffer the movie's size (flipped, since a raw
// video's rows run top down), read into one of a ring of pixel buffers, and fenced; the
// buffer is mapped a frame later, when the copy has long finished, so the GL thread never
// waits on it. The pixels go into one of a few frame buffers and a writer thread pipes
// them to ffmpeg, each as many times as the timeline needs it. If ffmpeg falls behind and
// every frame buffer is waiting, a frame is not kept: its share of the timeline goes to the
// next one, which is repeated for it, so the movie keeps time and only loses smoothness.
//
// Audio: the samples handed to the device (audio_render's output, 48 kHz stereo) are
// written to a WAV beside the movie, from the first block after the first frame. Where
// that block falls after the first frame is the audio's offset in the mux.
//
// The encoder is the first of h264_nvenc, h264_amf and libx264 that ffmpeg can open on
// this machine (a one-frame trial each), or GCN_RECORD_VCODEC's arguments as they are.
#include "record.h"
#include "gx/gl.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#include <csignal>
#endif

// audio.cpp calls this with every block it hands to the device.
extern void (*g_audio_tap)(const int16_t* samples, int frames);

namespace record {
namespace {

using Clock = std::chrono::steady_clock;

struct State {
    std::string path, video_path, audio_path, ffmpeg;
    int w = 0, h = 0, fps = 60;
    FILE* pipe = nullptr;
    bool on = false;

    // GL side
    bool gl_ready = false;
    GLuint fbo = 0, rb = 0;
    static constexpr int kSlots = 3;
    GLuint pbo[kSlots] = {};
    GLsync fence[kSlots] = {};
    int slot_n[kSlots] = {};   // how many movie frames each pending read stands for
    int cur = 0;
    bool started = false;
    Clock::time_point t0;
    int64_t emitted = 0;       // movie frames accounted for so far
    int carry = 0;             // frames owed by reads that could not be kept
    uint64_t dropped = 0;

    // writer
    static constexpr int kBuffers = 8;
    std::vector<std::vector<uint8_t>> buffers;
    std::vector<int> free_buffers;
    std::deque<std::pair<int, int>> queue;   // buffer, times to write it
    std::mutex m;
    std::condition_variable cv;
    bool closing = false;
    std::thread writer;
    uint64_t written = 0;

    // audio
    std::mutex audio_m;
    FILE* wav = nullptr;
    uint32_t wav_bytes = 0;
    std::atomic<bool> audio_on{false};
    Clock::time_point ta0;
    bool audio_started = false;
};
State g;

std::string in_quotes(const std::string& s) { return "\"" + s + "\""; }

int run(const std::string& cmd) {
#ifdef _WIN32
    // cmd /c strips one pair of quotes from around the whole line.
    return system(("\"" + cmd + "\"").c_str());
#else
    return system(cmd.c_str());
#endif
}

#ifdef _WIN32
constexpr const char* kNull = "NUL";
#else
constexpr const char* kNull = "/dev/null";
#endif

std::string pick_encoder() {
    if (const char* e = getenv("GCN_RECORD_VCODEC"); e && *e) return e;
    struct Option { const char* name; const char* args; };
    static const Option options[] = {
        {"h264_nvenc", "-c:v h264_nvenc -preset p5 -rc vbr -cq 18 -b:v 0 -pix_fmt yuv420p"},
        {"h264_amf", "-c:v h264_amf -usage transcoding -quality quality -rc cqp -qp_i 18 -qp_p 20 -pix_fmt nv12"},
        {"libx264", "-c:v libx264 -preset superfast -crf 18 -pix_fmt yuv420p"},
    };
    for (const Option& o : options) {
        char trial[256];
        snprintf(trial, sizeof(trial), " -hide_banner -loglevel quiet -f lavfi -i color=c=black:s=%dx%d:r=%d -frames:v 2 ",
                 g.w, g.h, g.fps);
        if (run(in_quotes(g.ffmpeg) + trial + o.args + " -f null - 2>" + kNull) == 0) {
            fprintf(stderr, "record: encoding with %s\n", o.name);
            return o.args;
        }
    }
    return "";
}

void writer_main() {
    for (;;) {
        std::pair<int, int> job;
        {
            std::unique_lock<std::mutex> lk(g.m);
            g.cv.wait(lk, [] { return g.closing || !g.queue.empty(); });
            if (g.queue.empty()) return;
            job = g.queue.front();
            g.queue.pop_front();
        }
        const std::vector<uint8_t>& px = g.buffers[job.first];
        bool ok = true;
        for (int i = 0; i < job.second && ok; i++) ok = fwrite(px.data(), 1, px.size(), g.pipe) == px.size();
        {
            std::lock_guard<std::mutex> lk(g.m);
            g.free_buffers.push_back(job.first);
            if (ok) g.written += job.second;
        }
        if (!ok) {
            fprintf(stderr, "record: ffmpeg stopped taking frames\n");
            std::lock_guard<std::mutex> lk(g.m);
            g.closing = true;
            g.queue.clear();
            return;
        }
    }
}

void audio_tap(const int16_t* s, int frames) {
    if (!g.audio_on.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lk(g.audio_m);
    if (!g.wav) return;
    if (!g.audio_started) {
        g.audio_started = true;
        g.ta0 = Clock::now();
    }
    fwrite(s, 4, frames, g.wav);
    g.wav_bytes += frames * 4;
}

void wav_header(FILE* f, uint32_t data_bytes) {
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); w32(36 + data_bytes); fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(48000); w32(48000 * 4); w16(4); w16(16);
    fwrite("data", 1, 4, f); w32(data_bytes);
    fseek(f, 0, SEEK_END);
}

bool gl_init() {
    glGenFramebuffers(1, &g.fbo);
    glGenRenderbuffers(1, &g.rb);
    glBindRenderbuffer(GL_RENDERBUFFER, g.rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, g.w, g.h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g.fbo);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g.rb);
    const bool ok = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glGenBuffers(State::kSlots, g.pbo);
    for (int i = 0; i < State::kSlots; i++) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, g.pbo[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)g.w * g.h * 4, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    return ok;
}

// Takes the read in `slot` out of its pixel buffer and hands it to the writer.
void collect(int slot) {
    if (!g.fence[slot]) return;
    glClientWaitSync(g.fence[slot], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
    glDeleteSync(g.fence[slot]);
    g.fence[slot] = nullptr;
    const int n = g.slot_n[slot] + g.carry;
    int buf = -1;
    {
        std::lock_guard<std::mutex> lk(g.m);
        if (g.closing) return;
        if (!g.free_buffers.empty()) {
            buf = g.free_buffers.back();
            g.free_buffers.pop_back();
        }
    }
    if (buf < 0) {   // the writer is behind: the next frame stands in for this one
        g.carry = n;
        g.dropped++;
        return;
    }
    g.carry = 0;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, g.pbo[slot]);
    const size_t size = (size_t)g.w * g.h * 4;
    if (const void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)size, GL_MAP_READ_BIT)) {
        memcpy(g.buffers[buf].data(), p, size);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    {
        std::lock_guard<std::mutex> lk(g.m);
        g.queue.emplace_back(buf, n);
    }
    g.cv.notify_one();
}

}  // namespace

bool start(const char* path, int w, int h, int fps) {
    g.path = path;
    g.w = (w + 1) & ~1;
    g.h = (h + 1) & ~1;
    g.fps = fps > 0 ? fps : 60;
    const char* f = getenv("GCN_FFMPEG");
    g.ffmpeg = f && *f ? f : "ffmpeg";
    g.video_path = g.path + ".video.mkv";
    g.audio_path = g.path + ".audio.wav";
    const std::string codec = pick_encoder();
    if (codec.empty()) {
        fprintf(stderr, "--record: ffmpeg (%s) could not encode a %dx%d movie; is it on the PATH? GCN_FFMPEG names it.\n",
                g.ffmpeg.c_str(), g.w, g.h);
        return false;
    }
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);   // a dead ffmpeg is a failed write, not the end of the game
#endif
    char input[160];
    snprintf(input, sizeof(input), " -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - ",
             g.w, g.h, g.fps);
    const std::string cmd = in_quotes(g.ffmpeg) + input + codec + " " + in_quotes(g.video_path);
#ifdef _WIN32
    g.pipe = popen(("\"" + cmd + "\"").c_str(), "wb");
#else
    g.pipe = popen(cmd.c_str(), "w");
#endif
    if (!g.pipe) {
        fprintf(stderr, "--record: could not start ffmpeg\n");
        return false;
    }
    g.wav = fopen(g.audio_path.c_str(), "wb");
    if (g.wav) wav_header(g.wav, 0);
    g.buffers.assign(State::kBuffers, std::vector<uint8_t>((size_t)g.w * g.h * 4));
    for (int i = 0; i < State::kBuffers; i++) g.free_buffers.push_back(i);
    g.writer = std::thread(writer_main);
    g_audio_tap = audio_tap;
    g.on = true;
    fprintf(stderr, "record: %s, %dx%d at %d fps\n", g.path.c_str(), g.w, g.h, g.fps);
    return true;
}

bool active() { return g.on; }

void frame(unsigned src, int sw, int sh) {
    if (!g.on || sw <= 0 || sh <= 0) return;
    if (!g.gl_ready) {
        if (!gl_init()) {
            fprintf(stderr, "record: could not make the readback framebuffer\n");
            g.on = false;
            return;
        }
        g.gl_ready = true;
    }
    const Clock::time_point now = Clock::now();
    if (!g.started) {
        g.started = true;
        g.t0 = now;
        g.audio_on.store(true, std::memory_order_release);
    }
    const int64_t due = (int64_t)(std::chrono::duration<double>(now - g.t0).count() * g.fps) + 1;
    const int n = (int)(due - g.emitted);
    if (n <= 0) return;   // this frame falls between two of the movie's
    g.emitted = due;

    // Fill or fit, about the centre.
    int sx0 = 0, sy0 = 0, sx1 = sw, sy1 = sh, dx0 = 0, dy0 = 0, dx1 = g.w, dy1 = g.h;
    const bool fill = (int64_t)sw * 10 < (int64_t)sh * 12;   // about square or taller: an eye
    if (fill) {
        if ((int64_t)sw * g.h > (int64_t)sh * g.w) {
            const int cw = (int)((int64_t)sh * g.w / g.h);
            sx0 = (sw - cw) / 2, sx1 = sx0 + cw;
        } else {
            const int ch = (int)((int64_t)sw * g.h / g.w);
            sy0 = (sh - ch) / 2, sy1 = sy0 + ch;
        }
    } else if ((int64_t)sw * g.h > (int64_t)sh * g.w) {
        const int ch = (int)((int64_t)g.w * sh / sw);
        dy0 = (g.h - ch) / 2, dy1 = dy0 + ch;
    } else {
        const int cw = (int)((int64_t)g.h * sw / sh);
        dx0 = (g.w - cw) / 2, dx1 = dx0 + cw;
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g.fbo);
    if (!fill) {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
    // Flipped: GL's rows run bottom up.
    glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, g.h - dy0, dx1, g.h - dy1, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    collect(g.cur);   // whatever this slot held from kSlots frames ago
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g.fbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, g.pbo[g.cur]);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, g.w, g.h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    g.fence[g.cur] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    g.slot_n[g.cur] = n;
    g.cur = (g.cur + 1) % State::kSlots;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void finish() {
    if (!g.on) return;
    g.on = false;
    g.audio_on.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(g.m);
        g.closing = true;
    }
    g.cv.notify_one();
    if (g.writer.joinable()) g.writer.join();
    g_audio_tap = nullptr;
    pclose(g.pipe);   // waits for ffmpeg to finish the video
    double offset = 0.0;
    {
        std::lock_guard<std::mutex> lk(g.audio_m);
        if (g.wav) {
            wav_header(g.wav, g.wav_bytes);
            fclose(g.wav);
            g.wav = nullptr;
        }
        if (g.audio_started && g.started) offset = std::chrono::duration<double>(g.ta0 - g.t0).count();
    }
    if (g.written == 0) {   // the game never showed a frame: a VR start that failed, say
        remove(g.video_path.c_str());
        remove(g.audio_path.c_str());
        fprintf(stderr, "record: no frames were shown, so there is no movie\n");
        return;
    }
    fprintf(stderr, "record: %llu frames (%.1f s), %llu not kept; muxing %s\n", (unsigned long long)g.written,
            (double)g.written / g.fps, (unsigned long long)g.dropped, g.path.c_str());
    char off[64];
    snprintf(off, sizeof(off), " -itsoffset %.4f ", offset);
    const bool have_audio = g.wav_bytes > 0;
    const std::string cmd = in_quotes(g.ffmpeg) + " -hide_banner -loglevel error -y -i " + in_quotes(g.video_path) +
                            (have_audio ? off + std::string("-i ") + in_quotes(g.audio_path) + " -map 0:v -map 1:a -c:a aac -b:a 192k"
                                        : std::string(" -map 0:v")) +
                            " -c:v copy -movflags +faststart " + in_quotes(g.path);
    if (run(cmd) == 0) {
        remove(g.video_path.c_str());
        remove(g.audio_path.c_str());
        fprintf(stderr, "record: wrote %s\n", g.path.c_str());
    } else {
        fprintf(stderr, "record: the mux failed; the video and audio are left in %s and %s\n", g.video_path.c_str(),
                g.audio_path.c_str());
    }
}

}  // namespace record
