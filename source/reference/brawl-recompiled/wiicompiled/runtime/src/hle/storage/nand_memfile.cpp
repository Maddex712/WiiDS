// Large NAND files kept in memory and written to disk in the background (Android).
//
// NANDOpen's shadow copy makes a write open crash-safe by copying the whole file, and NANDClose
// fsyncs and renames that copy over the original. Brawl opens its 14.6 MB collect.vff ten-odd
// times after a match to change a few 512-byte blocks; on Android's shared storage each round
// costs 50-70 ms on the game thread, and the menus hitch. Here a file that large is read into
// memory once. Its handles read and write that copy through a funopen() stream, and each close
// hands the new contents to a writer thread, which writes a scratch file, fsyncs it and renames
// it over the original. A crash still leaves either the old or the new save on disk.

#include "nand_internal.h"

#if defined(__ANDROID__)
#include <cerrno>
#include <condition_variable>
#include <memory>
#include <thread>
#endif

namespace NandMemFile {

#if defined(__ANDROID__)
namespace {

constexpr uintmax_t kMinSize = 1u << 20;  // smaller files keep the shadow copy, which is cheap for them
constexpr char kScratchSuffix[] = ".nandbg.tmp";
constexpr auto kSettleTimeout = std::chrono::seconds(5);

using Bytes = std::vector<uint8_t>;

struct Entry {
    std::shared_ptr<const Bytes> latest;  // what the game last closed (or what was on disk)
    uint64_t version = 0;                 // bumped by every commit
    uint64_t written = 0;                 // the version now on disk
};

std::mutex g_mutex;
std::condition_variable g_work;  // the writer: a commit is waiting
std::condition_variable g_done;  // Settle/FlushAll: a write finished
std::map<std::filesystem::path, Entry> g_files;
bool g_writerStarted = false;

// One handle's view of the file: it shares the contents until its first write, then owns a copy.
struct Stream {
    std::filesystem::path path;
    std::shared_ptr<const Bytes> shared;
    Bytes owned;
    bool writable = false;
    bool dirty = false;
    size_t position = 0;

    const Bytes& Data() const { return dirty ? owned : *shared; }
};

int ReadFn(void* cookie, char* buffer, int length) {
    auto* s = static_cast<Stream*>(cookie);
    const Bytes& data = s->Data();
    if (length <= 0 || s->position >= data.size()) return 0;
    const size_t n = std::min(static_cast<size_t>(length), data.size() - s->position);
    std::memcpy(buffer, data.data() + s->position, n);
    s->position += n;
    return static_cast<int>(n);
}

int WriteFn(void* cookie, const char* buffer, int length) {
    auto* s = static_cast<Stream*>(cookie);
    if (!s->writable) {
        errno = EBADF;
        return -1;
    }
    if (length <= 0) return 0;
    if (!s->dirty) {
        s->owned = *s->shared;
        s->shared.reset();
        s->dirty = true;
    }
    const size_t end = s->position + static_cast<size_t>(length);
    if (end > s->owned.size()) s->owned.resize(end);
    std::memcpy(s->owned.data() + s->position, buffer, static_cast<size_t>(length));
    s->position = end;
    return length;
}

fpos_t SeekFn(void* cookie, fpos_t offset, int whence) {
    auto* s = static_cast<Stream*>(cookie);
    long long base = 0;
    if (whence == SEEK_CUR) base = static_cast<long long>(s->position);
    else if (whence == SEEK_END) base = static_cast<long long>(s->Data().size());
    const long long target = base + static_cast<long long>(offset);
    if (target < 0) {
        errno = EINVAL;
        return -1;
    }
    s->position = static_cast<size_t>(target);
    return static_cast<fpos_t>(target);
}

int CloseFn(void* cookie) {
    delete static_cast<Stream*>(cookie);
    return 0;
}

std::filesystem::path ScratchPathFor(const std::filesystem::path& hostPath) {
    std::filesystem::path scratch = hostPath;
    scratch += kScratchSuffix;
    return scratch;
}

// Scratch file, fsync, rename over the original, fsync the directory: the original is replaced whole or not at all.
bool WriteToDisk(const std::filesystem::path& path, const Bytes& bytes) {
    const std::filesystem::path scratch = ScratchPathFor(path);
    FILE* file = NandFopen(scratch, "wb");
    if (!file) return false;
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    ok = ok && std::fflush(file) == 0 && fsync(fileno(file)) == 0;
    ok = std::fclose(file) == 0 && ok;
    if (!ok || !NandRename(scratch, path)) {
        NandRemove(scratch);
        return false;
    }
    const int directory = open(path.parent_path().c_str(), O_RDONLY);
    if (directory >= 0) {
        fsync(directory);
        close(directory);
    }
    return true;
}

void WriterLoop() {
    std::unique_lock<std::mutex> lock(g_mutex);
    for (;;) {
        const auto pending = std::find_if(g_files.begin(), g_files.end(),
                                          [](const auto& file) { return file.second.written < file.second.version; });
        if (pending == g_files.end()) {
            g_work.wait(lock);
            continue;
        }
        const std::filesystem::path path = pending->first;
        const std::shared_ptr<const Bytes> contents = pending->second.latest;
        const uint64_t version = pending->second.version;
        lock.unlock();

        const auto start = std::chrono::steady_clock::now();
        const bool ok = WriteToDisk(path, *contents);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (ok) {
            std::printf("[nand] saved '%s' in the background (%.1f MB, %.0f ms)\n", path.filename().string().c_str(),
                        contents->size() / 1048576.0, ms);
            std::fflush(stdout);
        } else {
            LogNandError("nand-background", "FAILED to write '%s', retrying in a second", HostPathText(path).c_str());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        lock.lock();
        if (ok) {
            if (const auto it = g_files.find(path); it != g_files.end()) {
                it->second.written = std::max(it->second.written, version);
            }
        }
        g_done.notify_all();
    }
}

void StartWriterLocked() {
    if (g_writerStarted) return;
    g_writerStarted = true;
    std::thread(WriterLoop).detach();
}

bool AllWrittenLocked(const std::filesystem::path* only) {
    for (const auto& [path, entry] : g_files) {
        if (only != nullptr && path != *only) continue;
        if (entry.written < entry.version) return false;
    }
    return true;
}

} // namespace

FILE* Open(const std::filesystem::path& hostPath, int32_t mode, void** stream) {
    std::shared_ptr<const Bytes> contents;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (const auto it = g_files.find(hostPath); it != g_files.end()) contents = it->second.latest;
    }
    if (!contents) {
        // Only a write open brings a file into memory; until then reads go to the disk as before.
        if (mode < 2) return nullptr;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(hostPath, ec)) return nullptr;
        const uintmax_t size = std::filesystem::file_size(hostPath, ec);
        if (ec || size < kMinSize) return nullptr;
        const std::filesystem::path scratch = ScratchPathFor(hostPath);
        if (PathExists(scratch)) {
            LogNandWarning("nand-background", "WARNING: discarding unfinished background save '%s' from a previous run",
                           HostPathText(scratch).c_str());
            NandRemove(scratch);
        }
        auto bytes = std::make_shared<Bytes>(static_cast<size_t>(size));
        FILE* file = NandFopen(hostPath, "rb");
        if (!file) return nullptr;
        const bool ok = std::fread(bytes->data(), 1, bytes->size(), file) == bytes->size();
        std::fclose(file);
        if (!ok) {
            LogNandWarning("NANDOpen", "WARNING: could not read '%s' into memory, using the shadow copy",
                           HostPathText(hostPath).c_str());
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto [it, inserted] = g_files.try_emplace(hostPath);
        if (inserted) it->second.latest = std::move(bytes);
        contents = it->second.latest;
        StartWriterLocked();
    }

    auto* s = new Stream{hostPath, std::move(contents), {}, mode >= 2, false, 0};
    FILE* file = funopen(s, ReadFn, s->writable ? WriteFn : nullptr, SeekFn, CloseFn);
    if (!file) {
        delete s;
        return nullptr;
    }
    *stream = s;
    return file;
}

void Commit(void* stream) {
    auto* s = static_cast<Stream*>(stream);
    if (!s->dirty) return;  // opened for writing, nothing written
    auto contents = std::make_shared<const Bytes>(std::move(s->owned));
    s->owned.clear();
    s->dirty = false;
    s->shared = contents;
    std::lock_guard<std::mutex> lock(g_mutex);
    Entry& entry = g_files[s->path];
    entry.latest = std::move(contents);
    ++entry.version;
    StartWriterLocked();
    g_work.notify_one();
}

bool Settle(const std::filesystem::path& hostPath) {
    std::unique_lock<std::mutex> lock(g_mutex);
    if (g_files.find(hostPath) == g_files.end()) return true;
    if (!g_done.wait_for(lock, kSettleTimeout, [&] { return AllWrittenLocked(&hostPath); })) {
        LogNandError("nand-background", "ERROR: '%s' still not written after 5 s; retaining it for retry",
                     HostPathText(hostPath).c_str());
        return false;
    }
    g_files.erase(hostPath);
    return true;
}

bool FlushAll() {
    std::unique_lock<std::mutex> lock(g_mutex);
    if (!g_done.wait_for(lock, kSettleTimeout, [] { return AllWrittenLocked(nullptr); })) {
        LogNandError("nand-background", "ERROR: background saves still pending after 5 s");
        return false;
    }
    return true;
}

#else

FILE* Open(const std::filesystem::path&, int32_t, void**) { return nullptr; }
void Commit(void*) {}
bool Settle(const std::filesystem::path&) { return true; }
bool FlushAll() { return true; }

#endif

} // namespace NandMemFile

// For callers outside the NAND layer (leaving the game, a restart): every background save on disk first.
extern "C" bool NAND_HLE_FlushBackgroundSaves() {
    return NandMemFile::FlushAll();
}
