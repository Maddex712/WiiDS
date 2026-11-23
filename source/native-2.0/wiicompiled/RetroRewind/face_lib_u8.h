#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace WiiDsFaceLib {

static inline uint32_t Read32(const uint8_t* bytes) {
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
           (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
}

static inline bool DecodeU8(const std::vector<uint8_t>& data, std::string_view target,
                     std::vector<uint8_t>& output) {
    constexpr size_t maxArchive = 64 * 1024 * 1024;
    constexpr size_t maxPayload = 32 * 1024 * 1024;
    constexpr size_t nodeSize = 12;
    if (data.size() < 0x20 || data.size() > maxArchive || Read32(data.data()) != 0x55AA382D) return false;

    const size_t root = Read32(data.data() + 4);
    const size_t headerSize = Read32(data.data() + 8);
    const size_t dataStart = Read32(data.data() + 12);
    if (root < 0x20 || root > data.size() || headerSize > data.size() - root ||
        headerSize < nodeSize || dataStart < root + headerSize || dataStart > data.size()) return false;
    const size_t headerEnd = root + headerSize;
    const uint8_t* const rootNode = data.data() + root;
    const size_t count = Read32(rootNode + 8);
    if (Read32(rootNode) != 0x01000000 || Read32(rootNode + 4) != 0 ||
        count == 0 || count > 65536 || count > (headerSize - 1) / nodeSize) return false;
    const size_t strings = root + count * nodeSize;
    if (data[strings] != 0) return false;

    struct Directory { size_t index; size_t end; };
    std::vector<Directory> stack{{0, count}};
    size_t foundOffset = 0;
    size_t foundSize = 0;
    for (size_t i = 1; i < count; ++i) {
        while (!stack.empty() && i == stack.back().end) stack.pop_back();
        if (stack.empty() || i >= stack.back().end) return false;
        const uint8_t* const node = rootNode + i * nodeSize;
        const uint32_t typeAndName = Read32(node);
        const uint32_t type = typeAndName >> 24;
        const size_t nameOffset = typeAndName & 0x00FFFFFF;
        if (nameOffset >= headerEnd - strings) return false;
        const char* const name = reinterpret_cast<const char*>(data.data() + strings + nameOffset);
        const size_t available = headerEnd - strings - nameOffset;
        const auto* const end = static_cast<const char*>(std::memchr(name, 0, std::min(size_t{256}, available)));
        if (!end || end == name) return false;
        const std::string_view entry(name, static_cast<size_t>(end - name));
        const size_t offset = Read32(node + 4);
        const size_t size = Read32(node + 8);
        if (type == 1) {
            if (offset != stack.back().index || size <= i || size > stack.back().end || stack.size() >= 64)
                return false;
            stack.push_back({i, size});
        } else if (type == 0) {
            if (offset > data.size() || (size && offset < dataStart) || size > data.size() - offset)
                return false;
            if (entry == target) {
                if (foundSize || !size || size > maxPayload) return false;
                foundOffset = offset;
                foundSize = size;
            }
        } else {
            return false;
        }
    }
    if (!foundSize) return false;
    std::vector<uint8_t> payload(foundSize);
    std::memcpy(payload.data(), data.data() + foundOffset, foundSize);
    output.swap(payload);
    return true;
}

static inline bool ExtractFromU8(const std::string& archive, std::string_view target, std::vector<uint8_t>& output) {
    std::ifstream file(archive, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto size = file.tellg();
    if (size < 0x20 || size > 64 * 1024 * 1024) return false;
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    return bool(file.read(reinterpret_cast<char*>(data.data()), data.size())) &&
           DecodeU8(data, target, output);
}

} // namespace WiiDsFaceLib
