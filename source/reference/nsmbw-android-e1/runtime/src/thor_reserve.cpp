#include "thor_reserve.h"
#include "abi_bridge.h"
#include "memory.h"
#include "nsmbw_disc_revision.h"
#include "nand_path.h"
#include "netplay_session.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <thread>

namespace ThorReserve {
namespace {
std::mutex mutex;
std::array<int32_t, 3> slots{-1, -1, -1};
std::array<int32_t, 8> published{1, 0, -1, 0, 0, -1, 0, 0};
std::atomic<int> requested{-1};
bool dirty = false;
uint32_t lastPickupId = 0;
uint32_t lastPickupScene = 0;
thread_local bool inGuestCall = false;
// The item released from the box. Catching it never scores: it was scored when first
// collected, and a duplicate returning to the box must not pay 1000 points per drop.
uint32_t spareId = 0, spareScene = 0;
thread_local bool skipScore = false;

// One bounded read on initialization; coalesced writes on a worker only after
// slot changes. A failed/partial write retains the prior complete file. Invalid
// input is preserved before any replacement, never parsed as an item.
class Persistence {
    std::filesystem::path path;
    std::mutex workMutex;
    std::condition_variable changed;
    std::thread worker;
    std::array<int32_t, 3> pending{};
    bool queued = false, stopping = false, writable = true;
    static std::array<unsigned char, 20> Encode(const std::array<int32_t, 3>& data) {
        std::array<unsigned char, 20> bytes{'T','H','R','R','S','V','1',0};
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned b = 0; b < 4; ++b) bytes[8 + i * 4 + b] = uint32_t(data[i]) >> (b * 8);
        return bytes;
    }
    void Write(const std::array<int32_t, 3>& data) {
        if (!writable) return;
        const auto temporary = std::filesystem::path(path.string() + ".tmp");
        const auto bytes = Encode(data);
        { std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
          if (!out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()).flush()) return; }
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            std::filesystem::copy_file(path, path.string() + ".bak",
                std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) return;
        }
        std::filesystem::rename(temporary, path, ec);
    }
public:
    Persistence() {
        const auto nandRoot = RuntimeNandPath::DiscoverNandRootPath().lexically_normal();
        // Configured NAND directories may be siblings. Key the complete resolved
        // root, not only its parent, so NAND and NAND-alt never share a reserve.
        uint64_t rootKey = 14695981039346656037ull;
        for (char8_t byte : nandRoot.generic_u8string()) {
            rootKey ^= static_cast<unsigned char>(byte);
            rootKey *= 1099511628211ull;
        }
        char filename[48];
        std::snprintf(filename, sizeof(filename), "ThorReserve-%016llx.bin",
            static_cast<unsigned long long>(rootKey));
        path = nandRoot.parent_path() / filename;
        std::array<unsigned char, 21> bytes{};
        std::error_code inspectError;
        const bool exists = std::filesystem::exists(path, inspectError);
        if (inspectError) writable = false;
        std::ifstream in(path, std::ios::binary);
        if (exists && !in) writable = false;
        if (in) {
            in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
            bool valid = in.gcount() == 20;
            const auto header = Encode({-1,-1,-1});
            for (unsigned i = 0; i < 8; ++i) valid &= bytes[i] == header[i];
            std::array<int32_t, 3> loaded{};
            for (unsigned i = 0; i < 3; ++i) {
                uint32_t value = 0;
                for (unsigned b = 0; b < 4; ++b) value |= uint32_t(bytes[8 + i * 4 + b]) << (b * 8);
                loaded[i] = static_cast<int32_t>(value);
                valid &= value == 0xffffffffu || value == 0 || value == 1 || value == 4 ||
                    value == 11 || value == 13 || value == 14;
            }
            in.close();
            if (valid) slots = loaded;
            else {
                writable = false;
                for (unsigned i = 0; i < 100; ++i) {
                    std::error_code ec;
                    const auto backup = path.string() + ".invalid-" + std::to_string(i);
                    if (std::filesystem::exists(backup, ec)) continue;
                    std::filesystem::rename(path, backup, ec);
                    writable = !ec;
                    break;
                }
            }
        }
        worker = std::thread([this] {
            std::unique_lock lock(workMutex);
            for (;;) {
                changed.wait(lock, [this] { return stopping || queued; });
                if (!queued && stopping) break;
                const auto data = pending;
                queued = false;
                lock.unlock(); Write(data); lock.lock();
            }
        });
    }
    ~Persistence() {
        { std::lock_guard lock(workMutex); stopping = true; }
        changed.notify_one();
        if (worker.joinable()) worker.join();
    }
    void Save(const std::array<int32_t, 3>& data) {
        { std::lock_guard lock(workMutex); pending = data; queued = true; }
        changed.notify_one();
    }
};
Persistence& Storage() { static Persistence instance; return instance; }

bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("NSMBW_THOR");
        return kNsmbwDiscRevision == 1 && value && std::string_view(value) == "1";
    }();
    return enabled;
}
bool Word(uint32_t address, uint32_t& value) { return Memory::TryRead32(address, value); }
bool Byte(uint32_t address, uint32_t& value) {
    uint32_t word;
    if (!Word(address & ~3u, word)) return false;
    value = (word >> ((3u - (address & 3u)) * 8u)) & 255u;
    return true;
}
bool Half(uint32_t address, uint32_t& value) {
    uint32_t high, low;
    if (!Byte(address, high) || !Byte(address + 1u, low)) return false;
    value = (high << 8) | low;
    return true;
}
bool Pointer(uint32_t p, uint32_t length) {
    return p >= 0x80000000u && p < 0x81800000u && (p & 3u) == 0 &&
        length < 0x81800000u - p && Memory::Contains(p, length);
}
bool Supported(uint32_t kind) {
    return kind == 0 || kind == 1 || kind == 4 || kind == 11 || kind == 13 || kind == 14;
}
int SuitItem(uint32_t suit) {
    constexpr int kinds[]{-1, 0, 1, 13, 11, 14, 4};
    return suit < 7 ? kinds[suit] : -1;
}
uint32_t RawKind(uint32_t kind) {
    switch (kind) {
    case 1: return 9;
    case 4: return 14;
    case 11: return 21;
    case 13: return 25;
    case 14: return 17;
    default: return 0;
    }
}
struct Context { uint32_t scene = 0, player = 0, index = 0, suit = 0, slot = 0; };

// NSMB2 storage priority (owner choice, 2026-10-19): a Super Mushroom is the lowest item. It never
// replaces a better stored power-up, though it does replace a stored Mini Mushroom; every other
// power-up replaces whatever is stored.
bool Accepts(int current, int incoming) {
    return incoming != 0 || current < 0 || current == 0 || current == 13;
}

// The dropped item, NSMB2 style: it appears at the top of the screen and blinks there for a second,
// following Mario sideways, then falls with item gravity through anything in its way until it
// reaches Mario's height, where the original FreeDrop state takes over and it lands normally.
constexpr int kHoverFrames = 60;
constexpr int kBlinkFrames = 4;   // shown/hidden phase length while it hovers
// Sounds from the game's own archive (wii_mj2d_sound.brsar indices).
constexpr uint32_t kSeStockItemUse = 186;   // SE_SYS_STOCK_ITEM_USE: taken out of the box
constexpr uint32_t kSeItemAppear = 599;     // SE_OBJ_ITEM_APPEAR: starts falling
constexpr float kTopInset = 28.0f;
struct Guided { uint32_t actor = 0, id = 0, scene = 0; int frame = 0; float speed = 0; } guided;

// Preserve the caller's register file and reserve a private ABI stack frame. The
// original lookup/constructor run only on the guest thread, never on Android UI.
bool Call(uint32_t target, CpuContext& cpu) {
    if (!Pointer(cpu.gpr[1], 16) || cpu.gpr[1] < 0x80002000u ||
        !Memory::Contains(cpu.gpr[1] - 0x2000u, 0x2000u)) return false;
    const uint32_t callerStack = cpu.gpr[1];
    cpu.gpr[1] = (callerStack - 128u) & ~15u;
    if (!Memory::TryWrite32(cpu.gpr[1], callerStack)) return false;
    inGuestCall = true;
    InvokeIndirectCpu(target, &cpu);
    inGuestCall = false;
    return true;
}

bool GetContext(CpuContext* cpu, Context& out, bool active) {
    uint32_t mode, pause, manager, profile, status, demo, pending;
    // R3 is host-side input and is deliberately not part of the original
    // lockstep network protocol. Even a one-player LAN session must reject it.
    if (!cpu || NetplaySession::Active() || !Word(0x80428730u, out.scene) || (out.scene & 0xffffu) != 5 ||
        !Word(0x80315B98u, mode) || mode != 0 ||
        !Pointer(cpu->gpr[13] - 22080u, 4) || !Word(cpu->gpr[13] - 22080u, manager) ||
        !Pointer(manager, 40) || !Byte(manager + 38u, out.slot) || out.slot > 2) return false;
    uint32_t entries = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t entry;
        if (!Word(0x80354E50u + i * 4u, entry)) return false;
        if (entry != 0) { ++entries; out.index = i; }
    }
    if (entries != 1) return false;
    CpuContext lookup = *cpu;
    lookup.gpr[3] = out.index;
    if (!Call(0x8005F900u, lookup)) return false;
    out.player = lookup.gpr[3];
    if (!Pointer(out.player, 0x14e4) || !Half(out.player + 8u, profile) || profile != 13 ||
        !Word(out.player + 0x1090u, out.suit) || out.suit > 6) return false;
    if (!active) return true;
    // Source: status bitset test 80056CF0, isDemoMode 8004DE30, and
    // ChangePowerupWithAnimation 80145AC0. Out-of-play/stunned/transition
    // and demonstration frames cannot consume the reserve.
    return Word(0x80429F48u, pause) && pause == 0 &&
        Word(out.player + 0x1014u, status) && (status & 0x78u) == 0 &&
        Word(out.player + 0x1020u, status) && (status & 0x20000020u) == 0 &&
        Byte(out.player + 0x1460u, demo) && demo == 0 &&
        Word(out.player + 0x14e0u, pending) && pending == out.suit;
}

// SndAudioMgr::startSystemSe(id, 1) (80195380) on the instance the game's own callers read
// from SDA r13-20984, so the sound plays exactly like the game's system sounds.
void SystemSound(const CpuContext* cpu, uint32_t id) {
    uint32_t audio;
    if (!cpu || !Pointer(cpu->gpr[13] - 20984u, 4) || !Word(cpu->gpr[13] - 20984u, audio) || !Pointer(audio, 4)) return;
    CpuContext sound = *cpu;
    sound.gpr[3] = audio;
    sound.gpr[4] = id;
    sound.gpr[5] = 1;
    Call(0x80195380u, sound);
}

// fBase_c's process-control byte (+0xF) bit 8 disables drawing only (the game's own draw test,
// 801621E0); execution, collision and the item itself are unchanged. +0xF is the low byte of
// the big-endian word at +0xC.
void SetHidden(uint32_t actor, bool hidden) {
    uint32_t word;
    if (!Word(actor + 0xcu, word)) return;
    const uint32_t next = hidden ? (word | 8u) : (word & ~8u);
    if (next != word) Memory::TryWrite32(actor + 0xcu, next);
}

bool Float(uint32_t address, float& value) {
    uint32_t bits;
    if (!Word(address, bits)) return false;
    value = std::bit_cast<float>(bits);
    return std::isfinite(value);
}

// The visible area's top edge in world units (y grows upward). dBgParameter_c's
// instance is the r13-22656 pointer read by the original screenCullCheck
// 800B56E0; its screen position is at +0x38 (getLoopScrollDispPosX reads +0x38)
// and its size at +0x40.
bool ScreenTop(const CpuContext* cpu, float& top) {
    uint32_t parameter;
    float height;
    return cpu && Pointer(cpu->gpr[13] - 22656u, 4) && Word(cpu->gpr[13] - 22656u, parameter) &&
        Pointer(parameter, 0x48) && Float(parameter + 0x3cu, top) &&
        Float(parameter + 0x44u, height) && height > 100.0f && height < 2000.0f;
}

// One step of the guided drop, on the guest thread after the item exists. Fields are
// dBaseActor_c's: position +0xAC, previous position +0xB8, speed +0xE8, forward
// speed +0x10C, gravity +0x114 and fall limit +0x118 (posMove 8006CD40,
// calcFallSpeed 8006CE70). Writing the position each frame carries the item through
// blocks; below Mario's head it is released to its original FreeDrop state.
void Guide(const CpuContext* cpu, const Context& game, float top, bool screen) {
    uint32_t id, profile, pause;
    float px, py, x, y, gravity, limit;
    if (!Pointer(guided.actor, 0x120) || !Word(guided.actor, id) || id != guided.id ||
        !Half(guided.actor + 8u, profile) || profile != 60 ||
        !Float(game.player + 0xacu, px) || !Float(game.player + 0xb0u, py) ||
        !Float(guided.actor + 0xacu, x) || !Float(guided.actor + 0xb0u, y)) { guided = {}; return; }
    if (!Word(0x80429F48u, pause) || pause != 0) return;
    if (guided.frame < kHoverFrames) {
        x = px;
        if (screen) y = std::max(top - kTopInset, py + 64.0f);
        // NSMB2: the released item blinks at the top of the screen before it drops.
        SetHidden(guided.actor, (guided.frame / kBlinkFrames) % 2 == 1);
    } else {
        if (guided.frame == kHoverFrames) {
            SetHidden(guided.actor, false);
            SystemSound(cpu, kSeItemAppear);
        }
        if (!Float(guided.actor + 0x114u, gravity) || gravity >= 0.0f || gravity < -1.0f) gravity = -0.125f;
        if (!Float(guided.actor + 0x118u, limit) || std::fabs(limit) < 1.0f || std::fabs(limit) > 8.0f) limit = 4.0f;
        guided.speed = std::max(guided.speed + gravity, -std::fabs(limit));
        y += guided.speed;
    }
    const bool release = guided.frame >= kHoverFrames && y <= py + 24.0f;
    const uint32_t xb = std::bit_cast<uint32_t>(x), yb = std::bit_cast<uint32_t>(y);
    Memory::TryWrite32(guided.actor + 0xacu, xb);
    Memory::TryWrite32(guided.actor + 0xb0u, yb);
    Memory::TryWrite32(guided.actor + 0xb8u, xb);
    Memory::TryWrite32(guided.actor + 0xbcu, yb);
    Memory::TryWrite32(guided.actor + 0xe8u, 0);
    Memory::TryWrite32(guided.actor + 0xecu, std::bit_cast<uint32_t>(release ? guided.speed : 0.0f));
    Memory::TryWrite32(guided.actor + 0x10cu, 0);
    if (release) guided = {};
    else ++guided.frame;
}
}

void ObserveAcceptedPickup(CpuContext* cpu) {
    if (!Enabled() || inGuestCall || !cpu || cpu->lr != 0x80A286A4u) return;
    // The hook is at the final successful pickup notification, after all
    // original collision eligibility decisions. Original execution continues.
    const uint32_t item = cpu->gpr[31];
    uint32_t kind, owner, id, profile;
    Context game;
    if ((cpu->gpr[29] != 1 && cpu->gpr[29] != 2) || !Pointer(item, 0xe00) ||
        !Half(item + 8u, profile) || profile != 60 || !Half(item + 0xdcau, kind) ||
        !Supported(kind) || !Byte(item + 0xdffu, owner) || !Word(item, id) ||
        !GetContext(cpu, game, false) || owner != game.index) return;
    Storage();
    std::lock_guard<std::mutex> lock(mutex);
    // This call is dScoreMng_c::UnKnownScoreSet for the pickup; its ScoreSet follows at once.
    if (id == spareId && game.scene == spareScene) { skipScore = true; spareId = 0; }
    if (id == lastPickupId && game.scene == lastPickupScene) return;
    lastPickupId = id;
    lastPickupScene = game.scene;
    // NSMB2: a duplicate goes into the box (a caught spare included, so it returns
    // to the box as on 3DS). Changing power-ups stores the displaced one, except
    // Super Mario's mushroom; Mini Mario turning Super keeps nothing either, as the
    // stored Mini Mushroom ranks below the mushroom. Small Mario stores nothing.
    int stored = SuitItem(game.suit);
    if (cpu->gpr[29] == 2) stored = static_cast<int>(kind);
    else if (stored == 0 || (stored == 13 && kind == 0)) return;
    if (stored < 0 || !Accepts(slots[game.slot], stored)) return;
    slots[game.slot] = stored;
    dirty = true;
    ++published[7];
}

void Tick(CpuContext* cpu) {
    if (!Enabled() || inGuestCall) return;
    auto& storage = Storage();
    Context game, activeGame;
    const bool valid = GetContext(cpu, game, false);
    const bool usable = valid && GetContext(cpu, activeGame, true);
    uint32_t scene = 0;
    Word(0x80428730u, scene);
    if ((scene & 0xffffu) != 5) { lastPickupId = 0; }
    const int useSlot = requested.exchange(-1, std::memory_order_acq_rel);
    int kind = -1;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (usable) kind = slots[game.slot];
    }
    bool spawned = false;
    // NSMB2: the stored item can always be dropped in a course, even one Mario already has.
    const bool changes = usable && kind >= 0;
    float top = 0;
    const bool screen = valid && ScreenTop(cpu, top);
    if (useSlot >= 0 && changes && useSlot == static_cast<int>(game.slot)) {
        uint32_t x, y, z, layer;
        if (Word(game.player + 0xacu, x) && Word(game.player + 0xb0u, y) &&
            Word(game.player + 0xb4u, z) && Byte(game.player + 0x38fu, layer) && layer < 2) {
            const float px = std::bit_cast<float>(x), py = std::bit_cast<float>(y), pz = std::bit_cast<float>(z);
            // The top of the screen, never below the old drop point 64 units above Mario.
            const float sy = screen ? std::max(top - kTopInset, py + 64.0f) : py + 64.0f;
            CpuContext spawn = *cpu;
            const uint32_t pos = (cpu->gpr[1] - 64u) & ~15u;
            if (std::isfinite(px) && std::isfinite(py) && std::isfinite(pz) &&
                Pointer(pos, 16) && Memory::TryWrite32(pos, x) &&
                Memory::TryWrite32(pos + 4u, std::bit_cast<uint32_t>(sy)) &&
                Memory::TryWrite32(pos + 8u, z)) {
                spawn.gpr[3] = 60;
                // Raw kind conversion is the original 80A2C050 switch. Preserve
                // original actor motion/collision/art rather than changing suit.
                // create_type 8 selects original FreeDrop: constructor's
                // table 80AF0E34[8] -> 80A270B4 -> state 80B168F8.
                spawn.gpr[4] = 0x08000000u | RawKind(static_cast<uint32_t>(kind));
                spawn.gpr[5] = pos;
                spawn.gpr[6] = 0;
                spawn.gpr[7] = layer;
                uint32_t issuedId;
                if (Call(0x80064610u, spawn) && Pointer(spawn.gpr[3], 0x120) &&
                    Word(spawn.gpr[3], issuedId)) {
                    spawned = true;
                    { std::lock_guard<std::mutex> lock(mutex); spareId = issuedId; spareScene = scene; }
                    // Taken out of the box: NSMBW's own stock-item use sound.
                    SystemSound(cpu, kSeStockItemUse);
                    if (screen) guided = {spawn.gpr[3], issuedId, scene, 0, 0};
                }
            }
        }
    }
    if (guided.actor && (!valid || scene != guided.scene)) {
        // An abandoned drop must never leave a collectable item invisible.
        uint32_t id;
        if (scene == guided.scene && Pointer(guided.actor, 0x120) && Word(guided.actor, id) && id == guided.id)
            SetHidden(guided.actor, false);
        guided = {};
    }
    if (guided.actor && !spawned) Guide(cpu, game, top, screen);
    std::lock_guard<std::mutex> lock(mutex);
    if (spawned) { slots[game.slot] = -1; ++published[6]; dirty = true; }
    ++published[1];
    published[2] = valid ? slots[game.slot] : -1;
    published[3] = changes && published[2] >= 0;
    published[4] = 0;
    published[5] = valid ? static_cast<int32_t>(game.slot) : -1;
    if (dirty) { storage.Save(slots); dirty = false; }
}

// ScoreSet (800E1F80) called from UnKnownScoreSet (return 800E2530) right after a caught
// spare: its own "no player" exit (r6 == -1) skips both the points and the popup.
void FilterScore(CpuContext* cpu) {
    if (!skipScore || !cpu) return;
    skipScore = false;
    cpu->gpr[6] = 0xffffffffu;
}

// The upper HUD as on NSMB DS: only coins and time stay on the upper screen; lives, Star
// Coins and score are on the lower one. dGameDisplay_c keeps the null panes registered by
// createLayout (801586F0, NPaneRegister): +1264 N_left_00, +1272 N_collection_00,
// +1276 N_score_00, +1280 N_areaZanki_00, +1284 N_areaCoin_00. nw4r::lyt::Pane keeps its
// translation at +0x2C and its flags at +0xBB (bit 0 visible), as RestDispSetup (80158580)
// uses them. Called on entry to AlphaEnterAndExit (80158A90), the last step of
// dGameDisplay_c::execute, so nothing in this frame turns the panes back on before drawing.
void UpperHud(CpuContext* cpu) {
    if (!Enabled() || !cpu || inGuestCall || NetplaySession::Active()) return;
    const uint32_t hud = cpu->gpr[3];
    if (!Pointer(hud, 1336)) return;
    for (uint32_t slot : {1264u, 1272u, 1276u}) {
        uint32_t pane, word;
        if (Word(hud + slot, pane) && Pointer(pane, 0xc0) && Word(pane + 0xb8u, word) && (word & 1u))
            Memory::TryWrite32(pane + 0xb8u, word & ~1u);
    }
    // The coin counter takes the lives' place in the top-left corner, as on the DS.
    uint32_t lives, coins, y;
    if (Word(hud + 1280u, lives) && Word(hud + 1284u, coins) && Pointer(lives, 0xc0) &&
        Pointer(coins, 0xc0) && Word(lives + 0x30u, y))
        Memory::TryWrite32(coins + 0x30u, y);
}

bool RequestUse() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!Enabled() || published[3] == 0) return false;
    int expected = -1;
    if (!requested.compare_exchange_strong(expected, published[5], std::memory_order_acq_rel)) return false;
    published[4] = 1;
    return true;
}
std::array<int32_t, 8> ReadWords() {
    std::lock_guard<std::mutex> lock(mutex);
    return published;
}
}
