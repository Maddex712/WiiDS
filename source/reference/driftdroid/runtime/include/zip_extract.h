#pragma once

// Unpacks a .zip file from disk into a folder, on a worker thread with progress. Used by the
// Switch launcher's "Mods..." screen for mods copied over as zips. Stored and deflated entries;
// no encryption, no zip64 (mods are far below 4 GB).
#if defined(__SWITCH__) || defined(ZIP_EXTRACT_TEST_MAIN)

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

namespace ZipExtract {

class Job {
public:
    Job() = default;
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    ~Job();

    // Unpacks `zip` into `target` (created; an existing folder of that name is an error). Works in
    // `target` + ".partial" and renames at the end, so a half-unpacked mod is never listed.
    void Start(const std::filesystem::path& zip, const std::filesystem::path& target);
    bool Finished() const { return done_.load(std::memory_order_acquire); }
    uint64_t BytesDone() const { return bytesDone_.load(std::memory_order_relaxed); }
    uint64_t BytesTotal() const { return bytesTotal_.load(std::memory_order_relaxed); }
    // Joins the worker. "" on success, otherwise a message for the player.
    std::string Finish();

private:
    void Run();

    std::thread worker_;
    std::filesystem::path zip_;
    std::filesystem::path target_;
    std::atomic<bool> done_{false};
    std::atomic<uint64_t> bytesDone_{0};
    std::atomic<uint64_t> bytesTotal_{0};
    std::string error_;
};

}  // namespace ZipExtract
#endif
