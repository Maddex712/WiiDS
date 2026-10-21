#pragma once
// wiids_disc: reads GameCube and Wii disc images in every common format and extracts them the
// way each WiiDS game needs: a Wii game's data partition as sys/ + files/, a GameCube game as a
// plain 1:1 ISO. Formats: ISO/GCM, CISO, GCZ, WBFS (single file or split .wbfs/.wbf1...), WIA and
// RVZ. NKit images are recognised and refused with a clear reason (they are not 1:1 copies).
//
// The WIA/RVZ reader follows Dolphin's documented format (docs/WiaAndRvz.md) and its reader;
// see THIRD-PARTY-NOTICES. All file access goes through Part, so it works on Android file
// descriptors (Storage Access Framework) as well as plain paths.
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wiids::disc {

enum class Format { Unknown, Iso, Ciso, Gcz, Wbfs, Wia, Rvz, Nkit };
enum class Platform { Unknown, GameCube, Wii };

const char* FormatName(Format format);

// One file of an image (a split WBFS has several, in order).
class Part {
public:
    virtual ~Part() = default;
    virtual uint64_t Size() const = 0;
    // Reads exactly `size` bytes or throws std::runtime_error.
    virtual void ReadAt(uint64_t offset, void* out, size_t size) = 0;
};

std::unique_ptr<Part> OpenPath(const std::string& path);
#if !defined(_WIN32)
// Takes ownership of the descriptor.
std::unique_ptr<Part> OpenFd(int fd);
#endif

struct Info {
    Format format = Format::Unknown;
    Platform platform = Platform::Unknown;
    std::string gameId;      // e.g. "RMCP01"
    int discNumber = 0;
    int revision = 0;
    std::string title;       // internal name from the disc header
    std::string region;      // "Europe (PAL)", "USA", "Japan", "Korea", ...
    uint64_t discSize = 0;   // size of the 1:1 disc image
};

struct Progress {
    std::atomic<uint64_t> done{0};
    std::atomic<uint64_t> total{0};
    std::atomic<bool> cancel{false};
};

// Identifies an image; throws std::runtime_error with a player-facing reason when it can't.
Info Identify(std::vector<std::unique_ptr<Part>>& parts);

// Wii: writes <destDir>/sys/{boot.bin,bi2.bin,apploader.img,fst.bin,main.dol} and <destDir>/files.
// main.dol and fst.bin are written last (renamed into place), so their presence means "complete".
void ExtractWii(std::vector<std::unique_ptr<Part>>& parts, const std::string& destDir, Progress& progress);

// GameCube: writes a plain 1:1 ISO to destIso (written to destIso + ".part", then renamed).
void ExtractGameCubeIso(std::vector<std::unique_ptr<Part>>& parts, const std::string& destIso, Progress& progress);

} // namespace wiids::disc
