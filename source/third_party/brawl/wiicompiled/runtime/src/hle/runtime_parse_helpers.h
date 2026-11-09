#pragma once

// Small guest-memory / string helpers shared by more than one HLE subsystem.
// Anything used by a single .cpp stays file-local.

#include "memory.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace RuntimeHle {

// An IOS ioctlv descriptor as it is laid out in guest memory: two big-endian
// words (address, size) per entry, 8 bytes apart. Shared by the /dev/net and
// /dev/isfs device layers, which previously each declared their own copy.
struct IoVector {
    uint32_t address = 0;
    uint32_t size = 0;
};

inline IoVector ReadIoVector(uint32_t vectorPtr, uint32_t index) {
    const uint32_t entry = vectorPtr + index * 8u;
    return {Memory::Read32(entry), Memory::Read32(entry + 4u)};
}

// ASCII-lowercase in place. Host path and disc-filename comparisons are
// case-insensitive; the guest side is not, so this only ever runs on host
// strings.
inline void LowerInPlace(std::string& text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
}

inline std::string Lower(std::string_view text) {
    std::string lowered(text);
    LowerInPlace(lowered);
    return lowered;
}

inline bool IsValidGameCode(uint32_t code) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        const char ch = static_cast<char>((code >> shift) & 0xffu);
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

inline uint32_t CurrentGameCode(uint32_t fallback) {
    if (Memory::Contains(0x80000000u, 4u)) {
        const uint32_t code = Memory::Read32(0x80000000u);
        if (IsValidGameCode(code)) {
            return code;
        }
    }

    return fallback;
}

} // namespace RuntimeHle
