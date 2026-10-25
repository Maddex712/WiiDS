#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ThorState {
inline constexpr size_t kFieldCount = 20;
using Snapshot = std::array<int32_t, kFieldCount>;
// Capture on the guest thread only. Android receives a copy, never guest RAM.
void Capture();
Snapshot Read();
// Bounded read-only gameplay events; schema, profile hash hi/lo, followed by six-word events.
std::vector<int32_t> AchievementEvents(int cursor);
}
