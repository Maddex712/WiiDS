#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstddef>
#ifdef __ANDROID__
#include <android/log.h>
#endif

// stb's IDAT and zlib allocations can exceed the advertised image dimensions.
// Bound all of them before allocation, including realloc, rather than only the final image.
namespace wiids_texture_decode {
static constexpr size_t limit = 64 * 1024 * 1024;
static thread_local size_t allocated = 0;
struct alignas(std::max_align_t) Header { size_t bytes; };
static void* allocate(size_t bytes) {
    if (bytes > limit - allocated || bytes > SIZE_MAX - sizeof(Header)) return nullptr;
    auto* h = static_cast<Header*>(malloc(sizeof(Header) + bytes));
    if (!h) return nullptr;
    h->bytes = bytes; allocated += bytes; return h + 1;
}
static void release(void* p) {
    if (!p) return;
    auto* h = static_cast<Header*>(p) - 1;
    allocated -= h->bytes; free(h);
}
static void* resize(void* p, size_t bytes) {
    if (!p) return allocate(bytes);
    auto* h = static_cast<Header*>(p) - 1;
    const size_t previous = h->bytes;
    if (bytes > limit - (allocated - previous) || bytes > SIZE_MAX - sizeof(Header)) return nullptr;
    auto* next = static_cast<Header*>(realloc(h, sizeof(Header) + bytes));
    if (!next) return nullptr;
    next->bytes = bytes; allocated = allocated - previous + bytes; return next + 1;
}
}
#define STBI_MALLOC(n) wiids_texture_decode::allocate(n)
#define STBI_REALLOC(p,n) wiids_texture_decode::resize(p,n)
#define STBI_FREE(p) wiids_texture_decode::release(p)
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

namespace wiids::textures {

// From the xxHash specification (Yann Collet): 64-bit lanes over 32-byte
// stripes, then the tail and the avalanche.
static const uint64_t P1 = 0x9E3779B185EBCA87ull, P2 = 0xC2B2AE3D27D4EB4Full, P3 = 0x165667B19E3779F9ull,
                      P4 = 0x85EBCA77C2B2AE63ull, P5 = 0x27D4EB2F165667C5ull;
static inline uint64_t rotl(uint64_t x, int r) { return x << r | x >> (64 - r); }
static inline uint64_t rd64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}
static inline uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
static inline uint64_t xround(uint64_t acc, uint64_t in) { return rotl(acc + in * P2, 31) * P1; }
static inline uint64_t xmerge(uint64_t acc, uint64_t v) { return (acc ^ xround(0, v)) * P1 + P4; }

static uint64_t xxh64(const void* data, size_t len, uint64_t seed) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    const uint8_t* end = p + len;
    uint64_t h;
    if (len >= 32) {
        uint64_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
        do {
            v1 = xround(v1, rd64(p));
            v2 = xround(v2, rd64(p + 8));
            v3 = xround(v3, rd64(p + 16));
            v4 = xround(v4, rd64(p + 24));
            p += 32;
        } while (end - p >= 32);
        h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
        h = xmerge(h, v1);
        h = xmerge(h, v2);
        h = xmerge(h, v3);
        h = xmerge(h, v4);
    } else {
        h = seed + P5;
    }
    h += uint64_t(len);
    while (end - p >= 8) {
        h ^= xround(0, rd64(p));
        h = rotl(h, 27) * P1 + P4;
        p += 8;
    }
    if (end - p >= 4) {
        h ^= uint64_t(rd32(p)) * P1;
        h = rotl(h, 23) * P2 + P3;
        p += 4;
    }
    while (p < end) {
        h ^= uint64_t(*p++) * P5;
        h = rotl(h, 11) * P1;
    }
    h ^= h >> 33;
    h *= P2;
    h ^= h >> 29;
    h *= P3;
    h ^= h >> 32;
    return h;
}


static constexpr uint32_t maxSide = 4096;
static constexpr size_t maxPixels = 4 * 1024 * 1024;
static constexpr size_t budgetBytes = 128 * 1024 * 1024;

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<std::vector<uint32_t>> levels;
};

static bool validSize(uint32_t w, uint32_t h) {
    return w && h && w <= maxSide && h <= maxSide && size_t(w) * h <= maxPixels;
}

using File = std::unique_ptr<FILE, decltype(&fclose)>;

static bool readDds(FILE* file, Image& image) {
    std::array<uint8_t, 148> header{};
    if (fread(header.data(), 1, 128, file) != 128 ||
        memcmp(header.data(), "DDS ", 4) || rd32(header.data() + 4) != 124 ||
        rd32(header.data() + 76) != 32) return false;
    const auto w = rd32(header.data() + 16);
    const auto h = rd32(header.data() + 12);
    if (!validSize(w, h) || rd32(header.data() + 112) != 0) return false;
    const auto* four = header.data() + 84;
    using Decode = void (*)(const void*, void*, int);
    Decode decode = nullptr;
    unsigned blockSize = 16;
    bool rgba = false, bgra = false;
    bool opaque = false;
    if (!memcmp(four, "DXT1", 4)) { decode = bcdec_bc1; blockSize = 8; }
    else if (!memcmp(four, "DXT3", 4)) decode = bcdec_bc2;
    else if (!memcmp(four, "DXT5", 4)) decode = bcdec_bc3;
    else if (!memcmp(four, "DX10", 4)) {
        if (fread(header.data() + 128, 1, 20, file) != 20 ||
            rd32(header.data() + 132) != 3 || rd32(header.data() + 140) != 1 ||
            (rd32(header.data() + 136) & 4)) return false;
        const auto format = rd32(header.data() + 128);
        if (format >= 70 && format <= 72) { decode = bcdec_bc1; blockSize = 8; }
        else if (format >= 73 && format <= 75) decode = bcdec_bc2;
        else if (format >= 76 && format <= 78) decode = bcdec_bc3;
        else if (format >= 97 && format <= 99) decode = bcdec_bc7;
        else if (format == 28 || format == 29) rgba = true;
        else if (format == 87 || format == 91) bgra = true;
    } else if (rd32(four) == 0 && rd32(header.data() + 88) == 32 &&
               rd32(header.data() + 96) == 0x0000ff00) {
        const auto red = rd32(header.data() + 92);
        const auto blue = rd32(header.data() + 100);
        const auto alpha = rd32(header.data() + 104);
        if (alpha && alpha != 0xff000000) return false;
        opaque = !alpha;
        rgba = red == 0xff && blue == 0x00ff0000;
        bgra = red == 0x00ff0000 && blue == 0xff;
    }
    if (!decode && !rgba && !bgra) return false;

    const uint32_t declared = std::max(1u, rd32(header.data() + 28));
    uint32_t maximum = 1;
    for (auto x = w, y = h; x > 1 || y > 1; x = std::max(1u, x / 2), y = std::max(1u, y / 2)) ++maximum;
    if (declared > maximum) return false;
    image = {w, h, {}};
    for (uint32_t level = 0, levelW = w, levelH = h; level < declared;
         ++level, levelW = std::max(1u, levelW / 2), levelH = std::max(1u, levelH / 2)) {
    std::vector<uint32_t> pixels(size_t(levelW) * levelH);
    if (decode) {
        alignas(8) std::array<uint8_t, 16> block{};
        std::array<uint32_t, 16> texels{};
        for (uint32_t y = 0; y < levelH; y += 4) {
            for (uint32_t x = 0; x < levelW; x += 4) {
                if (fread(block.data(), 1, blockSize, file) != blockSize) return false;
                decode(block.data(), texels.data(), 16);
                for (uint32_t row = 0; row < std::min(4u, levelH - y); ++row)
                    std::copy_n(texels.data() + row * 4, std::min(4u, levelW - x),
                                pixels.data() + (y + row) * levelW + x);
            }
        }
    } else {
        if (fread(pixels.data(), 4, pixels.size(), file) != pixels.size()) return false;
        for (auto& pixel : pixels) {
            if (bgra) pixel = (pixel & 0xff00ff00) | (pixel & 0xff) << 16 | (pixel >> 16 & 0xff);
            if (opaque) pixel |= 0xff000000;
        }
    }
    image.levels.push_back(std::move(pixels));
    }
    return true;
}

static bool readImage(const std::string& path, Image& image) {
    File file(fopen(path.c_str(), "rb"), fclose);
    if (!file) return false;
    if (path.compare(path.size() - 4, 4, ".dds") == 0) return readDds(file.get(), image);
    int w, h, channels;
    if (!stbi_info_from_file(file.get(), &w, &h, &channels) || !validSize(w, h)) return false;
    rewind(file.get());
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_file(file.get(), &w, &h, &channels, 4), stbi_image_free);
    if (!pixels || !validSize(w, h)) return false;
    image = {uint32_t(w), uint32_t(h), {}};
    image.levels.emplace_back(size_t(w) * h);
    memcpy(image.levels.front().data(), pixels.get(), size_t(w) * h * 4);
    return true;
}

static void addMips(Image& image, uint32_t levels) {
    auto w = image.width, h = image.height;
    while (image.levels.size() < levels && (w > 1 || h > 1)) {
        const auto& source = image.levels.back();
        const auto nextW = std::max(1u, w / 2), nextH = std::max(1u, h / 2);
        std::vector<uint32_t> next(size_t(nextW) * nextH);
        for (uint32_t y = 0; y < nextH; ++y) {
            for (uint32_t x = 0; x < nextW; ++x) {
                const auto left = x * 2, right = std::min(left + 1, w - 1);
                const auto top = y * 2 * w, bottom = std::min(y * 2 + 1, h - 1) * w;
                const auto a = source[top + left], b = source[top + right];
                const auto c = source[bottom + left], d = source[bottom + right];
                uint32_t pixel = 0;
                for (unsigned shift = 0; shift != 32; shift += 8) {
                    const auto total = ((a >> shift) & 255) + ((b >> shift) & 255) +
                                       ((c >> shift) & 255) + ((d >> shift) & 255);
                    pixel |= ((total + 2) / 4) << shift;
                }
                next[y * nextW + x] = pixel;
            }
        }
        image.levels.push_back(std::move(next));
        w = nextW;
        h = nextH;
    }
}

static std::string paletteSuffix(const uint8_t* data, size_t bytes, uint32_t format,
                                 const uint8_t* palette, size_t paletteBytes) {
    if (!palette || paletteBytes < 2) return {};
    uint32_t low = 0xffff, high = 0;
    auto record = [&](uint32_t index) {
        low = std::min(low, index);
        high = std::max(high, index);
    };
    if (format == 8) {
        for (size_t i = 0; i < bytes; ++i) { record(data[i] & 15); record(data[i] >> 4); }
    } else if (format == 9) {
        for (size_t i = 0; i < bytes; ++i) record(data[i]);
    } else if (format == 10) {
        for (size_t i = 0; i + 1 < bytes; i += 2)
            record((uint32_t(data[i]) << 8 | data[i + 1]) & 0x3fff);
    }
    if (low > high || size_t(high) * 2 + 2 > paletteBytes) return {};
    char suffix[18];
    snprintf(suffix, sizeof suffix, "_%016llx",
             static_cast<unsigned long long>(xxh64(palette + low * 2, (high - low + 1) * 2, 0)));
    return suffix;
}

// Stock rendering depends only on sampled entries. A pack's name also depends on gaps
// inside the min/max span. Keep that identity separate from the stock render hash.
static uint64_t paletteIdentity(uint64_t stock, const uint8_t* data, size_t bytes, uint32_t format,
                                const uint8_t* palette, size_t paletteBytes) {
    const auto suffix = paletteSuffix(data, bytes, format, palette, paletteBytes);
    return suffix.empty() ? stock : xxh64(suffix.data(), suffix.size(), stock);
}

class Pack {
public:
    Pack() {
        const char* configured = getenv("WIIDS_NATIVE_TEXTURE_PATHS");
        if (!configured) return;
        std::string paths(configured);
        for (size_t first = 0; first < paths.size();) {
            const auto end = paths.find(';', first);
            const auto folder = paths.substr(first, end == std::string::npos ? end : end - first);
            if (!folder.empty() && folder.front() == '/') index(folder);
            if (end == std::string::npos) break;
            first = end + 1;
        }
#ifdef __ANDROID__
        if (!files.empty()) __android_log_print(ANDROID_LOG_INFO, "WiiDSTextures",
                                                "Indexed %zu managed texture names", files.size());
#endif
    }

    bool replace(const uint8_t* data, size_t bytes, uint32_t format, uint32_t width, uint32_t height,
                 uint32_t levels, const uint8_t* palette, size_t paletteBytes,
                 const std::shared_ptr<void>& owner, Image& image) {
        if (files.empty() || !data || !bytes || !width || !height) return false;
        const auto hash = xxh64(data, bytes, 0);
        const auto suffix = paletteSuffix(data, bytes, format, palette, paletteBytes);
        char key[96];
        const auto formatSuffix = "_" + std::to_string(format);
        for (bool mipmapped : {levels > 1, levels <= 1}) {
            snprintf(key, sizeof key, "tex1_%ux%u%s_%016llx", width, height, mipmapped ? "_m" : "",
                     static_cast<unsigned long long>(hash));
            const std::string base(key);
            if (!suffix.empty() && load(base + suffix + formatSuffix, width, height, levels, owner, image))
                return true;
            if (palette && load(base + "_$" + formatSuffix, width, height, levels, owner, image))
                return true;
            if (load(base + formatSuffix, width, height, levels, owner, image)) return true;
        }
        return false;
    }

private:
    void index(const std::string& folder) {
        DIR* raw = opendir(folder.c_str());
        if (!raw) return;
        std::unique_ptr<DIR, decltype(&closedir)> dir(raw, closedir);
        std::vector<std::string> names;
        while (auto* entry = readdir(dir.get())) {
            std::string name(entry->d_name);
            if (name.size() < 10 || name.compare(0, 5, "tex1_") != 0 ||
                name.find("_arb") != std::string::npos) continue;
            const auto ext = name.substr(name.size() - 4);
            if (ext == ".png" || ext == ".dds") names.push_back(std::move(name));
        }
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            std::string key = name.substr(0, name.size() - 4);
            files.emplace(std::move(key), folder + "/" + name);
        }
    }

    bool load(const std::string& name, uint32_t w, uint32_t h, uint32_t levels,
              const std::shared_ptr<void>& owner, Image& image) {
        const auto found = files.find(name);
        if (found == files.end()) return false;
        size_t used = 0;
        live.erase(std::remove_if(live.begin(), live.end(), [&](const Allocation& allocation) {
            if (allocation.owner.expired()) return true;
            used += allocation.bytes;
            return false;
        }), live.end());
        if (used >= budgetBytes || !readImage(found->second, image)) return false;
        if (image.width < w || image.height < h ||
            uint64_t(image.width) * h != uint64_t(image.height) * w) return false;
        // Honor authored chains, including PNGs stored as separate Dolphin _mipN files.
        bool explicitMips = false;
        for (uint32_t level = 1; level <= 12; ++level)
            explicitMips |= files.find(name + "_mip" + std::to_string(level)) != files.end();
        if (explicitMips) {
            if (image.levels.size() != 1) return false;
            for (uint32_t level = 1, mw = std::max(1u, image.width / 2), mh = std::max(1u, image.height / 2);
                 level < levels; ++level, mw = std::max(1u, mw / 2), mh = std::max(1u, mh / 2)) {
                const auto mip = files.find(name + "_mip" + std::to_string(level));
                Image decoded;
                if (mip == files.end() || !readImage(mip->second, decoded) || decoded.width != mw ||
                    decoded.height != mh || decoded.levels.size() != 1) return false;
                image.levels.push_back(std::move(decoded.levels.front()));
            }
        } else if (image.levels.size() == 1) addMips(image, levels);
        else if (image.levels.size() < levels) return false;
        if (image.levels.size() > levels) image.levels.resize(levels);
        size_t bytes = 0;
        for (const auto& mip : image.levels) bytes += mip.size() * sizeof(uint32_t);
        if (bytes > budgetBytes - used) return false;
        live.push_back({owner, bytes});
#ifdef __ANDROID__
        if (reported++ < 24) __android_log_print(ANDROID_LOG_INFO, "WiiDSTextures",
                                                "Loaded %s (%ux%u)", name.c_str(),
                                                image.width, image.height);
#endif
        return true;
    }

    struct Allocation { std::weak_ptr<void> owner; size_t bytes; };
    std::unordered_map<std::string, std::string> files;
    std::vector<Allocation> live;
#ifdef __ANDROID__
    unsigned reported = 0;
#endif
};

}  // namespace wiids::textures
