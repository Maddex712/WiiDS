// "30 FPS races": make every race run in the game's own 30 frames-per-second mode.
//
// Mario Kart Wii already has one. When a race scene starts it picks 30 FPS for three or four
// screens, and for two screens online (0x80554224..0x80554270), and passes the answer to
// GameScene::setFramerate (0x8051AF84, r4 = 1 for 30 FPS). That stores the flag the game logic
// reads to take two steps per drawn frame, and tells the display to show each frame for two
// retraces, so the race plays at normal speed while drawing half as often.
//
// This option turns that answer into "30 FPS" for every race. A device that cannot hold 60 then
// plays at full speed instead of in slow motion, and one that can runs cooler. Only the race
// scene's own call is touched (by its return address): menus and every other scene keep choosing
// for themselves, exactly as they do in a four-player race on a console.
//
// Retro Rewind (pack 6.13.0, the Code.pul this build is tied to) decides again after the game has:
// its own routine at 0x8183A6D0 calls setFramerate with the pack's "30 FPS" setting, which put a
// race back to 60 whenever that setting was off - so this option did nothing there. That call is
// turned into "30 FPS" as well, except where Retro Rewind itself insists on 60 whatever its
// setting says (scene field 0x28 == 2 in game modes 2 and 5), which is left alone.
//
// Runs on entry to the callee (see ApplyRuntimeCallOptions in abi_bridge.h).

#include "abi_bridge.h"
#include "memory.h"

#include <atomic>

namespace {

constexpr uint32_t kSetFramerateReturnInRaceScene = 0x80554274u;
constexpr uint32_t kSetFramerateReturnInRetroRewind = 0x8183A748u;
constexpr uint32_t kThirtyFramesPerSecond = 1u;
constexpr uint32_t kRacedataPointer = 0x809BD728u;    // Racedata::sInstance
constexpr uint32_t kGameModeOffset = 0xB70u;
constexpr uint32_t kSceneKindOffset = 0x28u;

// The case Retro Rewind's routine keeps at 60 regardless of its own setting.
bool RetroRewindInsistsOnSixty(uint32_t scene) {
    if (!Memory::Contains(scene + kSceneKindOffset, 4) || Memory::Read32(scene + kSceneKindOffset) != 2u ||
        !Memory::Contains(kRacedataPointer, 4)) {
        return false;
    }
    const uint32_t racedata = Memory::Read32(kRacedataPointer);
    if (!Memory::Contains(racedata + kGameModeOffset, 4)) {
        return false;
    }
    const uint32_t mode = Memory::Read32(racedata + kGameModeOffset);
    return mode == 2u || mode == 5u;
}

std::atomic<bool> g_enabled{false};

} // namespace

void RuntimeSetThirtyFpsRaces(bool enabled) noexcept {
    g_enabled.store(enabled, std::memory_order_relaxed);
}

extern "C" void RuntimeFrameRateLockHook(CpuContext* ctx) {
    if (!g_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    if (ctx->lr == kSetFramerateReturnInRaceScene ||
        (ctx->lr == kSetFramerateReturnInRetroRewind && !RetroRewindInsistsOnSixty(ctx->gpr[3]))) {
        ctx->gpr[4] = kThirtyFramesPerSecond;
    }
}
