// wiids_disc: GameCube/Wii image readers and extraction (see the header).
// WIA/RVZ follow Dolphin's docs/WiaAndRvz.md and DiscIO/WIABlob.cpp (GPL-2.0-or-later).
#if defined(_WIN32)
#define NOMINMAX            // bzlib.h pulls in windows.h
#define WIN32_LEAN_AND_MEAN
#endif
#include "wiids_disc.h"

#include "aes128.h"

#include <bzlib.h>
#include <lzma.h>
#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace wiids::disc {
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

[[noreturn]] void Fail(const std::string& message) { throw std::runtime_error(message); }

uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint64_t Be64(const uint8_t* p) { return uint64_t(Be32(p)) << 32 | Be32(p + 4); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }
uint64_t Le64(const uint8_t* p) { return uint64_t(Le32(p + 4)) << 32 | Le32(p); }

constexpr uint64_t kWiiCluster = 0x8000, kWiiClusterData = 0x7C00, kWiiClusterHashes = 0x400;
constexpr uint32_t kWiiMagic = 0x5D1C9EA3, kGcMagic = 0xC2339F3D;

// Retail Wii common key (index 0); long public, the same constant Dolphin and every Wii tool use.
const uint8_t kCommonKey[16] = {0xeb, 0xe4, 0x2a, 0x22, 0x5e, 0x85, 0x93, 0xe4,
                                0x48, 0xd9, 0xc5, 0x45, 0x73, 0x81, 0xaa, 0xf7};

// ------------------------------------------------------------------ files
class PathPart final : public Part {
public:
    explicit PathPart(const std::string& path) : in_(fs::u8path(path), std::ios::binary) {
        if (!in_) Fail("Couldn't open " + path);
        in_.seekg(0, std::ios::end);
        size_ = uint64_t(in_.tellg());
    }
    uint64_t Size() const override { return size_; }
    void ReadAt(uint64_t offset, void* out, size_t size) override {
        if (offset + size > size_) Fail("The file ends early: it looks incomplete or damaged.");
        in_.seekg(std::streamoff(offset));
        if (!in_.read(static_cast<char*>(out), std::streamsize(size))) Fail("The file couldn't be read.");
    }

private:
    std::ifstream in_;
    uint64_t size_ = 0;
};

#if !defined(_WIN32)
class FdPart final : public Part {
public:
    explicit FdPart(int fd) : fd_(fd) {
        struct stat st{};
        if (fstat(fd_, &st) != 0) Fail("The file couldn't be opened.");
        size_ = uint64_t(st.st_size);
    }
    ~FdPart() override { if (fd_ >= 0) close(fd_); }
    uint64_t Size() const override { return size_; }
    void ReadAt(uint64_t offset, void* out, size_t size) override {
        if (offset + size > size_) Fail("The file ends early: it looks incomplete or damaged.");
        auto* p = static_cast<uint8_t*>(out);
        while (size) {
            const ssize_t n = pread(fd_, p, size, off_t(offset));
            if (n <= 0) Fail("The file couldn't be read.");
            p += n; offset += uint64_t(n); size -= size_t(n);
        }
    }

private:
    int fd_;
    uint64_t size_ = 0;
};
#endif

// Several parts read as one continuous file (split WBFS: .wbfs, .wbf1, .wbf2...).
class Joined {
public:
    explicit Joined(std::vector<std::unique_ptr<Part>>& parts) : parts_(parts) {
        for (auto& p : parts_) { starts_.push_back(size_); size_ += p->Size(); }
    }
    uint64_t Size() const { return size_; }
    void ReadAt(uint64_t offset, void* out, size_t size) {
        auto* p = static_cast<uint8_t*>(out);
        while (size) {
            size_t i = parts_.size();
            while (i > 0 && starts_[i - 1] > offset) --i;
            if (i == 0 || offset >= size_) Fail("The file ends early: it looks incomplete or damaged.");
            Part& part = *parts_[i - 1];
            const uint64_t within = offset - starts_[i - 1];
            const size_t n = size_t(std::min<uint64_t>(size, part.Size() - within));
            part.ReadAt(within, p, n);
            p += n; offset += n; size -= n;
        }
    }

private:
    std::vector<std::unique_ptr<Part>>& parts_;
    std::vector<uint64_t> starts_;
    uint64_t size_ = 0;
};

// ------------------------------------------------------------------ blobs (the 1:1 disc view)
class Blob {
public:
    virtual ~Blob() = default;
    virtual Format format() const = 0;
    virtual uint64_t DiscSize() const = 0;
    virtual void Read(uint64_t offset, void* out, size_t size) = 0;
    // WIA/RVZ store Wii partition data decrypted; they read it directly instead of re-encrypting.
    virtual bool ReadsWiiDecrypted() const { return false; }
    virtual void ReadWiiDecrypted(uint64_t /*partitionDataOffset*/, uint64_t /*offset*/, void* /*out*/, size_t /*size*/) {
        Fail("Internal error: decrypted reads are not available for this format.");
    }
};

class PlainBlob final : public Blob {
public:
    explicit PlainBlob(Joined& file) : file_(file) {}
    Format format() const override { return Format::Iso; }
    uint64_t DiscSize() const override { return file_.Size(); }
    void Read(uint64_t offset, void* out, size_t size) override { file_.ReadAt(offset, out, size); }

private:
    Joined& file_;
};

class CisoBlob final : public Blob {
public:
    explicit CisoBlob(Joined& file) : file_(file) {
        uint8_t header[0x8000];
        file_.ReadAt(0, header, sizeof(header));
        blockSize_ = Le32(header + 4);
        if (blockSize_ == 0 || blockSize_ > (64u << 20)) Fail("This CISO file is damaged.");
        uint64_t next = 0;
        for (size_t i = 0; i < 0x8000 - 8; ++i) {
            map_.push_back(header[8 + i] ? int64_t(next++) : -1);
            if (header[8 + i]) last_ = i + 1;
        }
    }
    Format format() const override { return Format::Ciso; }
    uint64_t DiscSize() const override { return uint64_t(last_) * blockSize_; }
    void Read(uint64_t offset, void* out, size_t size) override {
        auto* p = static_cast<uint8_t*>(out);
        while (size) {
            const uint64_t block = offset / blockSize_, within = offset % blockSize_;
            const size_t n = size_t(std::min<uint64_t>(size, blockSize_ - within));
            if (block >= map_.size() || map_[block] < 0) std::memset(p, 0, n);
            else file_.ReadAt(0x8000 + uint64_t(map_[block]) * blockSize_ + within, p, n);
            p += n; offset += n; size -= n;
        }
    }

private:
    Joined& file_;
    uint32_t blockSize_ = 0;
    size_t last_ = 0;
    std::vector<int64_t> map_;
};

class GczBlob final : public Blob {
public:
    explicit GczBlob(Joined& file) : file_(file) {
        uint8_t h[32];
        file_.ReadAt(0, h, sizeof(h));
        dataSize_ = Le64(h + 16);
        blockSize_ = Le32(h + 24);
        const uint32_t blocks = Le32(h + 28);
        if (blockSize_ == 0 || blockSize_ > (64u << 20) || uint64_t(blocks) * blockSize_ < dataSize_) Fail("This GCZ file is damaged.");
        Bytes table(size_t(blocks) * 8);
        file_.ReadAt(32, table.data(), table.size());
        for (uint32_t i = 0; i < blocks; ++i) pointers_.push_back(Le64(table.data() + 8 * i));
        dataStart_ = 32 + uint64_t(blocks) * 12;
        compressedEnd_ = file_.Size() - dataStart_;
    }
    Format format() const override { return Format::Gcz; }
    uint64_t DiscSize() const override { return dataSize_; }
    void Read(uint64_t offset, void* out, size_t size) override {
        auto* p = static_cast<uint8_t*>(out);
        while (size) {
            const uint64_t block = offset / blockSize_, within = offset % blockSize_;
            const size_t n = size_t(std::min<uint64_t>(size, blockSize_ - within));
            Load(block);
            std::memcpy(p, cache_.data() + within, n);
            p += n; offset += n; size -= n;
        }
    }

private:
    void Load(uint64_t block) {
        if (block == cached_) return;
        if (block >= pointers_.size()) Fail("Read past the end of the GCZ image.");
        constexpr uint64_t kUncompressed = 1ull << 63;
        const uint64_t start = pointers_[block] & ~kUncompressed;
        const uint64_t end = block + 1 < pointers_.size() ? pointers_[block + 1] & ~kUncompressed : compressedEnd_;
        if (end < start || end - start > blockSize_ + 1024) Fail("This GCZ file is damaged.");
        Bytes in(size_t(end - start));
        file_.ReadAt(dataStart_ + start, in.data(), in.size());
        cache_.assign(blockSize_, 0);
        if (pointers_[block] & kUncompressed) {
            std::memcpy(cache_.data(), in.data(), std::min<size_t>(in.size(), cache_.size()));
        } else {
            uLongf outSize = blockSize_;
            if (uncompress(cache_.data(), &outSize, in.data(), uLong(in.size())) != Z_OK) Fail("This GCZ file is damaged.");
        }
        cached_ = block;
    }
    Joined& file_;
    uint64_t dataSize_ = 0, dataStart_ = 0, compressedEnd_ = 0, cached_ = ~0ull;
    uint32_t blockSize_ = 0;
    std::vector<uint64_t> pointers_;
    Bytes cache_;
};

class WbfsBlob final : public Blob {
public:
    explicit WbfsBlob(Joined& file) : file_(file) {
        uint8_t h[12];
        file_.ReadAt(0, h, sizeof(h));
        const uint64_t hdSector = 1ull << h[8];
        sector_ = 1ull << h[9];
        if (sector_ < kWiiCluster || sector_ > (1ull << 30)) Fail("This WBFS file is damaged.");
        constexpr uint64_t kMaxDisc = uint64_t(143432 * 2) * kWiiCluster;
        const uint64_t entries = (kMaxDisc + sector_ - 1) / sector_;
        Bytes table(size_t(entries) * 2);
        file_.ReadAt(hdSector + 0x100, table.data(), table.size());
        for (uint64_t i = 0; i < entries; ++i) table_.push_back(Be16(table.data() + 2 * i));
    }
    Format format() const override { return Format::Wbfs; }
    uint64_t DiscSize() const override { return uint64_t(143432 * 2) * kWiiCluster; }
    void Read(uint64_t offset, void* out, size_t size) override {
        auto* p = static_cast<uint8_t*>(out);
        while (size) {
            const uint64_t index = offset / sector_, within = offset % sector_;
            const size_t n = size_t(std::min<uint64_t>(size, sector_ - within));
            if (index >= table_.size()) Fail("Read past the end of the WBFS image.");
            if (table_[index] == 0) std::memset(p, 0, n);
            else file_.ReadAt(uint64_t(table_[index]) * sector_ + within, p, n);
            p += n; offset += n; size -= n;
        }
    }

private:
    Joined& file_;
    uint64_t sector_ = 0;
    std::vector<uint16_t> table_;
};

// ------------------------------------------------------------------ WIA / RVZ
// Lagged Fibonacci generator for the discs' pseudorandom padding (RVZ packing).
class Lfg {
public:
    void SetSeed(const uint8_t* seed) {
        position_ = 0;
        for (size_t i = 0; i < 17; ++i) buffer_[i] = Be32(seed + 4 * i);
        for (size_t i = 17; i < kK; ++i) buffer_[i] = (buffer_[i - 17] << 23) ^ (buffer_[i - 16] >> 9) ^ buffer_[i - 1];
        // Output is big-endian with the format's "shift by 18" quirk folded in.
        for (uint32_t& x : buffer_) {
            const uint32_t v = (x & 0xFF00FFFF) | ((x >> 2) & 0x00FF0000);
            x = v;
        }
        for (int i = 0; i < 4; ++i) Advance();
    }
    void Forward(size_t bytes) {
        position_ += bytes;
        while (position_ >= kK * 4) { Advance(); position_ -= kK * 4; }
    }
    void GetBytes(size_t count, uint8_t* out) {
        while (count) {
            const size_t word = position_ / 4, byte = position_ % 4;
            *out++ = uint8_t(buffer_[word] >> (24 - 8 * byte));
            --count;
            if (++position_ == kK * 4) { Advance(); position_ = 0; }
        }
    }

private:
    static constexpr size_t kK = 521, kJ = 32;
    void Advance() {
        for (size_t i = 0; i < kJ; ++i) buffer_[i] ^= buffer_[i + kK - kJ];
        for (size_t i = kJ; i < kK; ++i) buffer_[i] ^= buffer_[i - kJ];
    }
    std::array<uint32_t, kK> buffer_{};
    size_t position_ = 0;
};

enum class WiaCompression : uint32_t { None = 0, Purge = 1, Bzip2 = 2, Lzma = 3, Lzma2 = 4, Zstd = 5 };

class WiaBlob final : public Blob {
public:
    WiaBlob(Joined& file, bool rvz) : file_(file), rvz_(rvz) {
        uint8_t h1[0x48];
        file_.ReadAt(0, h1, sizeof(h1));
        discSize_ = Be64(h1 + 0x24);
        if (Be64(h1 + 0x2C) != file_.Size()) Fail("This " + std::string(rvz ? "RVZ" : "WIA") + " file is incomplete (its size doesn't match).");
        const uint32_t h2Size = Be32(h1 + 0x0C);
        if (h2Size < 0xDC - 7 || h2Size > 0x1000) Fail("This disc image is damaged.");
        Bytes h2(std::max<size_t>(h2Size, 0xDC), 0);
        file_.ReadAt(0x48, h2.data(), h2Size);
        compression_ = WiaCompression(Be32(h2.data() + 4));
        chunkSize_ = Be32(h2.data() + 0x0C);
        std::memcpy(discHeader_.data(), h2.data() + 0x10, 0x80);
        const uint32_t partCount = Be32(h2.data() + 0x90), partEntrySize = Be32(h2.data() + 0x94);
        const uint64_t partOffset = Be64(h2.data() + 0x98);
        const uint32_t rawCount = Be32(h2.data() + 0xB4);
        const uint64_t rawOffset = Be64(h2.data() + 0xB8);
        const uint32_t rawSize = Be32(h2.data() + 0xC0);
        const uint32_t groupCount = Be32(h2.data() + 0xC4);
        const uint64_t groupOffset = Be64(h2.data() + 0xC8);
        const uint32_t groupSize = Be32(h2.data() + 0xD0);
        compressorDataSize_ = h2[0xD4];
        std::memcpy(compressorData_, h2.data() + 0xD5, 7);
        if (uint32_t(compression_) > 5 || (rvz_ && compression_ == WiaCompression::Purge) || (!rvz_ && compression_ == WiaCompression::Zstd))
            Fail("This disc image uses an unsupported compression method.");
        if (chunkSize_ < 0x8000 || chunkSize_ > (512u << 20)) Fail("This disc image is damaged.");

        Bytes parts(size_t(partCount) * partEntrySize);
        if (!parts.empty()) file_.ReadAt(partOffset, parts.data(), parts.size());
        for (uint32_t i = 0; i < partCount; ++i) {
            Partition p{};
            Bytes entry(0x30, 0);
            std::memcpy(entry.data(), parts.data() + size_t(i) * partEntrySize, std::min<size_t>(partEntrySize, 0x30));
            for (int k = 0; k < 2; ++k) {
                const uint8_t* d = entry.data() + 0x10 + 16 * k;
                p.data[k] = {Be32(d), Be32(d + 4), Be32(d + 8), Be32(d + 12)};
            }
            partitions_.push_back(p);
        }
        const Bytes raw = Decompress(rawOffset, rawSize, size_t(rawCount) * 0x18, compression_, 0, 0, 0);
        for (uint32_t i = 0; i < rawCount; ++i) {
            const uint8_t* r = raw.data() + 0x18 * i;
            raws_.push_back({Be64(r), Be64(r + 8), Be32(r + 16), Be32(r + 20)});
        }
        const size_t groupEntry = rvz_ ? 12 : 8;
        const Bytes groups = Decompress(groupOffset, groupSize, size_t(groupCount) * groupEntry, compression_, 0, 0, 0);
        for (uint32_t i = 0; i < groupCount; ++i) {
            const uint8_t* g = groups.data() + groupEntry * i;
            groups_.push_back({Be32(g), Be32(g + 4), rvz_ ? Be32(g + 8) : 0});
        }
    }

    Format format() const override { return rvz_ ? Format::Rvz : Format::Wia; }
    uint64_t DiscSize() const override { return discSize_; }

    // Raw (non-partition) disc areas only; Wii partition data is read decrypted instead.
    void Read(uint64_t offset, void* out, size_t size) override {
        auto* p = static_cast<uint8_t*>(out);
        if (offset < 0x80) {
            const size_t n = size_t(std::min<uint64_t>(size, 0x80 - offset));
            std::memcpy(p, discHeader_.data() + offset, n);
            p += n; offset += n; size -= n;
        }
        while (size) {
            const Raw* entry = nullptr;
            for (const Raw& r : raws_)
                if (offset >= (r.offset & ~0x7FFFull) && offset < r.offset + r.size) { entry = &r; break; }
            if (!entry) {
                for (const Partition& part : partitions_)
                    for (const PartData& d : part.data)
                        if (d.sectors && offset >= uint64_t(d.firstSector) * kWiiCluster && offset < (uint64_t(d.firstSector) + d.sectors) * kWiiCluster)
                            Fail("Internal error: encrypted Wii data requested from a WIA/RVZ image.");
                Fail("This disc image doesn't cover the requested area.");
            }
            const uint64_t start = entry->offset & ~0x7FFFull, total = entry->offset + entry->size - start;
            const size_t n = size_t(std::min<uint64_t>(size, entry->offset + entry->size - offset));
            ReadGroups(offset - start, p, n, chunkSize_, start, total, entry->groupIndex, entry->groupCount, 0);
            p += n; offset += n; size -= n;
        }
    }

    bool ReadsWiiDecrypted() const override { return true; }
    void ReadWiiDecrypted(uint64_t partitionDataOffset, uint64_t offset, void* out, size_t size) override {
        const Partition* part = nullptr;
        for (const Partition& p : partitions_)
            if (p.data[0].sectors && uint64_t(p.data[0].firstSector) * kWiiCluster == partitionDataOffset) { part = &p; break; }
        if (!part) Fail("This disc image's game partition couldn't be found.");
        const uint64_t chunkData = uint64_t(chunkSize_) / kWiiCluster * kWiiClusterData;
        const uint32_t lists = std::max<uint32_t>(1, uint32_t(chunkSize_ / 0x200000));
        auto* p = static_cast<uint8_t*>(out);
        for (const PartData& d : part->data) {
            if (!size) break;
            const uint64_t dataStart = uint64_t(d.firstSector - part->data[0].firstSector) * kWiiClusterData;
            const uint64_t dataSize = uint64_t(d.sectors) * kWiiClusterData;
            if (!d.sectors || offset >= dataStart + dataSize) continue;
            if (offset < dataStart) Fail("This disc image is damaged.");
            const size_t n = size_t(std::min<uint64_t>(size, dataStart + dataSize - offset));
            ReadGroups(offset - dataStart, p, n, chunkData, dataStart, dataSize, d.groupIndex, d.groupCount, lists);
            p += n; offset += n; size -= n;
        }
        if (size) Fail("Read past the end of the game partition.");
    }

private:
    struct PartData { uint32_t firstSector, sectors, groupIndex, groupCount; };
    struct Partition { PartData data[2]; };
    struct Raw { uint64_t offset, size; uint32_t groupIndex, groupCount; };
    struct Group { uint32_t offset4, size, packedSize; };

    // offsetInData: position within this data run (whose first group starts at dataStart).
    void ReadGroups(uint64_t offsetInData, uint8_t* out, size_t size, uint64_t chunk, uint64_t dataStart,
                    uint64_t dataSize, uint32_t groupIndex, uint32_t groupCount, uint32_t exceptionLists) {
        while (size) {
            const uint64_t i = offsetInData / chunk;
            if (i >= groupCount || groupIndex + i >= groups_.size()) Fail("This disc image is damaged.");
            const uint64_t groupStart = i * chunk, within = offsetInData - groupStart;
            const uint64_t groupBytes = std::min(chunk, dataSize - groupStart);
            const size_t n = size_t(std::min<uint64_t>(size, groupBytes - within));
            // RVZ padding is seeded relative to the start of this data run (as Dolphin does).
            const Bytes& data = LoadGroup(groupIndex + uint32_t(i), groupBytes, exceptionLists, groupStart);
            std::memcpy(out, data.data() + within, n);
            out += n; offsetInData += n; size -= n;
        }
    }

    const Bytes& LoadGroup(uint32_t index, uint64_t bytes, uint32_t exceptionLists, uint64_t dataOffset) {
        if (cachedGroup_ == index && cached_.size() == bytes) return cached_;
        const Group& g = groups_[index];
        WiaCompression method = compression_;
        uint32_t stored = g.size;
        if (rvz_) {
            if (!(stored & 0x80000000u)) method = WiaCompression::None;
            stored &= 0x7FFFFFFFu;
        }
        if (stored == 0) {
            cached_.assign(size_t(bytes), 0);
        } else {
            cached_ = Decompress(uint64_t(g.offset4) << 2, stored, size_t(bytes), method, exceptionLists, g.packedSize, dataOffset);
        }
        cachedGroup_ = index;
        return cached_;
    }

    // Decompresses one stored chunk, skips its hash-exception lists and undoes RVZ packing.
    Bytes Decompress(uint64_t fileOffset, uint32_t storedSize, size_t outSize, WiaCompression method,
                     uint32_t exceptionLists, uint32_t packedSize, uint64_t dataOffset) {
        Bytes in(storedSize);
        if (storedSize) file_.ReadAt(fileOffset, in.data(), in.size());
        size_t pos = 0;
        const bool compressedLists = method != WiaCompression::None && method != WiaCompression::Purge;
        if (exceptionLists && !compressedLists) {
            for (uint32_t l = 0; l < exceptionLists; ++l) {
                if (pos + 2 > in.size()) Fail("This disc image is damaged.");
                pos += 2 + size_t(Be16(in.data() + pos)) * 22;
            }
            pos = (pos + 3) & ~size_t(3);
        }
        const size_t payload = packedSize ? packedSize : outSize;
        Bytes out;
        switch (method) {
            case WiaCompression::None:
                out.assign(in.begin() + std::min(pos, in.size()), in.end());
                break;
            case WiaCompression::Purge: {
                const size_t end = in.size() >= 20 ? in.size() - 20 : 0; // trailing SHA-1
                out.assign(payload, 0);
                while (pos + 8 <= end) {
                    const uint32_t off = Be32(in.data() + pos), len = Be32(in.data() + pos + 4);
                    pos += 8;
                    if (off + uint64_t(len) > out.size() || pos + len > end) Fail("This disc image is damaged.");
                    std::memcpy(out.data() + off, in.data() + pos, len);
                    pos += len;
                }
                break;
            }
            default:
                out = Inflate(method, in.data() + pos, in.size() - pos, payload + (compressedLists ? size_t(exceptionLists) * 0x1000 * 23 : 0));
                break;
        }
        size_t skip = 0;
        if (exceptionLists && compressedLists) {
            for (uint32_t l = 0; l < exceptionLists; ++l) {
                if (skip + 2 > out.size()) Fail("This disc image is damaged.");
                skip += 2 + size_t(Be16(out.data() + skip)) * 22;
            }
        }
        if (skip) out.erase(out.begin(), out.begin() + std::min(skip, out.size()));
        if (packedSize) out = Unpack(out, outSize, dataOffset);
        if (out.size() < outSize) Fail("This disc image is damaged (a block is shorter than expected).");
        out.resize(outSize);
        return out;
    }

    static Bytes Unpack(const Bytes& packed, size_t outSize, uint64_t dataOffset) {
        Bytes out;
        out.reserve(outSize);
        size_t pos = 0;
        Lfg lfg;
        while (pos + 4 <= packed.size() && out.size() < outSize) {
            const uint32_t word = Be32(packed.data() + pos);
            pos += 4;
            const uint32_t size = word & 0x7FFFFFFF;
            const size_t n = std::min<size_t>(size, outSize - out.size());
            if (word & 0x80000000u) {
                if (pos + 68 > packed.size()) Fail("This disc image is damaged.");
                lfg.SetSeed(packed.data() + pos);
                pos += 68;
                lfg.Forward(size_t(dataOffset % 0x8000));
                const size_t at = out.size();
                out.resize(at + n);
                lfg.GetBytes(n, out.data() + at);
            } else {
                if (pos + size > packed.size()) Fail("This disc image is damaged.");
                out.insert(out.end(), packed.begin() + pos, packed.begin() + pos + n);
                pos += size;
            }
            dataOffset += size;
        }
        return out;
    }

    Bytes Inflate(WiaCompression method, const uint8_t* in, size_t inSize, size_t maxOut) {
        Bytes out(maxOut);
        size_t produced = 0;
        if (method == WiaCompression::Zstd) {
            const size_t r = ZSTD_decompress(out.data(), out.size(), in, inSize);
            if (ZSTD_isError(r)) Fail("This disc image is damaged (Zstandard data).");
            produced = r;
        } else if (method == WiaCompression::Bzip2) {
            unsigned int n = unsigned(out.size());
            const int r = BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(out.data()), &n,
                                                     const_cast<char*>(reinterpret_cast<const char*>(in)), unsigned(inSize), 0, 0);
            if (r != BZ_OK) Fail("This disc image is damaged (bzip2 data).");
            produced = n;
        } else { // LZMA / LZMA2 raw streams with 7-Zip SDK properties
            lzma_options_lzma options{};
            lzma_filter filters[2];
            if (method == WiaCompression::Lzma) {
                if (compressorDataSize_ < 5) Fail("This disc image is damaged (LZMA settings).");
                uint32_t d = compressorData_[0];
                if (d >= 9 * 5 * 5) Fail("This disc image is damaged (LZMA settings).");
                options.lc = d % 9; d /= 9; options.pb = d / 5; options.lp = d % 5;
                options.dict_size = Le32(compressorData_ + 1);
                filters[0].id = LZMA_FILTER_LZMA1;
            } else {
                if (compressorDataSize_ < 1 || compressorData_[0] > 40) Fail("This disc image is damaged (LZMA2 settings).");
                const uint8_t prop = compressorData_[0];
                options.dict_size = prop == 40 ? 0xFFFFFFFFu : ((2u | (prop & 1u)) << (prop / 2 + 11));
                filters[0].id = LZMA_FILTER_LZMA2;
            }
            filters[0].options = &options;
            filters[1].id = LZMA_VLI_UNKNOWN;
            lzma_stream stream = LZMA_STREAM_INIT;
            if (lzma_raw_decoder(&stream, filters) != LZMA_OK) Fail("This disc image couldn't be decoded (LZMA).");
            stream.next_in = in; stream.avail_in = inSize;
            stream.next_out = out.data(); stream.avail_out = out.size();
            const lzma_ret r = lzma_code(&stream, LZMA_FINISH);
            produced = out.size() - stream.avail_out;
            lzma_end(&stream);
            if (r != LZMA_STREAM_END && r != LZMA_OK && r != LZMA_BUF_ERROR) Fail("This disc image is damaged (LZMA data).");
        }
        out.resize(produced);
        return out;
    }

    Joined& file_;
    bool rvz_;
    uint64_t discSize_ = 0;
    WiaCompression compression_ = WiaCompression::None;
    uint32_t chunkSize_ = 0;
    std::array<uint8_t, 0x80> discHeader_{};
    uint8_t compressorDataSize_ = 0;
    uint8_t compressorData_[7] = {};
    std::vector<Partition> partitions_;
    std::vector<Raw> raws_;
    std::vector<Group> groups_;
    uint32_t cachedGroup_ = ~0u;
    Bytes cached_;
};

// ------------------------------------------------------------------ opening
struct Opened {
    std::unique_ptr<Joined> file;
    std::unique_ptr<Blob> blob;
};

Opened Open(std::vector<std::unique_ptr<Part>>& parts) {
    if (parts.empty()) Fail("No file was given.");
    Opened o;
    o.file = std::make_unique<Joined>(parts);
    if (o.file->Size() < 0x440) Fail("This file is too small to be a disc image.");
    uint8_t m[4];
    o.file->ReadAt(0, m, 4);
    if (std::memcmp(m, "WIA\x01", 4) == 0) o.blob = std::make_unique<WiaBlob>(*o.file, false);
    else if (std::memcmp(m, "RVZ\x01", 4) == 0) o.blob = std::make_unique<WiaBlob>(*o.file, true);
    else if (std::memcmp(m, "WBFS", 4) == 0) o.blob = std::make_unique<WbfsBlob>(*o.file);
    else if (std::memcmp(m, "CISO", 4) == 0) o.blob = std::make_unique<CisoBlob>(*o.file);
    else if (Le32(m) == 0xB10BC001u) o.blob = std::make_unique<GczBlob>(*o.file);
    else o.blob = std::make_unique<PlainBlob>(*o.file);
    return o;
}

std::string RegionOf(char code) {
    switch (code) {
        case 'E': return "USA";
        case 'P': case 'D': case 'F': case 'S': case 'I': case 'X': case 'Y': case 'U': return "Europe (PAL)";
        case 'J': return "Japan";
        case 'K': return "Korea";
        case 'W': return "Taiwan";
        default: return "Unknown region";
    }
}

Info IdentifyOpened(Opened& o) {
    Info info;
    info.format = o.blob->format();
    uint8_t h[0x80];
    o.blob->Read(0, h, sizeof(h));
    if (info.format == Format::Iso || info.format == Format::Ciso || info.format == Format::Gcz) {
        uint8_t nkit[4];
        if (o.blob->DiscSize() > 0x204) {
            o.blob->Read(0x200, nkit, 4);
            if (std::memcmp(nkit, "NKIT", 4) == 0) info.format = Format::Nkit;
        }
    }
    if (Be32(h + 0x18) == kWiiMagic) info.platform = Platform::Wii;
    else if (Be32(h + 0x1C) == kGcMagic) info.platform = Platform::GameCube;
    else Fail("This file isn't a GameCube or Wii disc image.");
    info.gameId.assign(reinterpret_cast<const char*>(h), 6);
    info.discNumber = h[6];
    info.revision = h[7];
    const char* title = reinterpret_cast<const char*>(h + 0x20);
    info.title.assign(title, strnlen(title, 0x60));
    info.region = RegionOf(info.gameId[3]);
    info.discSize = o.blob->DiscSize();
    return info;
}

// ------------------------------------------------------------------ Wii partition access
class WiiPartition {
public:
    explicit WiiPartition(Blob& blob) : blob_(blob) {
        uint8_t table[32];
        blob_.Read(0x40000, table, sizeof(table));
        for (int group = 0; group < 4 && !partition_; ++group) {
            const uint32_t count = Be32(table + 8 * group);
            const uint64_t at = uint64_t(Be32(table + 8 * group + 4)) << 2;
            if (!count || count > 64) continue;
            Bytes entries(size_t(count) * 8);
            blob_.Read(at, entries.data(), entries.size());
            for (uint32_t i = 0; i < count; ++i)
                if (Be32(entries.data() + 8 * i + 4) == 0) { partition_ = uint64_t(Be32(entries.data() + 8 * i)) << 2; break; }
        }
        if (!partition_) Fail("This disc has no game partition.");
        uint8_t header[0x2C0];
        blob_.Read(partition_, header, sizeof(header));
        dataOffset_ = partition_ + (uint64_t(Be32(header + 0x2B8)) << 2);
        dataSize_ = uint64_t(Be32(header + 0x2BC)) << 2;
        if (!blob_.ReadsWiiDecrypted()) {
            if (header[0x1F1] != 0) Fail("This disc uses a Korean or vWii key, which isn't supported.");
            uint8_t iv[16] = {}, key[16];
            std::memcpy(iv, header + 0x1DC, 8);
            Aes128Decryptor(kCommonKey).CbcDecrypt(iv, header + 0x1BF, key, 16);
            aes_ = std::make_unique<Aes128Decryptor>(key);
        }
    }
    uint64_t DataSize() const { return dataSize_ / kWiiCluster * kWiiClusterData; }
    void Read(uint64_t offset, void* out, size_t size) {
        if (blob_.ReadsWiiDecrypted()) { blob_.ReadWiiDecrypted(dataOffset_, offset, out, size); return; }
        auto* p = static_cast<uint8_t*>(out);
        while (size) {
            const uint64_t cluster = offset / kWiiClusterData, within = offset % kWiiClusterData;
            const size_t n = size_t(std::min<uint64_t>(size, kWiiClusterData - within));
            const uint8_t* plain = Load(cluster);
            std::memcpy(p, plain + within, n);
            p += n; offset += n; size -= n;
        }
    }

private:
    static constexpr uint64_t kBatch = 64; // one 2 MiB hash group per read

    // Returns the decrypted data of one cluster, reading and decrypting 64 clusters at a time.
    const uint8_t* Load(uint64_t cluster) {
        const uint64_t first = cluster / kBatch * kBatch;
        if (first != cachedFirst_) {
            const uint64_t clusters = std::min<uint64_t>(kBatch, dataSize_ / kWiiCluster - first);
            if (cluster >= first + clusters) Fail("Read past the end of the game partition.");
            raw_.resize(size_t(clusters * kWiiCluster));
            plain_.resize(size_t(clusters * kWiiClusterData));
            blob_.Read(dataOffset_ + first * kWiiCluster, raw_.data(), raw_.size());
            for (uint64_t i = 0; i < clusters; ++i) {
                const uint8_t* c = raw_.data() + i * kWiiCluster;
                aes_->CbcDecrypt(c + 0x3D0, c + kWiiClusterHashes, plain_.data() + i * kWiiClusterData, kWiiClusterData);
            }
            cachedFirst_ = first;
        }
        return plain_.data() + (cluster - first) * kWiiClusterData;
    }
    Blob& blob_;
    uint64_t partition_ = 0, dataOffset_ = 0, dataSize_ = 0, cachedFirst_ = ~0ull;
    std::unique_ptr<Aes128Decryptor> aes_;
    Bytes raw_, plain_;
};

// ------------------------------------------------------------------ extraction
using ReadFn = std::function<void(uint64_t, void*, size_t)>;

void CheckCancel(Progress& progress) {
    if (progress.cancel.load()) Fail("Cancelled.");
}

void WriteRange(const ReadFn& read, uint64_t offset, uint64_t size, const fs::path& dest, Progress& progress) {
    fs::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) Fail("Couldn't write " + dest.filename().u8string() + " (is storage full?).");
    Bytes buffer(1 << 20);
    while (size) {
        CheckCancel(progress);
        const size_t n = size_t(std::min<uint64_t>(size, buffer.size()));
        read(offset, buffer.data(), n);
        if (!out.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(n))) Fail("Couldn't write " + dest.filename().u8string() + " (is storage full?).");
        offset += n; size -= n;
        progress.done.fetch_add(n);
    }
}

uint32_t DolSize(const ReadFn& read, uint64_t at) {
    uint8_t h[0x100];
    read(at, h, sizeof(h));
    uint32_t end = 0x100;
    for (int i = 0; i < 18; ++i) {
        const uint32_t off = Be32(h + 4 * i), size = Be32(h + 0x90 + 4 * i);
        if (off) end = std::max(end, off + size);
    }
    return end;
}

void ExtractTree(const ReadFn& read, bool wii, const fs::path& dest, Progress& progress) {
    const int shift = wii ? 2 : 0;
    uint8_t boot[0x440];
    read(0, boot, sizeof(boot));
    const uint64_t dol = uint64_t(Be32(boot + 0x420)) << shift;
    const uint64_t fstAt = uint64_t(Be32(boot + 0x424)) << shift;
    const uint64_t fstSize = uint64_t(Be32(boot + 0x428)) << shift;
    if (!fstAt || fstSize < 12 || fstSize > (64u << 20)) Fail("The disc's file table is damaged.");
    Bytes fst(static_cast<size_t>(fstSize));
    read(fstAt, fst.data(), fst.size());
    const uint32_t count = Be32(fst.data() + 8);
    if (uint64_t(count) * 12 > fst.size()) Fail("The disc's file table is damaged.");
    uint64_t total = 0;
    for (uint32_t i = 1; i < count; ++i)
        if (!fst[12 * i]) total += Be32(fst.data() + 12 * i + 8);
    uint8_t loader[0x20];
    read(0x2440, loader, sizeof(loader));
    const uint64_t loaderSize = 0x20ull + Be32(loader + 0x14) + Be32(loader + 0x18);
    progress.total.store(total + 0x440 + 0x2000 + loaderSize + fstSize + DolSize(read, dol));
    const char* names = reinterpret_cast<const char*>(fst.data()) + uint64_t(count) * 12;
    const char* namesEnd = reinterpret_cast<const char*>(fst.data() + fst.size());
    auto name = [&](uint32_t i) {
        const char* n = names + (Be32(fst.data() + 12 * i) & 0xFFFFFF);
        if (n >= namesEnd) Fail("The disc's file table is damaged.");
        const std::string s(n, strnlen(n, size_t(namesEnd - n)));
        if (s.empty() || s == "." || s == ".." || s.find('/') != std::string::npos || s.find('\\') != std::string::npos)
            Fail("The disc's file table has an unsafe name.");
        return s;
    };
    const fs::path files = dest / "files", sys = dest / "sys";
    std::function<void(uint32_t, uint32_t, const fs::path&)> walk = [&](uint32_t i, uint32_t end, const fs::path& dir) {
        while (i < end) {
            const uint8_t* e = fst.data() + 12 * i;
            if (e[0]) {
                const uint32_t next = Be32(e + 8);
                if (next <= i || next > end) Fail("The disc's file table is damaged.");
                fs::create_directories(dir / fs::u8path(name(i)));
                walk(i + 1, next, dir / fs::u8path(name(i)));
                i = next;
            } else {
                WriteRange(read, uint64_t(Be32(e + 4)) << shift, Be32(e + 8), dir / fs::u8path(name(i)), progress);
                ++i;
            }
        }
    };
    fs::create_directories(files);
    walk(1, count, files);
    // The system files last; fst.bin and main.dol are renamed into place as the "complete" marker.
    WriteRange(read, 0, 0x440, sys / "boot.bin", progress);
    WriteRange(read, 0x440, 0x2000, sys / "bi2.bin", progress);
    WriteRange(read, 0x2440, loaderSize, sys / "apploader.img", progress);
    WriteRange(read, fstAt, fstSize, sys / "fst.bin.part", progress);
    WriteRange(read, dol, DolSize(read, dol), sys / "main.dol.part", progress);
    fs::rename(sys / "fst.bin.part", sys / "fst.bin");
    fs::rename(sys / "main.dol.part", sys / "main.dol");
}

// Trimmed images may omit unused padding, but never referenced game data.
void ValidateGameCubeData(Blob& blob, uint64_t available) {
    auto range = [&](uint64_t offset, uint64_t size) {
        if (offset > available || size > available - offset)
            Fail("This GameCube image is incomplete: required game data is missing. Choose a complete disc image and import it again.");
    };
    range(0, 0x2460);
    uint8_t boot[0x440], loader[0x20], dolHeader[0x100];
    blob.Read(0, boot, sizeof(boot));
    blob.Read(0x2440, loader, sizeof(loader));
    range(0x2440, 0x20ull + Be32(loader + 0x14) + Be32(loader + 0x18));
    const uint64_t dol = Be32(boot + 0x420), fstAt = Be32(boot + 0x424);
    const uint64_t fstSize = Be32(boot + 0x428);
    if (!dol || !fstAt || fstSize < 12 || fstSize > (64u << 20))
        Fail("This GameCube image has a damaged executable or file table.");
    range(dol, sizeof(dolHeader));
    blob.Read(dol, dolHeader, sizeof(dolHeader));
    bool hasSection = false;
    for (int i = 0; i < 18; ++i) {
        const uint32_t offset = Be32(dolHeader + 4 * i), size = Be32(dolHeader + 0x90 + 4 * i);
        if (!size) continue;
        if (offset < sizeof(dolHeader)) Fail("This GameCube image has a damaged executable.");
        range(dol + offset, size);
        hasSection = true;
    }
    if (!hasSection) Fail("This GameCube image has no executable data.");
    range(fstAt, fstSize);
    Bytes fst(static_cast<size_t>(fstSize));
    blob.Read(fstAt, fst.data(), fst.size());
    const uint32_t count = Be32(fst.data() + 8);
    if (fst[0] != 1 || !count || uint64_t(count) * 12 > fst.size())
        Fail("This GameCube image has a damaged file table.");
    for (uint32_t i = 1; i < count; ++i) {
        const uint8_t* entry = fst.data() + 12 * i;
        if (entry[0]) {
            const uint32_t end = Be32(entry + 8);
            if (end <= i || end > count) Fail("This GameCube image has a damaged file table.");
        } else {
            const uint32_t size = Be32(entry + 8);
            if (size) range(Be32(entry + 4), size);
        }
    }
}

} // namespace

const char* FormatName(Format format) {
    switch (format) {
        case Format::Iso: return "ISO";
        case Format::Ciso: return "CISO";
        case Format::Gcz: return "GCZ";
        case Format::Wbfs: return "WBFS";
        case Format::Wia: return "WIA";
        case Format::Rvz: return "RVZ";
        case Format::Nkit: return "NKit";
        default: return "Unknown";
    }
}

std::unique_ptr<Part> OpenPath(const std::string& path) { return std::make_unique<PathPart>(path); }
#if !defined(_WIN32)
std::unique_ptr<Part> OpenFd(int fd) { return std::make_unique<FdPart>(fd); }
#endif

Info Identify(std::vector<std::unique_ptr<Part>>& parts) {
    Opened o = Open(parts);
    return IdentifyOpened(o);
}

void ExtractWii(std::vector<std::unique_ptr<Part>>& parts, const std::string& destDir, Progress& progress) {
    Opened o = Open(parts);
    const Info info = IdentifyOpened(o);
    if (info.format == Format::Nkit) Fail("NKit images aren't complete copies of the disc. Convert it to RVZ or ISO in Dolphin first.");
    if (info.platform != Platform::Wii) Fail("This is a GameCube disc, not a Wii disc.");
    WiiPartition partition(*o.blob);
    ExtractTree([&](uint64_t off, void* out, size_t n) { partition.Read(off, out, n); }, true, fs::u8path(destDir), progress);
}

void ExtractGameCubeIso(std::vector<std::unique_ptr<Part>>& parts, const std::string& destIso, Progress& progress) {
    Opened o = Open(parts);
    const Info info = IdentifyOpened(o);
    if (info.format == Format::Nkit) Fail("NKit images aren't complete copies of the disc. Convert it to RVZ or ISO in Dolphin first.");
    if (info.platform != Platform::GameCube) Fail("This is a Wii disc, not a GameCube disc.");
    // Always a full-size disc, as the game checks: a CISO's size is whole blocks (over or under
    // the real size) and a trimmed ISO stops early; the missing tail is padding, written as zeros.
    constexpr uint64_t kGameCubeDiscSize = 1459978240;
    const uint64_t have = std::min<uint64_t>(info.discSize, kGameCubeDiscSize);
    // CISO explicitly represents unallocated blocks as zeroes, including its tail.
    ValidateGameCubeData(*o.blob, info.format == Format::Ciso ? kGameCubeDiscSize : have);
    const fs::path dest = fs::u8path(destIso), part = fs::u8path(destIso + ".part");
    progress.total.store(kGameCubeDiscSize);
    WriteRange([&](uint64_t off, void* out, size_t n) {
        auto* p = static_cast<uint8_t*>(out);
        const size_t real = off < have ? size_t(std::min<uint64_t>(n, have - off)) : 0;
        if (real) o.blob->Read(off, p, real);
        std::memset(p + real, 0, n - real);
    }, 0, kGameCubeDiscSize, part, progress);
    fs::rename(part, dest);
}

} // namespace wiids::disc
