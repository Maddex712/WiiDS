#pragma once
#include <cstdint>
#include <vector>

namespace port_android {
struct CpuWeight { int id; uint32_t weight; };

// Keep a group of fast cores, including both big and prime tiers. Never
// guess from CPU numbering, mix ranking scales, or restrict to one core.
inline std::vector<int> performanceCores(const std::vector<CpuWeight>& allowed) {
    uint32_t peak = 0;
    for (const auto& cpu : allowed) {
        if (!cpu.weight) return {};
        if (cpu.weight > peak) peak = cpu.weight;
    }
    if (!peak) return {};
    std::vector<int> selected;
    for (const auto& cpu : allowed)
        if (uint64_t(cpu.weight) * 4 >= uint64_t(peak) * 3) selected.push_back(cpu.id);
    // A homogeneous topology or an already-restricted allowed mask needs
    // no additional affinity. A single prime core would create contention.
    if (selected.size() < 2 || selected.size() == allowed.size()) return {};
    return selected;
}
}
