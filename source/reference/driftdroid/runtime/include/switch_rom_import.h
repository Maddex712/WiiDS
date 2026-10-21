#pragma once

#if defined(__SWITCH__)
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Game-data import for the Switch port: finds Wii disc images on the SD card and unpacks one into
// the DATA folder with hle/storage/wii_disc_extractor (shared with Android's importer). Supports
// ISO/GCM and WBFS, including WBFS split into .wbfs + .wbf1... parts (FAT32's 4 GiB file limit).
// Only PAL Mario Kart Wii (RMCP01) is accepted. The UI lives in switch_launcher.cpp.
namespace SwitchImport {

inline constexpr const char* kSupportedGameId = "RMCP01";
// The unpacked game is about 2.7 GB; leave a little room on top.
inline constexpr uint64_t kRequiredFreeBytes = 3'000'000'000ull;
// Reinstalling over an existing install overwrites its files one at a time, so only a little extra
// room is needed (the biggest single game file, plus slack).
inline constexpr uint64_t kReplaceFreeBytes = 500'000'000ull;

struct DiscImage {
    std::vector<std::string> parts;  // first part plus any .wbf1.. continuations
    std::string gameId;              // "" when unreadable
    uint64_t totalSize = 0;

    bool Supported() const { return gameId == kSupportedGameId; }
};

enum class DataState {
    Ready,     // extracted, complete, current format
    Missing,   // nothing there
    Partial,   // an import started but never finished
    Outdated,  // complete, but from an older data format
};

DataState GetDataState(const std::filesystem::path& root);

// Candidate images in the usual folders (supported ones first).
std::vector<DiscImage> FindDiscImages();
// Describes one file picked by the player (.iso/.gcm/.wbfs, with split parts). False when the
// extension is not a disc image at all.
bool InspectDiscFile(const std::string& path, DiscImage& out);

uint64_t FreeSpaceBytes();

// Extraction on a worker thread; the UI polls progress. Writes the completion marker only after a
// verified, fully extracted game, so an interrupted import is never mistaken for a finished one.
class ImportJob {
public:
    ImportJob() = default;
    ImportJob(const ImportJob&) = delete;
    ImportJob& operator=(const ImportJob&) = delete;
    ~ImportJob();

    // Opens the image and starts extracting. Returns "" or an error message.
    std::string Start(const DiscImage& image, const std::filesystem::path& dataRoot);
    bool Finished() const { return done_.load(std::memory_order_acquire); }
    uint64_t BytesDone() const;
    uint64_t BytesTotal() const;
    // Joins the worker and reports the result: "" on success, otherwise an error message.
    std::string Finish();

private:
    std::thread worker_;
    std::vector<int> fds_;
    std::filesystem::path dataRoot_;
    std::atomic<bool> done_{false};
    int result_ = 0;
    char error_[512] = {};
};

}  // namespace SwitchImport
#endif
