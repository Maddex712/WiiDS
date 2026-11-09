// Command Processor / Pixel Engine registers, write-gather pipe and FIFO feeding.
#include <algorithm>
#include "../runtime.h"
#include "../platform.h"
#include "gx.h"
#include "render.h"
#include "render_gl.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

uint32_t pi_fifo_base();
uint32_t pi_fifo_end();
uint32_t& pi_fifo_wptr();

static uint16_t g_cp_sr, g_cp_cr, g_cp_clear;
static uint32_t g_cp_base, g_cp_end, g_cp_hiwm, g_cp_lowm, g_cp_rwdist, g_cp_wptr, g_cp_rptr, g_cp_bp;
static uint16_t g_pe_ctrl, g_pe_token;
static uint16_t g_pe_regs[0x20];

// Shared with the generated code, which appends 32-bit stores to the pipe itself: see
// gp_store32 in recomp.h.
extern "C" {
uint8_t g_gp_buf[64];
uint32_t g_gp_len;
}
static std::vector<uint8_t> g_pending;  // unconsumed partial command bytes
// The whole length of g_pending's first command, once its header has arrived (else 0). A
// game streaming a skinned model writes draws of hundreds of vertices a line at a time, and
// each line had the parser look at the draw again only to find it still short.
static uint32_t g_pending_need;

// CP control: bit1 = breakpoint enable, bit5 = breakpoint interrupt enable (Dolphin UCPCtrlReg)
enum { CP_CR_READ = 1, CP_CR_BPEN = 2, CP_CR_OVFINT = 4, CP_CR_UNFINT = 8, CP_CR_LINK = 16, CP_CR_BPINT = 32 };
enum { PE_TOKEN_EN = 1, PE_FINISH_EN = 2, PE_TOKEN_INT = 4, PE_FINISH_INT = 8 };

static uint32_t g_pe_finish_raised, g_pe_finish_coalesced, g_pe_finish_acked;
static bool toklog() { static const bool on = getenv("GCN_TOKLOG") != nullptr; return on; }
static void pe_update_irq() {
    pi_set_interrupt(INT_PE_TOKEN, (g_pe_ctrl & PE_TOKEN_INT) && (g_pe_ctrl & PE_TOKEN_EN));
    pi_set_interrupt(INT_PE_FINISH, (g_pe_ctrl & PE_FINISH_INT) && (g_pe_ctrl & PE_FINISH_EN));
}

void pe_signal_token(uint16_t token, bool interrupt) {
    if (toklog()) fprintf(stderr, "[tok] %04X%s\n", token, interrupt ? " int" : "");
    g_pe_token = token;
    if (interrupt) { g_pe_ctrl |= PE_TOKEN_INT; pe_update_irq(); }
}
void pe_signal_finish() {
    g_pe_finish_raised++;
    if (toklog()) fprintf(stderr, "[fin] raised (%u)%s\n", g_pe_finish_raised, (g_pe_ctrl & PE_FINISH_INT) ? ", coalesced" : "");
    if (g_pe_ctrl & PE_FINISH_INT) g_pe_finish_coalesced++;
    g_pe_ctrl |= PE_FINISH_INT;
    pe_update_irq();
}

void cp_init() {}

static gx::GuestFrameHook g_guest_frame_hook;
void gx::set_guest_frame_hook(GuestFrameHook hook) { g_guest_frame_hook = hook; }

// GCN_RAM_DUMP_AT=<frame>,... writes the guest RAM at the end of each of those submitted
// frames (the count scripted input keys on) to GCN_RAM_DUMP_DIR (default .) as
// ram_<frame>.bin, from the guest thread, so each is one consistent moment. For finding a
// game's variables by comparing its memory across a scripted run.
static void ram_dump_frame(uint32_t frame) {
    static const std::vector<uint32_t> at = [] {
        std::vector<uint32_t> v;
        if (const char* e = getenv("GCN_RAM_DUMP_AT"))
            for (const char* p = e; *p;) {
                char* end;
                v.push_back((uint32_t)strtoul(p, &end, 10));
                p = *end ? end + 1 : end;
            }
        return v;
    }();
    if (at.empty() || std::find(at.begin(), at.end(), frame) == at.end()) return;
    const char* dir = getenv("GCN_RAM_DUMP_DIR");
    char path[1024];
    snprintf(path, sizeof path, "%s/ram_%05u.bin", dir ? dir : ".", frame);
    if (FILE* f = fopen(path, "wb")) {
        fwrite(g_mem, 1, RAM_SIZE, f);
        fclose(f);
        // The presented frame too, which is what the frame dumps are numbered by.
        fprintf(stderr, "[ram] frame %u (presented %u) written to %s\n", frame, gx::present_count(), path);
    }
}

// ---------------------------------------------------------------------------
// The front end on its own thread.
//
// Parsing the command stream, decoding vertices and building pixel state can be half of
// the guest thread's time in a scene with many small draws. The hardware runs its
// GPU alongside the CPU off the same FIFO, and a game synchronises with it only through
// what the GPU signals back -- PE tokens and draw-done -- so the front end can run on a
// thread of its own as long as those signals stay where they were.
//
// So the guest thread skims (gx::skim): it walks the commands, copies every complete one
// into a queue -- display lists inlined, since their memory may be reused -- and keeps
// the frame protocol: at a finish it waits until the front end has caught up to that
// command, and a token read waits until the front end has reached the token it returns
// (see "draw-sync lag" and pe_read16); either signal is raised by the guest thread itself,
// at the same guest instruction as when the front end ran inline, which keeps replays
// exact. Vertex arrays and textures are
// still read when the front end gets to the draw, as the hardware reads them; a game that
// rewrites one before the GPU has signalled that it is done with it is already racing
// the hardware. The display copy that ends a frame is counted by the skim too, since
// scripted input and the guest checks key on it.
//
// GCN_GX_SYNC=1 runs the front end inline, as before.
// ---------------------------------------------------------------------------
static bool fe_threaded() {
    static const bool on = !getenv("GCN_GX_SYNC");
    return on;
}

// GCN_STALLS (gx.h).
static std::atomic<uint64_t> g_stall_us[(int)gx::Stall::Count];
static std::atomic<uint32_t> g_tok_reads, g_tok_lagged, g_tok_spun;   // draw-sync lag, below
static uint64_t now_us() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool gx::stalls_on() {
    static const bool on = getenv("GCN_STALLS") != nullptr;
    return on;
}
void gx::stall_add(Stall s, uint64_t us) { g_stall_us[(int)s] += us; }
gx::StallTimer::StallTimer(Stall st) : s(st), t0(stalls_on() ? now_us() : 0) {}
gx::StallTimer::~StallTimer() {
    if (t0) stall_add(s, now_us() - t0);
}
static void stall_report(uint32_t frame) {
    static uint32_t last;
    if (!gx::stalls_on() || frame < last + 60) return;
    last = frame;
    static const char* names[] = {"guest: fe queue full or a frame ahead", "guest: draw-done", "guest: token read",
                                  "fe: transform queue full", "transform: render queue full"};
    fprintf(stderr, "[stalls] f%u per frame over 60:", frame);
    for (int i = 0; i < (int)gx::Stall::Count; i++)
        fprintf(stderr, "%s %s %.2f ms", i ? "," : "", names[i], g_stall_us[i].exchange(0) / 60000.0);
    fprintf(stderr, "; token reads %u, lagged %u, spun %u", g_tok_reads.exchange(0), g_tok_lagged.exchange(0),
            g_tok_spun.exchange(0));
    // How far the guest is ahead of the screen, which is the latency a viewer feels.
    fprintf(stderr, "; %d frames ahead of the screen", (int)(frame - gx::frames_taken()));
    fprintf(stderr, "\n");
}

// One condition variable each way, each signalled only when its waiter is asleep (the
// flags below, under the mutex). The guest hands over a chunk every few kilobytes and the
// front end finishes one as often; a notify per chunk on a shared variable was a futex
// syscall each time on Android whether or not anyone waited, the largest single item in
// both threads' profiles once the chunks were small.
static std::mutex g_fe_mutex;
static std::condition_variable g_fe_work_cv;   // the front end waits for chunks
static std::condition_variable g_fe_done_cv;   // the guest waits for the front end
static bool g_fe_sleeping;                     // the front end is waiting on g_fe_work_cv
static bool g_guest_waiting;                   // the guest is waiting on g_fe_done_cv
static std::deque<std::vector<uint8_t>> g_fe_queue;
static std::vector<std::vector<uint8_t>> g_fe_spare;   // emptied chunks, capacity kept
static size_t g_fe_bytes;            // queued, not yet processed
static bool g_fe_busy;               // the thread is processing a chunk
static uint64_t g_fe_handed;         // bytes ever handed over (the guest's count)
static uint64_t g_fe_done;           // bytes ever processed, under g_fe_mutex
// Where in the stream the latest draw-sync token stands, in g_fe_handed's terms: reading
// the token register waits until the front end has got that far. See skim_sync.
static uint64_t g_token_at;

// Draw-sync lag. A game reads a token to learn how far the GPU has got, and real hardware
// is commonly most of a frame behind the CPU, so a game that recycles buffers by token
// keeps enough of them for that. Showing it the latest token issued before the last frame
// boundary, rather than the latest of all, is a GPU a frame behind: the front end is
// nearly always past that point already, so the read does not wait for it, where waiting
// for the latest token put the guest behind every draw the front end had yet to decode.
// A game that reads a token over and over without issuing one is waiting for the GPU to
// free something, and gets the latest (with the wait) after kLagSpin reads, so a game whose
// buffers are full still moves. Both are functions of what the guest did, not of timing,
// so replays stay exact.
static bool g_lag_on;
static uint16_t g_tok_latest;    // the latest token issued, and where it stands
static uint64_t g_tok_latest_at;
static bool g_tok_frame_valid;   // a token issued before the last frame boundary
static uint16_t g_tok_frame;
static uint64_t g_tok_frame_at;
static uint32_t g_tok_reads_since_issue;
static constexpr uint32_t kLagSpin = 8;

void gx::set_draw_sync_lag(bool on) { g_lag_on = on; }
static bool lag_on() {
    static const int env = [] {
        const char* e = getenv("GCN_GX_TOKEN_LAG");
        return e ? atoi(e) : -1;
    }();
    return env >= 0 ? env != 0 : g_lag_on;
}
static constexpr size_t kFeMaxBytes = 8u << 20;
// Latency. Every queue between the guest and the display fills when the stage after it is
// the slow one, and what the viewer sees is then as many frames late as the queues hold.
// The front end's was bounded only in bytes, and a frame of a map screen is a few hundred
// kilobytes: with the renderer behind, the picture trailed the controls -- and the sound,
// which follows the guest -- by most of a second. So the guest may also be no more than
// kFeMaxFrames frames ahead of the front end (one, with one frame in each of the queues
// after it: three frames from the guest to the screen when the renderer is the slow stage,
// at the same frame rate as with two each, which made six), waiting at the end of a frame
// (in real time only, so replays are unaffected). See also kMaxQueuedBatches in xf.cpp.
static constexpr size_t kFeMaxFrames = 1;
static std::deque<uint64_t> g_frame_ends;   // where each frame not yet processed ends
static std::vector<uint8_t> g_skim_out;

// View turns (gx::set_view_turn), each pinned to where the guest had got in the stream, in
// g_fe_handed's terms, and applied by the front end as it reaches that point -- between
// chunks, since the guest hands over what it has gathered first. Applied by the guest
// thread when it set them, a turn landed on whichever batch the front end, up to a frame
// behind, was building: in the headset, a frame of the world now and then drawn a frame's
// turn off. Under g_fe_mutex.
// The game's draw tags (gx::set_draw_tag) go in the same queue, in order with the turns.
namespace gx { void view_turn_reached(const float q[4]); void draw_tag_reached(uint8_t tag); }
struct PendingTurn { uint64_t at; float q[4]; int tag = -1; };   // a draw tag when tag >= 0
static std::deque<PendingTurn> g_turns;

// On the front end, before it processes what starts at `at`: the turns set by then.
static void turns_reached(uint64_t at) {
    for (;;) {
        PendingTurn t;
        {
            std::lock_guard<std::mutex> lk(g_fe_mutex);
            if (g_turns.empty() || g_turns.front().at > at) return;
            t = g_turns.front();
            g_turns.pop_front();
        }
        if (t.tag >= 0) gx::draw_tag_reached((uint8_t)t.tag);
        else gx::view_turn_reached(t.q);
    }
}

static void fe_thread_main() {
    plat_thread_role(ThreadRole::GuestWorker, "gx-frontend");
    for (;;) {
        std::vector<uint8_t> chunk;
        uint64_t start;
        {
            std::unique_lock<std::mutex> lk(g_fe_mutex);
            while (g_fe_queue.empty()) {
                g_fe_sleeping = true;
                g_fe_work_cv.wait(lk);
            }
            g_fe_sleeping = false;
            chunk = std::move(g_fe_queue.front());
            g_fe_queue.pop_front();
            g_fe_busy = true;
            start = g_fe_done;
        }
        turns_reached(start);
        gx::process(chunk.data(), (uint32_t)chunk.size(), false);
        {
            std::lock_guard<std::mutex> lk(g_fe_mutex);
            g_fe_bytes -= chunk.size();
            g_fe_done += chunk.size();
            g_fe_busy = false;
            chunk.clear();
            if (g_fe_spare.size() < 256) g_fe_spare.push_back(std::move(chunk));
            if (g_guest_waiting) g_fe_done_cv.notify_one();
        }
    }
}

// Waits, on the guest thread, until `ready` holds. Call with the lock held.
template <class Pred> static void guest_wait(std::unique_lock<std::mutex>& lk, Pred ready) {
    if (ready()) return;
    g_guest_waiting = true;
    g_fe_done_cv.wait(lk, ready);
    g_guest_waiting = false;
}

// Hands what the skim has gathered to the front end, waiting while too much is queued.
static void fe_submit() {
    if (g_skim_out.empty()) return;
    static const bool started = [] {
        gx::g_fe_threaded = true;
        std::thread(fe_thread_main).detach();
        return true;
    }();
    (void)started;
    std::unique_lock<std::mutex> lk(g_fe_mutex);
    {
        gx::StallTimer stall(gx::Stall::FeFull);
        guest_wait(lk, [] { return g_fe_bytes < kFeMaxBytes; });
    }
    g_fe_bytes += g_skim_out.size();
    g_fe_handed += g_skim_out.size();
    g_fe_queue.push_back(std::move(g_skim_out));
    g_skim_out.clear();
    if (!g_fe_spare.empty()) {
        g_skim_out = std::move(g_fe_spare.back());
        g_fe_spare.pop_back();
    }
    if (g_fe_sleeping) g_fe_work_cv.notify_one();
}

// See g_turns. What the game wrote just before may still be in the gather pipe and come
// after the turn; it is in the same frame, and a frame's batch has one turn, the last.
void gx::set_view_turn(const float q[4]) {
    if (!fe_threaded()) {
        view_turn_reached(q);
        return;
    }
    fe_submit();
    PendingTurn t;
    memcpy(t.q, q, sizeof(t.q));
    std::lock_guard<std::mutex> lk(g_fe_mutex);
    t.at = g_fe_handed;
    g_turns.push_back(t);
}

// See g_turns and set_draw_tag.
void gx::set_draw_tag(uint8_t tag) {
    if (!fe_threaded()) {
        draw_tag_reached(tag);
        return;
    }
    fe_submit();
    PendingTurn t;
    memset(t.q, 0, sizeof(t.q));
    t.tag = tag;
    std::lock_guard<std::mutex> lk(g_fe_mutex);
    t.at = g_fe_handed;
    g_turns.push_back(t);
}

// Waits until the front end has processed everything handed to it.
static void fe_wait_idle() {
    gx::StallTimer stall(gx::Stall::Finish);
    std::unique_lock<std::mutex> lk(g_fe_mutex);
    guest_wait(lk, [] { return g_fe_queue.empty() && !g_fe_busy; });
}

// Waits until the front end has processed the first `at` bytes ever handed to it.
static void fe_wait_until(uint64_t at, gx::Stall kind = gx::Stall::Token) {
    gx::StallTimer stall(kind);
    std::unique_lock<std::mutex> lk(g_fe_mutex);
    guest_wait(lk, [at] { return g_fe_done >= at; });
}

// A draw-sync token is how a game learns that the GPU has got past a point in the
// stream -- typically that it has read a buffer the CPU wants to write again. Waiting
// for the front end at every token kept that true, but a game can set one after every
// skinned model, dozens a frame, and each wait put the guest and the front end back in
// series. What makes it true is only that the front end has got there by the time the
// game *reads* the token, so the wait is made there instead (pe_read16), and the token's
// value and its interrupt still land at the guest instruction they always did: the same
// value is read at the same point, which keeps replays exact. GCN_GX_TOKEN_EAGER=1 waits
// at every token as before.
static bool token_eager() {
    static const bool on = getenv("GCN_GX_TOKEN_EAGER") != nullptr;
    return on;
}

static void skim_sync(gx::SkimSync kind, uint32_t value, std::vector<uint8_t>&) {
    switch (kind) {
    case gx::SkimSync::Token:
    case gx::SkimSync::TokenInt:
    case gx::SkimSync::Finish:
        fe_submit();
        if (kind == gx::SkimSync::Finish || token_eager()) fe_wait_idle();
        else g_token_at = g_fe_handed;
        if (kind != gx::SkimSync::Finish) {
            g_tok_latest = (uint16_t)value;
            g_tok_latest_at = g_fe_handed;
            g_tok_reads_since_issue = 0;
        }
        if (kind == gx::SkimSync::Finish) pe_signal_finish();
        else pe_signal_token((uint16_t)value, kind == gx::SkimSync::TokenInt);
        break;
    case gx::SkimSync::Frame:
        fe_submit();
        g_frame_ends.push_back(g_fe_handed);
        while (g_frame_ends.size() > kFeMaxFrames) {
            const uint64_t at = g_frame_ends.front();
            g_frame_ends.pop_front();
            fe_wait_until(at, gx::Stall::FeFull);
        }
        gx::g_frames_submitted++;
        gx::copy_writebacks_land(gx::g_frames_submitted.load());
        stall_report(gx::g_frames_submitted);
        if (g_tok_latest_at) {
            g_tok_frame_valid = true;
            g_tok_frame = g_tok_latest;
            g_tok_frame_at = g_tok_latest_at;
        }
        debug_guest_check("frame");
        heap_trace_frame(gx::g_frames_submitted.load());
        ram_dump_frame(gx::g_frames_submitted.load());
        if (g_guest_frame_hook) g_guest_frame_hook(gx::g_frames_submitted.load());
        break;
    }
}

static uint32_t cp_consume(const uint8_t* data, uint32_t len, uint32_t* need) {
    if (!fe_threaded()) return gx::process(data, len, true);
    const uint32_t used = gx::skim(data, len, true, g_skim_out, skim_sync, need);
    // Handed over in batches: this runs for every 32-byte line out of the gather pipe, and
    // a hand-off per line would cost more than the line. The syncs and the end of a frame
    // hand over whatever is gathered regardless. Small ones, though: a game that reads a
    // draw-sync token back waits for the front end to reach it (pe_read16), and with 64 KB
    // batches the front end had often not started on what it was waited for. In a room of
    // skinned models setting thirty tokens a frame, 4 KB took the guest from 59 to 73 fps
    // (8 KB and 2 KB were within 2 fps of it).
    if (g_skim_out.size() >= (4u << 10)) fe_submit();
    return used;
}

// Feed bytes to the command processor, handling commands split across chunks.
static void cp_feed(const uint8_t* data, uint32_t len) {
    // GCN_FEED_DUMP=<path>: every byte handed to the command parser, in order.
    static FILE* dump = [] { const char* p = getenv("GCN_FEED_DUMP"); return p ? fopen(p, "wb") : nullptr; }();
    static FILE* index = [] { const char* p = getenv("GCN_FEED_DUMP"); if (!p) return (FILE*)nullptr; std::string s = std::string(p) + ".idx"; return fopen(s.c_str(), "w"); }();
    static uint64_t fed;
    if (dump) { fwrite(data, 1, len, dump); fflush(dump); fprintf(index, "%llX %08X %u\n", (unsigned long long)fed, (uint32_t)(data - g_mem), len); fflush(index); fed += len; }
    uint32_t need = 0;
    if (g_pending.empty()) {
        uint32_t used = cp_consume(data, len, &need);
        if (used < len) {
            g_pending.assign(data + used, data + len);
            g_pending_need = need;
        }
        return;
    }
    g_pending.insert(g_pending.end(), data, data + len);
    static size_t warned = 1 << 16;
    if (g_pending.size() > warned) {
        warned *= 4;
        LOG(LOG_GX, "FIFO parser waiting on a %zu-byte partial command (first bytes %02X %02X %02X %02X)", g_pending.size(),
            g_pending[0], g_pending[1], g_pending[2], g_pending[3]);
    }
    if (g_pending.size() < g_pending_need) return;
    uint32_t used = cp_consume(g_pending.data(), (uint32_t)g_pending.size(), &need);
    g_pending.erase(g_pending.begin(), g_pending.begin() + used);
    g_pending_need = g_pending.empty() ? 0 : need;
}

// Consume everything between the CP read pointer and the CPU write pointer (the GP
// FIFO lives in RAM; data written while reads are disabled is processed later).
static void cp_update_irq() {
    bool bp_irq = (g_cp_sr & 0x10) && (g_cp_cr & CP_CR_BPINT);
    bool ovf_irq = (g_cp_sr & 1) && (g_cp_cr & CP_CR_OVFINT);
    bool unf_irq = (g_cp_sr & 2) && (g_cp_cr & CP_CR_UNFINT);
    pi_set_interrupt(INT_CP, bp_irq || ovf_irq || unf_irq);
}

// The FIFO's high and low watermarks. A game that holds the GP at a breakpoint for a
// frame while it writes the next keeps writing until the FIFO's overflow interrupt
// suspends the writing thread (the SDK's GXCPInterruptHandler), and the underflow
// interrupt, once the GP has drained below the low mark, resumes it. Without them the
// writer laps the reader: with a 64 KB FIFO and a frame or two of commands ahead, the
// lines the GP had not reached were overwritten before it got there, and what it then
// parsed was the next frame's vertices. The status bits follow the condition; the
// enable bits in the control register are what the SDK toggles to take each once.
// A FIFO's end register names its last line, not the address past it: the SDK sets it to
// base + size - 4, and the hardware wraps only after the line at the end is written (CPU
// side) or read (GP side), as Dolphin has it. Wrapping a line early lost the last line of
// a buffer filled to the brim, which is what a display list sized by a dry run is: the
// write pointer came back to the base with its wrap bit set, GXEndDisplayList took that
// for an overflow and returned a size of zero, and every call of the list drew nothing.
// The exclusive end, for the arithmetic below; 0 while the register is unset.
static inline uint32_t cp_end_excl() { return g_cp_end ? (g_cp_end & 0x03FFFFE0) + 32 : 0; }

static void cp_watermarks() {
    if (!(g_cp_cr & CP_CR_LINK)) return;
    uint32_t base = g_cp_base & 0x03FFFFE0, end = cp_end_excl();
    if (!end || end <= base) return;
    uint32_t wptr = pi_fifo_wptr() & 0x03FFFFE0, r = g_cp_rptr & 0x03FFFFE0;
    if (r < base || r >= end) r = base;
    if (wptr < base || wptr >= end) return;
    uint32_t d = wptr >= r ? wptr - r : (end - r) + (wptr - base);
    g_cp_rwdist = d;
    if (g_cp_hiwm && d > (g_cp_hiwm & 0x03FFFFE0)) g_cp_sr |= 1;
    if (d < (g_cp_lowm & 0x03FFFFE0)) g_cp_sr |= 2;
    cp_update_irq();
}

// Consume everything between the CP read pointer and the CPU write pointer (the GP
// FIFO lives in RAM; data written while reads are disabled is processed later).
// Honors the GP breakpoint, which games use to hold the GPU back.
// The CP's write pointer follows the PI's while the link bit is on: each burst the
// write-gather pipe puts in the FIFO moves both (as in Dolphin's GatherPipeBursted). It
// keeps its last value when a game clears the link to move the CPU elsewhere: the GP
// still reads up to it. A game that unlinks, lets the GP finish the old FIFO, and only
// then points the CP at a new one relies on that; draining only while linked lost the old
// FIFO's last frame, with the draw-done the game's frame wait was sleeping for.
//
// Only a burst moves it, not a register write. It was once copied from the PI's whenever
// the GP drained and at every CP_CR write, and a game that keeps two FIFOs -- the CPU
// filling one while the GP reads the other -- broke on it: GXSetGPFifo writes the new
// FIFO's write pointer while the link bit is still on from before and then clears the
// link, and the copy at that CP_CR write put the CPU's pointer, in the other FIFO, in
// place of the one just written. The GP stopped, and the game's draw-done never came.
static void cp_follow_pi() {
    if (g_cp_cr & CP_CR_LINK) g_cp_wptr = pi_fifo_wptr();
}

// Set by a drain that read everything up to the CP's write pointer, and cleared by any CP
// register write. While it holds and the CPU is unlinked from the GP's FIFO, nothing a line
// out of the gather pipe does can change the CP: its write pointer moves only while linked,
// so a drain would find nothing to read and set every register and status bit to what it
// already holds. A game that redirects the pipe into its own buffers -- CPU skinning, a
// display list built on the fly -- writes thousands of lines a frame that way.
static bool g_cp_settled;

// True when it got as far as checking the watermarks.
static bool cp_drain() {
    g_cp_settled = false;
    if (!(g_cp_cr & CP_CR_READ)) return false;
    uint32_t base = g_cp_base & 0x03FFFFE0, end = cp_end_excl();
    uint32_t wptr = g_cp_wptr & 0x03FFFFE0;
    if (!end || end <= base) return false;
    uint32_t r = g_cp_rptr & 0x03FFFFE0;
    if (r < base || r >= end) r = base;
    if (wptr < base || wptr >= end) {
        // The write pointer is in another FIFO: the GP would follow a pointer that has
        // nothing to do with its own.
        static uint32_t warned;
        if (warned++ < 8) LOG(LOG_GX, "CP wptr %08X outside GP FIFO %08X-%08X (rptr %08X)", wptr, base, end, r);
        return false;
    }
    bool bp_on = g_cp_cr & CP_CR_BPEN;
    uint32_t bp = g_cp_bp & 0x03FFFFE0;
    // The GP halts whenever its read pointer sits at an enabled breakpoint; moving or
    // disabling the breakpoint lets it continue. Games use this to keep the GPU one
    // frame behind the CPU.
    for (int guard = 0; r != wptr && guard < 4; guard++) {
        if (bp_on && r == bp) break;
        uint32_t stop = wptr > r ? wptr : end;
        if (bp_on && bp > r && bp < stop) stop = bp;
        if (stop > r) cp_feed(phys_ptr(r), stop - r);
        r = stop >= end ? base : stop;
    }
    bool hit = bp_on && r == bp;
    if (hit) g_cp_sr |= 0x10; else g_cp_sr &= ~0x10;
    cp_update_irq();
    g_cp_rptr = r;
    g_cp_rwdist = wptr >= r ? wptr - r : (end - r) + (wptr - base);
    cp_watermarks();
    g_cp_settled = r == wptr;
    return true;
}

// Once per 32 bytes the game writes to the GPU -- tens of thousands of times a frame in a
// heavy scene, where this path was a fifth of the guest thread -- so the common case is
// kept short: the line inside RAM without dma_fit's call, the watermarks checked once,
// and the buffer shifted only when it held more than the line.
static void gp_flush32() {
    uint32_t& wptr = pi_fifo_wptr();
    uint32_t addr = wptr & 0x03FFFFFF;
    const uint32_t fit = (addr & 0x01FFFFFF) + 32 <= RAM_SIZE ? 32u : dma_fit("write-gather", addr, 32);
    memcpy(phys_ptr(addr), g_gp_buf, fit);
    addr += 32;
    if (addr > pi_fifo_end()) {  // the end is the last line; see cp_end_excl
        addr = pi_fifo_base();
        wptr = (wptr ^ 0x04000000) & 0x04000000;  // toggle wrap bit
        wptr |= addr;
    } else {
        wptr = (wptr & 0x04000000) | addr;
    }
    if (!g_cp_settled || (g_cp_cr & CP_CR_LINK)) {  // else nothing to do: see g_cp_settled
        cp_follow_pi();
        if (!cp_drain()) cp_watermarks();  // cp_drain returns early while the GP is stopped, and the writer is not
    }
    if (g_gp_len > 32) memmove(g_gp_buf, g_gp_buf + 32, g_gp_len - 32);
    g_gp_len -= 32;
}

extern "C" void gp_flush_line() { gp_flush32(); }

uint32_t gp_pending() { return g_gp_len; }

// A write to WPAR empties the gather buffer; see hle_mtspr. Found the hard way: a game can
// stream CPU-skinned vertices through the redirected pipe into a heap block sized
// exactly for them, and GXRestoreWriteGatherPipe's 31 bytes of zero padding, written out
// as a partial line at the PI register write that follows, zeroed the next block's
// header (the allocator crash of 2026-10-08, caught by the heap check and named by the
// watch replay). On the hardware those bytes never reach memory: the SDK waits for the
// pipe to report empty and then rewrites WPAR, which discards them.
void gp_reset() {
    if (g_gp_len) LOG(LOG_GX, "gather buffer reset with %u bytes pending at wptr %08X", g_gp_len, pi_fifo_wptr());
    g_gp_len = 0;
}

// Write out whatever the gather buffer holds short of a full line, before the PI FIFO
// registers change, so that nothing from the old destination is prepended to the new
// one. Along the SDK's own path (GXRedirectWriteGatherPipe, GXRestoreWriteGatherPipe)
// this never has anything to write: the SDK pads the pipe with zero bytes, waits for it
// to report empty and rewrites WPAR, and the WPAR write empties the buffer (gp_reset).
// Writing the padding out instead was wrong on both sides. Prepended to the redirected
// stream it put every float of such a game's CPU-skinned vertex arrays a few bytes
// off; written after the stream it ran past the array -- sized to the line -- into the
// next heap block's header, which the allocator found twelve minutes later.
//
// Draining at other points the hardware might -- sync, a WPAR or pointer read, an idle
// tick -- was tried and leaves the write pointer mid-line, and the game's frame protocol
// (a GP breakpoint set at a pointer it reads back) then stops the GP short of the token
// it waits for, or desyncs the command parser outright.
void gp_flush_partial() {
    if (!g_gp_len) return;
    if (g_log_enabled[LOG_GX]) {
        char hex[3 * 32 + 1] = {};
        for (uint32_t i = 0; i < g_gp_len && i < 32; i++) snprintf(hex + 3 * i, 4, "%02X ", g_gp_buf[i]);
        LOG(LOG_GX, "gather buffer drained with %u bytes pending at wptr %08X: %s", g_gp_len, pi_fifo_wptr(), hex);
    }
    uint32_t& wptr = pi_fifo_wptr();
    uint32_t addr = wptr & 0x03FFFFFF;
    memcpy(phys_ptr(addr), g_gp_buf, dma_fit("write-gather", addr, g_gp_len));
    wptr = (wptr & 0x04000000) | ((addr + g_gp_len) & 0x03FFFFFF);
    g_gp_len = 0;
    cp_follow_pi();
    cp_drain();
}

void gp_write8(uint8_t v) {
    g_gp_buf[g_gp_len++] = v;
    if (g_gp_len >= 32) gp_flush32();
}
void gp_write16(uint16_t v) {
    g_gp_buf[g_gp_len++] = v >> 8;
    g_gp_buf[g_gp_len++] = (uint8_t)v;
    if (g_gp_len >= 32) gp_flush32();
}
#ifdef GCN_CALL_TRACE
const char* func_name(uint32_t addr);
// GCN_GP_STACK=<hex word> prints the guest call stack the first few times that word is
// pushed into the write-gather pipe. Unlike the drain side, the guest is still inside the
// code that wanted the command here, so this names it. Needs GCN_TRACE_CALLS.
static void gp_stack_probe(uint32_t v) {
    static const char* want = getenv("GCN_GP_STACK");
    if (!want) return;
    static const uint32_t w = (uint32_t)strtoul(want, nullptr, 16);
    if (v != w) return;
    static int shown;
    if (shown++ >= 3) return;
    CPU* c = cpu_current();
    fprintf(stderr, "[gp] word %08X written; guest call stack innermost first:\n", v);
    const uint32_t have = c && c->depth < 256 ? c->depth : 0;
    for (uint32_t i = 1; i <= have; i++)
        fprintf(stderr, "[gp]   %08X %s\n", c->stack[have - i], func_name(c->stack[have - i]));
}
#endif

void gp_write32(uint32_t v) {
#ifdef GCN_CALL_TRACE
    gp_stack_probe(v);
#endif
    g_gp_buf[g_gp_len++] = v >> 24;
    g_gp_buf[g_gp_len++] = (uint8_t)(v >> 16);
    g_gp_buf[g_gp_len++] = (uint8_t)(v >> 8);
    g_gp_buf[g_gp_len++] = (uint8_t)v;
    if (g_gp_len >= 32) gp_flush32();
}

static uint16_t lo(uint32_t v) { return (uint16_t)v; }
static uint16_t hi(uint32_t v) { return (uint16_t)(v >> 16); }
static void set_lo(uint32_t& r, uint16_t v) { r = (r & 0xFFFF0000u) | v; }
static void set_hi(uint32_t& r, uint16_t v) { r = (r & 0xFFFFu) | ((uint32_t)v << 16); }

uint16_t cp_read16(uint32_t off) {
    switch (off & 0xFF) {
    case 0x00: {
        // The idle bits as the hardware reports them (after Dolphin's SetCpStatusRegister):
        // not idle while data waits behind a breakpoint. A game that moves its FIFO
        // spins on these until the old one has drained, and the retrace handler that
        // moves the breakpoint runs meanwhile; reported idle regardless, it moved on at
        // once and the frame behind the breakpoint, with the draw-done its frame count
        // waited for, was never run.
        uint32_t base = g_cp_base & 0x03FFFFE0, end = cp_end_excl();
        uint32_t r = g_cp_rptr & 0x03FFFFE0, w = g_cp_wptr & 0x03FFFFE0;
        uint32_t d = (end > base && r >= base && r < end && w >= base && w < end) ? (w >= r ? w - r : (end - r) + (w - base)) : 0;
        bool at_bp = (g_cp_cr & CP_CR_BPEN) && r == (g_cp_bp & 0x03FFFFE0);
        bool read_idle = d == 0 || r == w;
        bool cmd_idle = d == 0 || at_bp || !(g_cp_cr & CP_CR_READ);
        return (uint16_t)((g_cp_sr & 0x13) | (read_idle ? 4 : 0) | (cmd_idle ? 8 : 0));
    }
    case 0x02: return g_cp_cr;
    case 0x04: return g_cp_clear;
    case 0x20: return lo(g_cp_base); case 0x22: return hi(g_cp_base);
    case 0x24: return lo(g_cp_end); case 0x26: return hi(g_cp_end);
    case 0x28: return lo(g_cp_hiwm); case 0x2A: return hi(g_cp_hiwm);
    case 0x2C: return lo(g_cp_lowm); case 0x2E: return hi(g_cp_lowm);
    case 0x30: return lo(g_cp_rwdist); case 0x32: return hi(g_cp_rwdist);
    case 0x34: return lo(g_cp_wptr); case 0x36: return hi(g_cp_wptr);
    case 0x38: return lo(g_cp_rptr); case 0x3A: return hi(g_cp_rptr);
    case 0x3C: return lo(g_cp_bp); case 0x3E: return hi(g_cp_bp);
    }
    return 0;  // perf counters etc.
}

void cp_write16(uint32_t off, uint16_t v) {
    g_cp_settled = false;
    switch (off & 0xFF) {
    case 0x00: g_cp_sr = v; break;
    case 0x02:
        if ((v ^ g_cp_cr) & ~1u) LOG(LOG_GX, "CP_CR %04X -> %04X", g_cp_cr, v);
        g_cp_cr = v;
        if (!(v & CP_CR_BPEN)) g_cp_sr &= ~0x10;
        cp_update_irq();
        cp_drain();
        break;
    case 0x04: g_cp_clear = v; g_cp_sr &= ~(v & 3); cp_watermarks(); break;
    case 0x20: set_lo(g_cp_base, v & ~0x1F); break; case 0x22: set_hi(g_cp_base, v & 0x3FF); LOG(LOG_GX, "CP base %08X", g_cp_base); break;
    case 0x24: set_lo(g_cp_end, v & ~0x1F); break; case 0x26: set_hi(g_cp_end, v & 0x3FF); break;
    case 0x28: set_lo(g_cp_hiwm, v); break; case 0x2A: set_hi(g_cp_hiwm, v); cp_watermarks(); break;
    case 0x2C: set_lo(g_cp_lowm, v); break; case 0x2E: set_hi(g_cp_lowm, v); cp_watermarks(); break;
    case 0x30: set_lo(g_cp_rwdist, v); break; case 0x32: set_hi(g_cp_rwdist, v); break;
    case 0x34: set_lo(g_cp_wptr, v); break; case 0x36: set_hi(g_cp_wptr, v); LOG(LOG_GX, "CP wptr %08X", g_cp_wptr); break;
    case 0x38: set_lo(g_cp_rptr, v); break; case 0x3A: set_hi(g_cp_rptr, v); LOG(LOG_GX, "CP rptr %08X", g_cp_rptr); break;
    case 0x3C: set_lo(g_cp_bp, v); cp_drain(); break;
    case 0x3E: set_hi(g_cp_bp, v); LOG(LOG_GX, "CP bp %08X (rptr %08X)", g_cp_bp, g_cp_rptr); cp_drain(); break;
    }
}

uint16_t pe_read16(uint32_t off) {
    switch (off & 0xFF) {
    // The interrupt bits are write-only (acknowledge by writing 1) and read as 0, as in
    // Dolphin. The SDK's token handler acknowledges with a read-modify-write that clears
    // only the token bit, so reading back a pending finish acknowledged it too, before
    // it was delivered: a game whose frame wait is on the draw-done callback, and whose
    // frame ends with tokens, waited forever.
    case 0x0A: return g_pe_ctrl & (PE_TOKEN_EN | PE_FINISH_EN);
    case 0x0E:
        if (!fe_threaded() || !g_token_at) return g_pe_token;
        g_tok_reads++;
        if (lag_on() && g_tok_frame_valid && ++g_tok_reads_since_issue <= kLagSpin) {
            g_tok_lagged++;
            fe_wait_until(g_tok_frame_at);
            return g_tok_frame;
        }
        if (lag_on() && g_tok_reads_since_issue > kLagSpin) g_tok_spun++;
        fe_wait_until(g_token_at);
        return g_pe_token;
    }
    return g_pe_regs[(off & 0x3F) >> 1];
}

void pe_write16(uint32_t off, uint16_t v) {
    switch (off & 0xFF) {
    case 0x0A:
        if ((v & PE_FINISH_INT) && (g_pe_ctrl & PE_FINISH_INT)) {
            g_pe_finish_acked++;
            if (toklog()) fprintf(stderr, "[fin] acked (%u)\n", g_pe_finish_acked);
        }
        g_pe_ctrl = (uint16_t)((g_pe_ctrl & (PE_TOKEN_INT | PE_FINISH_INT)) & ~(v & (PE_TOKEN_INT | PE_FINISH_INT))) |
                    (v & (PE_TOKEN_EN | PE_FINISH_EN));
        pe_update_irq();
        return;
    case 0x0E: g_pe_token = v; return;
    }
    g_pe_regs[(off & 0x3F) >> 1] = v;
}

GCN_WEAK uint32_t efb_peek(uint32_t addr) { return 0; }
GCN_WEAK void efb_poke(uint32_t addr, uint32_t v) {}

void debug_dump_fifo() {
    fprintf(stderr, "FIFO: CP cr=%04X sr=%04X base=%08X end=%08X wptr=%08X rptr=%08X rwdist=%08X bp=%08X | PI base=%08X end=%08X wptr=%08X | pending=%zu gp=%u\n",
            g_cp_cr, g_cp_sr, g_cp_base, g_cp_end, g_cp_wptr, g_cp_rptr, g_cp_rwdist, g_cp_bp, pi_fifo_base(), pi_fifo_end(), pi_fifo_wptr(),
            g_pending.size(), g_gp_len);
    fprintf(stderr, "  PE token=%04X ctrl=%04X finish raised=%u acked=%u coalesced=%u\n", g_pe_token, g_pe_ctrl, g_pe_finish_raised, g_pe_finish_acked, g_pe_finish_coalesced);
    fprintf(stderr, "  front end: queued %zu bytes in %zu batches, busy=%d sleeping=%d, guest waiting=%d\n", (size_t)g_fe_bytes, g_fe_queue.size(), (int)g_fe_busy, (int)g_fe_sleeping, (int)g_guest_waiting);
    // GCN_FIFO_DUMP=<path>: the GP FIFO's memory, base to end, for decoding offline.
    if (const char* path = getenv("GCN_FIFO_DUMP")) {
        uint32_t base = g_cp_base & 0x03FFFFE0, end = cp_end_excl();
        if (FILE* f = fopen(path, "wb")) {
            fwrite(phys_ptr(base), 1, end > base ? end - base : 0, f);
            fclose(f);
            fprintf(stderr, "  FIFO %08X-%08X written to %s\n", base, end, path);
        }
    }
    {
        uint32_t w = pi_fifo_wptr() & 0x03FFFFE0;
        fprintf(stderr, "  RAM FIFO before wptr:");
        for (uint32_t a = w - 256; a < w; a++) { if ((a & 31) == 0) fprintf(stderr, "\n   %08X:", a); fprintf(stderr, " %02X", *phys_ptr(a)); }
        fprintf(stderr, "\n  gp:");
        for (uint32_t i = 0; i < g_gp_len; i++) fprintf(stderr, " %02X", g_gp_buf[i]);
        fprintf(stderr, "\n");
    }
    for (uint32_t a = 0; a < 0x01800000 - 5; a++) {
        const uint8_t* q = phys_ptr(a);
        if (q[0] == 0x61 && (q[1] == 0x47 || q[1] == 0x48) && q[2] == 0x00 && q[3] == 0xB0 && (q[4] == 0x04 || q[4] == 0x05))
            fprintf(stderr, "  token write found at %08X: %02X %02X%02X%02X\n", a, q[1], q[2], q[3], q[4]);
    }
    fprintf(stderr, "  pending:");
    for (size_t i = 0; i < g_pending.size() && i < 64; i++) fprintf(stderr, " %02X", g_pending[i]);
    fprintf(stderr, "\n  vtx size for vat%d = %u\n", g_pending.empty() ? 0 : g_pending[0] & 7, g_pending.empty() ? 0 : gx::vertex_size(g_pending[0] & 7));
    fprintf(stderr, "  VCD lo=%08X hi=%08X VAT%d A=%08X B=%08X C=%08X\n", gx::g_state.cp[0x50], gx::g_state.cp[0x60], g_pending.empty() ? 0 : g_pending[0] & 7,
            gx::g_state.cp[0x70 + (g_pending.empty() ? 0 : g_pending[0] & 7)], gx::g_state.cp[0x80 + (g_pending.empty() ? 0 : g_pending[0] & 7)],
            gx::g_state.cp[0x90 + (g_pending.empty() ? 0 : g_pending[0] & 7)]);
}
