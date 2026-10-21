// Switch game-data import: scanning for Wii disc images, the completion marker and the extraction
// job. The player-facing UI is in switch_launcher.cpp.
#if defined(__SWITCH__)

#include "switch_rom_import.h"

#include <switch.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "hle/storage/wii_disc_extractor.h"

namespace fs = std::filesystem;

namespace {

using SwitchImport::DiscImage;

// Written only after an import has finished and verified, so a cancelled or interrupted import
// (which already wrote sys/ early on) is never mistaken for a complete one.
constexpr const char* kCompleteMarker = ".import_complete";

// Bump kDataFormatVersion if a future build needs the game files extracted differently; players on
// an older format are then asked to import again instead of hitting silent breakage. The marker is
// plain text ("channel=beta-switch\nformat=1\n") so it is easy to inspect by hand.
constexpr const char* kDataChannel = "beta-switch";
constexpr int kDataFormatVersion = 1;

// Returns the marker's format number, or -1 when the marker is missing or unreadable.
int ReadMarkerFormat(const fs::path& root) {
    std::FILE* f = std::fopen((root / kCompleteMarker).string().c_str(), "rb");
    if (f == nullptr) {
        return -1;
    }
    char text[128] = {};
    const size_t n = std::fread(text, 1, sizeof(text) - 1, f);
    std::fclose(f);
    text[n] = 0;
    const char* p = std::strstr(text, "format=");
    return p != nullptr ? std::atoi(p + 7) : -1;
}

bool HasExtractedStructure(const fs::path& root) {
    std::error_code ec;
    return fs::is_directory(root / "files", ec) && fs::is_regular_file(root / "sys" / "fst.bin", ec) &&
           fs::is_regular_file(root / "sys" / "main.dol", ec);
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Game ID: the first 6 bytes of the disc. In a WBFS container the disc header copy sits at the
// start of the first disc slot, one "hd sector" (1 << header[8]) into the file.
std::string ReadGameId(const std::string& path, bool wbfs) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return "";
    }
    uint8_t header[12] = {};
    long offset = 0;
    if (wbfs) {
        if (std::fread(header, 1, sizeof(header), f) != sizeof(header) || std::memcmp(header, "WBFS", 4) != 0 ||
            header[8] > 16) {
            std::fclose(f);
            return "";
        }
        offset = 1L << header[8];
    }
    char id[7] = {};
    const bool ok = std::fseek(f, offset, SEEK_SET) == 0 && std::fread(id, 1, 6, f) == 6;
    std::fclose(f);
    if (!ok) {
        return "";
    }
    for (char& c : id) {
        if (c != 0 && !std::isalnum(static_cast<unsigned char>(c))) {
            return "";
        }
    }
    return id;
}

void ScanDirectory(const fs::path& dir, int depth, std::vector<DiscImage>& out) {
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path& path = it->path();
        std::error_code typeEc;
        if (it->is_directory(typeEc)) {
            // wbfs/<Game [ID]>/<ID>.wbfs (Wii Backup Manager's layout) is one level down.
            if (depth > 0) {
                ScanDirectory(path, depth - 1, out);
            }
            continue;
        }
        const std::string ext = Lower(path.extension().string());
        const bool wbfs = ext == ".wbfs";
        if (!wbfs && ext != ".iso" && ext != ".gcm") {
            continue;
        }
        DiscImage image;
        image.parts.push_back(path.string());
        if (wbfs) {
            for (int i = 1; i <= 9; ++i) {
                fs::path part = path;
                part.replace_extension(".wbf" + std::to_string(i));
                if (!fs::is_regular_file(part, typeEc)) {
                    break;
                }
                image.parts.push_back(part.string());
            }
        }
        for (const auto& part : image.parts) {
            image.totalSize += fs::file_size(part, typeEc);
        }
        image.gameId = ReadGameId(image.parts[0], wbfs);
        out.push_back(std::move(image));
    }
}

std::vector<DiscImage> FindDiscImages() {
    std::vector<DiscImage> images;
    const char* dirs[] = {"sdmc:/",           "sdmc:/switch/WiiCompiled", "sdmc:/wbfs",  "sdmc:/roms",
                          "sdmc:/roms/wii",   "sdmc:/roms/Wii",           "sdmc:/Wii",   "sdmc:/wii",
                          "sdmc:/games",      "sdmc:/iso",                "sdmc:/isos"};
    for (const char* dir : dirs) {
        // One level down for folders people drop games into (wbfs/<Game [ID]>/, switch/WiiCompiled/<any>/).
        const bool oneLevel = std::strcmp(dir, "sdmc:/wbfs") == 0 || std::strcmp(dir, "sdmc:/switch/WiiCompiled") == 0;
        ScanDirectory(dir, oneLevel ? 1 : 0, images);
    }
    // The same file can be reached twice (sdmc:/roms/wii vs Wii on a case-insensitive card).
    std::sort(images.begin(), images.end(), [](const DiscImage& a, const DiscImage& b) {
        return Lower(a.parts[0]) < Lower(b.parts[0]);
    });
    images.erase(std::unique(images.begin(), images.end(),
                             [](const DiscImage& a, const DiscImage& b) { return Lower(a.parts[0]) == Lower(b.parts[0]); }),
                 images.end());
    // Supported images first.
    std::stable_partition(images.begin(), images.end(), [](const DiscImage& d) { return d.gameId == "RMCP01"; });
    return images;
}

uint64_t FreeSpaceBytes() {
    struct statvfs st{};
    if (statvfs("sdmc:/", &st) != 0) {
        return UINT64_MAX;
    }
    return static_cast<uint64_t>(st.f_bavail) * st.f_frsize;
}

}  // namespace

namespace SwitchImport {

DataState GetDataState(const fs::path& root) {
    if (root.empty()) {
        return DataState::Missing;
    }
    std::error_code ec;
    const int format = ReadMarkerFormat(root);
    if (HasExtractedStructure(root) && format == kDataFormatVersion) {
        return DataState::Ready;
    }
    if (format >= 0 && format != kDataFormatVersion && HasExtractedStructure(root)) {
        return DataState::Outdated;
    }
    if (fs::exists(root / "sys", ec) || fs::exists(root / "files", ec)) {
        return DataState::Partial;
    }
    return DataState::Missing;
}

std::vector<DiscImage> FindDiscImages() {
    return ::FindDiscImages();
}

bool InspectDiscFile(const std::string& path, DiscImage& out) {
    const std::string ext = Lower(fs::path(path).extension().string());
    // A split WBFS continuation (.wbf1, .wbf2 ...) is described by its first part.
    const bool wbfs = ext == ".wbfs";
    if (!wbfs && ext != ".iso" && ext != ".gcm") {
        return false;
    }
    std::error_code ec;
    out = DiscImage{};
    out.parts.push_back(path);
    if (wbfs) {
        for (int i = 1; i <= 9; ++i) {
            fs::path part = path;
            part.replace_extension(".wbf" + std::to_string(i));
            if (!fs::is_regular_file(part, ec)) {
                break;
            }
            out.parts.push_back(part.string());
        }
    }
    for (const auto& part : out.parts) {
        out.totalSize += fs::file_size(part, ec);
    }
    out.gameId = ReadGameId(out.parts[0], wbfs);
    return true;
}

uint64_t FreeSpaceBytes() {
    return ::FreeSpaceBytes();
}

ImportJob::~ImportJob() {
    if (worker_.joinable()) {
        worker_.join();
    }
    for (int fd : fds_) {
        close(fd);
    }
}

std::string ImportJob::Start(const DiscImage& image, const fs::path& dataRoot) {
    if (worker_.joinable()) {
        return "An import is already running.";
    }
    fds_.clear();
    for (const auto& part : image.parts) {
        const int fd = open(part.c_str(), O_RDONLY);
        if (fd < 0) {
            for (int f : fds_) close(f);
            fds_.clear();
            return "Could not open " + part;
        }
        fds_.push_back(fd);
    }
    dataRoot_ = dataRoot;
    std::error_code ec;
    fs::create_directories(dataRoot_, ec);
    fs::remove(dataRoot_ / kCompleteMarker, ec);
    done_.store(false, std::memory_order_release);
    result_ = 0;
    error_[0] = 0;
    worker_ = std::thread([this] {
        const std::string dest = dataRoot_.string();
        result_ = WiiDiscExtractor_ExtractParts(fds_.data(), static_cast<int>(fds_.size()), dest.c_str(), error_,
                                                sizeof(error_));
        done_.store(true, std::memory_order_release);
    });
    return "";
}

uint64_t ImportJob::BytesDone() const {
    return WiiDiscExtractor_BytesDone();
}

uint64_t ImportJob::BytesTotal() const {
    return WiiDiscExtractor_BytesTotal();
}

std::string ImportJob::Finish() {
    if (worker_.joinable()) {
        worker_.join();
    }
    for (int fd : fds_) {
        close(fd);
    }
    fds_.clear();
    if (result_ != 0) {
        return error_[0] ? std::string(error_) : std::string("The import failed.");
    }
    if (!HasExtractedStructure(dataRoot_)) {
        return "The import finished but the game files are incomplete.";
    }
    std::FILE* marker = std::fopen((dataRoot_ / kCompleteMarker).string().c_str(), "wb");
    if (marker == nullptr) {
        return "Could not finish the import (the SD card may be full or read-only).";
    }
    std::fprintf(marker, "channel=%s\nformat=%d\n", kDataChannel, kDataFormatVersion);
    std::fclose(marker);
    return "";
}

}  // namespace SwitchImport

#endif  // __SWITCH__
