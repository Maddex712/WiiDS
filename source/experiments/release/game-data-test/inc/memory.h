#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace Memory {
inline std::vector<uint8_t>& Ram() { static std::vector<uint8_t> r(0x01800000); return r; }
inline bool Contains(uint32_t a, size_t n) { return a >= 0x80000000u && a - 0x80000000u + n <= Ram().size(); }
inline uint8_t* GetPointer(uint32_t a, size_t) { return Ram().data() + (a - 0x80000000u); }
}
