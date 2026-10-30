#include "netplay/game_menus.h"
#include "netplay_session.h"

#include "det_clock.h"
#include "frame_input.h"
#include "hle/audio/ax_dsp.h"
#include "memory.h"
#include "netplay/net_socket.h"
#include "runtime_log.h"

#include <xxhash.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace NetplaySession {
namespace {

using Clock = std::chrono::steady_clock;
using WiiRemoteInput::Kind;
using WiiRemoteInput::KpadSample;

constexpr uint32_t kMagic = 0x4E50574Eu;  // "NWPN"
constexpr uint8_t kVersion = 2;  // 2: GameCube controller records
constexpr uint32_t kNone = 0xFFFFFFFFu;
constexpr int kMaxSlots = 4;
constexpr uint32_t kRing = 512;          // frames of input kept per slot
constexpr uint32_t kMaxFramesPerBlock = 48;
constexpr uint32_t kHashSlices = 64;     // one slice of guest memory fingerprinted per frame
constexpr size_t kMaxPacket = 1400;
constexpr uint16_t kWpadHome = 0x8000;

enum PacketType : uint8_t {
    kHello = 1,   // client -> host until the host answers
    kInput = 2,   // input history, acknowledgements, a memory fingerprint, RTT timestamps
    kDesync = 3,  // host -> clients: the first frame whose fingerprints differ
    kBye = 4,
};

enum RecordFlags : uint8_t {
    kConnected = 0x01,
    kKindShift = 1,  // 2 bits: 0 remote, 1 + Nunchuk, 2 + Classic, 3 GameCube controller (Brawl)
    kRepeat = 0x80,  // same as the previous frame's record in this block
};

struct InputRecord {
    bool connected = false;
    uint8_t kind = 0;
    KpadSample sample{};
    FrameInput::GcPad pad{};  // kind kGcPad
};

constexpr uint8_t kGcPad = 3;

bool SamePad(const FrameInput::GcPad& a, const FrameInput::GcPad& b) {
    return a.button == b.button && a.stickX == b.stickX && a.stickY == b.stickY && a.substickX == b.substickX &&
           a.substickY == b.substickY && a.triggerL == b.triggerL && a.triggerR == b.triggerR;
}

bool SameRecord(const InputRecord& a, const InputRecord& b) {
    if (a.connected != b.connected || a.kind != b.kind) {
        return false;
    }
    if (!a.connected) {
        return true;
    }
    if (a.kind == kGcPad) {
        return SamePad(a.pad, b.pad);
    }
    return std::memcmp(&a.sample, &b.sample, sizeof(KpadSample)) == 0;
}

// A GameCube controller at rest: what every player holds before their first input arrives (Brawl is played on
// GameCube controllers; a player with only a Wii Remote sends that instead once they press something).
InputRecord Neutral() {
    InputRecord record;
    record.connected = true;
    record.kind = kGcPad;
    record.pad.connected = true;
    return record;
}

uint8_t KindCode(Kind kind) {
    return kind == Kind::RemoteWithNunchuk ? 1 : kind == Kind::RemoteWithClassic ? 2 : 0;
}

Kind KindFromCode(uint8_t code) {
    return code == 1 ? Kind::RemoteWithNunchuk : code == 2 ? Kind::RemoteWithClassic : Kind::Remote;
}

// ---- wire helpers (little-endian on every supported host) ----
struct Writer {
    uint8_t data[kMaxPacket];
    size_t size = 0;
    bool overflow = false;
    void Raw(const void* p, size_t n) {
        if (size + n > sizeof(data)) {
            overflow = true;
            return;
        }
        std::memcpy(data + size, p, n);
        size += n;
    }
    void U8(uint8_t v) { Raw(&v, 1); }
    void U16(uint16_t v) { Raw(&v, 2); }
    void U32(uint32_t v) { Raw(&v, 4); }
    void U64(uint64_t v) { Raw(&v, 8); }
    void F32(float v) { Raw(&v, 4); }
    void I16(int16_t v) { Raw(&v, 2); }
};

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool bad = false;
    void Raw(void* p, size_t n) {
        if (pos + n > size) {
            bad = true;
            std::memset(p, 0, n);
            return;
        }
        std::memcpy(p, data + pos, n);
        pos += n;
    }
    uint8_t U8() { uint8_t v; Raw(&v, 1); return v; }
    uint16_t U16() { uint16_t v; Raw(&v, 2); return v; }
    uint32_t U32() { uint32_t v; Raw(&v, 4); return v; }
    uint64_t U64() { uint64_t v; Raw(&v, 8); return v; }
    float F32() { float v; Raw(&v, 4); return v; }
    int16_t I16() { int16_t v; Raw(&v, 2); return v; }
};

void WriteRecord(Writer& w, const InputRecord& record, const InputRecord* previous) {
    if (previous != nullptr && SameRecord(record, *previous)) {
        w.U8(kRepeat);
        return;
    }
    w.U8(static_cast<uint8_t>((record.connected ? kConnected : 0) | (record.kind << kKindShift)));
    if (!record.connected) {
        return;
    }
    if (record.kind == kGcPad) {
        const FrameInput::GcPad& p = record.pad;
        w.U16(p.button);
        w.U8(static_cast<uint8_t>(p.stickX));
        w.U8(static_cast<uint8_t>(p.stickY));
        w.U8(static_cast<uint8_t>(p.substickX));
        w.U8(static_cast<uint8_t>(p.substickY));
        w.U8(p.triggerL);
        w.U8(p.triggerR);
        return;
    }
    const KpadSample& s = record.sample;
    w.U16(static_cast<uint16_t>(s.hold & 0xFFFFu));
    for (float v : s.acc) w.F32(v);
    if (record.kind == 1) {
        for (float v : s.stick) w.F32(v);
        for (float v : s.nunchukAcc) w.F32(v);
    } else if (record.kind == 2) {
        w.U16(static_cast<uint16_t>(s.clHold & 0xFFFFu));
        for (float v : s.clLStick) w.F32(v);
        for (float v : s.clRStick) w.F32(v);
        for (int16_t v : s.clLStickRaw) w.I16(v);
        for (int16_t v : s.clRStickRaw) w.I16(v);
        w.U8(s.clTriggerL);
        w.U8(s.clTriggerR);
    }
}

bool ReadRecord(Reader& r, InputRecord& record, const InputRecord* previous) {
    const uint8_t flags = r.U8();
    if ((flags & kRepeat) != 0) {
        if (previous == nullptr) {
            return false;
        }
        record = *previous;
        return !r.bad;
    }
    record = {};
    record.connected = (flags & kConnected) != 0;
    record.kind = static_cast<uint8_t>((flags >> kKindShift) & 3);
    if (!record.connected) {
        return !r.bad;
    }
    if (record.kind == kGcPad) {
        FrameInput::GcPad& p = record.pad;
        p.connected = true;
        p.button = r.U16();
        p.stickX = static_cast<int8_t>(r.U8());
        p.stickY = static_cast<int8_t>(r.U8());
        p.substickX = static_cast<int8_t>(r.U8());
        p.substickY = static_cast<int8_t>(r.U8());
        p.triggerL = r.U8();
        p.triggerR = r.U8();
        return !r.bad;
    }
    KpadSample& s = record.sample;
    s.hold = r.U16();
    for (float& v : s.acc) v = r.F32();
    if (record.kind == 1) {
        s.hasNunchuk = true;
        for (float& v : s.stick) v = r.F32();
        for (float& v : s.nunchukAcc) v = r.F32();
    } else if (record.kind == 2) {
        s.hasClassic = true;
        s.clHold = r.U16();
        for (float& v : s.clLStick) v = r.F32();
        for (float& v : s.clRStick) v = r.F32();
        for (int16_t& v : s.clLStickRaw) v = r.I16();
        for (int16_t& v : s.clRStickRaw) v = r.I16();
        s.clTriggerL = r.U8();
        s.clTriggerR = r.U8();
    }
    return !r.bad;
}

// ---- session state ----
struct SlotState {
    SlotState() { frames.fill(kNone); }
    std::array<InputRecord, kRing> records{};
    std::array<uint32_t, kRing> frames{};
    // Every frame up to and including this one is known (kNone: none yet).
    uint32_t contiguous = kNone;

    bool Has(uint32_t frame) const { return frames[frame % kRing] == frame; }
    void Put(uint32_t frame, const InputRecord& record) {
        records[frame % kRing] = record;
        frames[frame % kRing] = frame;
        while (Has(contiguous + 1)) {
            ++contiguous;
        }
    }
};

struct RemoteState {
    Netplay::Address address{};
    bool heard = false;
    Clock::time_point lastHeard{};
    // What this remote has of each slot (from its acknowledgements).
    std::array<uint32_t, kMaxSlots> acked{kNone, kNone, kNone, kNone};
    uint32_t theirTimeMs = 0;            // their send time, echoed back for RTT
    Clock::time_point theirTimeAt{};
    uint32_t rttMs = 0;
    Clock::time_point lastSent{};
};

struct Session {
    Session() { hashFrames.fill(kNone); }
    Config config;
    Netplay::UdpSocket socket;
    Netplay::Address hostAddress{};
    std::mutex mutex;
    std::condition_variable inputArrived;
    std::array<SlotState, kMaxSlots> slots{};
    // Indexed by slot: the host keeps one per client; a client keeps slot 0 = the host.
    std::array<RemoteState, kMaxSlots> remotes{};
    std::array<uint64_t, kRing> hashes{};
    std::array<uint32_t, kRing> hashFrames{};
    uint32_t latestHashFrame = kNone;
    uint32_t desyncFrame = 0;
    uint32_t frame = 0;
    int waitingFor = -1;
    Clock::time_point waitStart{};
    int lostSlot = -1;
    std::atomic<bool> stop{false};
    std::thread thread;
    Clock::time_point start = Clock::now();

    bool IsHost() const { return config.localSlot == 0; }
    uint32_t NowMs() const {
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    }
};

Session* g_session = nullptr;
std::atomic<bool> g_active{false};
StallHandler g_stallHandler = nullptr;

void WriteHeader(Writer& w, const Session& s, PacketType type) {
    w.U32(kMagic);
    w.U8(kVersion);
    w.U8(type);
    w.U8(s.config.localSlot);
    w.U8(0);
    w.U64(s.config.sessionId);
}

// Builds and sends one input packet to `remoteSlot` (the host when this device is a client).
void SendInputLocked(Session& s, int remoteSlot) {
    RemoteState& remote = s.remotes[remoteSlot];
    if (!remote.address.Valid()) {
        return;
    }
    Writer w;
    WriteHeader(w, s, kInput);
    for (int slot = 0; slot < kMaxSlots; ++slot) {
        w.U32(s.slots[slot].contiguous);
    }
    // Which slots this packet carries: a client sends its own; the host relays everyone's but the
    // receiver's own.
    std::vector<int> carried;
    if (s.IsHost()) {
        for (int slot = 0; slot < s.config.playerCount; ++slot) {
            if (slot != remoteSlot) {
                carried.push_back(slot);
            }
        }
    } else {
        carried.push_back(s.config.localSlot);
    }
    const size_t countPos = w.size;
    w.U8(0);
    uint8_t blocks = 0;
    for (int slot : carried) {
        const SlotState& state = s.slots[slot];
        if (state.contiguous == kNone) {
            continue;
        }
        const uint32_t acked = remote.acked[slot];
        uint32_t first = acked == kNone ? 0 : acked + 1;
        // Frames below the input delay are implicit; nobody sends them.
        first = std::max(first, s.config.inputDelay);
        if (first > state.contiguous) {
            continue;
        }
        const uint32_t last = std::min(state.contiguous, first + kMaxFramesPerBlock - 1);
        const size_t blockStart = w.size;
        w.U8(static_cast<uint8_t>(slot));
        w.U32(first);
        w.U8(static_cast<uint8_t>(last - first + 1));
        const InputRecord* previous = nullptr;
        for (uint32_t f = first; f <= last; ++f) {
            const InputRecord& record = state.records[f % kRing];
            WriteRecord(w, record, previous);
            previous = &record;
        }
        if (w.overflow) {
            w.overflow = false;
            w.size = blockStart;
            break;
        }
        ++blocks;
    }
    w.data[countPos] = blocks;
    // The newest memory fingerprint, and the RTT timestamps.
    w.U32(s.latestHashFrame);
    w.U64(s.latestHashFrame == kNone ? 0 : s.hashes[s.latestHashFrame % kRing]);
    w.U32(s.NowMs());
    w.U32(remote.theirTimeMs);
    w.U32(remote.theirTimeMs == 0
              ? 0
              : static_cast<uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - remote.theirTimeAt).count()));
    w.U32(s.desyncFrame);
    if (!w.overflow) {
        s.socket.SendTo(remote.address, w.data, w.size);
        remote.lastSent = Clock::now();
    }
}

void SendAllLocked(Session& s) {
    if (s.IsHost()) {
        for (int slot = 1; slot < s.config.playerCount; ++slot) {
            SendInputLocked(s, slot);
        }
    } else {
        SendInputLocked(s, 0);
    }
}

void NoteHash(Session& s, uint32_t frame, uint64_t hash, int fromSlot) {
    if (frame == kNone || s.hashFrames[frame % kRing] != frame) {
        return;  // not computed here yet (or too old to compare)
    }
    if (s.hashes[frame % kRing] != hash && (s.desyncFrame == 0 || frame < s.desyncFrame)) {
        s.desyncFrame = frame == 0 ? 1 : frame;
        RT_LOGF(RT_TAG_RUNTIME, "netplay: DESYNC - player %d's memory differs from ours at frame %u\n", fromSlot + 1,
                frame);
    }
}

void HandlePacket(Session& s, const Netplay::Address& from, const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.U32() != kMagic || r.U8() != kVersion) {
        return;
    }
    const uint8_t type = r.U8();
    const uint8_t fromSlot = r.U8();
    r.U8();
    const uint64_t sessionId = r.U64();
    if (r.bad || sessionId != s.config.sessionId || fromSlot >= s.config.playerCount ||
        fromSlot == s.config.localSlot) {
        return;
    }
    // A host hears every client; a client listens to the host alone.
    const int remoteSlot = s.IsHost() ? fromSlot : 0;
    if (!s.IsHost() && fromSlot != 0) {
        return;
    }
    RemoteState& remote = s.remotes[remoteSlot];
    if (!remote.heard || remote.address != from) {
        RT_LOGF(RT_TAG_RUNTIME, "netplay: player %u is at %s\n", fromSlot + 1, Netplay::ToString(from).c_str());
    }
    remote.address = from;
    remote.heard = true;
    remote.lastHeard = Clock::now();

    if (type == kHello) {
        if (s.IsHost()) {
            SendInputLocked(s, remoteSlot);  // answers the hello
        }
        return;
    }
    if (type == kBye) {
        RT_LOGF(RT_TAG_RUNTIME, "netplay: player %u left\n", fromSlot + 1);
        return;
    }
    if (type != kInput) {
        return;
    }
    for (int slot = 0; slot < kMaxSlots; ++slot) {
        remote.acked[slot] = r.U32();
    }
    const uint8_t blocks = r.U8();
    bool newInput = false;
    for (uint8_t b = 0; b < blocks && !r.bad; ++b) {
        const uint8_t slot = r.U8();
        const uint32_t first = r.U32();
        const uint8_t count = r.U8();
        // A client may only speak for itself; the host speaks for everyone but the receiver.
        const bool allowed = slot < s.config.playerCount && slot != s.config.localSlot &&
                             (s.IsHost() ? slot == fromSlot : true);
        InputRecord previous{};
        bool havePrevious = false;
        for (uint8_t i = 0; i < count; ++i) {
            InputRecord record;
            if (!ReadRecord(r, record, havePrevious ? &previous : nullptr)) {
                return;
            }
            previous = record;
            havePrevious = true;
            const uint32_t frame = first + i;
            if (allowed && !s.slots[slot].Has(frame) && frame + kRing > s.frame + kRing / 2 &&
                frame >= s.config.inputDelay) {
                s.slots[slot].Put(frame, record);
                newInput = true;
            }
        }
    }
    const uint32_t hashFrame = r.U32();
    const uint64_t hash = r.U64();
    const uint32_t theirTime = r.U32();
    const uint32_t echo = r.U32();
    const uint32_t echoDelay = r.U32();
    const uint32_t reportedDesync = r.U32();
    if (r.bad) {
        return;
    }
    NoteHash(s, hashFrame, hash, fromSlot);
    if (!s.IsHost() && reportedDesync != 0 && s.desyncFrame == 0) {
        s.desyncFrame = reportedDesync;
        RT_LOGF(RT_TAG_RUNTIME, "netplay: DESYNC reported by the host at frame %u\n", reportedDesync);
    }
    remote.theirTimeMs = theirTime;
    remote.theirTimeAt = Clock::now();
    if (echo != 0) {
        const uint32_t now = s.NowMs();
        if (now >= echo + echoDelay) {
            remote.rttMs = now - echo - echoDelay;
        }
    }
    if (newInput) {
        s.inputArrived.notify_all();
        if (s.IsHost()) {
            SendAllLocked(s);  // relay at once rather than at the next resend tick
        }
    }
}

void NetworkThread(Session* s) {
    std::vector<uint8_t> buffer(2048);
    Clock::time_point lastResend{};
    Clock::time_point lastHello{};
    while (!s->stop.load(std::memory_order_relaxed)) {
        Netplay::Address from;
        const int received = s->socket.ReceiveFrom(from, buffer.data(), buffer.size(), 2);
        std::lock_guard<std::mutex> lock(s->mutex);
        if (received > 0) {
            HandlePacket(*s, from, buffer.data(), static_cast<size_t>(received));
        }
        const auto now = Clock::now();
        if (!s->IsHost() && !s->remotes[0].heard && now - lastHello > std::chrono::milliseconds(100)) {
            Writer w;
            WriteHeader(w, *s, kHello);
            s->socket.SendTo(s->hostAddress, w.data, w.size);
            s->remotes[0].address = s->hostAddress;
            lastHello = now;
        }
        // Resend what is still unacknowledged (lost datagrams), and keep quiet links alive.
        if (now - lastResend > std::chrono::milliseconds(20)) {
            lastResend = now;
            for (int slot = 0; slot < s->config.playerCount; ++slot) {
                RemoteState& remote = s->remotes[slot];
                if (slot == s->config.localSlot || !remote.heard || (!s->IsHost() && slot != 0)) {
                    continue;
                }
                // What this remote should have: a client's own input, or (from the host) everyone's
                // but its own.
                bool unacked = false;
                for (int other = 0; other < s->config.playerCount; ++other) {
                    const bool carried = s->IsHost() ? other != slot : other == s->config.localSlot;
                    const uint32_t have = s->slots[other].contiguous;
                    if (carried && have != kNone && have >= s->config.inputDelay && remote.acked[other] != have) {
                        unacked = true;
                    }
                }
                if (unacked || now - remote.lastSent > std::chrono::milliseconds(250)) {
                    SendInputLocked(*s, slot);
                }
            }
        }
    }
}

// XXH3 of slice `frame % kHashSlices` of MEM1 + the 64 MiB of MEM2 the game uses.
uint64_t HashSlice(uint32_t frame) {
    constexpr uint64_t kMem1 = 24ull << 20;
    constexpr uint64_t kTotal = kMem1 + (64ull << 20);
    constexpr uint64_t kSlice = kTotal / kHashSlices;
    const uint64_t begin = (frame % kHashSlices) * kSlice;
    const uint64_t end = begin + kSlice;
    XXH3_state_t* state = XXH3_createState();
    XXH3_64bits_reset(state);
    const auto add = [&](uint32_t base, uint64_t from, uint64_t to) {
        if (from < to && Memory::Contains(base + static_cast<uint32_t>(from), static_cast<size_t>(to - from))) {
            XXH3_64bits_update(state, Memory::GetPointer(base + static_cast<uint32_t>(from), static_cast<size_t>(to - from)),
                               static_cast<size_t>(to - from));
        }
    };
    // Brawl's 0x804951C0..0x80495380 (0x1C0 bytes of .bss, audio-level-like values) rounds differently on Windows
    // and Linux while every other byte of a match stays equal (tested 700 frames in), so the check skips it.
    constexpr uint64_t kSkipFrom = 0x004951C0u, kSkipTo = 0x00495380u;
    const uint64_t mem1From = std::min(begin, kMem1), mem1To = std::min(end, kMem1);
    add(0x80000000u, mem1From, std::min(mem1To, std::max(mem1From, kSkipFrom)));
    add(0x80000000u, std::max(mem1From, kSkipTo), mem1To);
    add(0x90000000u, std::max(begin, kMem1) - kMem1, std::max(end, kMem1) - kMem1);
    const uint64_t hash = XXH3_64bits_digest(state);
    XXH3_freeState(state);
    return hash;
}

// After a restart every device boots to the title. Until the host's player count is picked, player
// 1's input comes from here instead of the host's remote, on every device alike (it reads nothing
// but the game's own state, which is the same everywhere): the title, the host's file, the number of
// players. The other remotes stay at rest until then; afterwards everyone plays.
struct Autopilot {
    bool active = false;
    int saveFile = 0;
    uint32_t pulse = 0;           // the buttons this press holds
    bool confirmedCount = false;  // 2 went in on the right number of players
    uint32_t doneAt = 0;
};
Autopilot g_autopilot;

constexpr uint32_t kWpadScreenLeft = 0x0008, kWpadScreenRight = 0x0004, kWpadScreenDown = 0x0001, kWpadTwo = 0x0100;

// Player 1's buttons this frame, or false once the autopilot is done. It presses like a person: a
// press every 20 frames, held for 4, chosen from what is on screen when the press starts, and
// keeps pressing until the screen has moved on (a press during a menu animation is ignored).
bool AutopilotButtons(uint32_t frame, uint8_t players, uint32_t& buttons) {
    Autopilot& a = g_autopilot;
    if (!a.active) {
        return false;
    }
    buttons = 0;
    const GameMenus::SelectPlayers selectPlayers = GameMenus::SelectPlayersState();
    if (a.doneAt == 0 && a.confirmedCount &&
        (selectPlayers == GameMenus::SelectPlayers::Leaving || selectPlayers == GameMenus::SelectPlayers::Hidden)) {
        a.doneAt = frame + 30;  // the number went in: hand over once the screen is gone
    }
    if (a.doneAt != 0) {
        if (frame >= a.doneAt) {
            a.active = false;
            RT_LOGF(RT_TAG_RUNTIME, "netplay: player count picked; everyone has their remote now\n");
            return false;
        }
        return true;
    }
    if (frame % 20 == 0) {
        a.pulse = 0;
        if (selectPlayers == GameMenus::SelectPlayers::Choosing) {
            if (GameMenus::SelectPlayersSettled()) {
                const int cursor = GameMenus::SelectPlayersCursor();
                const int target = players - 1;
                if (cursor == target) {
                    a.pulse = kWpadTwo;
                    a.confirmedCount = true;
                } else if (cursor == 0) {
                    a.pulse = kWpadScreenDown;
                } else {
                    a.pulse = cursor < target ? kWpadScreenRight : kWpadScreenLeft;
                }
            }
        } else if (selectPlayers == GameMenus::SelectPlayers::Hidden) {
            if (GameMenus::FileSelectWaiting()) {
                const int cursor = GameMenus::FileSelectCursor();
                a.pulse = cursor == a.saveFile ? kWpadTwo : cursor < a.saveFile ? kWpadScreenRight : kWpadScreenLeft;
            } else if (GameMenus::CurrentScene() != 0x00A) {
                a.pulse = kWpadTwo;  // the title ("Press 2") and everything before it
            }
        }
    }
    buttons = frame % 20 < 4 ? a.pulse : 0;
    return true;
}

// The FrameInput source: sample this device's controller for frame retrace + delay, then wait for
// every player's input for this frame.
void FrameSource(uint32_t retrace, FrameInput::Frame& frame) {
    Session& s = *g_session;

    // This frame's memory, as every device sees it at this same point.
    AxDspHle::JoinMixWorker();
    const uint64_t hash = HashSlice(retrace);

    // This device's player: its controller in port 1 (or its first Wii Remote), as that player's port.
    const FrameInput::Frame local = FrameInput::SampleLocal();
    InputRecord mine;
    if (local[0].pad.connected) {
        mine = Neutral();
        mine.pad = local[0].pad;
    } else {
        mine.connected = local[0].connected;
        mine.kind = KindCode(local[0].kind);
        mine.sample = local[0].sample;
        mine.sample.hold &= ~static_cast<uint32_t>(kWpadHome);  // the HOME Menu would pause one device only
    }
    if (!mine.connected) {
        mine = Neutral();  // a player always has a controller in the session
    }

    std::unique_lock<std::mutex> lock(s.mutex);
    s.frame = retrace;
    s.hashes[retrace % kRing] = hash;
    s.hashFrames[retrace % kRing] = retrace;
    s.latestHashFrame = retrace;
    SlotState& own = s.slots[s.config.localSlot];
    if (own.contiguous == kNone) {
        own.contiguous = s.config.inputDelay - 1;  // frames below the delay are implicit
    }
    own.Put(retrace + s.config.inputDelay, mine);
    SendAllLocked(s);

    // Wait for every slot's input for this frame.
    bool waited = false;
    for (;;) {
        int missing = -1;
        for (int slot = 0; slot < s.config.playerCount; ++slot) {
            if (retrace >= s.config.inputDelay && !s.slots[slot].Has(retrace)) {
                missing = slot;
                break;
            }
        }
        if (missing < 0) {
            break;
        }
        if (!waited) {
            waited = true;
            s.waitStart = Clock::now();
        }
        s.waitingFor = missing;
        s.inputArrived.wait_for(lock, std::chrono::milliseconds(50));
        const auto waitedFor = Clock::now() - s.waitStart;
        // A device silent this long is gone (closed, crashed, out of range). Clients hear everyone
        // through the host, so for them it is the host that went quiet.
        const int path = s.IsHost() ? missing : 0;
        if (s.lostSlot < 0 && waitedFor > std::chrono::seconds(12) &&
            Clock::now() - s.remotes[path].lastHeard > std::chrono::seconds(12)) {
            s.lostSlot = missing;
            RT_LOGF(RT_TAG_RUNTIME, "netplay: lost player %d\n", missing + 1);
        }
        if (waitedFor > std::chrono::milliseconds(1000) && g_stallHandler != nullptr) {
            lock.unlock();
            g_stallHandler();
            lock.lock();
        }
        static Clock::time_point lastReport{};
        if (waitedFor > std::chrono::seconds(2) && Clock::now() - lastReport > std::chrono::seconds(2)) {
            lastReport = Clock::now();
            RT_LOGF(RT_TAG_RUNTIME, "netplay: frame %u waiting for player %d (%lld ms)\n", retrace, missing + 1,
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(waitedFor).count()));
        }
    }
    s.waitingFor = -1;

    for (uint32_t chan = 0; chan < frame.size(); ++chan) {
        FrameInput::Remote& remote = frame[chan];
        remote = {};
        if (chan >= s.config.playerCount) {
            continue;
        }
        const InputRecord record =
            retrace < s.config.inputDelay ? Neutral() : s.slots[chan].records[retrace % kRing];
        if (record.kind == kGcPad) {
            remote.pad = record.pad;
            remote.pad.connected = record.connected;
            continue;
        }
        remote.connected = record.connected;
        remote.kind = KindFromCode(record.kind);
        remote.sample = record.sample;
        remote.sample.hasNunchuk = record.connected && record.kind == 1;
        remote.sample.hasClassic = record.connected && record.kind == 2;
    }
    uint32_t autopilot = 0;
    if (AutopilotButtons(retrace, s.config.playerCount, autopilot)) {
        for (uint32_t chan = 0; chan < s.config.playerCount && chan < frame.size(); ++chan) {
            FrameInput::Remote& remote = frame[chan];
            remote.connected = true;
            remote.kind = WiiRemoteInput::Kind::Remote;
            remote.sample = Neutral().sample;
            if (chan == 0) {
                remote.sample.hold = autopilot;
            }
        }
    }
    lock.unlock();
    if (waited) {
        // Don't race to catch up after a wait: that only puts this device ahead again.
        DetClock::ResetPacing();
    }
}

std::string Field(const std::string& text, const char* key) {
    const std::string needle = std::string(key) + "=";
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        if (pos == 0 || text[pos - 1] == ';') {
            const size_t start = pos + needle.size();
            const size_t end = text.find(';', start);
            return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }
        pos += needle.size();
    }
    return {};
}

} // namespace

bool LoadConfigFromEnvironment(Config& out) {
    std::string text;
    if (const char* env = std::getenv("NSMBW_NETPLAY"); env != nullptr) {
        text = env;
    }
#if defined(__ANDROID__)
    if (text.empty()) {
        char value[PROP_VALUE_MAX] = {};
        if (__system_property_get("debug.nsmbw.net", value) > 0) {
            text = value;
        }
    }
#endif
    if (text.empty()) {
        return false;
    }
    Config config;
    config.sessionId = std::strtoull(Field(text, "id").c_str(), nullptr, 10);
    config.localSlot = static_cast<uint8_t>(std::strtoul(Field(text, "slot").c_str(), nullptr, 10));
    if (const std::string players = Field(text, "players"); !players.empty()) {
        config.playerCount = static_cast<uint8_t>(std::strtoul(players.c_str(), nullptr, 10));
    }
    if (const std::string delay = Field(text, "delay"); !delay.empty()) {
        config.inputDelay = static_cast<uint32_t>(std::strtoul(delay.c_str(), nullptr, 10));
    }
    config.port = static_cast<uint16_t>(std::strtoul(Field(text, "port").c_str(), nullptr, 10));
    config.host = Field(text, "host");
    config.autopilot = Field(text, "autopilot") == "1";
    config.saveFile = static_cast<int>(std::strtol(Field(text, "file").c_str(), nullptr, 10));
    if (config.sessionId == 0 || config.playerCount < 2 || config.playerCount > kMaxSlots ||
        config.localSlot >= config.playerCount || config.inputDelay < 1 || config.inputDelay > 30 ||
        (config.localSlot != 0 && config.host.empty())) {
        RT_LOGF(RT_TAG_RUNTIME, "netplay: ignoring malformed session settings '%s'\n", text.c_str());
        return false;
    }
    out = config;
    return true;
}

bool Start(const Config& config) {
    if (g_session != nullptr) {
        return true;
    }
    auto* s = new Session();
    s->config = config;
    if (!s->IsHost() && !Netplay::ResolveAddress(config.host, Netplay::kDefaultPort, s->hostAddress)) {
        RT_LOGF(RT_TAG_RUNTIME, "netplay: cannot resolve the host '%s'\n", config.host.c_str());
        delete s;
        return false;
    }
    const uint16_t port = config.port != 0 ? config.port : s->IsHost() ? Netplay::kDefaultPort : 0;
    // Right after a restart the old process may still hold the port for a moment.
    bool opened = s->socket.Open(port);
    for (int attempt = 0; !opened && port != 0 && attempt < 30; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        opened = s->socket.Open(port);
    }
    if (!opened && !(port != 0 && !s->IsHost() && s->socket.Open(0))) {
        RT_LOGF(RT_TAG_RUNTIME, "netplay: cannot open UDP port %u\n", port);
        delete s;
        return false;
    }
    // Frames below the input delay are implicit neutral input for everyone.
    for (int slot = 0; slot < config.playerCount; ++slot) {
        s->slots[slot].contiguous = config.inputDelay - 1;
    }
    g_session = s;
    g_autopilot.active = config.autopilot;
    g_autopilot.saveFile = config.saveFile;
    DetClock::Enable();
    FrameInput::SetSource(&FrameSource);
    s->thread = std::thread(NetworkThread, s);
    g_active.store(true, std::memory_order_release);
    RT_LOGF(RT_TAG_RUNTIME,
            "netplay: session %llu, player %u of %u, input delay %u frames, UDP port %u%s%s\n",
            static_cast<unsigned long long>(config.sessionId), config.localSlot + 1, config.playerCount,
            config.inputDelay, s->socket.LocalPort(), s->IsHost() ? "" : ", host ",
            s->IsHost() ? "" : Netplay::ToString(s->hostAddress).c_str());
    return true;
}

bool Active() {
    return g_active.load(std::memory_order_acquire);
}

void SetStallHandler(StallHandler handler) {
    g_stallHandler = handler;
}

Status GetStatus() {
    Status status;
    if (!Active()) {
        return status;
    }
    Session& s = *g_session;
    std::lock_guard<std::mutex> lock(s.mutex);
    status.active = true;
    status.localSlot = s.config.localSlot;
    status.playerCount = s.config.playerCount;
    status.frame = s.frame;
    status.waitingForSlot = s.waitingFor;
    if (s.waitingFor >= 0) {
        status.waitedMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s.waitStart).count());
    }
    status.desyncFrame = s.desyncFrame;
    status.lostSlot = s.lostSlot;
    for (int slot = 0; slot < kMaxSlots; ++slot) {
        status.pingMs[slot] = s.remotes[slot].rttMs;
    }
    return status;
}

} // namespace NetplaySession
