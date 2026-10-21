// "Low-detail computer karts": make single-player races load computer-controlled karts the way the
// game itself loads them in split-screen.
//
// With two or more screens Mario Kart Wii gives every computer kart the "_4" archive (a one-bone
// driver, "model_cpu", and the kart body and tyres merged into one model) and skips the driver's
// animation. With one screen it loads the full archive for all twelve karts: separate driver, body
// and tyre models and a fifteen-bone animated driver each, which is most of what a full race costs
// over a time trial. The decision is made in three places, all from the screen count:
//
//   RaceScene::OnEnter          passes the archive suffix to ArchiveMgr::LoadKartArchive (r8)
//   Kart::BRRESHandle::__ct     sets handle+0x11 for karts that are not a local player
//   DriverController::Load      sets controller+0xA8 for computer drivers
//
// These hooks reproduce the split-screen answers for computer karts while one screen is shown. They
// run on entry to the callees (see ApplyRuntimeCallOptions in abi_bridge.h); the player's own kart,
// ghosts and online opponents are left alone.

#include "abi_bridge.h"
#include "memory.h"
#include "runtime_log.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <string>

extern "C" const char* DVDResolveHostPathForTest(const char* dvdPath);

namespace {

constexpr uint32_t kRacedataPointer = 0x809BD728u;    // Racedata::sInstance
constexpr uint32_t kScreenCountOffset = 0x27u;
constexpr uint32_t kPlayerStride = 0xF0u;
constexpr uint32_t kPlayerVehicleOffset = 0x30u;
constexpr uint32_t kPlayerCharacterOffset = 0x34u;
constexpr uint32_t kPlayerTypeOffset = 0x38u;
constexpr uint32_t kPlayerTypeComputer = 1u;
constexpr uint32_t kMaxPlayers = 12u;

// ArchiveMgr::LoadKartArchive's own name tables ("Race/Kart/%s%s-%s%s").
constexpr uint32_t kCharacterNames = 0x808B3960u + 0x130u;   // 0x30 entries, indexed by character
constexpr uint32_t kVehicleNames = 0x808B3960u + 0x1F0u;     // 0x24 entries, indexed by vehicle
constexpr uint32_t kTeamSuffixes = 0x808B3960u + 0x280u;
constexpr uint32_t kLoadKartArchiveReturnInOnEnter = 0x805540F8u;
// Retro Rewind (pack 6.13.0, the Code.pul this build is tied to) leaves that call alone and gives
// computer drivers their costumes inside LoadKartArchive instead: it replaces the snprintf that
// builds the archive name (0x80540EF4) with its own, which tries "<character>-<n><suffix>" and
// falls back to the plain name when that archive does not exist. Costumes ship no "_4" archive, so
// a costumed computer driver gets the plain character's low-detail one with nothing to do here.
// (6.12.8 wrapped the call from 0x818026B0 and needed its own case in this file.)
constexpr uint32_t kSuffixSplitScreen = 2u;   // "_4"

std::atomic<bool> g_enabled{false};
// Which players got the low-detail archive in the race being loaded. Guest thread only.
std::array<bool, kMaxPlayers> g_lowDetail{};

std::string GuestString(uint32_t address) {
    std::string text;
    if (address == 0) {
        return text;
    }
    for (uint32_t i = 0; i < 64 && Memory::Contains(address + i); ++i) {
        const char c = static_cast<char>(Memory::Read8(address + i));
        if (c == 0) {
            break;
        }
        text.push_back(c);
    }
    return text;
}

uint32_t Racedata() {
    return Memory::Contains(kRacedataPointer, 4) ? Memory::Read32(kRacedataPointer) : 0;
}

// r4 player, r5 vehicle, r6 character, r7 team colour, r8 archive suffix.
void OnLoadKartArchive(CpuContext* ctx) {
    const uint32_t player = ctx->gpr[4] & 0xFFu;
    if (player >= kMaxPlayers) {
        return;
    }
    g_lowDetail[player] = false;
    if (!g_enabled.load(std::memory_order_relaxed) ||
        ctx->lr != kLoadKartArchiveReturnInOnEnter) {
        return;
    }
    const uint32_t racedata = Racedata();
    if (racedata == 0 || Memory::Read8(racedata + kScreenCountOffset) != 1 || ctx->gpr[8] != 0) {
        return;
    }
    // The caller read vehicle and character from this player's entry; check we are looking at the
    // same one before trusting its type.
    const uint32_t entry = racedata + player * kPlayerStride;
    const uint32_t vehicle = ctx->gpr[5];
    const uint32_t character = ctx->gpr[6];
    if (Memory::Read32(entry + kPlayerVehicleOffset) != vehicle ||
        Memory::Read32(entry + kPlayerCharacterOffset) != character ||
        Memory::Read32(entry + kPlayerTypeOffset) != kPlayerTypeComputer || vehicle >= 0x24u || character >= 0x30u ||
        ctx->gpr[7] > 2u) {
        return;
    }
    const std::string vehiclePart = "/Race/Kart/" + GuestString(Memory::Read32(kVehicleNames + vehicle * 4)) +
                                    GuestString(Memory::Read32(kTeamSuffixes + ctx->gpr[7] * 4)) + "-";
    const uint32_t nameSlot = kCharacterNames + character * 4;
    std::string path = vehiclePart + GuestString(Memory::Read32(nameSlot)) + "_4.szs";
    if (DVDResolveHostPathForTest(path.c_str()) == nullptr) {
        RT_LOGF(RT_TAG_RUNTIME, "low-detail karts: no %s, player %u keeps the full model\n", path.c_str(),
                static_cast<unsigned>(player));
        return;
    }
    ctx->gpr[8] = kSuffixSplitScreen;
    g_lowDetail[player] = true;
}

// r3 = Kart::BRRESHandle, already constructed; +0x13 is its player.
void OnLoadBrres(CpuContext* ctx) {
    const uint32_t handle = ctx->gpr[3];
    if (!Memory::Contains(handle, 0x18)) {
        return;
    }
    const uint32_t player = Memory::Read8(handle + 0x13u);
    if (player < kMaxPlayers && g_lowDetail[player]) {
        Memory::Write8(handle + 0x11u, 1);
    }
}

// r3 = DriverController, r4 = player. Load() has just written +0xA8 from the screen count.
void OnDriverLoadModels(CpuContext* ctx) {
    const uint32_t controller = ctx->gpr[3];
    const uint32_t player = ctx->gpr[4] & 0xFFu;
    if (player < kMaxPlayers && g_lowDetail[player] && Memory::Contains(controller, 0xCC)) {
        Memory::Write8(controller + 0xA8u, 1);
    }
}

} // namespace

void RuntimeSetLowDetailComputerKarts(bool enabled) noexcept {
    g_enabled.store(enabled, std::memory_order_relaxed);
}

extern "C" void RuntimeLowDetailKartsHook(uint32_t target, CpuContext* ctx) {
    switch (target) {
    case 0x80540E3Cu:
        OnLoadKartArchive(ctx);
        break;
    case 0x80576D8Cu:
    case 0x80576E90u:
        OnLoadBrres(ctx);
        break;
    case 0x807C7828u:
        OnDriverLoadModels(ctx);
        break;
    default:
        break;
    }
}
