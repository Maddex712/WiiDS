#pragma once
#include <cstddef>
#include <cstdint>

// Deliberately read-only API: compiling the real snapshot source against this
// mock fails if it starts writing guest memory.
class Memory {
public:
    static constexpr uint32_t kMem2CachedBase = 0x90000000u;
    static constexpr uint32_t kMem2CachedEnd = 0x98000000u;
    static bool TryRead32(uint32_t address, uint32_t& value) noexcept;
    static bool Contains(uint32_t address, size_t length = 1);
};
