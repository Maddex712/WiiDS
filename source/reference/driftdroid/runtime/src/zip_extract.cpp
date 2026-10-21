#if defined(__SWITCH__) || defined(ZIP_EXTRACT_TEST_MAIN)

#include "zip_extract.h"

#include <cryptopp/crc.h>
#include <cryptopp/zinflate.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace ZipExtract {
namespace {

constexpr char kErrOpen[] = "Could not open the zip file.";
constexpr char kErrNotZip[] = "That file is not a zip this app can unpack (damaged, encrypted or too large).";
constexpr char kErrDamaged[] = "The zip file is damaged: a file inside it failed its check.";
constexpr char kErrWrite[] = "Could not write to the SD card. Check that it has enough free space.";
constexpr char kErrExists[] = "A mod with that name is already there.";

uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

struct Entry {
    std::string name;
    uint64_t offset = 0;
    uint32_t packed = 0;
    uint32_t unpacked = 0;
    uint32_t crc = 0;
    uint16_t method = 0;
    bool directory = false;
};

// Rejects anything that could land outside the target folder.
bool SafeRelativePath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos) {
        return false;
    }
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = std::min(path.find('/', start), path.size());
        if (path.compare(start, end - start, "..") == 0) {
            return false;
        }
        start = end + 1;
    }
    return true;
}

bool ReadAt(FILE* file, uint64_t offset, uint8_t* out, size_t size) {
    return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 && std::fread(out, 1, size, file) == size;
}

bool ReadTable(FILE* file, std::vector<Entry>& entries) {
    if (std::fseek(file, 0, SEEK_END) != 0) {
        return false;
    }
    const long fileSize = std::ftell(file);
    if (fileSize < 22) {
        return false;
    }
    // The end record is the last thing in the file, apart from an optional comment.
    const size_t tailSize = static_cast<size_t>(std::min<long>(fileSize, 22 + 65535));
    std::vector<uint8_t> tail(tailSize);
    if (!ReadAt(file, static_cast<uint64_t>(fileSize) - tailSize, tail.data(), tailSize)) {
        return false;
    }
    size_t end = tailSize - 22;
    for (;; --end) {
        if (Le32(&tail[end]) == 0x06054B50u) {
            break;
        }
        if (end == 0) {
            return false;
        }
    }
    const uint32_t count = Le16(&tail[end + 10]);
    const uint32_t tableSize = Le32(&tail[end + 12]);
    const uint32_t tableOffset = Le32(&tail[end + 16]);
    if (count == 0xFFFFu || tableOffset == 0xFFFFFFFFu || static_cast<uint64_t>(tableOffset) + tableSize > static_cast<uint64_t>(fileSize)) {
        return false;  // zip64 or nonsense
    }
    std::vector<uint8_t> table(tableSize);
    if (tableSize == 0 || !ReadAt(file, tableOffset, table.data(), tableSize)) {
        return false;
    }
    size_t at = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 46 > table.size() || Le32(&table[at]) != 0x02014B50u) {
            return false;
        }
        const uint8_t* p = &table[at];
        const size_t nameSize = Le16(p + 28), extraSize = Le16(p + 30), commentSize = Le16(p + 32);
        if (at + 46 + nameSize + extraSize + commentSize > table.size()) {
            return false;
        }
        Entry e;
        e.method = Le16(p + 10);
        e.crc = Le32(p + 16);
        e.packed = Le32(p + 20);
        e.unpacked = Le32(p + 24);
        e.offset = Le32(p + 42);
        e.name.assign(reinterpret_cast<const char*>(p + 46), nameSize);
        if ((Le16(p + 8) & 1u) != 0 || (e.method != 0 && e.method != 8) || e.packed == 0xFFFFFFFFu ||
            e.unpacked == 0xFFFFFFFFu || e.offset == 0xFFFFFFFFu) {
            return false;
        }
        e.directory = !e.name.empty() && e.name.back() == '/';
        if (e.directory) {
            e.name.pop_back();
        }
        // Skip the clutter macOS adds to zips.
        const bool clutter = e.name.compare(0, 9, "__MACOSX/") == 0 || e.name == "__MACOSX";
        if (!clutter && !e.name.empty()) {
            if (!SafeRelativePath(e.name)) {
                return false;
            }
            entries.push_back(std::move(e));
        }
        at += 46 + nameSize + extraSize + commentSize;
    }
    return !entries.empty();
}

struct Output {
    FILE* file = nullptr;
    CryptoPP::CRC32 crc;
    uint64_t written = 0;
    bool failed = false;
    void Write(const uint8_t* data, size_t size) {
        if (size == 0 || failed) {
            return;
        }
        if (std::fwrite(data, 1, size, file) != size) {
            failed = true;
            return;
        }
        crc.Update(data, size);
        written += size;
    }
};

class OutputSink final : public CryptoPP::Bufferless<CryptoPP::Sink> {
public:
    explicit OutputSink(Output& output) : output_(output) {}
    size_t Put2(const CryptoPP::byte* data, size_t size, int, bool) override {
        output_.Write(data, size);
        return 0;
    }

private:
    Output& output_;
};

}  // namespace

Job::~Job() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Job::Start(const fs::path& zip, const fs::path& target) {
    zip_ = zip;
    target_ = target;
    worker_ = std::thread([this] {
        Run();
        done_.store(true, std::memory_order_release);
    });
}

std::string Job::Finish() {
    if (worker_.joinable()) {
        worker_.join();
    }
    return error_;
}

void Job::Run() {
    std::error_code ec;
    if (fs::exists(target_, ec)) {
        error_ = kErrExists;
        return;
    }
    FILE* zip = std::fopen(zip_.string().c_str(), "rb");
    if (zip == nullptr) {
        error_ = kErrOpen;
        return;
    }
    std::vector<Entry> entries;
    if (!ReadTable(zip, entries)) {
        std::fclose(zip);
        error_ = kErrNotZip;
        return;
    }
    uint64_t total = 0;
    for (const Entry& e : entries) {
        total += e.packed;
    }
    bytesTotal_.store(total, std::memory_order_relaxed);

    fs::path partial = target_;
    partial += ".partial";
    fs::remove_all(partial, ec);
    fs::create_directories(partial, ec);

    std::vector<uint8_t> chunk(64 * 1024);
    std::vector<char> fileBuffer(256 * 1024);
    std::string lastDirectory;
    const auto ensureDirectory = [&](const fs::path& dir) {
        const std::string key = dir.string();
        if (key != lastDirectory) {
            std::error_code dirEc;
            fs::create_directories(dir, dirEc);
            lastDirectory = key;
        }
    };

    const char* failure = nullptr;
    for (const Entry& e : entries) {
        const fs::path path = partial / fs::path(e.name);
        if (e.directory) {
            ensureDirectory(path);
            continue;
        }
        ensureDirectory(path.parent_path());
        uint8_t header[30];
        if (!ReadAt(zip, e.offset, header, sizeof(header)) || Le32(header) != 0x04034B50u ||
            std::fseek(zip, Le16(header + 26) + Le16(header + 28), SEEK_CUR) != 0) {
            failure = kErrNotZip;
            break;
        }
        Output output;
        output.file = std::fopen(path.string().c_str(), "wb");
        if (output.file == nullptr) {
            failure = kErrWrite;
            break;
        }
        std::setvbuf(output.file, fileBuffer.data(), _IOFBF, fileBuffer.size());
        try {
            std::unique_ptr<CryptoPP::Inflator> inflator;
            if (e.method == 8) {
                inflator = std::make_unique<CryptoPP::Inflator>(new OutputSink(output));
            }
            for (uint64_t left = e.packed; left > 0 && failure == nullptr;) {
                const size_t n = std::fread(chunk.data(), 1, static_cast<size_t>(std::min<uint64_t>(left, chunk.size())), zip);
                if (n == 0) {
                    failure = kErrNotZip;
                    break;
                }
                if (inflator) {
                    inflator->Put(chunk.data(), n);
                } else {
                    output.Write(chunk.data(), n);
                }
                left -= n;
                bytesDone_.fetch_add(n, std::memory_order_relaxed);
                if (output.failed) {
                    failure = kErrWrite;
                }
            }
            if (failure == nullptr && inflator) {
                inflator->MessageEnd();
            }
        } catch (const CryptoPP::Exception&) {
            failure = kErrDamaged;
        }
        if (std::fclose(output.file) != 0 && failure == nullptr) {
            failure = kErrWrite;
        }
        if (failure == nullptr) {
            uint8_t crc[4];
            output.crc.Final(crc);
            if (output.failed) {
                failure = kErrWrite;
            } else if (output.written != e.unpacked || Le32(crc) != e.crc) {
                failure = kErrDamaged;
            }
        }
        if (failure != nullptr) {
            break;
        }
    }
    std::fclose(zip);
    if (failure != nullptr) {
        fs::remove_all(partial, ec);
        error_ = failure;
        return;
    }
    fs::rename(partial, target_, ec);
    if (ec) {
        fs::remove_all(partial, ec);
        error_ = kErrWrite;
    }
}

}  // namespace ZipExtract

#ifdef ZIP_EXTRACT_TEST_MAIN
#include <chrono>
int main(int argc, char** argv) {
    if (argc < 3) {
        return 2;
    }
    ZipExtract::Job job;
    job.Start(argv[1], argv[2]);
    while (!job.Finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const std::string error = job.Finish();
    std::printf("%s (%llu of %llu bytes)\n", error.empty() ? "ok" : error.c_str(),
                static_cast<unsigned long long>(job.BytesDone()), static_cast<unsigned long long>(job.BytesTotal()));
    return error.empty() ? 0 : 1;
}
#endif
#endif
