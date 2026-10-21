#pragma once

// Downloads and unpacks the Retro Rewind pack straight from its official update server.
//
// The archive is a 1.9 GB zip. Rather than saving it and unpacking afterwards (which needs twice the
// space), the job reads the zip's file table first, then streams the archive and unpacks every file
// as its bytes arrive. Each file is checked against the checksum in the file table, and the file
// table itself against a hash pinned here, so a damaged or changed download is refused. An
// interrupted download continues from the file it stopped at.
//
// A release that exists only as a small update on top of the last full archive (6.13.1 over 6.13.0)
// is installed as both: the full archive, then the update archive unpacked over it the same way.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

namespace RetroRewindDownload {

struct Release {
    const char* version;
    const char* host;
    const char* path;
    uint64_t archiveBytes;
    // The zip's file table (central directory plus end record): where it sits and what it hashes to.
    uint64_t tableOffset;
    uint32_t tableBytes;
    const char* tableSha256;
    // Binaries/Code.pul of this release: the file this build has native code for.
    const char* codePulSha256;
    // Unpacked size of the RetroRewind6 folder, for the free-space check.
    uint64_t unpackedBytes;
};

const Release& PinnedRelease();

// The version a finished download leaves installed: the pinned full archive plus the update the
// job unpacks over it.
const char* PinnedVersion();

// Free space to ask for before starting: the unpacked pack plus working room.
uint64_t RequiredFreeBytes();

class Job {
public:
    Job() = default;
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    ~Job();

    // Installs into `target` (the RetroRewind6 folder itself), working in `target` + ".partial" and
    // renaming at the end. `caBundle` is the cacert.pem used to verify the server.
    void Start(const std::filesystem::path& target, const std::filesystem::path& caBundle);
    bool Finished() const { return done_.load(std::memory_order_acquire); }
    uint64_t BytesDone() const { return bytesDone_.load(std::memory_order_relaxed); }
    uint64_t BytesTotal() const { return bytesTotal_.load(std::memory_order_relaxed); }
    // True while the job is waiting to reconnect after a dropped connection.
    bool Reconnecting() const { return reconnecting_.load(std::memory_order_relaxed); }
    // Asks the worker to stop; what was unpacked so far is kept for the next attempt.
    void Cancel() { cancel_.store(true, std::memory_order_release); }
    // Joins the worker. "" on success, otherwise a message for the player.
    std::string Finish();

private:
    void Run();

    std::thread worker_;
    std::filesystem::path target_;
    std::filesystem::path caBundle_;
    std::atomic<bool> done_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<bool> reconnecting_{false};
    std::atomic<uint64_t> bytesDone_{0};
    std::atomic<uint64_t> bytesTotal_{0};
    std::string error_;
};

}  // namespace RetroRewindDownload
