#include "hle/disc_interface.h"

#include "memory.h"
#include "runtime_config.h"
#include "runtime_log.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace DiscInterface {
namespace {

constexpr uint32_t kDeviceFd = 0x44490001u;
constexpr int32_t kInvalid = -4;
constexpr int32_t kNotFound = -6;
constexpr int32_t kIoError = -12;
constexpr int32_t kDiComplete = 1;
constexpr uint32_t kCommandBytes = 32;
std::mutex g_mutex;
bool g_open = false;

// Hold the complete result outside guest memory until its size and read have
// both succeeded. A failed read must leave the guest's destination unchanged.
int32_t ReadMetadata(const std::filesystem::path& path, size_t count,
                     bool exactSize, std::array<uint8_t, 64>& bytes)
{
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        RT_LOG(RT_TAG_DVD) << "DI metadata status failed: " << path << std::endl;
        return kIoError;
    }
    if (!exists) {
        RT_LOG(RT_TAG_DVD) << "DI requires the owner's real disc metadata file: "
                           << path << " (missing; no data generated)" << std::endl;
        return kNotFound;
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return kIoError;
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return kIoError;
    }
    const std::streampos size = input.tellg();
    if (size < 0) {
        return kIoError;
    }
    if ((exactSize && size != static_cast<std::streamoff>(count)) ||
        size < static_cast<std::streamoff>(count)) {
        RT_LOG(RT_TAG_DVD) << "DI metadata has an invalid size: " << path << std::endl;
        return kInvalid;
    }
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));
    if (!input || input.gcount() != static_cast<std::streamsize>(count)) {
        return kIoError;
    }
    // Also reject growth during an exact-size sidecar read. Never silently
    // truncate a larger input to something resembling a BCA dump.
    if (exactSize && input.peek() != std::char_traits<char>::eof()) {
        return kInvalid;
    }
    if (input.bad()) {
        return kIoError;
    }
    return kDiComplete;
}

} // namespace

int32_t Open(uint32_t mode)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (mode != 0 && mode != 1) {
        return kInvalid;
    }
    g_open = true;
    return static_cast<int32_t>(kDeviceFd);
}

bool OwnsFd(uint32_t fd)
{
    return fd == kDeviceFd;
}

int32_t Close(uint32_t fd)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!OwnsFd(fd) || !g_open) {
        return kInvalid;
    }
    g_open = false;
    return 0;
}

int32_t Ioctl(uint32_t fd, uint32_t cmd, uint32_t inPtr, uint32_t inLen,
              uint32_t outPtr, uint32_t outLen)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!OwnsFd(fd) || !g_open) {
        return kInvalid;
    }
    const uint32_t payloadBytes = cmd == 0xDAu ? 64u : cmd == 0x70u ? 32u : 0u;
    if (payloadBytes == 0 || inPtr == 0 || inLen != kCommandBytes ||
        !Memory::Contains(inPtr, inLen) || outPtr == 0 || outLen < payloadBytes ||
        !Memory::Contains(outPtr, outLen)) {
        return kInvalid;
    }
    const auto root = RuntimeConfigFile::ResolvedDvdRoot();
    if (root.empty()) {
        RT_LOG(RT_TAG_DVD) << "DI metadata read requires a configured extracted-disc root"
                           << std::endl;
        return kNotFound;
    }
    const auto path = cmd == 0xDAu ? root / "disc" / "bca.bin" : root / "sys" / "boot.bin";
    std::array<uint8_t, 64> bytes;
    std::error_code bcaError;
    if (cmd == 0xDAu && !std::filesystem::exists(path, bcaError)) {
        // Disc images (ISO, WBFS, RVZ...) don't carry the BCA. Answer as Dolphin does
        // (Core/HW/DVD/DVDInterface.cpp, ReadBCA): NSMBW checks that the first 0x33 bytes are
        // 0 followed by a 1; the remaining bytes are not checked. A real bca.bin dump still wins.
        bytes.fill(0);
        bytes[0x33] = 1;
        std::memcpy(Memory::GetPointer(outPtr, payloadBytes), bytes.data(), payloadBytes);
        return kDiComplete;
    }
    const int32_t result = ReadMetadata(path, payloadBytes, cmd == 0xDAu, bytes);
    if (result != kDiComplete) {
        return result;
    }
    std::memcpy(Memory::GetPointer(outPtr, payloadBytes), bytes.data(), payloadBytes);
    return kDiComplete;
}

} // namespace DiscInterface
