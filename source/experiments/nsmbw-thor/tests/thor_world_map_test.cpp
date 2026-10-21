#include "thor_world_map.h"
#include "memory.h"
#include <atomic>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>
#include <unordered_map>

namespace {
std::unordered_map<uint32_t, uint32_t> words;
int checks = 0;
void Check(bool condition) { ++checks; if (!condition) { std::fprintf(stderr, "Failed check %d\n", checks); std::abort(); } }
constexpr uint32_t map = 0x80500000, first = map + 0x5c7f4 + 4, player = 0x80700000;
uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }
void Point(int id, uint32_t name, float x, float z) {
    const uint32_t address = first + id * 0x54;
    words[address + 4] = name; words[address + 8] = 0;
    words[address + 0xc] = Bits(x); words[address + 0x14] = Bits(z);
}
void Setup() {
    words.clear(); words[0x80428730] = 3; words[0x8042a18c] = map;
    words[map + 0x338c] = 0; words[0x8042a24c] = 0xff020000;
    for (uint32_t offset = 0; offset < 0x3f08; offset += 4) words[map + 0x5c7f4 + offset] = 0;
    Point(0, 0x57323031, -100, 80); // W201, real graph naming convention.
    Point(6, 0x46303100, 20, 300); // F01, intermediate path.
    Point(191, 0x57325430, 500, -50); // W2T0.
    words[first + 0x18] = first + 6 * 0x54;
    words[first + 6 * 0x54 + 0x18] = first + 191 * 0x54;
    words[0x8042a1a0] = player; words[player + 0xac] = Bits(8); words[player + 0xb4] = Bits(125);
}
std::vector<int32_t> Sample() { ThorWorldMap::Capture(); return ThorWorldMap::ReadWords(); }
}
bool Memory::TryRead32(uint32_t address, uint32_t& value) noexcept {
    const auto found = words.find(address); if (found == words.end()) return false;
    value = found->second; return true;
}
bool Memory::Contains(uint32_t address, size_t length) {
    return address >= 0x80000000 && address < 0x81800000 && length <= 0x81800000u - address;
}
int main() {
    setenv("NSMBW_THOR", "1", 1);
    Setup(); auto state = Sample();
    Check(state.size() == 48); Check(state[0] == 1 && state[1] == 1 && state[2] == 3);
    Check(state[3] == 2 && state[4] == 0 && state[5] == 3);
    Check(state[6] == static_cast<int32_t>(Bits(8)) && state[7] == static_cast<int32_t>(Bits(125)));
    Check(state[8] == static_cast<int32_t>(Bits(-100)) && state[9] == static_cast<int32_t>(Bits(500)));
    Check(state[10] == static_cast<int32_t>(Bits(-50)) && state[11] == static_cast<int32_t>(Bits(300)));
    Check(state[12] == 0 && state[13] == 1 && state[14] == 0x57323031 && state[15] == 0);
    Check(state[18] == 6 && state[24] == 6 && state[25] == 0 && state[30] == 191 && state[36] == 191);
    state[12] = 99; Check(ThorWorldMap::ReadWords()[12] == 0); // Immutable copy.
    words[player + 0xac] = Bits(50); state = Sample();
    Check(state[6] == static_cast<int32_t>(Bits(50)) && state[1] == 2);
    Check(state[8] == static_cast<int32_t>(Bits(-100))); // Bounds never follow marker.
    words[first + 0x18] = first + 1; state = Sample(); Check(state[18] == -1);
    words[first + 0x18] = first + 4 * 0x54; state = Sample(); Check(state[18] == -1); // Unused target.
    words[first + 0x18] = first + 192 * 0x54; state = Sample(); Check(state[18] == -1);
    words[player + 0xac] = Bits(std::numeric_limits<float>::quiet_NaN()); state = Sample();
    Check(state[2] == 1 && state[6] == 0 && state[7] == 0);
    words[0x8042a1a0] = 0x817ffffc; state = Sample(); Check(state[2] == 1);
    words[0x8042a24c] = 0x00090000; state = Sample(); Check(state[3] == -1);
    words[first + 6 * 0x54 + 0xc] = Bits(std::numeric_limits<float>::infinity()); state = Sample();
    Check(state[5] == 2 && state.size() == 36);
    Setup(); words[first + 4] = 0x57003031; state = Sample(); Check(state[5] == 2); // Embedded zero.
    Setup(); words[first + 4] = 0x57322f31; state = Sample(); Check(state[5] == 2); // Invalid name.
    Setup(); words[map + 0x338c] = 4; state = Sample(); Check(state.size() == 12 && state[2] == 0);
    Setup(); words[0x8042a18c] = 0x817fff00; state = Sample(); Check(state.size() == 12 && state[2] == 0);
    Setup(); words[0x8042a18c] = map + 1; state = Sample(); Check(state[2] == 0);
    Setup(); words[0x8042a18c] = 0x817c0000; words[0x817c338c] = 0;
    state = Sample(); Check(state[2] == 0); // Valid header, connect would exceed MEM1.
    Setup(); words.erase(first + 0xc); state = Sample(); Check(state[5] == 2);
    Setup(); words[first + 8] = 0x37000000; state = Sample(); Check(state[15] == '7');
    Setup(); words[player + 0xb4] = Bits(-1e8f); state = Sample(); Check(state[2] == 1);
    Setup(); for (int id = 0; id < 192; ++id) Point(id, 0x46303031, id * 4.0f, 0);
    state = Sample(); Check(state.size() == 12 + 192 * 12 && state[5] == 192);
    Setup(); const auto original = words;
    words[map + 0x338c] = 3;
    for (uint32_t offset = 0; offset < 0x3f08; offset += 4)
        words[map + 0x5c7f4 + 3 * 0x3f08 + offset] = original.at(map + 0x5c7f4 + offset);
    // The alternate graph's own links must reference the alternate table.
    words[first + 3 * 0x3f08 + 0x18] = first + 3 * 0x3f08 + 6 * 0x54;
    state = Sample(); Check(state[4] == 3 && state[5] == 3 && state[18] == 6);
    Setup(); words[0x80428730] = 5; state = Sample(); Check(state.size() == 12 && state[2] == 0 && state[3] == -1);
    Setup(); words.erase(0x80428730); state = Sample(); Check(state[2] == 0);
    Setup(); for (int id : {0, 6, 191}) words[first + id * 0x54 + 4] = 0;
    state = Sample(); Check(state[2] == 0 && state.size() == 12);
    Setup(); std::atomic<bool> running{true}; std::atomic<bool> broken{false};
    std::thread reader([&] { while (running.load()) {
        const auto copy = ThorWorldMap::ReadWords();
        if (copy.size() != 12 + static_cast<size_t>(copy[5]) * 12 || copy[0] != 1) broken = true;
    }});
    for (int i = 0; i < 1000; ++i) Sample();
    running = false; reader.join(); Check(!broken.load());
    std::printf("Passed %d actual-source world-map checks (mock guest memory).\n", checks);
}
