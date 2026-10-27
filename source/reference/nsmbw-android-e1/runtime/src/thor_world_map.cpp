#include "thor_world_map.h"
#include "memory.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string_view>

namespace ThorWorldMap {
namespace {
constexpr uint32_t kScene = 0x80428730u;
constexpr uint32_t kMapInstance = 0x8042A18Cu;
constexpr uint32_t kPlayerInstance = 0x8042A1A0u;
constexpr uint32_t kWorldWord = 0x8042A24Cu; // getWorldNo reads the byte at +1.
constexpr uint32_t kSubIndex = 0x338Cu, kConnect = 0x5C7F4u;
constexpr uint32_t kConnectSize = 0x3F08u, kPointSize = 0x54u;
constexpr int kPointCount = 192;
constexpr size_t kHeaderWords = 12, kPointWords = 12;
std::mutex mutex;
std::vector<int32_t> published{1, 0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0};
uint32_t sequence = 0;

bool Word(uint32_t address, uint32_t& value) { return Memory::TryRead32(address, value); }
bool Pointer(uint32_t address, uint32_t length) {
    return (address & 3u) == 0 && address >= 0x80000000u &&
           address < 0x81800000u && length <= 0x81800000u - address &&
           Memory::Contains(address, length);
}
bool Byte(uint32_t address, uint32_t& value) {
    uint32_t word = 0;
    if (!Word(address & ~3u, word)) return false;
    value = (word >> (24u - (address & 3u) * 8u)) & 255u;
    return true;
}
uint32_t WorldUnlockFlags() {
    // E1 IsWorldOpen (800FB2D0) uses getSaveGame(-1) and world flag bit 0.
    // getSaveGame (800E0380): manager + 0x6C0 + selected slot * 0x980.
    // isWorldDataFlag (800CE350): byte at game + 0x32 + world.
    uint32_t manager = 0, slot = 0;
    if (!Word(0x8042A040u, manager) || !Pointer(manager, 0x28u) ||
        !Byte(manager + 0x26u, slot) || slot >= 3u ||
        !Pointer(manager, 0x6C0u + slot * 0x980u + 0x3Cu)) return 0;
    uint32_t mask = 0;
    for (uint32_t world = 0; world < 9u; ++world) {
        uint32_t flags = 0;
        if (!Byte(manager + 0x6C0u + slot * 0x980u + 0x32u + world, flags)) return 0;
        if (flags & 1u) mask |= 1u << world;
    }
    return 4u | (mask << 8u);
}
bool Coordinate(uint32_t address, uint32_t& bits) {
    if (!Word(address, bits)) return false;
    const float value = std::bit_cast<float>(bits);
    return std::isfinite(value) && std::fabs(value) < 1.0e7f;
}
bool NameCharacter(uint32_t value) {
    return (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9');
}
struct Point {
    bool used = false;
    uint32_t name = 0, fifth = 0, x = 0, z = 0;
    std::array<int32_t, 6> links{-1, -1, -1, -1, -1, -1};
};

void Publish(std::vector<int32_t>& next) {
    std::lock_guard<std::mutex> lock(mutex);
    published.swap(next);
}
}

void Capture() {
    static const bool enabled = [] {
        const char* value = std::getenv("NSMBW_THOR");
        return value && std::string_view(value) == "1";
    }();
    if (!enabled) return;
    std::vector<int32_t> next{1, static_cast<int32_t>(++sequence), 0, -1, -1, 0, 0, 0, 0, 0, 0, 0};
    uint32_t scene = 0, map = 0, sub = 0;
    if (!Word(kScene, scene) || (scene & 0xffffu) != 3u ||
        !Word(kMapInstance, map) || !Pointer(map, kSubIndex + 4u) ||
        !Word(map + kSubIndex, sub) || sub > 3u) {
        Publish(next);
        return;
    }
    // Check the addition before forming a guest address. The complete active
    // connect object, including every potential link target, must be mapped.
    const uint32_t offset = kConnect + sub * kConnectSize;
    if (offset > 0x81800000u - map || !Pointer(map + offset, kConnectSize)) {
        Publish(next);
        return;
    }
    const uint32_t first = map + offset + 4u;
    std::array<Point, kPointCount> points;
    bool haveBounds = false;
    float minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    for (int index = 0; index < kPointCount; ++index) {
        const uint32_t address = first + static_cast<uint32_t>(index) * kPointSize;
        Point& point = points[index];
        uint32_t suffix = 0;
        if (!Word(address + 4u, point.name) || (point.name >> 24) == 0u ||
            !Word(address + 8u, suffix)) continue;
        bool validName = true, ended = false;
        for (int c = 0; c < 5; ++c) {
            const uint32_t character = c < 4 ? (point.name >> (24 - c * 8)) & 0xffu : suffix >> 24;
            if (character == 0) ended = true;
            else if (ended || !NameCharacter(character)) validName = false;
        }
        if (!validName || !Coordinate(address + 0xCu, point.x) || !Coordinate(address + 0x14u, point.z)) continue;
        point.fifth = suffix >> 24;
        point.used = true;
        const float x = std::bit_cast<float>(point.x), z = std::bit_cast<float>(point.z);
        if (!haveBounds) { minX = maxX = x; minZ = maxZ = z; haveBounds = true; }
        else {
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (z < minZ) minZ = z;
            if (z > maxZ) maxZ = z;
        }
        for (int direction = 0; direction < 6; ++direction) {
            uint32_t link = 0;
            if (Word(address + 0x18u + static_cast<uint32_t>(direction) * 4u, link) &&
                link >= first && link < first + kPointCount * kPointSize &&
                (link - first) % kPointSize == 0u) {
                point.links[direction] = static_cast<int32_t>((link - first) / kPointSize);
            }
        }
    }
    if (!haveBounds) { Publish(next); return; }
    next[2] = static_cast<int32_t>(1u | WorldUnlockFlags());
    uint32_t world = 0;
    if (Word(kWorldWord, world) && ((world >> 16) & 0xffu) < 9u) next[3] = (world >> 16) & 0xffu;
    next[4] = static_cast<int32_t>(sub);
    next[8] = std::bit_cast<int32_t>(minX); next[9] = std::bit_cast<int32_t>(maxX);
    next[10] = std::bit_cast<int32_t>(minZ); next[11] = std::bit_cast<int32_t>(maxZ);
    uint32_t player = 0, playerX = 0, playerZ = 0;
    if (Word(kPlayerInstance, player) && Pointer(player, 0xB8u) &&
        Coordinate(player + 0xACu, playerX) && Coordinate(player + 0xB4u, playerZ)) {
        next[2] |= 2;
        next[6] = static_cast<int32_t>(playerX); next[7] = static_cast<int32_t>(playerZ);
    }
    next.reserve(kHeaderWords + kPointCount * kPointWords);
    for (int index = 0; index < kPointCount; ++index) {
        const Point& point = points[index];
        if (!point.used) continue;
        ++next[5];
        next.insert(next.end(), {index, (point.name >> 24) == 'W' ? 1 : 0,
            static_cast<int32_t>(point.name), static_cast<int32_t>(point.fifth),
            static_cast<int32_t>(point.x), static_cast<int32_t>(point.z)});
        for (const int32_t link : point.links) next.push_back(link >= 0 && points[link].used ? link : -1);
    }
    Publish(next);
}

std::vector<int32_t> ReadWords() {
    std::lock_guard<std::mutex> lock(mutex);
    return published;
}
}
