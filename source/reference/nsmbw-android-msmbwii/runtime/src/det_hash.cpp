#include "det_hash.h"

#include "det_clock.h"
#include "memory.h"
#include "runtime_config.h"
#include "runtime_log.h"

#include <xxhash.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>

namespace DetHash {
namespace {

constexpr uint32_t kMem1Base = 0x80000000u;
constexpr uint32_t kMem2Base = 0x90000000u;
constexpr size_t kMem1Bytes = 24u * 1024u * 1024u;
constexpr size_t kMem2Bytes = 64u * 1024u * 1024u;

struct Settings {
    std::filesystem::path hashFile;
    uint32_t every = 60;
    std::set<uint32_t> dumps;
    std::filesystem::path dumpDirectory;
};

const Settings& GetSettings() {
    static const Settings settings = [] {
        Settings parsed;
        if (const char* hash = std::getenv("NSMBW_DET_HASH"); hash != nullptr && *hash != '\0') {
            std::string text(hash);
            // "<file>:<every>", but a Windows drive letter's colon is part of the path.
            const size_t colon = text.rfind(':');
            if (colon != std::string::npos && colon > 1 && colon + 1 < text.size() &&
                text.find_first_not_of("0123456789", colon + 1) == std::string::npos) {
                parsed.every = static_cast<uint32_t>(std::strtoul(text.c_str() + colon + 1, nullptr, 10));
                text.resize(colon);
            }
            if (parsed.every == 0) {
                parsed.every = 1;
            }
            parsed.hashFile = std::filesystem::u8path(text);
            if (parsed.hashFile.is_relative()) {
                parsed.hashFile = RuntimeConfigFile::ApplicationDataDirectory() / parsed.hashFile;
            }
        }
        if (const char* dump = std::getenv("NSMBW_DET_DUMP"); dump != nullptr && *dump != '\0') {
            for (const char* p = dump; *p != '\0';) {
                char* end = nullptr;
                const unsigned long retrace = std::strtoul(p, &end, 10);
                if (end == p) {
                    break;
                }
                parsed.dumps.insert(static_cast<uint32_t>(retrace));
                p = *end == ',' ? end + 1 : end;
            }
        }
        parsed.dumpDirectory = parsed.hashFile.empty() ? RuntimeConfigFile::ApplicationDataDirectory() / "Logs"
                                                       : parsed.hashFile.parent_path();
        if (!parsed.hashFile.empty() || !parsed.dumps.empty()) {
            RT_LOGF(RT_TAG_RUNTIME, "determinism diagnostics: hashes every %u retraces to %s, %zu dump(s)\n",
                    parsed.every, RuntimeConfigFile::PathToUtf8(parsed.hashFile).c_str(), parsed.dumps.size());
        }
        return parsed;
    }();
    return settings;
}

uint64_t HashRange(uint32_t base, size_t bytes) {
    if (!Memory::Contains(base, bytes)) {
        return 0;
    }
    return XXH3_64bits(Memory::GetPointer(base, bytes), bytes);
}

void WriteImage(const std::filesystem::path& path, uint32_t base, size_t bytes) {
    if (!Memory::Contains(base, bytes)) {
        return;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(Memory::GetPointer(base, bytes)), static_cast<std::streamsize>(bytes));
}

} // namespace

Digest HashGuestMemory() {
    return {HashRange(kMem1Base, kMem1Bytes), HashRange(kMem2Base, kMem2Bytes)};
}

void OnRetrace(uint32_t retraceCount) {
    const Settings& settings = GetSettings();
    if (!settings.hashFile.empty() && retraceCount % settings.every == 0) {
        static std::ofstream file = [&] {
            std::error_code ec;
            std::filesystem::create_directories(settings.hashFile.parent_path(), ec);
            return std::ofstream(settings.hashFile, std::ios::trunc);
        }();
        if (file) {
            const Digest digest = HashGuestMemory();
            char line[96];
            std::snprintf(line, sizeof(line), "%u %llu %016llx %016llx\n", retraceCount,
                          static_cast<unsigned long long>(DetClock::Now()),
                          static_cast<unsigned long long>(digest.mem1), static_cast<unsigned long long>(digest.mem2));
            file << line << std::flush;
        }
    }
    if (settings.dumps.count(retraceCount) != 0) {
        std::error_code ec;
        std::filesystem::create_directories(settings.dumpDirectory, ec);
        const std::string stem = "mem_r" + std::to_string(retraceCount);
        WriteImage(settings.dumpDirectory / (stem + "_mem1.bin"), kMem1Base, kMem1Bytes);
        WriteImage(settings.dumpDirectory / (stem + "_mem2.bin"), kMem2Base, kMem2Bytes);
        RT_LOGF(RT_TAG_RUNTIME, "determinism diagnostics: memory images at retrace %u\n", retraceCount);
    }
}

} // namespace DetHash
