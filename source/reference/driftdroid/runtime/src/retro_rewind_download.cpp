// Only the Switch launcher uses this today (Android has its own installer in Kotlin); the test
// program at the bottom builds it on a PC.
#if defined(__SWITCH__) || defined(RR_DOWNLOAD_TEST_MAIN)

#include "retro_rewind_download.h"

#include <cryptopp/crc.h>
#include <cryptopp/sha.h>
#include <cryptopp/zinflate.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace fs = std::filesystem;

namespace RetroRewindDownload {
namespace {

// Retro Rewind 6.13.0 as published on the project's own update server. The same archive, size and
// Code.pul the Android app pins in RetroRewindRelease.kt. It is the newest full archive there is:
// 6.13.1 exists only as a small update on top of it (175 character sound files and two tracks, no
// game code); kUpdate below is unpacked over it.
constexpr Release kRelease{
    .version = "6.13.0",
    .host = "cdn.update.rwfc.net",
    .path = "/RetroRewind/zip/6.13.0-full.zip",
    .archiveBytes = 1938862721ull,
    .tableOffset = 1938354676ull,
    .tableBytes = 508045u,
    .tableSha256 = "3e66ed61cf76d2af4bf40cc9313e31a8b31c6665e76205c53ee12dce0cd32b4a",
    .codePulSha256 = "7471e1d2f61c11ab0de6544728406ee1359a4eab736b2dc6a73b741f69e7b4a3",
    .unpackedBytes = 2205000000ull,
};

// The 6.13.1 update archive: same layout, a few files that are added to or replace the ones from
// the full archive, among them version.txt.
constexpr Release kUpdate{
    .version = "6.13.1",
    .host = "cdn.update.rwfc.net",
    .path = "/RetroRewind/zip/6.13.1.zip",
    .archiveBytes = 29403976ull,
    .tableOffset = 29379179ull,
    .tableBytes = 24797u,
    .tableSha256 = "17eb709bdca8d0ffb134ad33e8955e404c7d65cca1b262b394958e2bc7787f25",
    .codePulSha256 = "7471e1d2f61c11ab0de6544728406ee1359a4eab736b2dc6a73b741f69e7b4a3",
    .unpackedBytes = 36000000ull,
};

constexpr char kPackPrefix[] = "RetroRewind6/";
constexpr char kStateFile[] = ".download_state";
constexpr int kSocketTimeoutSeconds = 15;
constexpr int kMaxFailuresInARow = 8;
constexpr uint64_t kStateSaveInterval = 8ull << 20;

constexpr char kErrNoServer[] =
    "Could not reach the Retro Rewind server. Check the internet connection and try again.";
constexpr char kErrDropped[] =
    "The connection kept dropping. Your progress is saved: choose Download again to continue.";
constexpr char kErrChanged[] =
    "The Retro Rewind download on the server has changed. This version of DriftDroid cannot install it.";
constexpr char kErrWrite[] = "Could not write to the SD card. Check that it has enough free space.";
constexpr char kErrCancelled[] = "Download stopped. Your progress is saved.";
constexpr char kErrSecure[] = "Could not set up a secure connection (cacert.pem is missing or unreadable).";

std::string Hex(const uint8_t* data, size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 15]);
    }
    return out;
}

uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// ---------------------------------------------------------------------------------------------
// HTTPS: one ranged GET at a time
// ---------------------------------------------------------------------------------------------

struct Tls {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt roots;
    bool ready = false;

    Tls() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_x509_crt_init(&roots);
    }
    ~Tls() {
        mbedtls_x509_crt_free(&roots);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }
    bool Init(const fs::path& caBundle) {
        static const char tag[] = "driftdroid_rr_download";
        if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const unsigned char*>(tag),
                                  sizeof(tag) - 1) != 0) {
            return false;
        }
        ready = mbedtls_x509_crt_parse_file(&roots, caBundle.string().c_str()) >= 0;
        return ready;
    }
};

int BioSend(void* ctx, const unsigned char* buf, size_t len) {
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    const ssize_t n = ::send(*static_cast<int*>(ctx), buf, len, flags);
    return n < 0 ? -0x004E /* MBEDTLS_ERR_NET_SEND_FAILED */ : static_cast<int>(n);
}

int BioRecv(void* ctx, unsigned char* buf, size_t len) {
    const ssize_t n = ::recv(*static_cast<int*>(ctx), buf, len, 0);
    return n < 0 ? -0x004C /* MBEDTLS_ERR_NET_RECV_FAILED */ : static_cast<int>(n);
}

class HttpsRange {
public:
    explicit HttpsRange(Tls& tls) : tls_(tls) {}
    ~HttpsRange() { Close(); }

    // Requests bytes [first, last] of the release archive. False when the server could not be
    // reached or did not answer with that range; `changed` is set when the archive is not the
    // pinned size any more.
    bool Open(const Release& release, uint64_t first, uint64_t last, bool& changed) {
        Close();
        changed = false;
        if (!Connect(release.host)) {
            return false;
        }
        mbedtls_ssl_init(&ssl_);
        mbedtls_ssl_config_init(&config_);
        haveSsl_ = true;
        if (mbedtls_ssl_config_defaults(&config_, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
            return false;
        }
        mbedtls_ssl_conf_authmode(&config_, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&config_, &tls_.roots, nullptr);
        mbedtls_ssl_conf_rng(&config_, mbedtls_ctr_drbg_random, &tls_.drbg);
        if (mbedtls_ssl_setup(&ssl_, &config_) != 0 || mbedtls_ssl_set_hostname(&ssl_, release.host) != 0) {
            return false;
        }
        mbedtls_ssl_set_bio(&ssl_, &fd_, BioSend, BioRecv, nullptr);
        int rc = 0;
        while ((rc = mbedtls_ssl_handshake(&ssl_)) != 0) {
            if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
                return false;
            }
        }

        char request[512];
        const int requestSize = std::snprintf(request, sizeof(request),
                                              "GET %s HTTP/1.1\r\n"
                                              "Host: %s\r\n"
                                              "Range: bytes=%llu-%llu\r\n"
                                              "User-Agent: DriftDroid\r\n"
                                              "Accept-Encoding: identity\r\n"
                                              "Connection: close\r\n\r\n",
                                              release.path, release.host, static_cast<unsigned long long>(first),
                                              static_cast<unsigned long long>(last));
        for (int sent = 0; sent < requestSize;) {
            rc = mbedtls_ssl_write(&ssl_, reinterpret_cast<const unsigned char*>(request) + sent, requestSize - sent);
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            if (rc <= 0) {
                return false;
            }
            sent += rc;
        }

        // Response head: status line and the Content-Range that says which bytes follow.
        std::string head;
        while (head.size() < 4 || head.compare(head.size() - 4, 4, "\r\n\r\n") != 0) {
            uint8_t c = 0;
            if (head.size() > 16384 || !ReadExact(&c, 1)) {
                return false;
            }
            head.push_back(static_cast<char>(c));
        }
        std::transform(head.begin(), head.end(), head.begin(), [](unsigned char c) { return std::tolower(c); });
        if (head.compare(0, 5, "http/") != 0) {
            return false;
        }
        const size_t space = head.find(' ');
        if (space == std::string::npos || std::atoi(head.c_str() + space + 1) != 206) {
            return false;
        }
        unsigned long long gotFirst = 0, gotLast = 0, gotTotal = 0;
        const size_t range = head.find("content-range: bytes ");
        if (range == std::string::npos ||
            std::sscanf(head.c_str() + range + 21, "%llu-%llu/%llu", &gotFirst, &gotLast, &gotTotal) != 3) {
            return false;
        }
        if (gotTotal != release.archiveBytes) {
            changed = true;
            return false;
        }
        return gotFirst == first && gotLast == last;
    }

    // Reads up to `size` bytes; 0 at the end of the stream or on an error.
    size_t Read(uint8_t* out, size_t size) {
        if (pos_ == len_) {
            int rc = 0;
            do {
                rc = mbedtls_ssl_read(&ssl_, buffer_.data(), buffer_.size());
            } while (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE);
            if (rc <= 0) {
                return 0;
            }
            pos_ = 0;
            len_ = static_cast<size_t>(rc);
        }
        const size_t n = std::min(size, len_ - pos_);
        std::memcpy(out, buffer_.data() + pos_, n);
        pos_ += n;
        return n;
    }

    bool ReadExact(uint8_t* out, size_t size) {
        while (size > 0) {
            const size_t n = Read(out, size);
            if (n == 0) {
                return false;
            }
            out += n;
            size -= n;
        }
        return true;
    }

    void Close() {
        if (haveSsl_) {
            mbedtls_ssl_free(&ssl_);
            mbedtls_ssl_config_free(&config_);
            haveSsl_ = false;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        pos_ = len_ = 0;
    }

private:
    bool Connect(const char* host) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* list = nullptr;
        if (::getaddrinfo(host, "443", &hints, &list) != 0 || list == nullptr) {
            return false;
        }
        for (const addrinfo* a = list; a != nullptr && fd_ < 0; a = a->ai_next) {
            const int fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (fd < 0) {
                continue;
            }
            timeval timeout{};
            timeout.tv_sec = kSocketTimeoutSeconds;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            if (::connect(fd, a->ai_addr, a->ai_addrlen) == 0) {
                fd_ = fd;
            } else {
                ::close(fd);
            }
        }
        ::freeaddrinfo(list);
        return fd_ >= 0;
    }

    Tls& tls_;
    int fd_ = -1;
    bool haveSsl_ = false;
    mbedtls_ssl_context ssl_;
    mbedtls_ssl_config config_;
    std::vector<uint8_t> buffer_ = std::vector<uint8_t>(16384);
    size_t pos_ = 0;
    size_t len_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Zip
// ---------------------------------------------------------------------------------------------

struct Entry {
    std::string relative;  // path inside RetroRewind6, "" when the entry is outside the pack
    uint64_t offset = 0;   // of the entry's local header
    uint32_t packed = 0;
    uint32_t unpacked = 0;
    uint32_t crc = 0;
    uint16_t method = 0;
    bool wanted = false;
    bool directory = false;
};

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

bool ParseTable(const std::vector<uint8_t>& table, std::vector<Entry>& entries) {
    size_t at = 0;
    while (at + 46 <= table.size() && Le32(&table[at]) == 0x02014B50u) {
        const uint8_t* p = &table[at];
        const uint16_t flags = Le16(p + 8);
        const size_t nameSize = Le16(p + 28);
        const size_t extraSize = Le16(p + 30);
        const size_t commentSize = Le16(p + 32);
        if (at + 46 + nameSize + extraSize + commentSize > table.size()) {
            return false;
        }
        Entry e;
        e.method = Le16(p + 10);
        e.crc = Le32(p + 16);
        e.packed = Le32(p + 20);
        e.unpacked = Le32(p + 24);
        e.offset = Le32(p + 42);
        const std::string name(reinterpret_cast<const char*>(p + 46), nameSize);
        // Encrypted entries, sizes stored after the data, zip64 and other packers are not expected
        // in this archive; refusing them keeps the stream reader simple and exact.
        if ((flags & 0x0009u) != 0 || (e.method != 0 && e.method != 8) || e.packed == 0xFFFFFFFFu ||
            e.offset == 0xFFFFFFFFu) {
            return false;
        }
        if (name.compare(0, sizeof(kPackPrefix) - 1, kPackPrefix) == 0 && name.size() > sizeof(kPackPrefix) - 1) {
            e.relative = name.substr(sizeof(kPackPrefix) - 1);
            e.directory = e.relative.back() == '/';
            if (e.directory) {
                e.relative.pop_back();
            }
            if (!SafeRelativePath(e.relative)) {
                return false;
            }
            e.wanted = true;
        }
        if (!entries.empty() && e.offset <= entries.back().offset) {
            return false;  // the stream reader relies on archive order
        }
        entries.push_back(std::move(e));
        at += 46 + nameSize + extraSize + commentSize;
    }
    return !entries.empty();
}

// Receives unpacked bytes: writes them out and keeps the running checksum and size.
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

enum class Step { Ok, Network, Damaged, Disk, Cancelled };

// True when `pack`/Binaries/Code.pul is the one this build has native code for.
bool HasPinnedCode(const fs::path& pack) {
    FILE* file = std::fopen((pack / "Binaries" / "Code.pul").string().c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    CryptoPP::SHA256 sha;
    std::vector<uint8_t> chunk(64 * 1024);
    for (size_t n = 0; (n = std::fread(chunk.data(), 1, chunk.size(), file)) > 0;) {
        sha.Update(chunk.data(), n);
    }
    std::fclose(file);
    uint8_t digest[CryptoPP::SHA256::DIGESTSIZE];
    sha.Final(digest);
    return Hex(digest, sizeof(digest)) == kRelease.codePulSha256;
}

std::string PackVersion(const fs::path& pack) {
    char text[32] = {};
    if (FILE* file = std::fopen((pack / "version.txt").string().c_str(), "rb")) {
        const size_t n = std::fread(text, 1, sizeof(text) - 1, file);
        std::fclose(file);
        text[n] = 0;
    }
    std::string version(text);
    while (!version.empty() && (version.back() == '\n' || version.back() == '\r' || version.back() == ' ')) {
        version.pop_back();
    }
    return version;
}

}  // namespace

const Release& PinnedRelease() { return kRelease; }

const char* PinnedVersion() { return kUpdate.version; }

uint64_t RequiredFreeBytes() { return kRelease.unpackedBytes + kUpdate.unpackedBytes + (200ull << 20); }

Job::~Job() {
    Cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Job::Start(const fs::path& target, const fs::path& caBundle) {
    target_ = target;
    caBundle_ = caBundle;
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
    const auto cancelled = [this] { return cancel_.load(std::memory_order_acquire); };
    // Waits before a reconnect, a little longer each time; false when cancelled meanwhile.
    const auto backOff = [&](int failures) {
        reconnecting_.store(true, std::memory_order_relaxed);
        const int tenths = std::min(150, 10 << std::min(failures, 4));
        for (int i = 0; i < tenths && !cancelled(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        reconnecting_.store(false, std::memory_order_relaxed);
        return !cancelled();
    };

    auto tls = std::make_unique<Tls>();
    if (!tls->Init(caBundle_)) {
        error_ = kErrSecure;
        return;
    }
    HttpsRange http(*tls);

    fs::path partial = target_;
    partial += ".partial";
    std::error_code ec;
    // An installed pack that already holds this build's game code and is only behind on the small
    // update gets just that, unpacked straight over it, instead of the whole 1.9 GB again. A pack
    // already at the newest version is downloaded afresh: that is what asking again means.
    const bool updateOnly =
        !fs::exists(partial, ec) && HasPinnedCode(target_) &&
        (PackVersion(target_) != kUpdate.version || fs::exists(target_ / kStateFile, ec));
    const fs::path work = updateOnly ? target_ : partial;
    const fs::path statePath = work / kStateFile;
    // Which archive an interrupted run was in, and the entry it had reached.
    std::string stateHash;
    unsigned long long stateNext = 0;
    if (FILE* state = std::fopen(statePath.string().c_str(), "rb")) {
        char hash[80] = {};
        if (std::fscanf(state, "%79s %llu", hash, &stateNext) == 2) {
            stateHash = hash;
        }
        std::fclose(state);
    }
    std::vector<uint8_t> chunk(64 * 1024);
    std::vector<char> fileBuffer(256 * 1024);

    // The full archive, then the update over it.
    for (const Release* stage : {&kRelease, &kUpdate}) {
    const Release& release = *stage;
    const bool fullArchive = stage == &kRelease;
    if (fullArchive && (updateOnly || stateHash == kUpdate.tableSha256)) {
        continue;
    }

    // 1. The file table.
    std::vector<uint8_t> table(release.tableBytes);
    for (int failures = 0;;) {
        bool changed = false;
        if (http.Open(release, release.tableOffset, release.tableOffset + release.tableBytes - 1, changed) &&
            http.ReadExact(table.data(), table.size())) {
            break;
        }
        http.Close();
        if (changed) {
            error_ = kErrChanged;
            return;
        }
        if (++failures >= 3) {
            error_ = kErrNoServer;
            return;
        }
        if (!backOff(failures)) {
            error_ = kErrCancelled;
            return;
        }
    }
    http.Close();
    {
        uint8_t digest[CryptoPP::SHA256::DIGESTSIZE];
        CryptoPP::SHA256().CalculateDigest(digest, table.data(), table.size());
        if (Hex(digest, sizeof(digest)) != release.tableSha256) {
            error_ = kErrChanged;
            return;
        }
    }
    std::vector<Entry> entries;
    if (!ParseTable(table, entries)) {
        error_ = kErrChanged;
        return;
    }
    table = {};

    size_t first = entries.size(), last = 0;
    uint64_t total = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].wanted) {
            first = std::min(first, i);
            last = i;
            total += entries[i].packed;
        }
    }
    if (first == entries.size()) {
        error_ = kErrChanged;
        return;
    }
    bytesTotal_.store(total, std::memory_order_relaxed);
    const uint64_t streamEnd = last + 1 < entries.size() ? entries[last + 1].offset : release.tableOffset;

    // 2. Where to continue from.
    size_t index = first;
    if (stateHash == release.tableSha256 && stateNext >= first && stateNext <= last + 1) {
        index = static_cast<size_t>(stateNext);
    }
    if (fullArchive && index == first) {
        fs::remove_all(partial, ec);  // nothing usable there: start clean
    }
    fs::create_directories(work, ec);
    if (!fs::is_directory(work, ec)) {
        error_ = kErrWrite;
        return;
    }
    const auto saveState = [&](size_t next) {
        if (FILE* state = std::fopen(statePath.string().c_str(), "wb")) {
            std::fprintf(state, "%s %llu\n", release.tableSha256, static_cast<unsigned long long>(next));
            std::fclose(state);
        }
    };
    uint64_t done = 0;
    for (size_t i = first; i < index; ++i) {
        if (entries[i].wanted) {
            done += entries[i].packed;
        }
    }
    bytesDone_.store(done, std::memory_order_relaxed);

    // 3. Stream the archive from the current entry on, unpacking as the bytes arrive.
    std::string lastDirectory;
    uint64_t savedAt = done;
    int failures = 0;
    bool open = false;
    uint64_t position = 0;

    const auto ensureDirectory = [&](const fs::path& dir) {
        const std::string key = dir.string();
        if (key != lastDirectory) {
            std::error_code dirEc;
            fs::create_directories(dir, dirEc);
            lastDirectory = key;
        }
    };
    // Reads `size` stream bytes into nothing.
    const auto discard = [&](uint64_t size) {
        while (size > 0) {
            const size_t n = http.Read(chunk.data(), static_cast<size_t>(std::min<uint64_t>(size, chunk.size())));
            if (n == 0) {
                return false;
            }
            size -= n;
        }
        return true;
    };
    const auto processEntry = [&](const Entry& e) -> Step {
        if (e.offset < position || !discard(e.offset - position)) {
            return Step::Network;
        }
        uint8_t header[30];
        if (!http.ReadExact(header, sizeof(header))) {
            return Step::Network;
        }
        if (Le32(header) != 0x04034B50u) {
            return Step::Damaged;
        }
        const uint64_t nameAndExtra = static_cast<uint64_t>(Le16(header + 26)) + Le16(header + 28);
        if (!discard(nameAndExtra)) {
            return Step::Network;
        }
        position = e.offset + sizeof(header) + nameAndExtra + e.packed;

        if (!e.wanted) {
            return discard(e.packed) ? Step::Ok : Step::Network;
        }
        const fs::path path = work / fs::path(e.relative);
        if (e.directory) {
            ensureDirectory(path);
            return discard(e.packed) ? Step::Ok : Step::Network;
        }
        ensureDirectory(path.parent_path());

        Output output;
        output.file = std::fopen(path.string().c_str(), "wb");
        if (output.file == nullptr) {
            return Step::Disk;
        }
        std::setvbuf(output.file, fileBuffer.data(), _IOFBF, fileBuffer.size());
        Step result = Step::Ok;
        const uint64_t doneBefore = bytesDone_.load(std::memory_order_relaxed);
        try {
            std::unique_ptr<CryptoPP::Inflator> inflator;
            if (e.method == 8) {
                inflator = std::make_unique<CryptoPP::Inflator>(new OutputSink(output));
            }
            for (uint64_t left = e.packed; left > 0 && result == Step::Ok;) {
                if (cancelled()) {
                    result = Step::Cancelled;
                    break;
                }
                const size_t n = http.Read(chunk.data(), static_cast<size_t>(std::min<uint64_t>(left, chunk.size())));
                if (n == 0) {
                    result = Step::Network;
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
                    result = Step::Disk;
                }
            }
            if (result == Step::Ok && inflator) {
                inflator->MessageEnd();
            }
        } catch (const CryptoPP::Exception&) {
            result = Step::Damaged;
        }
        if (std::fclose(output.file) != 0 && result == Step::Ok) {
            result = Step::Disk;
        }
        if (result == Step::Ok) {
            if (output.failed) {
                result = Step::Disk;
            } else {
                uint8_t crc[4];
                output.crc.Final(crc);
                if (output.written != e.unpacked || Le32(crc) != e.crc) {
                    result = Step::Damaged;
                }
            }
        }
        if (result != Step::Ok) {
            bytesDone_.store(doneBefore, std::memory_order_relaxed);
            std::error_code removeEc;
            fs::remove(path, removeEc);
        }
        return result;
    };

    while (index <= last) {
        if (cancelled()) {
            saveState(index);
            error_ = kErrCancelled;
            return;
        }
        if (!open) {
            bool changed = false;
            position = entries[index].offset;
            open = http.Open(release, position, streamEnd - 1, changed);
            if (changed) {
                saveState(index);
                error_ = kErrChanged;
                return;
            }
        }
        const Step step = open ? processEntry(entries[index]) : Step::Network;
        if (step == Step::Ok) {
            ++index;
            failures = 0;
            const uint64_t now = bytesDone_.load(std::memory_order_relaxed);
            if (now - savedAt >= kStateSaveInterval) {
                saveState(index);
                savedAt = now;
            }
            continue;
        }
        http.Close();
        open = false;
        saveState(index);
        if (step == Step::Disk) {
            error_ = kErrWrite;
            return;
        }
        if (step == Step::Cancelled) {
            error_ = kErrCancelled;
            return;
        }
        if (++failures >= kMaxFailuresInARow) {
            error_ = kErrDropped;
            return;
        }
        if (!backOff(failures)) {
            error_ = kErrCancelled;
            return;
        }
    }
    http.Close();
    saveState(index);
    // From here on an interrupted run belongs to the next archive, if there is one.
    stateHash.clear();
    stateNext = 0;
    }  // each archive

    // 4. The pack must be the one this build has code for, then it goes live.
    if (!HasPinnedCode(work)) {
        error_ = kErrChanged;
        return;
    }
    fs::remove(statePath, ec);
    if (updateOnly) {
        return;  // unpacked in place: nothing to move
    }
    // Downloading again over an installed pack: the old one steps aside only now, and is deleted
    // once the new one is in place.
    fs::path old = target_;
    old += ".old";
    fs::remove_all(old, ec);
    const bool replacing = fs::exists(target_, ec);
    if (replacing) {
        fs::rename(target_, old, ec);
        if (ec) {
            error_ = kErrWrite;
            return;
        }
    }
    fs::rename(partial, target_, ec);
    if (ec) {
        error_ = kErrWrite;
        if (replacing) {
            std::error_code restoreEc;
            fs::rename(old, target_, restoreEc);
        }
        return;
    }
    if (replacing) {
        fs::remove_all(old, ec);
    }
}

}  // namespace RetroRewindDownload

#ifdef RR_DOWNLOAD_TEST_MAIN
// PC check: retro_rewind_download_test <target folder> <cacert.pem>
int main(int argc, char** argv) {
    if (argc < 3) {
        return 2;
    }
    RetroRewindDownload::Job job;
    job.Start(argv[1], argv[2]);
    const char* stopAt = std::getenv("RR_TEST_CANCEL_AT_MB");
    while (!job.Finished()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::fprintf(stderr, "%llu / %llu MB%s\n", static_cast<unsigned long long>(job.BytesDone() >> 20),
                     static_cast<unsigned long long>(job.BytesTotal() >> 20), job.Reconnecting() ? " (reconnecting)" : "");
        if (stopAt != nullptr && (job.BytesDone() >> 20) >= std::strtoull(stopAt, nullptr, 10)) {
            job.Cancel();
        }
    }
    const std::string error = job.Finish();
    std::fprintf(stderr, "result: %s\n", error.empty() ? "ok" : error.c_str());
    return error.empty() ? 0 : 1;
}
#endif

#endif  // __SWITCH__ || RR_DOWNLOAD_TEST_MAIN
