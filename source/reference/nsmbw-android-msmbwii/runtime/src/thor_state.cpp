#include "thor_state.h"
#include "memory.h"
#include <cstdlib>
#include <algorithm>
#include <bit>
#include <cmath>
#include <mutex>
#include <string_view>

namespace ThorState {
namespace {
std::mutex mutex;
Snapshot published{};
uint32_t sequence = 0;
bool Word(uint32_t address, uint32_t& value) { return Memory::TryRead32(address, value); }
bool Pointer(uint32_t p, size_t size) {
    return p >= 0x80000000u && (p & 3u) == 0 &&
        static_cast<uint64_t>(p) + size <= 0x81800000ull && Memory::Contains(p, size);
}
bool CourseDataPointer(uint32_t p, size_t size) {
    // Stage archives are allocated in cached MEM2 on the real E1 runtime.
    // Actor/manager pointers remain MEM1-only; only the course-data array may
    // use either cached RAM bank, still checked against actual mapped memory.
    const bool mem2 = p >= Memory::kMem2CachedBase && (p & 3u) == 0 &&
        static_cast<uint64_t>(p) + size <= Memory::kMem2CachedEnd;
    return Pointer(p, size) || (mem2 && Memory::Contains(p, size));
}
bool Byte(uint32_t a, uint32_t& value) {
    uint32_t word = 0;
    if (!Word(a & ~3u, word)) return false;
    value = (word >> (24u - (a & 3u) * 8u)) & 255u;
    return true;
}
bool Float(uint32_t a, float& value) {
    uint32_t word = 0;
    if (!Word(a, word)) return false;
    value = std::bit_cast<float>(word);
    return std::isfinite(value);
}
// daPyMng::getPlayer -> fBase registry, not a guessed direct object pointer.
// Canonical E1 8005F900 / 80162D00 / 80162C30. Bound traversal rejects bad cycles.
uint32_t PlayerOne() {
    uint32_t id = 0, node = 0;
    if (!Word(0x80354E10u, id) || id == 0 || id == 0xffffffffu ||
        !Word(0x80377A70u + (id & 7u) * 8u, node)) return 0;
    for (unsigned i = 0; node && i < 4096; ++i) {
        uint32_t object = 0, actualId = 0, next = 0;
        if (!Pointer(node, 12) || !Word(node + 8u, object) ||
            !Pointer(object, 180) || !Word(object, actualId)) return 0;
        if (actualId == id) return object;
        if (!Word(node + 4u, next) || next == node) return 0;
        node = next;
    }
    return 0;
}
void CourseExtras(Snapshot& next, uint32_t mode) {
    // Scene 5 becomes current before stage actors and run globals are ready.
    // BSS ownership zeroes are valid player indices, so never interpret them
    // until both the active stage and actual player object have been resolved.
    uint32_t stage = 0;
    if (!Word(0x8042A1C8u, stage) || !Pointer(stage, 4624)) return;
    const uint32_t player = PlayerOne();
    if (!player) return;
    uint32_t mask = 0;
    bool starsValid = true;
    // Original HUD setCollectCoin (80159AF0): 4 is uncollected; 0..3 own it.
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t owner = 0;
        if (!Word(0x803741B0u + i * 4u, owner) || owner > 4) starsValid = false;
        else if (owner < 4) mask |= 1u << i;
    }
    if (starsValid) { next[2] |= 64; next[13] = static_cast<int32_t>(mask); }
    uint32_t score = 0;
    if (Word(0x80429CC0u, score) && score <= 999999999u) {
        next[2] |= 128; next[15] = static_cast<int32_t>(score);
    }
    // Same loaded course pair and selected-save chain the original HUD reads.
    uint32_t world = 0, course = 0, header = 0, slot = 0, flags = 0;
    if (mode == 0 && Byte(stage + 4620u, world) && world < 10 &&
        Byte(stage + 4621u, course) && course < 42 &&
        Word(0x8042A040u, header) && Pointer(header, 40) &&
        Byte(header + 38u, slot) && slot < 3) {
        const uint32_t flagsAddress = header + slot * 2432u + 1728u + world * 168u + course * 4u + 108u;
        if (Pointer(flagsAddress, 4) && Word(flagsAddress, flags)) {
            next[2] |= 256; next[14] = static_cast<int32_t>(flags & 7u);
        }
    }
    uint32_t file = 0, zoneId = 0, cd = 0, zones = 0, count = 0;
    if (!Byte(stage + 4622u, file) || file >= 4 || !Byte(stage + 4623u, zoneId) ||
        !Word(0x80429E98u, cd) || !Pointer(cd, 3776)) return;
    const uint32_t cdFile = cd + file * 944u;
    if (!Word(cdFile + 44u, zones) || !Word(cdFile + 156u, count) ||
        count == 0 || count > 256 || !CourseDataPointer(zones, count * 24u)) return;
    float x = 0, y = 0;
    if (!Float(player + 172u, x) || !Float(player + 176u, y)) return;
    // getAreaDataP 8008E410: real active-area x/y/width/height, 24-byte records.
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t record = zones + i * 24u;
        uint32_t id = 0, xy = 0, wh = 0;
        if (!Byte(record + 12u, id) || id != zoneId) continue;
        if (!Word(record, xy) || !Word(record + 4u, wh)) return;
        const float left = static_cast<float>(xy >> 16);
        const float top = -static_cast<float>(xy & 65535u);
        const float width = static_cast<float>(wh >> 16);
        const float height = static_cast<float>(wh & 65535u);
        // A horizontal strip cannot truthfully summarize a vertical/square area.
        // Permit the small entry/goal margins used by the original stage actors.
        if (width < 32 || height < 16 || width <= height ||
            x < left - 64 || x > left + width + 64 || y > top + 128 || y < top - height - 128) return;
        next[2] |= 32;
        next[12] = static_cast<int32_t>(std::lround(std::clamp((x - left) / width, 0.0f, 1.0f) * 1000));
        next[16] = static_cast<int32_t>(zoneId);
        next[18] = static_cast<int32_t>(std::lround(x * 16));
        next[19] = static_cast<int32_t>(std::lround(width * 16));
        return;
    }
}
}

void Capture() {
    static const bool enabled = [] {
        const char* value = std::getenv("NSMBW_THOR");
        return value && std::string_view(value) == "1";
    }();
    if (!enabled) return;
    // [schema, sequence, fields, scene, mode, world, stage, character, lives, coins, time, paused]
    // All optional values start unavailable. This does not change or hide the original Wii HUD.
    Snapshot next{2, static_cast<int32_t>(++sequence), 0, -1, -1, -1, -1, -1, -1, -1, -1, 0,
        -1, -1, -1, -1, -1, -1, -1, -1};
    uint32_t scene = 0, mode = 0;
    if (Word(0x80428730u, scene) && (scene & 0xffffu) < 0x300u) {
        next[2] |= 1;
        next[3] = static_cast<int32_t>(scene & 0xffffu);
    }
    // STAGE (5) is also the title screen. m_startGameInfo.mGameMode 2/3 are title/replay.
    // Every global below is unchanged in the strict E1 -> E2 map's
    // 800e0d20-807683f6 +0 range; see build/nsmbw-thor/address-map.txt [E2].
    // See private owner-generated telemetry evidence for field semantics.
    const bool modeRead = next[3] == 5 && Word(0x80315B98u, mode) && mode < 4;
    if (modeRead) next[4] = static_cast<int32_t>(mode);
    const bool stage = modeRead && mode < 2;
    if (stage) {
        CourseExtras(next, mode);
        uint32_t course = 0;
        if (Word(0x80315B9Cu, course) && ((course >> 24) & 0xffu) < 9) {
            next[2] |= 2;
            // StartGameInfo's shown pair is distinct from the internally loaded course.
            next[5] = static_cast<int32_t>((course >> 24) & 0xffu); // shown world, +0xC
            next[6] = static_cast<int32_t>((course >> 16) & 0xffu); // shown stage, +0xD
        }
        // getCoinAll (80060200), decRest (80060600): type[player] selects the character arrays.
        uint32_t character = 0, lives = 0, coins = 0;
        if (Word(0x80354E60u, character) && character < 4 &&
            Word(0x80354E90u + character * 4u, lives) && lives <= 99 &&
            Word(0x80354EA0u + character * 4u, coins) && coins <= 99) {
            next[2] |= 4;
            next[7] = static_cast<int32_t>(character);
            next[8] = static_cast<int32_t>(lives);
            next[9] = static_cast<int32_t>(coins);
        }
        // dTimer: createInstance 800E3870 -> global; execute 800E3930 rounds Q12 up for display.
        uint32_t timer = 0, remaining = 0;
        if (Word(0x8042A070u, timer) && timer >= 0x80000000u && timer < 0x817ffff0u &&
            (timer & 3u) == 0 && Memory::Contains(timer, 12) && Word(timer + 4u, remaining) &&
            remaining <= (999u << 12)) {
            next[2] |= 8;
            next[10] = static_cast<int32_t>((remaining + 4095u) >> 12);
        }
        uint32_t stop = 0;
        if (Word(0x80429F48u, stop)) {
            next[2] |= 16;
            next[11] = (stop & 5u) != 0 ? 1 : 0;
        }
    }
    // The world map (scene 3) shows lives too, as NSMB2's lower map does: the same per-character
    // player type and rest arrays, which persist across scenes. Coins stay course-only.
    if (next[3] == 3) {
        uint32_t character = 0, lives = 0;
        if (Word(0x80354E60u, character) && character < 4 &&
            Word(0x80354E90u + character * 4u, lives) && lives <= 99) {
            next[2] |= 4;
            next[7] = static_cast<int32_t>(character);
            next[8] = static_cast<int32_t>(lives);
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    published = next;
}

Snapshot Read() {
    std::lock_guard<std::mutex> lock(mutex);
    return published;
}
}
