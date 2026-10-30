#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace RuntimeHle {

using PrintfNext32 = std::function<uint32_t()>;
using PrintfNext64 = std::function<uint64_t()>;
using PrintfNextDouble = std::function<double()>;
using PrintfReadString = std::function<std::string(uint32_t)>;

std::string FormatGuestPrintf(const std::string& format,
                              size_t& outChars,
                              const PrintfNext32& next32,
                              const PrintfNext64& next64,
                              const PrintfNextDouble& nextDouble,
                              const PrintfReadString& readString);

} // namespace RuntimeHle
