#pragma once

#include "isa/big_endian.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace DvdFstContract {

struct RegisteredFile {
    std::string hostPath;
    std::string dvdPath;
    uint32_t size = 0;
    uint32_t discOffsetWords = 0;
};

struct IndexedEntry {
    std::string hostPath;
    std::string dvdPath;
    uint32_t size = 0;
    uint32_t discOffsetWords = 0;
    uint32_t parentIndex = 0;
    uint32_t subtreeEnd = 0;
    bool isDirectory = false;
};

struct Image {
    std::vector<IndexedEntry> entries;
    std::map<std::string, int32_t> pathToEntry;
    std::vector<uint8_t> bytes;
};

struct GuestPlacement {
    uint32_t address = 0;
    uint32_t reservedArenaHi = 0;
};

inline std::string CanonicalizePath(const std::string& input) {
    std::string path = input;
    std::replace(path.begin(), path.end(), '\\', '/');

    std::vector<std::string> components;
    size_t cursor = 0;
    while (cursor < path.size()) {
        while (cursor < path.size() && path[cursor] == '/') {
            ++cursor;
        }
        const size_t start = cursor;
        while (cursor < path.size() && path[cursor] != '/') {
            ++cursor;
        }
        if (start == cursor) {
            continue;
        }

        std::string component = path.substr(start, cursor - start);
        if (component == ".") {
            continue;
        }
        if (component == "..") {
            if (!components.empty()) {
                components.pop_back();
            }
            continue;
        }
        components.push_back(std::move(component));
    }

    std::string canonical = "/";
    for (size_t i = 0; i < components.size(); ++i) {
        if (i != 0) {
            canonical.push_back('/');
        }
        canonical += components[i];
    }
    return canonical;
}

inline std::string NormalizeLookupPath(const std::string& input) {
    std::string path = CanonicalizePath(input);
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return path;
}

namespace Detail {

struct TreeNode {
    std::string name;
    std::map<std::string, std::unique_ptr<TreeNode>> children;
    std::optional<RegisteredFile> file;
};

inline std::vector<std::string> Components(const std::string& canonicalPath) {
    std::vector<std::string> result;
    size_t cursor = canonicalPath == "/" ? canonicalPath.size() : 1;
    while (cursor < canonicalPath.size()) {
        const size_t slash = canonicalPath.find('/', cursor);
        const size_t end = slash == std::string::npos ? canonicalPath.size() : slash;
        result.push_back(canonicalPath.substr(cursor, end - cursor));
        cursor = end + 1;
    }
    return result;
}

inline std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline uint32_t EmitTree(const TreeNode& directory,
                         uint32_t directoryIndex,
                         const std::string& directoryPath,
                         Image& image,
                         std::vector<std::string>& names) {
    for (const auto& [lookupName, childPointer] : directory.children) {
        (void)lookupName;
        const TreeNode& child = *childPointer;
        const std::string childPath = directoryPath == "/"
            ? "/" + child.name
            : directoryPath + "/" + child.name;
        const uint32_t index = static_cast<uint32_t>(image.entries.size());

        if (!child.children.empty()) {
            if (child.file.has_value()) {
                throw std::runtime_error("DVD FST path is both a file and a directory: " + childPath);
            }
            image.entries.push_back({{}, childPath, 0, 0, directoryIndex, 0, true});
            names.push_back(child.name);
            image.pathToEntry.emplace(NormalizeLookupPath(childPath), static_cast<int32_t>(index));
            image.entries[index].subtreeEnd = EmitTree(child, index, childPath, image, names);
            continue;
        }

        if (!child.file.has_value()) {
            throw std::runtime_error("DVD FST contains an empty implicit node: " + childPath);
        }
        const RegisteredFile& file = *child.file;
        image.entries.push_back({file.hostPath, childPath, file.size, file.discOffsetWords,
                                 directoryIndex, index + 1, false});
        names.push_back(child.name);
        image.pathToEntry.emplace(NormalizeLookupPath(childPath), static_cast<int32_t>(index));
    }
    return static_cast<uint32_t>(image.entries.size());
}

} // namespace Detail

inline Image BuildImage(const std::vector<RegisteredFile>& registrations) {
    // Overlay scanning deliberately registers later mappings last. Collapse those
    // mappings before assigning FST indices so one guest path has one stable entry.
    std::map<std::string, RegisteredFile> filesByPath;
    for (RegisteredFile file : registrations) {
        file.dvdPath = CanonicalizePath(file.dvdPath);
        if (file.dvdPath == "/") {
            throw std::runtime_error("DVD FST cannot register the root as a file");
        }
        filesByPath[NormalizeLookupPath(file.dvdPath)] = std::move(file);
    }

    Detail::TreeNode root;
    for (const auto& [lookupPath, file] : filesByPath) {
        (void)lookupPath;
        Detail::TreeNode* node = &root;
        const std::vector<std::string> components = Detail::Components(file.dvdPath);
        for (const std::string& component : components) {
            const std::string key = Detail::Lowercase(component);
            auto& child = node->children[key];
            if (!child) {
                child = std::make_unique<Detail::TreeNode>();
                child->name = component;
            }
            node = child.get();
        }
        node->name = components.back();
        node->file = file;
    }

    Image image;
    image.entries.push_back({{}, "/", 0, 0, 0, 0, true});
    image.pathToEntry.emplace("/", 0);
    std::vector<std::string> names(1);
    image.entries[0].subtreeEnd = Detail::EmitTree(root, 0, "/", image, names);

    if (image.entries.size() > std::numeric_limits<uint32_t>::max() / 12u) {
        throw std::runtime_error("DVD FST contains too many entries");
    }

    const size_t entriesSize = image.entries.size() * 12u;
    std::vector<uint8_t> stringTable(1, 0);
    std::vector<uint32_t> nameOffsets(image.entries.size(), 0);
    for (size_t i = 1; i < names.size(); ++i) {
        if (stringTable.size() > 0x00FFFFFFu) {
            throw std::runtime_error("DVD FST name table exceeds the Wii 24-bit offset limit");
        }
        nameOffsets[i] = static_cast<uint32_t>(stringTable.size());
        stringTable.insert(stringTable.end(), names[i].begin(), names[i].end());
        stringTable.push_back(0);
    }

    image.bytes.assign(entriesSize + stringTable.size(), 0);
    for (size_t i = 0; i < image.entries.size(); ++i) {
        const IndexedEntry& entry = image.entries[i];
        const uint32_t typeAndName = (entry.isDirectory ? 0x01000000u : 0u) | nameOffsets[i];
        const uint32_t word1 = entry.isDirectory ? entry.parentIndex : entry.discOffsetWords;
        const uint32_t word2 = entry.isDirectory ? entry.subtreeEnd : entry.size;
        BigEndian::Write32(image.bytes.data(), i * 12u + 0u, typeAndName);
        BigEndian::Write32(image.bytes.data(), i * 12u + 4u, word1);
        BigEndian::Write32(image.bytes.data(), i * 12u + 8u, word2);
    }
    std::copy(stringTable.begin(), stringTable.end(), image.bytes.begin() + entriesSize);
    return image;
}

inline std::optional<GuestPlacement> ReserveBelowArena(uint32_t arenaLo,
                                                        uint32_t arenaHi,
                                                        size_t byteCount) {
    constexpr uint32_t kAlignment = 32;
    if (byteCount == 0 || byteCount > std::numeric_limits<uint32_t>::max() || arenaHi <= arenaLo) {
        return std::nullopt;
    }
    const uint32_t size = static_cast<uint32_t>(byteCount);
    if (size > arenaHi - arenaLo) {
        return std::nullopt;
    }
    const uint32_t unaligned = arenaHi - size;
    const uint32_t address = unaligned & ~(kAlignment - 1u);
    if (address < arenaLo || static_cast<uint64_t>(address) + size > arenaHi) {
        return std::nullopt;
    }
    return GuestPlacement{address, address};
}

} // namespace DvdFstContract

namespace DvdReadContract {

inline constexpr int32_t kInterruptTransferComplete = 1;
inline constexpr int32_t kInterruptDriveError = 2;

struct LowReadCompletion {
    int32_t returnValue;
    int32_t callbackResult;
};

inline constexpr LowReadCompletion CompletionFor(bool succeeded) noexcept {
    return succeeded ? LowReadCompletion{1, kInterruptTransferComplete}
                     : LowReadCompletion{0, kInterruptDriveError};
}

enum class HostReadFailure : uint8_t {
    None,
    MissingFile,
    BadOffset,
    ShortRead,
};

inline constexpr const char* Describe(HostReadFailure failure) noexcept {
    switch (failure) {
    case HostReadFailure::None:
        return "no error";
    case HostReadFailure::MissingFile:
        return "host file is missing or cannot be opened";
    case HostReadFailure::BadOffset:
        return "read offset is outside the host file";
    case HostReadFailure::ShortRead:
        return "host file did not contain the complete requested range";
    }
    return "unknown host read error";
}

// Read into private storage first and publish it only after the complete host
// range has been obtained. Callers can therefore leave a guest DMA destination
// untouched for every failure, including a host file truncated after indexing.
// Disc files are read-only for the whole run, and the game streams from them constantly - a THP
// movie reads a chunk every frame. Opening the file for each read cost an open, a size query, a
// seek and a close on top of the read itself; on Switch every one of those is an IPC round trip to
// the FS service, and together they had the game thread blocked for a quarter of every frame on
// movie-heavy menus. So keep a small set of handles open and reuse them.
class HostReadHandleCache {
public:
    struct Handle {
        std::FILE* file = nullptr;
        uint64_t size = 0;
        // Read-ahead window: the bytes [windowStart, windowStart + window.size()) of the file.
        // Disc files never change while the game runs, so a window never goes stale.
        std::vector<uint8_t> window;
        uint64_t windowStart = 0;
    };

    // Returns an open handle for `hostPath`, opening it if needed. Caller holds mutex().
    Handle* Acquire(const std::filesystem::path& hostPath) {
        const std::string key = hostPath.string();
        for (auto& entry : m_entries) {
            if (entry.handle.file != nullptr && entry.path == key) {
                entry.lastUse = ++m_clock;
                return &entry.handle;
            }
        }
        std::FILE* file = std::fopen(key.c_str(), "rb");
        if (file == nullptr) {
            return nullptr;
        }
        if (std::fseek(file, 0, SEEK_END) != 0) {
            std::fclose(file);
            return nullptr;
        }
        const long end = std::ftell(file);
        if (end < 0) {
            std::fclose(file);
            return nullptr;
        }
        Entry* victim = &m_entries[0];
        for (auto& entry : m_entries) {
            if (entry.handle.file == nullptr) {
                victim = &entry;
                break;
            }
            if (entry.lastUse < victim->lastUse) {
                victim = &entry;
            }
        }
        if (victim->handle.file != nullptr) {
            std::fclose(victim->handle.file);
        }
        victim->path = key;
        victim->handle = {file, static_cast<uint64_t>(end), {}, 0};
        victim->lastUse = ++m_clock;
        return &victim->handle;
    }

    // Drops a handle whose read failed, so the next attempt reopens the file from scratch.
    void Evict(const Handle* handle) {
        for (auto& entry : m_entries) {
            if (&entry.handle == handle && entry.handle.file != nullptr) {
                std::fclose(entry.handle.file);
                entry = {};
            }
        }
    }

    std::mutex& mutex() { return m_mutex; }

    static HostReadHandleCache& Instance() {
        static HostReadHandleCache cache;
        return cache;
    }

private:
    struct Entry {
        std::string path;
        Handle handle;
        uint64_t lastUse = 0;
    };
    std::array<Entry, 16> m_entries{};
    uint64_t m_clock = 0;
    std::mutex m_mutex;
};

inline bool ReadExactFromHandle(HostReadHandleCache::Handle& handle,
                                uint64_t offset,
                                uint32_t length,
                                std::vector<uint8_t>& destination,
                                HostReadFailure& failure) {
    if (offset >= handle.size) {
        failure = HostReadFailure::BadOffset;
        return false;
    }
    if (static_cast<uint64_t>(length) > handle.size - offset) {
        failure = HostReadFailure::ShortRead;
        return false;
    }
    // Each host read is a filesystem-service round trip (an IPC on Switch), and streamed media -
    // THP movies, music - reads a small chunk every frame. Small reads therefore fill a 512 KiB
    // window with one host read and are served from it until they walk past its end.
    constexpr uint64_t kReadAheadBytes = 512u * 1024u;
    auto seekAndRead = [&](uint8_t* out, uint64_t at, uint64_t bytes) {
        if (at > static_cast<uint64_t>(std::numeric_limits<long>::max()) ||
            std::fseek(handle.file, static_cast<long>(at), SEEK_SET) != 0) {
            failure = HostReadFailure::BadOffset;
            return false;
        }
        if (std::fread(out, 1, bytes, handle.file) != bytes) {
            failure = HostReadFailure::ShortRead;
            return false;
        }
        return true;
    };
    std::vector<uint8_t> staged(length);
    if (length != 0) {
        const uint64_t windowEnd = handle.windowStart + handle.window.size();
        if (offset >= handle.windowStart && offset + length <= windowEnd) {
            std::memcpy(staged.data(), handle.window.data() + (offset - handle.windowStart), length);
        } else if (length >= kReadAheadBytes / 2) {
            if (!seekAndRead(staged.data(), offset, length)) {
                return false;
            }
        } else {
            const uint64_t fill = std::min<uint64_t>(kReadAheadBytes, handle.size - offset);
            handle.window.resize(fill);
            if (!seekAndRead(handle.window.data(), offset, fill)) {
                handle.window.clear();
                handle.windowStart = 0;
                return false;
            }
            handle.windowStart = offset;
            std::memcpy(staged.data(), handle.window.data(), length);
        }
    }
    destination = std::move(staged);
    return true;
}

inline bool ReadExact(const std::filesystem::path& hostPath,
                      uint64_t offset,
                      uint32_t length,
                      std::vector<uint8_t>& destination,
                      HostReadFailure& failure) {
    failure = HostReadFailure::None;
    auto& cache = HostReadHandleCache::Instance();
    std::lock_guard lock(cache.mutex());
    HostReadHandleCache::Handle* handle = cache.Acquire(hostPath);
    if (handle == nullptr) {
        failure = HostReadFailure::MissingFile;
        return false;
    }
    if (ReadExactFromHandle(*handle, offset, length, destination, failure)) {
        return true;
    }
    // A failure may come from a stale handle rather than the request; retry once on a fresh one
    // before reporting it, so a transient error cannot become permanent.
    cache.Evict(handle);
    handle = cache.Acquire(hostPath);
    if (handle == nullptr) {
        failure = HostReadFailure::MissingFile;
        return false;
    }
    failure = HostReadFailure::None;
    return ReadExactFromHandle(*handle, offset, length, destination, failure);
}

} // namespace DvdReadContract
