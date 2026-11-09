// Input logging and replay. See input_log.h.
#include "input_log.h"
#include "runtime.h"
#include "platform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

uint32_t gx_frames_submitted();

namespace {

struct Entry {
    uint32_t key;  // poll sequence number (v2) or presented frame (v1)
    uint8_t chan;
    PadState s;
};

uint32_t g_poll_seq;  // pad polls so far, all channels

// ---- recording ----
FILE* g_out;
PadState g_last[4];
bool g_have_last[4];
std::mutex g_mutex;

bool copy_file(const std::string& from, const std::string& to) {
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return true;
}

bool same(const PadState& a, const PadState& b) {
    return a.connected == b.connected && a.buttons == b.buttons && a.stick_x == b.stick_x &&
           a.stick_y == b.stick_y && a.cstick_x == b.cstick_x && a.cstick_y == b.cstick_y &&
           a.trig_l == b.trig_l && a.trig_r == b.trig_r;
}

// ---- replay ----
std::vector<Entry> g_replay;     // sorted by key, as logged
size_t g_replay_pos;             // first entry not yet applied
PadState g_replay_state[4];
bool g_replay_loaded;
bool g_replay_by_frame;          // a v1 log
uint32_t g_replay_last_key;      // the last change, or the end marker when the log has one

uint32_t replay_key() { return g_replay_by_frame ? gx_frames_submitted() : g_poll_seq; }

// The clock's jumps, in the order recorded (see input_log_jump). Only the guest thread
// takes them, but clock_update_limit asks for the next one from any thread that
// schedules an event, so the next count is kept in an atomic.
// A jump and a pose record also carry the polls of the pad lines either side of them in the
// log (lo, hi): the log is written as things happen, so the recording made that jump, or
// that read, between those two polls. A replay that makes it outside them has parted from
// its recording -- the guest executed something else -- and says so (parted, below).
struct Jump { uint64_t at, edges; uint32_t lo, hi; };
std::vector<Jump> g_jumps;
size_t g_jump_pos;
std::atomic<uint64_t> g_next_jump_at{UINT64_MAX};

// ---- poses (input_pose) ----
// A kind's reads are counted from 1; a record is the state from that read on. The same
// channel serves recording and replay, since every read is counted whichever it is.
struct PoseRecord { uint32_t read; bool valid; std::vector<float> v; uint32_t lo, hi; };
struct PoseChannel {
    std::string kind;
    uint32_t reads = 0;                // reads made so far, this run
    std::vector<PoseRecord> replay;    // the log's records, in order
    size_t pos = 0;                    // first replay record not yet applied
    bool have = false;                 // a state is in force (replayed or recorded)
    bool valid = false;
    std::vector<float> v;              // the state in force
};
std::vector<PoseChannel> g_poses;

// The first time the replay makes a jump or a pose read outside the polls the recording
// made it between: where the replay parted from its recording. Said once; the replay goes on.
void parted(const char* what, uint32_t lo, uint32_t hi) {
    static bool said;
    if (said || g_replay_by_frame) return;
    if (g_poll_seq >= lo && g_poll_seq <= hi) return;
    said = true;
    fprintf(stderr, "[input] the replay has parted from its recording: %s at poll %u (frame %u), recorded between polls %u and %u\n",
            what, g_poll_seq, gx_frames_submitted(), lo, hi);
}

PoseChannel& pose_channel(const char* kind) {
    for (auto& c : g_poses) if (c.kind == kind) return c;
    g_poses.emplace_back();
    g_poses.back().kind = kind;
    return g_poses.back();
}

void record(int chan, const PadState& s) {
    if (!g_out) return;
    if (g_have_last[chan] && same(g_last[chan], s)) return;
    g_last[chan] = s;
    g_have_last[chan] = true;
    fprintf(g_out, "%u %u %d %d %04X %02X %02X %02X %02X %02X %02X\n", g_poll_seq, gx_frames_submitted(), chan,
            s.connected ? 1 : 0, s.buttons, s.stick_x, s.stick_y, s.cstick_x, s.cstick_y, s.trig_l, s.trig_r);
    // Flushed per line: the point of the log is to survive whatever happens next.
    fflush(g_out);
}

}  // namespace

bool input_log_start(const std::string& dir, const std::string& memcard_path) {
    plat_make_dirs(dir);
    const std::string path = dir + "/inputs.txt";
    g_out = fopen(path.c_str(), "wb");
    if (!g_out) {
        fprintf(stderr, "[input] cannot write %s; not logging input\n", path.c_str());
        return false;
    }
    char stamp[64] = "";
    time_t now = time(nullptr);
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    rtc_set_base(now);
    fprintf(g_out, "# gcn-recomp input log v4\n# game %s  started %s\n# epoch %lld\n", GCN_DISC_ID, stamp, (long long)now);
    fprintf(g_out, "# poll frame chan connected buttons stick_x stick_y cstick_x cstick_y trig_l trig_r\n");
    fprintf(g_out, "# (hex; each line is that channel's state from that pad poll on)\n");
    fprintf(g_out, "# jump <back-edge count> <edges>: the clock caught up with the host there (decimal)\n");
    fprintf(g_out, "# pose <kind> <read> <valid> <values...>: what the game's Nth read of that pose saw, from there on\n");
    fflush(g_out);
    if (plat_readable(memcard_path.c_str())) {
        if (!copy_file(memcard_path, dir + "/memcard_a.raw"))
            fprintf(stderr, "[input] could not snapshot %s into the log\n", memcard_path.c_str());
    }
    fprintf(stderr, "[input] logging to %s\n", path.c_str());
    return true;
}


void input_log_end() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_out) return;
    fprintf(g_out, "# end %u %u\n", g_poll_seq, gx_frames_submitted());
    fflush(g_out);
}

bool input_replay_load(const std::string& dir, std::string& memcard_out) {
    const std::string path = dir + "/inputs.txt";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "[input] no %s to replay\n", path.c_str());
        return false;
    }
    // A line of the eyes' poses holds 22 floats, over 300 characters. With room for 256 those
    // lines were cut, every record failed input_pose's size check, and a replay gave the game
    // the live head instead of the recorded one. A line too long for this is refused outright.
    char line[2048];
    int version = 1;
    uint32_t end_key = 0;
    // The poll of the last pad line read, and the jumps and pose records since it, which
    // take the next pad line's poll as the end of their bracket (Jump).
    uint32_t last_key = 0;
    std::vector<size_t> open_jumps;
    std::vector<std::pair<std::string, size_t>> open_poses;
    while (fgets(line, sizeof(line), f)) {
        if (!strchr(line, '\n') && !feof(f)) {
            fprintf(stderr, "[input] a line of %s is longer than %zu characters; not replaying it\n", path.c_str(),
                    sizeof(line) - 2);
            fclose(f);
            return false;
        }
        if (line[0] == '#') {
            long long epoch;
            unsigned end_poll, end_frame;
            if (sscanf(line, "# gcn-recomp input log v%d", &version) == 1) continue;
            if (sscanf(line, "# epoch %lld", &epoch) == 1) rtc_set_base((time_t)epoch);
            if (sscanf(line, "# end %u %u", &end_poll, &end_frame) == 2) end_key = version >= 2 ? end_poll : end_frame;
            continue;
        }
        if (line[0] == '\n') continue;
        unsigned long long jat, jedges;
        if (sscanf(line, "jump %llu %llu", &jat, &jedges) == 2) {
            g_jumps.push_back(Jump{(uint64_t)jat, (uint64_t)jedges, last_key, UINT32_MAX});
            open_jumps.push_back(g_jumps.size() - 1);
            continue;
        }
        char kind[32];
        unsigned read, valid;
        int used = 0;
        if (sscanf(line, "pose %31s %u %u%n", kind, &read, &valid, &used) == 3) {
            PoseRecord r;
            r.read = read;
            r.valid = valid != 0;
            r.lo = last_key;
            r.hi = UINT32_MAX;
            for (const char* p = line + used; r.valid;) {
                char* end;
                const float f = strtof(p, &end);
                if (end == p) break;
                r.v.push_back(f);
                p = end;
            }
            PoseChannel& ch = pose_channel(kind);
            ch.replay.push_back(r);
            open_poses.emplace_back(ch.kind, ch.replay.size() - 1);
            continue;
        }
        unsigned key, frame, chan, conn, buttons, sx, sy, cx, cy, l, r;
        if (version >= 2) {
            if (sscanf(line, "%u %u %u %u %x %x %x %x %x %x %x", &key, &frame, &chan, &conn, &buttons, &sx, &sy, &cx, &cy, &l, &r) != 11) continue;
        } else {
            if (sscanf(line, "%u %u %u %x %x %x %x %x %x %x", &key, &chan, &conn, &buttons, &sx, &sy, &cx, &cy, &l, &r) != 10) continue;
        }
        if (chan > 3) continue;
        Entry e;
        e.key = key;
        e.chan = (uint8_t)chan;
        e.s.connected = conn != 0;
        e.s.buttons = (uint16_t)buttons;
        e.s.stick_x = (uint8_t)sx; e.s.stick_y = (uint8_t)sy;
        e.s.cstick_x = (uint8_t)cx; e.s.cstick_y = (uint8_t)cy;
        e.s.trig_l = (uint8_t)l; e.s.trig_r = (uint8_t)r;
        g_replay.push_back(e);
        last_key = key;
        for (size_t j : open_jumps) g_jumps[j].hi = key;
        for (auto& p : open_poses) pose_channel(p.first.c_str()).replay[p.second].hi = key;
        open_jumps.clear();
        open_poses.clear();
    }
    fclose(f);
    if (g_replay.empty()) {
        fprintf(stderr, "[input] %s holds no input\n", path.c_str());
        return false;
    }
    g_replay_last_key = std::max(g_replay.back().key, end_key);
    g_replay_by_frame = version < 2;
    std::stable_sort(g_jumps.begin(), g_jumps.end(), [](const Jump& a, const Jump& b) { return a.at < b.at; });
    g_next_jump_at = g_jumps.empty() ? UINT64_MAX : g_jumps[0].at;
    for (auto& s : g_replay_state) { s = PadState(); s.connected = true; }
    g_replay_loaded = true;
    memcard_out.clear();
    const std::string snap = dir + "/memcard_a.raw";
    if (plat_readable(snap.c_str())) {
        memcard_out = dir + "/memcard_replay.raw";
        if (!copy_file(snap, memcard_out)) {
            fprintf(stderr, "[input] could not copy the card snapshot; replaying with the live card\n");
            memcard_out.clear();
        }
    }
    size_t poses = 0;
    for (const auto& c : g_poses) poses += c.replay.size();
    fprintf(stderr, "[input] replaying %zu changes over %u %s, %zu clock jumps and %zu pose changes from %s%s%s\n", g_replay.size(),
            g_replay_last_key, g_replay_by_frame ? "frames" : "polls", g_jumps.size(), poses, path.c_str(),
            memcard_out.empty() ? "" : " (with its card snapshot)",
            g_replay_by_frame ? " (v1 log: keyed by frame, so only approximate)" : "");
    return true;
}

void input_log_jump(uint64_t at, uint64_t edges) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_out) return;
    fprintf(g_out, "jump %llu %llu\n", (unsigned long long)at, (unsigned long long)edges);
    fflush(g_out);
}

uint64_t input_replay_next_jump_at() { return g_next_jump_at.load(); }

uint64_t input_replay_take_jump(uint64_t count) {
    if (g_jump_pos >= g_jumps.size() || g_jumps[g_jump_pos].at > count) return 0;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        parted("a clock jump", g_jumps[g_jump_pos].lo, g_jumps[g_jump_pos].hi);
    }
    const uint64_t edges = g_jumps[g_jump_pos++].edges;
    g_next_jump_at = g_jump_pos < g_jumps.size() ? g_jumps[g_jump_pos].at : UINT64_MAX;
    return edges;
}

bool input_replay_active() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_replay_loaded && replay_key() <= g_replay_last_key;
}

bool input_pose(const char* kind, bool* valid, float* v, int n) {
    std::lock_guard<std::mutex> lk(g_mutex);
    PoseChannel& ch = pose_channel(kind);
    ch.reads++;
    bool replayed = false;
    if (g_replay_loaded && replay_key() <= g_replay_last_key) {
        while (ch.pos < ch.replay.size() && ch.replay[ch.pos].read <= ch.reads) {
            const PoseRecord& r = ch.replay[ch.pos++];
            if (r.read == ch.reads) parted(ch.kind == "eyes" ? "a read of the eyes" : "a read of a hand", r.lo, r.hi);
            ch.have = true;
            ch.valid = r.valid;
            ch.v = r.v;
        }
        // An invalid record carries no values, and is replayed as invalid: the pose was
        // lost then. Requiring n values of every record once let each such stretch through
        // to the live pose -- a harness's stand-in eyes, valid and level -- and a session
        // whose headset lost tracking now and then parted from its recording at the first
        // loss that the game's hooks acted on.
        if (ch.have && (!ch.valid || (int)ch.v.size() == n)) {
            *valid = ch.valid;
            if (ch.valid)
                for (int i = 0; i < n; i++) v[i] = ch.v[i];
            replayed = true;
        }
    }
    if (g_out) {
        bool changed = !ch.have || ch.valid != *valid || (int)ch.v.size() != n;
        for (int i = 0; !changed && *valid && i < n; i++) changed = ch.v[i] != v[i];
        if (changed) {
            ch.have = true;
            ch.valid = *valid;
            ch.v.assign(v, v + n);
            fprintf(g_out, "pose %s %u %d", kind, ch.reads, *valid ? 1 : 0);
            if (*valid) for (int i = 0; i < n; i++) fprintf(g_out, " %.9g", v[i]);
            fputc('\n', g_out);
            fflush(g_out);
        }
    }
    return replayed;
}

bool input_pad_poll(int chan, PadState& s) {
    if (chan < 0 || chan > 3) return false;
    std::lock_guard<std::mutex> lk(g_mutex);
    bool replayed = false;
    if (g_replay_loaded) {
        const uint32_t key = replay_key();
        while (g_replay_pos < g_replay.size() && g_replay[g_replay_pos].key <= key) {
            const Entry& e = g_replay[g_replay_pos++];
            g_replay_state[e.chan] = e.s;
        }
        if (key > g_replay_last_key) {
            static bool said;
            if (!said) {
                said = true;
                fprintf(stderr, "[input] replay finished at poll %u (frame %u); live input from here\n", g_poll_seq, gx_frames_submitted());
            }
        } else {
            s = g_replay_state[chan];
            replayed = true;
        }
    }
    record(chan, s);
    g_poll_seq++;
    return replayed;
}
