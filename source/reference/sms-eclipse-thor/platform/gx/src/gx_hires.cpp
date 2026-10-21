// Custom ("HD") textures in Dolphin's texture pack format.
//
// A pack is a folder of images named after the GX texture they replace:
//   tex1_<w>x<h>[_m]_<data hash>[_<palette hash>]_<format>.png
// <w>x<h> and <format> are the GX texture's size and format number, _m marks a
// texture sampled with mipmaps, and the hashes are XXH64 (seed 0) of the base
// level's bytes as they sit in memory and, for colour-indexed formats, of the
// palette entries the texture uses ("$" in place of the palette hash matches
// any palette). A file ending in _mip<n> is level n of the texture named by
// the rest; one ending in _arb has hand-made ("arbitrary") mipmaps, so its
// levels are not regenerated. Every pack made for Dolphin's "Load Custom
// Textures" works unchanged (the folder is usually named after the game ID,
// GMS or GMSE01), since it depends only on the texture data the game loads.
//
// Packs are found under the directories in SMS_TEXTURE_PACKS (separated by ':'
// or ';'), else under mods/textures/ in the working directory (or two levels
// up, when started from build/<os>-<arch>/). SMS_TEXTURE_PACKS=0 disables
// them. Resource textures prepare on a worker, then upload before gameplay.
// Late textures show their original until a bounded frame-end upload finishes.
#include "gx_internal.h"
#include "gl_funcs.h"
#include "gx_glcache.h"
#include "gx_glthread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#endif

#define STBI_ONLY_PNG
#define STBI_NO_STDIO_OVERRIDE
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "third_party/stb_image.h"
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "third_party/bcdec.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"
#include <unordered_set>

namespace gx {

// ------------------------------------------------------------------ XXH64
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

uint64_t xxh64(const void* data, size_t len, uint64_t seed) {
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

// ------------------------------------------------------------------ the index
struct PackFile {
    std::string path;               // level 0
    std::vector<std::string> mips;  // explicit levels 1.. (_mip<n>), may have gaps
    bool arbitrary = false;
};
static std::unordered_map<std::string, PackFile>& s_index =
    *new std::unordered_map<std::string, PackFile>;  // key: the texture name (read by the worker; never destroyed)
static std::atomic<int> s_state{-1};                       // published after the index and GL formats are ready
static uint32_t s_uploaded = 0;                            // replacements in GL

static bool endsWith(const std::string& s, const char* suf) {
    size_t n = strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

static void indexFile(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = char(tolower(c));
    if (ext != ".png" && ext != ".dds") return;
    std::string stem = p.stem().string();
    if (stem.compare(0, 5, "tex1_") != 0) return;
    bool arb = false;
    if (endsWith(stem, "_arb")) {
        arb = true;
        stem.resize(stem.size() - 4);
    }
    int level = 0;
    size_t m = stem.rfind("_mip");
    if (m != std::string::npos && m + 4 < stem.size() &&
        stem.find_first_not_of("0123456789", m + 4) == std::string::npos) {
        level = atoi(stem.c_str() + m + 4);
        stem.resize(m);
    }
    PackFile& f = s_index[stem];
    if (level == 0) {
        if (f.path.empty()) f.path = p.string();
        f.arbitrary = f.arbitrary || arb;
    } else {
        if (f.mips.size() < size_t(level)) f.mips.resize(size_t(level));
        if (f.mips[size_t(level) - 1].empty()) f.mips[size_t(level) - 1] = p.string();
    }
}

static void queryFormats();
static void startWorker();
static void requestPack();
static int preloadMode();

static void scan() {
    s_state = 0;
    std::vector<std::string> dirs;
    const char* e = getenv("SMS_TEXTURE_PACKS");
    if (e && strcmp(e, "0") == 0) return;
    if (e && *e) {
        std::string all = e, cur;
        for (char c : all + ";") {
            if (c == ';' || (c == ':' && !(cur.size() == 1 && isalpha(uint8_t(cur[0]))))) {
                if (!cur.empty()) dirs.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
    } else {
        dirs = {"mods/textures", "../../mods/textures"};
    }
    namespace fs = std::filesystem;
    for (const std::string& d : dirs) {
        std::error_code ec;
        if (!fs::is_directory(d, ec)) continue;
        size_t before = s_index.size();
        for (auto it = fs::recursive_directory_iterator(d, fs::directory_options::follow_directory_symlink, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            // The UHD pack's widescreen_stars_fix blanks two star textures to
            // hide stars Dolphin's widescreen hack shows at the screen's edges.
            // One is the particle star of Mario's wall bonk and a stunned
            // Cataquack, which it hides too; the port's widescreen needs no fix.
            if (it->is_directory(ec) && it->path().filename() == "widescreen_stars_fix") {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(ec)) indexFile(it->path());
        }
        logmsg("texture pack: %zu textures under %s", s_index.size() - before, d.c_str());
    }
    for (auto it = s_index.begin(); it != s_index.end();)  // mips without a level 0
        it = it->second.path.empty() ? s_index.erase(it) : std::next(it);
    if (!s_index.empty()) {
        queryFormats();
        startWorker();
        if (preloadMode() == 2) requestPack();
        s_state = 1;
    }
}

bool hiresEnabled() {
    if (s_state < 0) scan();
    return s_state == 1;
}

// SMS_TEXTURE_PACK_LOG=1 logs the pack name of every texture the game loads,
// and whether the pack replaced it (for checking or making a pack).
bool hiresLog() {
    static int on = -1;
    if (on < 0) on = getenv("SMS_TEXTURE_PACK_LOG") && atoi(getenv("SMS_TEXTURE_PACK_LOG")) != 0;
    return on != 0;
}

// SMS_TEXTURE_DUMP=dir: every texture the game loads is written there once,
// as tex1_<...>.png under its pack name (level 0, decoded), the way packs are
// made: dump, edit or upscale, keep the name.
const char* hiresDumpDir() {
    static const char* dir = nullptr;
    static bool checked = false;
    if (!checked) {
        checked = true;
        const char* e = getenv("SMS_TEXTURE_DUMP");
        if (e && *e && strcmp(e, "0") != 0) {
            dir = e;
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
        }
    }
    return dir;
}

void hiresDump(const std::string& name, const uint8_t* rgba, uint32_t w, uint32_t h) {
    static std::unordered_set<std::string> done;
    const char* dir = hiresDumpDir();
    if (!dir || !done.insert(name).second) return;
    std::string path = std::string(dir) + "/" + name + ".png";
    if (!stbi_write_png(path.c_str(), int(w), int(h), 4, rgba, int(w) * 4))
        logmsg("texture dump: cannot write %s", path.c_str());
}

// ------------------------------------------------------------------ names
// The palette entries a colour-indexed texture's base level uses.
static void paletteRange(const uint8_t* data, size_t bytes, uint32_t fmt, uint32_t* lo, uint32_t* hi) {
    uint32_t mn = 0xFFFF, mx = 0;
    if (fmt == 8) {  // C4: two indices per byte
        for (size_t i = 0; i < bytes; i++) {
            uint32_t a = data[i] & 15, b = data[i] >> 4;
            mn = std::min(mn, std::min(a, b));
            mx = std::max(mx, std::max(a, b));
        }
    } else if (fmt == 9) {  // C8
        for (size_t i = 0; i < bytes; i++) {
            mn = std::min<uint32_t>(mn, data[i]);
            mx = std::max<uint32_t>(mx, data[i]);
        }
    } else {  // C14X2: big-endian halfwords
        for (size_t i = 0; i + 1 < bytes; i += 2) {
            uint32_t v = (uint32_t(data[i]) << 8 | data[i + 1]) & 0x3FFF;
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
    }
    if (mn > mx) mn = mx = 0;
    *lo = mn;
    *hi = mx;
}

std::string hiresName(const uint8_t* data, uint32_t fmt, uint32_t w, uint32_t h, bool mipmapped, const uint8_t* tlut,
                      uint32_t tlutBytes, bool onlyIfPresent) {
    size_t bytes = texLevelBytes(fmt, w, h);
    char base[96];
    snprintf(base, sizeof base, "tex1_%ux%u%s_%016llx", w, h, mipmapped ? "_m" : "",
             (unsigned long long)xxh64(data, bytes, 0));
    char fmtPart[8];
    snprintf(fmtPart, sizeof fmtPart, "_%u", fmt);
    bool ci = fmt == 8 || fmt == 9 || fmt == 10;
    if (ci && tlut) {
        uint32_t lo, hi;
        paletteRange(data, bytes, fmt, &lo, &hi);
        uint32_t off = 2 * lo, len = 2 * (hi + 1 - lo);
        if (off + len > tlutBytes) len = off < tlutBytes ? tlutBytes - off : 0;
        char tl[24];
        snprintf(tl, sizeof tl, "_%016llx", (unsigned long long)xxh64(tlut + off, len, 0));
        std::string full = std::string(base) + tl + fmtPart;
        if (!onlyIfPresent || s_index.count(full)) return full;
        std::string wild = std::string(base) + "_$" + fmtPart;
        if (s_index.count(wild)) return wild;
    }
    std::string plain = std::string(base) + fmtPart;
    if (!onlyIfPresent || s_index.count(plain)) return plain;
    return std::string();
}

// ------------------------------------------------------------------ loading
struct Loaded {
    int w = 0, h = 0;
    std::vector<std::vector<uint8_t>> levels;  // levels[0] always present
    GLenum compressed = 0;                     // the levels' GL block format, 0 for RGBA8
    int blocks = 0;                            // and their DDS block format (DDS_BC*)
    int first = 0;                             // the level levels[0] is (a completion's)
    int scale = 0;                             // log2 of w over the original's width
    bool mipmapped = false;                    // the original is sampled with mipmaps (_m)
    std::vector<float> original;               // the original's coverage by level (hiresNoteOriginal)
    bool arbitrary = false;
    bool failed = false;
};

// What the GL takes compressed (queried on the render thread by the scan).
static bool s_s3tc = false, s_bptc = false;

// hiresNoteOriginal's, by replacement name: noted on resource-loading threads
// and the render thread, read by the workers' decodes.
static std::mutex& s_originalMu = *new std::mutex;
static std::unordered_map<std::string, std::vector<float>>& s_original =
    *new std::unordered_map<std::string, std::vector<float>>;

float alphaCoverage(const uint8_t* rgba, size_t texels) {
    size_t pass = 0;
    for (size_t i = 0; i < texels; i++) pass += rgba[i * 4 + 3] >= 128;
    return texels ? float(double(pass) / double(texels)) : 1.0f;
}

void hiresNoteOriginal(const std::string& name, std::vector<float> coverage) {
    if (coverage.empty() || name.find("_m_") == std::string::npos) return;  // only mipmapped ones use it
    std::lock_guard<std::mutex> lk(s_originalMu);
    s_original[name] = std::move(coverage);
}

static int maxSize();

static void queryFormats() {
    GLint n = 0, major = 0, minor = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    s_bptc = major > 4 || (major == 4 && minor >= 2);  // core since 4.2
    for (GLint i = 0; i < n; i++) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
        if (!e) continue;
        if (!strcmp(e, "GL_EXT_texture_compression_s3tc")) s_s3tc = true;
        // The desktop extension's name, and OpenGL ES's (most phones have neither).
        if (!strcmp(e, "GL_ARB_texture_compression_bptc") || !strcmp(e, "GL_EXT_texture_compression_bptc"))
            s_bptc = true;
    }
    logmsg("texture pack: the GPU takes BC1-BC3 %s and BC7 %s", s_s3tc ? "compressed" : "decoded to RGBA",
           s_bptc ? "compressed" : "decoded to RGBA");
    if (maxSize())
        logmsg("texture pack: images held as RGBA over %dx%d texels are halved until they fit", maxSize(), maxSize());
}

// SMS_TEXTURE_PACK_MAX_SIZE: replacements held as RGBA (PNGs, and blocks the
// GPU cannot take) with more texels than a square this wide are halved until
// they fit, before any decoding (0, the default: no limit). A GPU without BC7
// would need 341 MiB for each of the UHD pack's 8192x8192 images, and over a
// GiB for its 16384x16384 ones; blocks it takes stay whole, at a quarter of
// that. Texels, not the longer side, so the pack's 4160x400 strips stay HD.
static int maxSize() {
    static const int size = [] {
        const char* e = getenv("SMS_TEXTURE_PACK_MAX_SIZE");
        return e && atoi(e) > 0 ? atoi(e) : 0;
    }();
    return size;
}

// The levels to drop from the top of a w x h RGBA image for it to fit maxSize.
static int levelsOverMax(int w, int h) {
    const uint64_t most = uint64_t(maxSize()) * uint64_t(maxSize());
    int n = 0;
    if (most)
        while (uint64_t(std::max(1, w >> n)) * uint64_t(std::max(1, h >> n)) > most) n++;
    return n;
}

static inline void boxTexel(const uint8_t* a, const uint8_t* b, const uint8_t* c, const uint8_t* d, bool cutout,
                            uint8_t* o);

// An RGBA image (lw x lh) at half its size, by a 2x2 box filter.
static std::vector<uint8_t> halveRgba(const std::vector<uint8_t>& src, int lw, int lh) {
    int nw = std::max(1, lw / 2), nh = std::max(1, lh / 2);
    std::vector<uint8_t> dst(size_t(nw) * nh * 4);
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            int x0 = std::min(2 * x, lw - 1), x1 = std::min(2 * x + 1, lw - 1);
            int y0 = std::min(2 * y, lh - 1), y1 = std::min(2 * y + 1, lh - 1);
            boxTexel(&src[(size_t(y0) * lw + x0) * 4], &src[(size_t(y0) * lw + x1) * 4],
                     &src[(size_t(y1) * lw + x0) * 4], &src[(size_t(y1) * lw + x1) * 4], false,
                     &dst[(size_t(y) * nw + x) * 4]);
        }
    return dst;
}

// DDS (the format most Dolphin packs ship in): BC1-3 and BC7 blocks with the
// file's own mip levels, or 32-bit RGBA/BGRA. Blocks the GL cannot take are
// decoded here; the levels a file leaves out are made by completeChain.
enum { DDS_BC1 = BLOCK_BC1, DDS_BC2 = BLOCK_BC2, DDS_BC3 = BLOCK_BC3, DDS_BC7 = BLOCK_BC7, DDS_RGBA, DDS_BGRA };

static bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool ok = n > 0 && fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

static std::vector<uint8_t> halveBlocks(int fmt, const std::vector<uint8_t>& src, int lw, int lh);

// `dropped`: the levels over maxSize it left out (the explicit _mip<n> files'
// numbers count them).
static bool decodeDds(const std::string& path, Loaded* L, int* dropped = nullptr) {
    std::vector<uint8_t> file;
    if (!readFile(path, file) || file.size() < 128 || memcmp(file.data(), "DDS ", 4) != 0) return false;
    const uint8_t* d = file.data();
    int h = int(rd32(d + 12)), w = int(rd32(d + 16));
    int mips = std::max(1, int(rd32(d + 28)));
    uint32_t pfFlags = rd32(d + 80), rmask = rd32(d + 92);
    size_t off = 128;
    int fmt = 0;
    if (!memcmp(d + 84, "DXT1", 4)) fmt = DDS_BC1;
    else if (!memcmp(d + 84, "DXT3", 4)) fmt = DDS_BC2;
    else if (!memcmp(d + 84, "DXT5", 4)) fmt = DDS_BC3;
    else if (!memcmp(d + 84, "DX10", 4) && file.size() >= 148) {
        uint32_t dxgi = rd32(d + 128);
        off = 148;
        if (dxgi >= 70 && dxgi <= 72) fmt = DDS_BC1;
        else if (dxgi >= 73 && dxgi <= 75) fmt = DDS_BC2;
        else if (dxgi >= 76 && dxgi <= 78) fmt = DDS_BC3;
        else if (dxgi >= 97 && dxgi <= 99) fmt = DDS_BC7;
        else if (dxgi == 28 || dxgi == 29) fmt = DDS_RGBA;
        else if (dxgi == 87 || dxgi == 91) fmt = DDS_BGRA;
    } else if ((pfFlags & 0x40) && rd32(d + 88) == 32) {
        fmt = rmask == 0x000000FFu ? DDS_RGBA : DDS_BGRA;
    }
    if (!fmt || w <= 0 || h <= 0) return false;
    bool block = fmt <= DDS_BC7;
    size_t bsz = fmt == DDS_BC1 ? 8 : 16;
    int full = 1;
    for (int m = std::max(w, h); m > 1; m >>= 1) full++;
    // Supported blocks stay compressed whatever levels the file has: the
    // missing ones are encoded in the same format (completeChain).
    bool gpu = (fmt <= DDS_BC3 && s_s3tc) || (fmt == DDS_BC7 && s_bptc);
    static const GLenum kGl[] = {0, 0x83F1, 0x83F2, 0x83F3, 0x8E8C};  // S3TC DXT1/3/5 RGBA, BPTC UNORM
    auto bytesOf = [&](int lw, int lh) {
        return block ? size_t((lw + 3) / 4) * size_t((lh + 3) / 4) * bsz : size_t(lw) * lh * 4;
    };
    int lw = w, lh = h, count = std::min(mips, full);
    // Levels of an image decoded to RGBA over maxSize are skipped while the
    // file has smaller ones, the rest made from its last (as blocks: a large
    // level never sits in memory as RGBA).
    const int drop = gpu ? 0 : levelsOverMax(w, h);
    std::vector<uint8_t> made;
    if (dropped) *dropped = drop;
    for (int m = 0; m < drop; m++) {
        int nw = std::max(1, lw / 2), nh = std::max(1, lh / 2);
        if (made.empty() && m + 1 < count && off + bytesOf(lw, lh) + bytesOf(nw, nh) <= file.size()) {
            off += bytesOf(lw, lh);
            count--;
        } else {
            if (made.empty()) {
                if (off + bytesOf(lw, lh) > file.size()) return false;
                made.assign(d + off, d + off + bytesOf(lw, lh));
            }
            made = block ? halveBlocks(fmt, made, lw, lh) : halveRgba(made, lw, lh);
            count = 1;
        }
        lw = nw;
        lh = nh;
    }
    L->w = lw;
    L->h = lh;
    L->compressed = gpu ? kGl[fmt] : 0;
    L->blocks = gpu ? fmt : 0;
    for (int m = 0; m < count; m++) {
        int bw = (lw + 3) / 4, bh = (lh + 3) / 4;
        size_t bytes = bytesOf(lw, lh);
        if (made.empty() && off + bytes > file.size()) break;
        const uint8_t* src = made.empty() ? d + off : made.data();
        if (gpu) {
            L->levels.emplace_back(src, src + bytes);
        } else if (block) {  // decode whole blocks into a padded image, then crop
            std::vector<uint8_t> pad(size_t(bw) * 4 * bh * 4 * 4);
            int pitch = bw * 4 * 4;
            for (int by = 0; by < bh; by++)
                for (int bx = 0; bx < bw; bx++) {
                    const uint8_t* b = src + (size_t(by) * bw + bx) * bsz;
                    uint8_t* o = pad.data() + size_t(by) * 4 * pitch + size_t(bx) * 16;
                    if (fmt == DDS_BC1) bcdec_bc1(b, o, pitch);
                    else if (fmt == DDS_BC2) bcdec_bc2(b, o, pitch);
                    else if (fmt == DDS_BC3) bcdec_bc3(b, o, pitch);
                    else bcdec_bc7(b, o, pitch);
                }
            std::vector<uint8_t> px(size_t(lw) * lh * 4);
            for (int y = 0; y < lh; y++) memcpy(&px[size_t(y) * lw * 4], &pad[size_t(y) * pitch], size_t(lw) * 4);
            L->levels.push_back(std::move(px));
        } else {
            std::vector<uint8_t> px(src, src + bytes);
            if (fmt == DDS_BGRA)
                for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]);
            L->levels.push_back(std::move(px));
        }
        off += bytes;
        lw = std::max(1, lw / 2);
        lh = std::max(1, lh / 2);
    }
    return !L->levels.empty();
}

// Each replacement is decoded once, on a worker thread, uploaded into its own
// GL texture on the render thread and its pixels freed; every cache entry
// whose data has that name samples the one texture.
struct Replacement {
    enum { QUEUED, READY, FAILED } state = QUEUED;
    GLuint tex = 0;
    int scale = 0;       // log2 of its size over the GX size
    size_t bytes = 0;    // in GL
    int w = 0, h = 0;    // its level 0
    int levels = 0;      // in GL; the rest of a compressed chain follows (completeLater)
    uint32_t used = 0;   // the display frame it was last sampled in
};
static size_t s_bytes = 0;     // all READY replacements
static uint32_t s_frame = 0;   // display frames (hiresEndFrame)
static std::unordered_map<std::string, Replacement> s_repl;  // render thread only
// Never destroyed: the workers are still waiting on s_cv when the process
// exits, and destroying a condition variable with a waiter blocks forever.
static std::mutex& s_mu = *new std::mutex;
static std::condition_variable& s_cv = *new std::condition_variable;
// What a request is for, in the order the decode worker takes them: a
// texture a resource or a draw asked for, one of the whole pack
// (SMS_TEXTURE_PACK_PRELOAD=all), or the levels completeLater makes (on the
// completion worker).
enum Kind { DEMAND, PACK, REST };
struct Request { std::string name; uint32_t w, h; int kind = DEMAND; };
struct Decoded { Request request; std::unique_ptr<Loaded> image; size_t bytes; };
static std::deque<Request>& s_queue = *new std::deque<Request>;
static std::deque<Request>& s_packQueue = *new std::deque<Request>;  // after s_queue
static std::deque<Decoded>& s_decoded = *new std::deque<Decoded>;
static std::unordered_set<std::string>& s_requested = *new std::unordered_set<std::string>;
static std::unordered_set<std::string>& s_packPending = *new std::unordered_set<std::string>;  // in s_packQueue
static size_t s_decodedBytes = 0, s_decodedDemand = 0;
static size_t s_decodedPack = 0, s_decodedPackBytes = 0;  // of those, the whole pack's (uploaded during loads)
static std::atomic<bool> s_stop{false};
static bool s_busy = false, s_busyPack = false, s_busyRest = false;
static std::thread* s_worker = nullptr;
static std::thread* s_completer = nullptr;
// The completion worker's queue and results, apart from s_mu: outside loads
// the render thread only try-locks s_restMu, so the idle-priority worker,
// which a busy computer can leave waiting for long, never holds up a frame.
static std::mutex& s_restMu = *new std::mutex;
static std::condition_variable& s_restCv = *new std::condition_variable;
static std::deque<Request>& s_restQueue = *new std::deque<Request>;
static std::deque<Decoded>& s_restDone = *new std::deque<Decoded>;
static size_t s_restDoneBytes = 0;
static std::vector<std::string> s_restLater;  // render thread: completeLater's, not yet in s_restQueue

static bool syncLoading() {
    static const bool sync = getenv("SMS_TEXTURE_PACK_SYNC") && atoi(getenv("SMS_TEXTURE_PACK_SYNC")) != 0;
    return sync;
}

static size_t pendingBudget() {
    static const size_t budget = [] {
        const char* e = getenv("SMS_TEXTURE_PACK_PENDING_MB");
        return size_t(e && atoi(e) > 0 ? atoi(e) : 256) << 20;
    }();
    return budget;
}

// A texture a stage asks for that the whole pack's queue still holds moves
// ahead of it, so the stage's load waits for it.
static void requestTexture(const std::string& name, uint32_t w, uint32_t h, int kind = DEMAND) {
    if (name.empty()) return;
    std::lock_guard<std::mutex> lk(s_mu);
    if (s_requested.insert(name).second) {
        (kind == PACK ? s_packQueue : s_queue).push_back({name, w, h, kind});
        if (kind == PACK) s_packPending.insert(name);
        s_cv.notify_all();
    } else if (kind == DEMAND && s_packPending.erase(name)) {
        s_queue.push_back({name, w, h, DEMAND});
        s_cv.notify_all();
    }
}

// SMS_TEXTURE_PACK_PRELOAD: 1 (the default) prepares the textures a stage's
// resources name while it loads; all also reads the rest of the pack from the
// start, sends it during loads (uploadPack) and keeps it (as Dolphin's
// "Prefetch Custom Textures" does: the UHD pack takes about 4 GB of video
// memory); 0 reads each texture when it is first drawn, showing the original
// until it is ready.
static int preloadMode() {
    static const int mode = [] {
        const char* e = getenv("SMS_TEXTURE_PACK_PRELOAD");
        if (!e || !*e) return 1;
        if (!strcmp(e, "all")) return 2;
        return atoi(e) != 0 || !strcmp(e, "on") ? 1 : 0;
    }();
    return syncLoading() ? 0 : mode;
}
static bool preloading() { return preloadMode() != 0; }

// The levels a pack leaves out are made at the lowest priority, of processor
// and of disk (the completion worker and its helpers), so they wait for idle
// time instead of slowing the game's frames or its disc reads.
static void idlePriority() {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#elif defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
#elif defined(__linux__)
    sched_param p{};
    pthread_setschedparam(pthread_self(), SCHED_IDLE, &p);
    syscall(SYS_ioprio_set, 1 /* IOPRIO_WHO_PROCESS: this thread */, 0, 3 << 13 /* IOPRIO_CLASS_IDLE */);
#endif
}

// Inspect resources before endian conversion. Only names/hashes are retained:
// the loader may free or reuse its buffer immediately after this returns.
static uint32_t readBe32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
static uint32_t readBe16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }

static void prefetchTimg(const uint8_t* data, size_t size, size_t off) {
    if (off > size || size - off < 0x20) return;
    const uint8_t* t = data + off;
    uint32_t fmt = t[0], w = readBe16(t + 2), h = readBe16(t + 4);
    if (fmt > 14 || !w || !h || w > 1024 || h > 1024) return;
    uint32_t image = readBe32(t + 0x1C);
    size_t bytes = texLevelBytes(fmt, w, h);
    if (image < 0x20 || image > size - off || !bytes || bytes > size - off - image) return;
    const uint8_t* palette = nullptr;
    uint32_t paletteBytes = 0;
    if (fmt == 8 || fmt == 9 || fmt == 10) {
        uint32_t pal = readBe32(t + 0x0C);
        paletteBytes = readBe16(t + 0x0A) * 2;
        if (!paletteBytes || pal < 0x20 || pal > size - off || paletteBytes > size - off - pal) return;
        palette = t + pal;
    }
    bool mipmapped = t[0x14] >= 2 && t[0x17] != 0;
    std::string name = hiresName(t + image, fmt, w, h, mipmapped, palette, paletteBytes, true);
    if (name.empty()) name = hiresName(t + image, fmt, w, h, !mipmapped, palette, paletteBytes, true);
    if (name.empty()) return;
    std::vector<float> coverage;  // for completeChain, which uses it for mipmapped replacements
    std::vector<uint8_t> rgba;
    size_t at = off + image;
    const bool m = name.find("_m_") != std::string::npos;
    for (uint32_t l = 0, lw = w, lh = h; m && l < std::max(1u, uint32_t(t[0x18])); l++) {
        size_t n = texLevelBytes(fmt, lw, lh);
        if (n > size - at) break;
        rgba.resize(size_t(lw) * lh * 4);
        decodeTexture(data + at, fmt, lw, lh, palette, t[0x09], rgba.data());
        coverage.push_back(alphaCoverage(rgba.data(), size_t(lw) * lh));
        at += n;
        lw = std::max(1u, lw / 2);
        lh = std::max(1u, lh / 2);
    }
    hiresNoteOriginal(name, std::move(coverage));
    requestTexture(name, w, h);
}

void hiresPrefetchResource(const void* ptr, uint32_t size, const char* name) {
    // Never scan directories or query GL on a resource-loading thread.
    if (s_state != 1 || !preloading() || !ptr || size < 0x20) return;
    const uint8_t* data = static_cast<const uint8_t*>(ptr);
    bool model = !memcmp(data, "J3D2", 4), particle = !memcmp(data, "JEFFjpa1", 8);
    if (!model && !particle) {
        if (name) {
            std::string ext = std::filesystem::path(name).extension().string();
            for (char& c : ext) c = char(tolower(uint8_t(c)));
            if (ext == ".bti" && data[0x19] != 0x6E) prefetchTimg(data, size, 0);
        }
        return;
    }
    uint32_t blocks = readBe32(data + 0x0C);
    size_t pos = 0x20;
    for (uint32_t i = 0; i < blocks && pos <= size && size - pos >= 8; ++i) {
        uint32_t len = readBe32(data + pos + 4);
        if (len < 8 || len > size - pos) break;
        const uint8_t* b = data + pos;
        if (!memcmp(b, "TEX1", 4)) {
            if (particle) {
                prefetchTimg(b, len, 0x20);
            } else if (len >= 0x14) {
                uint32_t count = readBe16(b + 8), headers = readBe32(b + 0x0C);
                if (headers >= 0x14 && headers <= len && count <= (len - headers) / 0x20)
                    for (uint32_t j = 0; j < count; ++j) prefetchTimg(b, len, headers + size_t(j) * 0x20);
            }
        }
        pos += len;
    }
}

// ------------------------------------------------------------------ cut-out coverage
// A 2x2 box filter averages a thin cut-out strand's alpha with the clear
// texels beside it, so in the levels completeChain makes a fence, grate or
// leaf fades under the alpha test that draws it (alpha >= 128 in most of the
// game's cut-out materials) until it is gone: Pinna Park's climbing-cage mesh
// vanished a few metres away, where the disc's own hand-drawn levels keep it
// solid. So for a cut-out replacing a mipmapped texture (alpha mostly clear
// or opaque, with some of each) every level made is scaled up until as many
// of its texels pass 128 as in the last level the pack supplied (Castano,
// "Computing Alpha Mipmaps"), or as in the original's level of that size if
// more: the cage's levels thicken its mesh (30% of texels pass in level 0,
// 69% in the 8x8), so it reads as a green mesh at any distance. The scale
// only grows alpha, and at most 8 times. Their colours are weighted by alpha,
// so the clear texels' colour (black, in most packs) doesn't darken the
// strands. Others, such as the HUD's blended pictures, keep the box filter.
struct AlphaHist {
    uint64_t n[256] = {};
    void add(const AlphaHist& o) {
        for (int a = 0; a < 256; a++) n[a] += o.n[a];
    }
    uint64_t total() const {
        uint64_t t = 0;
        for (int a = 0; a < 256; a++) t += n[a];
        return t;
    }
};
struct Coverage {
    bool known = false;  // `target` measured (on the first level made's source)
    double target = -1;  // the share of texels at or above 128 to keep; < 0: not a cut-out
    const Loaded* L = nullptr;
    // The share for replacement level `level`.
    double at(int level) const {
        const int k = level - L->scale;  // the original's level of that size
        if (k < 0 || L->original.empty() || L->original[0] >= 1.0f) return target;
        return std::max(target, double(L->original[std::min<size_t>(size_t(k), L->original.size() - 1)]));
    }
};

static void measureCoverage(Coverage* cov, const AlphaHist& src) {
    uint64_t total = src.total(), clear = 0, soft = 0;
    for (int a = 0; a < 256; a++) {
        if (a < 128) clear += src.n[a];
        if (a >= 16 && a < 240) soft += src.n[a];
    }
    cov->known = true;
    // Mostly in-between alpha (smoke, glows, shadows) is blended, not cut out.
    if (total && clear >= total / 50 && total - clear >= total / 100 && soft <= total / 4)
        cov->target = double(total - clear) / double(total);
}

// The alpha that should become 128 in level `level`, of histogram `h`: the
// highest that lets at least the target's share pass (rounding the other way
// would lose a level's lone strands, at a quarter of their alpha, altogether).
static int coverageCut(const Coverage& cov, int level, const AlphaHist& h) {
    if (cov.target < 0) return 128;
    const double want = cov.at(level) * double(h.total());
    uint64_t at = 0;  // texels at or above t
    for (int t = 255; t > 128; t--) at += h.n[t];
    for (int t = 128; t > 16; t--) {
        at += h.n[t];
        if (double(at) >= want) return t;
    }
    return 16;
}

static inline uint8_t scaleAlpha(uint8_t a, int cut) { return uint8_t(std::min(255, a * 128 / cut)); }

// A 2x2 box filter of texels a-d into o; for a cut-out, colour by alpha.
static inline void boxTexel(const uint8_t* a, const uint8_t* b, const uint8_t* c, const uint8_t* d, bool cutout,
                            uint8_t* o) {
    const int sa = a[3] + b[3] + c[3] + d[3];
    for (int i = 0; i < 3; i++)
        o[i] = cutout && sa ? uint8_t((a[i] * a[3] + b[i] * b[3] + c[i] * c[3] + d[i] * d[3] + sa / 2) / sa)
                            : uint8_t((a[i] + b[i] + c[i] + d[i] + 2) / 4);
    o[3] = uint8_t((sa + 2) / 4);
}

// The next level of a compressed one (lw x lh), by the same 2x2 box filter,
// in the same block format. Two block rows are decoded at a time, so even a
// 16384x16384 level never sits in memory as RGBA, and a large level's block
// rows are shared between threads (an 8192x8192 BC7 file takes about a second
// on four), leaving two cores to the game and render threads it runs beside.
// For a cut-out (or one not yet known not to be) a first pass measures the
// alpha of the source and of the level, without encoding, for `cov`.
static std::vector<uint8_t> nextBlockLevel(int fmt, const std::vector<uint8_t>& src, int lw, int lh, bool idle,
                                           Coverage* cov, int level) {
    int nw = std::max(1, lw / 2), nh = std::max(1, lh / 2);
    int sbw = (lw + 3) / 4, sbh = (lh + 3) / 4, dbw = (nw + 3) / 4, dbh = (nh + 3) / 4;
    size_t bsz = fmt == DDS_BC1 ? 8 : 16;
    std::vector<uint8_t> dst(size_t(dbw) * dbh * bsz);
    int pitch = sbw * 16;  // bytes in a decoded source row
    int cut = 128;
    // With `hist` (source, level), it measures; else it encodes into dst.
    auto rows = [&](int from, int to, AlphaHist* hist) {
        std::vector<uint8_t> in(size_t(pitch) * 8), out(size_t(dbw) * 16 * 4);
        for (int by = from; by < to; by++) {
            for (int k = 0; k < 2 && 2 * by + k < sbh; k++)  // source rows 8*by to 8*by+7
                for (int bx = 0; bx < sbw; bx++) {
                    const uint8_t* b = src.data() + (size_t(2 * by + k) * sbw + bx) * bsz;
                    uint8_t* o = in.data() + size_t(k) * 4 * pitch + size_t(bx) * 16;
                    if (fmt == DDS_BC1) bcdec_bc1(b, o, pitch);
                    else if (fmt == DDS_BC2) bcdec_bc2(b, o, pitch);
                    else if (fmt == DDS_BC3) bcdec_bc3(b, o, pitch);
                    else bcdec_bc7(b, o, pitch);
                }
            if (hist && !cov->known)
                for (int y = 8 * by; y < std::min(8 * by + 8, lh); y++)
                    for (int x = 0; x < lw; x++) hist[0].n[in[size_t(y - 8 * by) * pitch + x * 4 + 3]]++;
            for (int r = 0; r < 4; r++) {  // this block row's 4 output rows; past the image, the last one again
                int y = std::min(4 * by + r, nh - 1);
                const uint8_t* s0 = &in[size_t(std::min(2 * y, lh - 1) - 8 * by) * pitch];
                const uint8_t* s1 = &in[size_t(std::min(2 * y + 1, lh - 1) - 8 * by) * pitch];
                uint8_t* o = &out[size_t(r) * dbw * 16];
                for (int x = 0; x < dbw * 4; x++) {
                    int xx = std::min(x, nw - 1);
                    int x0 = std::min(2 * xx, lw - 1) * 4, x1 = std::min(2 * xx + 1, lw - 1) * 4;
                    boxTexel(s0 + x0, s0 + x1, s1 + x0, s1 + x1, !hist && cov->target >= 0, o + x * 4);
                    if (hist && x < nw && 4 * by + r < nh) hist[1].n[o[x * 4 + 3]]++;
                    if (cut < 128) o[x * 4 + 3] = scaleAlpha(o[x * 4 + 3], cut);
                }
            }
            if (hist) continue;
            for (int bx = 0; bx < dbw; bx++) {
                uint8_t block[64];
                for (int r = 0; r < 4; r++) memcpy(block + r * 16, &out[(size_t(r) * dbw * 4 + bx * 4) * 4], 16);
                encodeBlock(fmt, block, dst.data() + (size_t(by) * dbw + bx) * bsz);
            }
        }
    };
    int threads = std::max(1, std::min<int>({int(std::thread::hardware_concurrency()) - 2, 8, dbh / 16}));
    auto pass = [&](std::vector<AlphaHist>* hists) {  // one pair of histograms per thread when measuring
        auto slot = [hists](int t) { return hists ? &(*hists)[size_t(t) * 2] : nullptr; };
        std::vector<std::thread> pool;
        for (int t = 1; t < threads; t++)
            pool.emplace_back([&rows, idle, h = slot(t), from = dbh * t / threads, to = dbh * (t + 1) / threads] {
                if (idle) idlePriority();
                rows(from, to, h);
            });
        rows(0, dbh / threads, slot(0));
        for (std::thread& t : pool) t.join();
    };
    if (!cov->known || cov->target >= 0) {
        std::vector<AlphaHist> hists(size_t(threads) * 2);
        pass(&hists);
        for (int t = 1; t < threads; t++) {
            hists[0].add(hists[size_t(t) * 2]);
            hists[1].add(hists[size_t(t) * 2 + 1]);
        }
        if (!cov->known) measureCoverage(cov, hists[0]);
        cut = coverageCut(*cov, level, hists[1]);
    }
    pass(nullptr);
    return dst;
}

// A compressed level (lw x lh) at half its size, as nextBlockLevel makes it.
static std::vector<uint8_t> halveBlocks(int fmt, const std::vector<uint8_t>& src, int lw, int lh) {
    Loaded none;
    Coverage cov;
    cov.L = &none;
    return nextBlockLevel(fmt, src, lw, lh, false, &cov, 0);
}

// Complete every replacement's mip chain down to 1x1 with 2x2 box filtering,
// whatever levels the pack supplied: the sampler minifies a replacement as far
// as its original (samplerFor), and without the lower levels a far wall
// samples an 8192x8192 replacement's top level and shimmers as the camera
// moves (Noki Bay's undersea walls, whose pack files have one level). A
// cut-out's levels keep its coverage (Coverage).
// Compressed levels take a while to encode, so outside SMS_TEXTURE_PACK_SYNC
// they follow the pack's own levels instead of delaying them (completeLater).
static void completeChain(Loaded* L, bool idle = false) {
    if (L->levels.empty()) return;
    int lw = L->w, lh = L->h;
    for (size_t i = 1; i < L->levels.size(); i++) {
        lw = std::max(1, lw / 2);
        lh = std::max(1, lh / 2);
    }
    Coverage cov;
    cov.L = L;
    cov.known = !L->mipmapped;
    if (L->compressed) {
        while (lw > 1 || lh > 1) {
            L->levels.push_back(nextBlockLevel(L->blocks, L->levels.back(), lw, lh, idle, &cov, int(L->levels.size())));
            lw = std::max(1, lw / 2);
            lh = std::max(1, lh / 2);
        }
        return;
    }
    while (lw > 1 || lh > 1) {
        int nw = std::max(1, lw / 2), nh = std::max(1, lh / 2);
        const std::vector<uint8_t>& src = L->levels.back();
        if (!cov.known) {
            AlphaHist h;
            for (size_t i = 3; i < src.size(); i += 4) h.n[src[i]]++;
            measureCoverage(&cov, h);
        }
        std::vector<uint8_t> dst(size_t(nw) * nh * 4);
        AlphaHist h;
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                int x0 = std::min(2 * x, lw - 1), x1 = std::min(2 * x + 1, lw - 1);
                int y0 = std::min(2 * y, lh - 1), y1 = std::min(2 * y + 1, lh - 1);
                uint8_t* o = &dst[(size_t(y) * nw + x) * 4];
                boxTexel(&src[(size_t(y0) * lw + x0) * 4], &src[(size_t(y0) * lw + x1) * 4],
                         &src[(size_t(y1) * lw + x0) * 4], &src[(size_t(y1) * lw + x1) * 4], cov.target >= 0, o);
                h.n[o[3]]++;
            }
        int cut = coverageCut(cov, int(L->levels.size()), h);
        if (cut < 128)
            for (size_t i = 3; i < dst.size(); i += 4) dst[i] = scaleAlpha(dst[i], cut);
        L->levels.push_back(std::move(dst));
        lw = nw;
        lh = nh;
    }
}

static int chainLength(int w, int h) {
    int n = 1;
    for (int m = std::max(w, h); m > 1; m >>= 1) n++;
    return n;
}

// ------------------------------------------------------------------ completed levels on disk
// The levels completeChain encodes for a compressed replacement are kept on
// disk, so each is made once per computer instead of once per session (an
// 8192x8192 BC7 file takes about a second of four cores). One file per
// replacement, <name>.mips, under texture-mips/ beside the shader cache
// (userCacheDir); a file is used only while its source keeps the size and
// modification time it was made from. SMS_TEXTURE_PACK_CACHE=dir keeps them
// there instead; =0 turns this off. The magic's digit changes with the way
// levels are made (2: cut-outs keep their coverage), so older files are remade.
static const char kMipsMagic[8] = {'S', 'M', 'S', 'M', 'I', 'P', 'S', '2'};
enum { MIPS_ORIGINAL = 1 };  // made knowing the original's coverage (Coverage)
struct MipsHeader {  // the same in 32- and 64-bit builds, which share the folder
    char magic[8];
    uint64_t size;
    int64_t mtime;
    uint32_t w, h, blocks, first, count, flags;
};
static_assert(sizeof(MipsHeader) == 48, "MipsHeader has no padding");

static const std::string& mipCacheDir() {
    static const std::string dir = [] {
        const char* e = getenv("SMS_TEXTURE_PACK_CACHE");
        if (e && !strcmp(e, "0")) return std::string();
        std::string d = e && *e ? std::string(e) : userCacheDir();
        if (d.empty()) return d;
        if (!(e && *e)) d += "/texture-mips";
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        return ec ? std::string() : d;
    }();
    return dir;
}

static bool sourceStamp(const std::string& path, uint64_t* size, int64_t* mtime) {
    std::error_code ec;
    uint64_t n = std::filesystem::file_size(path, ec);
    if (ec) return false;
    auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return false;
    *size = n;
    *mtime = int64_t(t.time_since_epoch().count());
    return true;
}

static size_t blockLevelBytes(int blocks, int w, int h) {
    return size_t((w + 3) / 4) * size_t((h + 3) / 4) * (blocks == DDS_BC1 ? 8 : 16);
}

// Appends the cached levels after the pack's own, if the cache has them.
static void loadCachedLevels(const std::string& name, const PackFile& f, Loaded* L) {
    const std::string& dir = mipCacheDir();
    const int first = int(L->levels.size()), full = chainLength(L->w, L->h);
    if (dir.empty() || first >= full) return;
    uint64_t size;
    int64_t mtime;
    if (!sourceStamp(f.path, &size, &mtime)) return;
    FILE* in = fopen((dir + "/" + name + ".mips").c_str(), "rb");
    if (!in) return;
    MipsHeader hd;
    std::vector<std::vector<uint8_t>> levels;
    if (fread(&hd, sizeof hd, 1, in) == 1 && !memcmp(hd.magic, kMipsMagic, 8) && hd.size == size && hd.mtime == mtime &&
        hd.w == uint32_t(L->w) && hd.h == uint32_t(L->h) && hd.blocks == uint32_t(L->blocks) &&
        hd.first == uint32_t(first) && hd.count == uint32_t(full - first) &&
        // Levels made before the original was seen (SMS_TEXTURE_PACK_PRELOAD=all)
        // are made again once it has been.
        ((hd.flags & MIPS_ORIGINAL) || L->original.empty())) {
        int lw = std::max(1, L->w >> first), lh = std::max(1, L->h >> first);
        for (int i = first; i < full; i++) {
            std::vector<uint8_t> lv(blockLevelBytes(L->blocks, lw, lh));
            if (fread(lv.data(), lv.size(), 1, in) != 1) break;
            levels.push_back(std::move(lv));
            lw = std::max(1, lw / 2);
            lh = std::max(1, lh / 2);
        }
    }
    fclose(in);
    if (int(levels.size()) != full - first) return;
    for (auto& lv : levels) L->levels.push_back(std::move(lv));
}

// Keeps the levels a completion made (L->levels from L->first on).
static void storeCachedLevels(const std::string& name, const PackFile& f, const Loaded& L) {
    const std::string& dir = mipCacheDir();
    MipsHeader hd{};
    if (dir.empty() || L.levels.empty() || !sourceStamp(f.path, &hd.size, &hd.mtime)) return;
    memcpy(hd.magic, kMipsMagic, 8);
    hd.w = uint32_t(L.w);
    hd.h = uint32_t(L.h);
    hd.blocks = uint32_t(L.blocks);
    hd.first = uint32_t(L.first);
    hd.count = uint32_t(L.levels.size());
    hd.flags = L.original.empty() ? 0 : MIPS_ORIGINAL;
    const std::string path = dir + "/" + name + ".mips", part = path + ".part";
    FILE* out = fopen(part.c_str(), "wb");
    if (!out) return;
    bool ok = fwrite(&hd, sizeof hd, 1, out) == 1;
    // A MiB at a time, at most 16 MiB a second, each pushed to the disk as it
    // goes: written at once, a stage's levels (a few hundred MiB) would leave
    // the disk busy enough to hold up the game's disc reads for whole frames.
    using Clock = std::chrono::steady_clock;
    const size_t kChunk = size_t(1) << 20;
    size_t written = sizeof hd;
    for (const auto& lv : L.levels)
        for (size_t off = 0; ok && off < lv.size(); off += kChunk) {
            if (s_stop) ok = false;  // shutting down: not worth the wait
            const auto start = Clock::now();
            const size_t n = std::min(kChunk, lv.size() - off);
            ok = ok && fwrite(lv.data() + off, n, 1, out) == 1 && fflush(out) == 0;
#ifdef __linux__
            sync_file_range(fileno(out), off64_t(written), off64_t(n), SYNC_FILE_RANGE_WRITE);
#endif
            written += n;
            std::this_thread::sleep_until(start + std::chrono::microseconds(n * 1000000 / (16u << 20)));
        }
    ok = fclose(out) == 0 && ok;
    std::error_code ec;
    if (ok) std::filesystem::rename(part, path, ec);
    if (!ok || ec) std::filesystem::remove(part, ec);
}

// L->scale, as finishUpload finds it, and L->mipmapped, from `name`.
static void originalScale(const std::string& name, Loaded* L) {
    unsigned w = 0, h = 0;
    int end = 0;
    if (sscanf(name.c_str(), "tex1_%ux%u%n", &w, &h, &end) != 2 || !w || !h) return;
    L->mipmapped = !name.compare(size_t(end), 3, "_m_");
    while (L->scale < 6 && (w << (L->scale + 1)) <= unsigned(L->w) && (h << (L->scale + 1)) <= unsigned(L->h))
        L->scale++;
}

// `whole`: also the compressed levels completeChain makes, else only the
// pack's and the cached ones (RGBA images are always completed: they are
// decoded anyway).
static Loaded* decode(const std::string& name, bool whole, bool cached = true) {
    const PackFile& f = s_index.at(name);
    Loaded* L = new Loaded;
    L->arbitrary = f.arbitrary;
    {
        std::lock_guard<std::mutex> lk(s_originalMu);
        auto it = s_original.find(name);
        if (it != s_original.end()) L->original = it->second;
    }
    if (endsWith(f.path, ".dds") || endsWith(f.path, ".DDS")) {
        int dropped = 0;
        if (!decodeDds(f.path, L, &dropped)) {
            logmsg("texture pack: cannot read %s (not a DDS of a supported format)", f.path.c_str());
            L->failed = true;
        }
        if (!L->failed) {
            int lw = std::max(1, L->w >> (L->levels.size() - 1));
            int lh = std::max(1, L->h >> (L->levels.size() - 1));
            for (size_t level = L->levels.size(); level + dropped <= f.mips.size() && (lw > 1 || lh > 1); ++level) {
                const std::string& path = f.mips[level + dropped - 1];
                Loaded mip;
                lw = std::max(1, lw / 2);
                lh = std::max(1, lh / 2);
                if (path.empty() || !decodeDds(path, &mip) || mip.w != lw || mip.h != lh || mip.compressed != L->compressed)
                    break;
                L->levels.push_back(std::move(mip.levels.front()));
            }
        }
        if (cached && !L->failed && L->compressed) loadCachedLevels(name, f, L);
        originalScale(name, L);
        if (whole || !L->compressed) completeChain(L);
        return L;
    }
    int n = 0;
    uint8_t* px = stbi_load(f.path.c_str(), &L->w, &L->h, &n, 4);
    if (!px) {
        logmsg("texture pack: cannot read %s (%s)", f.path.c_str(), stbi_failure_reason());
        L->failed = true;
        return L;
    }
    L->levels.emplace_back(px, px + size_t(L->w) * L->h * 4);
    stbi_image_free(px);
    const int dropped = levelsOverMax(L->w, L->h);
    for (int m = 0; m < dropped; m++) {
        L->levels[0] = halveRgba(L->levels[0], L->w, L->h);
        L->w = std::max(1, L->w / 2);
        L->h = std::max(1, L->h / 2);
    }
    int lw = L->w, lh = L->h;
    for (size_t i = size_t(dropped); i < f.mips.size(); i++) {  // explicit levels, in order, while their sizes fit
        const std::string& mp = f.mips[i];
        lw = std::max(1, lw / 2);
        lh = std::max(1, lh / 2);
        int mw, mh;
        uint8_t* m = mp.empty() ? nullptr : stbi_load(mp.c_str(), &mw, &mh, &n, 4);
        if (!m || mw != lw || mh != lh) {
            if (m) stbi_image_free(m);
            break;
        }
        L->levels.emplace_back(m, m + size_t(mw) * mh * 4);
        stbi_image_free(m);
    }
    originalScale(name, L);
    completeChain(L);
    return L;
}

// The backlogs of decoded textures can exceed their byte budget by one
// result, allowing even a single texture larger than the budget to make
// progress. The whole pack's has its own: it waits for loads (uploadPack).
static bool backlogFull() {
    return s_decoded.size() - s_decodedPack >= 64 || s_decodedBytes - s_decodedPackBytes >= pendingBudget();
}
static bool packBacklogFull() { return s_decodedPack >= 64 || s_decodedPackBytes >= pendingBudget(); }

static size_t levelBytes(const Loaded& L) {
    size_t bytes = 0;
    for (const auto& level : L.levels) bytes += level.size();
    return bytes;
}

// The decode worker: what resources and draws ask for, then the whole pack.
static void worker() {
    for (;;) {
        Request req;
        {
            std::unique_lock<std::mutex> lk(s_mu);
            s_cv.wait(lk, [] {
                return s_stop || (!s_queue.empty() && !backlogFull()) || (!s_packQueue.empty() && !packBacklogFull());
            });
            if (s_stop) return;
            std::deque<Request>& q = !s_queue.empty() && !backlogFull() ? s_queue : s_packQueue;
            req = std::move(q.front());
            q.pop_front();
            if (req.kind == PACK && !s_packPending.erase(req.name)) continue;  // a stage took it first
            (req.kind == DEMAND ? s_busy : s_busyPack) = true;
        }
        std::unique_ptr<Loaded> L(decode(req.name, false));
        const size_t bytes = levelBytes(*L);
        {
            std::lock_guard<std::mutex> lk(s_mu);
            (req.kind == DEMAND ? s_busy : s_busyPack) = false;
            if (s_stop) return;
            s_decodedBytes += bytes;
            if (req.kind == DEMAND) s_decodedDemand++;
            if (req.kind == PACK) {
                s_decodedPack++;
                s_decodedPackBytes += bytes;
            }
            s_decoded.push_back({std::move(req), std::move(L), bytes});
        }
        s_cv.notify_all();
    }
}

// The completion worker, at idle priority: the levels a compressed
// replacement's file leaves out, once its own levels are in GL. Each set it
// makes is kept on disk (storeCachedLevels), so it is made once.
static void completer() {
    idlePriority();
    for (;;) {
        Request req;
        {
            std::unique_lock<std::mutex> lk(s_restMu);
            s_restCv.wait(lk, [] {
                return s_stop || (!s_restQueue.empty() && s_restDone.size() < 8 && s_restDoneBytes < pendingBudget());
            });
            if (s_stop) return;
            req = std::move(s_restQueue.front());
            s_restQueue.pop_front();
            s_busyRest = true;
        }
        std::unique_ptr<Loaded> L(decode(req.name, false, false));
        if (!L->failed) {  // only the levels the pack left out
            L->first = int(L->levels.size());
            completeChain(L.get(), true);
            L->levels.erase(L->levels.begin(), L->levels.begin() + L->first);
            storeCachedLevels(req.name, s_index.at(req.name), *L);
        }
        const size_t bytes = levelBytes(*L);
        std::lock_guard<std::mutex> lk(s_restMu);
        s_busyRest = false;
        if (s_stop) return;
        s_restDoneBytes += bytes;
        s_restDone.push_back({std::move(req), std::move(L), bytes});
    }
}

static void startWorker() {
    if (!syncLoading() && !s_worker) {
        s_stop = false;
        s_worker = new std::thread(worker);
        s_completer = new std::thread(completer);
    }
}

// The whole pack, behind what stages ask for (SMS_TEXTURE_PACK_PRELOAD=all).
static void requestPack() {
    for (const auto& kv : s_index) {
        unsigned w = 0, h = 0;
        if (sscanf(kv.first.c_str(), "tex1_%ux%u", &w, &h) == 2 && w && h) requestTexture(kv.first, w, h, PACK);
    }
}

// The completion worker makes the compressed levels a replacement's file
// leaves out after its own levels are in GL (handed over by exchangeRest).
static void completeLater(const std::string& name) { s_restLater.push_back(name); }

// GL texture names, made a batch at a time: glGenTextures returns them, so
// with the GL thread each call waits for it to finish the frame's work.
static std::vector<GLuint> s_names;
static GLuint newTexture() {
    if (s_names.empty()) {
        s_names.resize(32);
        glGenTextures(GLsizei(s_names.size()), s_names.data());
    }
    GLuint t = s_names.back();
    s_names.pop_back();
    return t;
}

// A decoded replacement (or completion) on its way into GL. A level larger
// than a piece goes in as bands of glTex(Compressed)SubImage2D, each small
// enough for the GL thread to copy rather than wait for (glt::kMaxInline),
// and outside a load a frame sends SMS_TEXTURE_PACK_UPLOAD_MB (8 by default;
// 0 for no limit) at most: a large texture arrives over a few frames instead
// of stopping one. A replacement is sampled once all its levels are in.
struct Upload {
    Decoded item;
    GLuint tex = 0;
    size_t level = 0;   // the next of item.image->levels
    size_t offset = 0;  // the bytes of it already sent
};
static std::deque<Upload>& s_uploads = *new std::deque<Upload>;  // render thread only
static std::vector<GLuint> s_fresh;  // replacements prepared during the current load (touchFresh)
static const size_t kPiece = size_t(4) << 20;
uint64_t g_statHiresBytes = 0;

static size_t frameUploadBudget() {
    static const size_t budget = [] {
        const char* e = getenv("SMS_TEXTURE_PACK_UPLOAD_MB");
        int mb = e && *e ? atoi(e) : 8;
        return mb > 0 ? size_t(mb) << 20 : SIZE_MAX;
    }();
    return budget;
}

// The replacement a completion's levels belong to, while it still has just
// the levels before them (it may have been freed or reloaded meanwhile).
static Replacement* completionTarget(const Upload& u) {
    auto it = s_repl.find(u.item.request.name);
    const Loaded* L = u.item.image.get();
    if (it == s_repl.end() || it->second.state != Replacement::READY || it->second.levels != L->first || L->failed ||
        L->levels.empty() || (u.tex && it->second.tex != u.tex))
        return nullptr;
    return &it->second;
}

// Sets up u's texture; false if there is nothing to send.
static bool beginUpload(Upload& u) {
    if (u.item.request.kind == REST) {
        Replacement* r = completionTarget(u);
        if (r) u.tex = r->tex;
        return r != nullptr;
    }
    Replacement& r = s_repl[u.item.request.name];
    if (u.item.image->failed) {
        r.state = Replacement::FAILED;
        return false;
    }
    if (r.state != Replacement::QUEUED) return false;
    if (!r.tex) r.tex = newTexture();
    u.tex = r.tex;
    return true;
}

// Sends u's levels, at least one piece and then while `sent` is under
// `allowance`; true once they are all in. A completion's levels already have
// their storage (finishUpload), so they only fill it.
static bool sendLevels(Upload& u, int unit, size_t allowance, size_t* sent) {
    Loaded* L = u.item.image.get();
    const bool fill = u.item.request.kind == REST;
    glcBindTexture(unit, u.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    bool first = true;
    while (u.level < L->levels.size()) {
        if (!first && *sent >= allowance) return false;
        first = false;
        const std::vector<uint8_t>& lv = L->levels[u.level];
        const int n = L->first + int(u.level);
        const int lw = std::max(1, L->w >> n), lh = std::max(1, L->h >> n);
        size_t bytes = lv.size();
        if (u.offset == 0 && bytes <= kPiece && !fill) {
            if (L->compressed)
                glCompressedTexImage2D(GL_TEXTURE_2D, n, L->compressed, lw, lh, 0, GLsizei(bytes), lv.data());
            else
                glTexImage2D(GL_TEXTURE_2D, n, GL_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, lv.data());
        } else {
            if (u.offset == 0 && !fill) {  // the level's storage, then its bands
                if (L->compressed)
                    glCompressedTexImage2D(GL_TEXTURE_2D, n, L->compressed, lw, lh, 0, GLsizei(lv.size()), nullptr);
                else
                    glTexImage2D(GL_TEXTURE_2D, n, GL_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
            // rows of blocks, or of pixels
            const int rowPixels = L->compressed ? 4 : 1;
            const size_t rowBytes = lv.size() / size_t((lh + rowPixels - 1) / rowPixels);
            const int y = int(u.offset / rowBytes) * rowPixels;
            bytes = std::min(std::max<size_t>(1, kPiece / rowBytes) * rowBytes, lv.size() - u.offset);
            const int h = std::min(int(bytes / rowBytes) * rowPixels, lh - y);
            if (L->compressed)
                glCompressedTexSubImage2D(GL_TEXTURE_2D, n, 0, y, lw, h, L->compressed, GLsizei(bytes), lv.data() + u.offset);
            else
                glTexSubImage2D(GL_TEXTURE_2D, n, 0, y, lw, h, GL_RGBA, GL_UNSIGNED_BYTE, lv.data() + u.offset);
        }
        u.offset += bytes;
        *sent += bytes;
        g_statHiresBytes += bytes;
        if (u.offset == lv.size()) {
            u.level++;
            u.offset = 0;
        }
    }
    return true;
}

static void finishUpload(Upload& u, int unit) {
    const Loaded* L = u.item.image.get();
    size_t bytes = 0;
    for (const auto& lv : L->levels) bytes += lv.size();
    const int levels = L->first + int(L->levels.size());
    glcBindTexture(unit, u.tex);
    if (u.item.request.kind == REST) {  // sampling reaches the new levels
        Replacement* r = completionTarget(u);
        if (!r) return;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
        r->levels = levels;
        r->bytes += bytes;
        s_bytes += bytes;
        return;
    }
    const std::string& name = u.item.request.name;
    Replacement& r = s_repl[name];
    int scale = 0;
    while (scale < 6 && (u.item.request.w << (scale + 1)) <= uint32_t(L->w) &&
           (u.item.request.h << (scale + 1)) <= uint32_t(L->h))
        scale++;
    // Sampling stops at the last level until completeLater's arrive. Their
    // storage is made now: adding levels to a texture in use later would make
    // the driver remake (and copy) all of it.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
    const int full = chainLength(L->w, L->h);
    for (int n = levels; n < full && L->compressed; n++) {
        const int lw = std::max(1, L->w >> n), lh = std::max(1, L->h >> n);
        glCompressedTexImage2D(GL_TEXTURE_2D, n, L->compressed, lw, lh, 0, GLsizei(blockLevelBytes(L->blocks, lw, lh)),
                               nullptr);
    }
    r.w = L->w;
    r.h = L->h;
    r.levels = levels;
    r.bytes = bytes;
    if (r.levels < full) completeLater(name);
    r.scale = scale;
    r.state = Replacement::READY;
    r.used = s_frame;  // keep preloaded textures through the first draw
    s_bytes += bytes;
    s_uploaded++;
    extern uint32_t g_statTexUploads;
    g_statTexUploads++;
}

// The GL texture replacing `name` (a name hiresName found in the pack), or 0
// while it is still preparing or if it cannot be read. Normal draws only
// request/look up replacements; uploads happen at frame boundaries/loading.
// SMS_TEXTURE_PACK_SYNC=1 decodes on the spot (repeatable captures).
GLuint hiresTexture(const std::string& name, int unit, uint32_t gxW, uint32_t gxH, int* scale) {
    auto it = s_repl.find(name);
    if (it == s_repl.end()) {
        s_repl[name];
        if (syncLoading()) {
            Upload u;
            u.item.request = {name, gxW, gxH, DEMAND};
            u.item.image.reset(decode(name, true));
            size_t sent = 0;
            if (beginUpload(u)) {
                sendLevels(u, unit, SIZE_MAX, &sent);
                finishUpload(u, unit);
            }
        } else {
            requestTexture(name, gxW, gxH);
        }
        it = s_repl.find(name);
    }
    if (it->second.state != Replacement::READY) return 0;
    it->second.used = s_frame;
    *scale = it->second.scale;
    return it->second.tex;
}

// Hands completeLater's names to the completion worker and takes the levels
// it has made; outside a load only when its lock is free.
static void exchangeRest(bool block) {
    std::unique_lock<std::mutex> lk(s_restMu, std::defer_lock);
    if (block) lk.lock();
    else if (!lk.try_lock()) return;
    for (std::string& name : s_restLater) s_restQueue.push_back({std::move(name), 0, 0, REST});
    bool notify = !s_restLater.empty() || !s_restDone.empty();
    s_restLater.clear();
    std::vector<Upload> done;
    while (!s_restDone.empty()) {
        done.emplace_back();
        done.back().item = std::move(s_restDone.front());
        s_restDone.pop_front();
        s_restDoneBytes -= done.back().item.bytes;
    }
    lk.unlock();
    if (notify) s_restCv.notify_all();
    for (Upload& u : done)
        if (beginUpload(u)) s_uploads.push_back(std::move(u));
}

// Takes the first decoded texture of the whole pack (`pack`), or of the rest.
static bool takeDecoded(Upload& u, bool pack) {  // with s_mu held
    auto it = std::find_if(s_decoded.begin(), s_decoded.end(),
                           [&](const Decoded& d) { return (d.request.kind == PACK) == pack; });
    if (it == s_decoded.end()) return false;
    u.item = std::move(*it);
    s_decoded.erase(it);
    s_decodedBytes -= u.item.bytes;
    if (u.item.request.kind == DEMAND) s_decodedDemand--;
    if (pack) {
        s_decodedPack--;
        s_decodedPackBytes -= u.item.bytes;
    }
    s_cv.notify_all();
    return true;
}

// Sends decoded textures into GL: all of the ones resources asked for when
// `wait` (a load: until none is left to decode), else up to a frame's
// allowance, or 2 ms. The whole pack's wait for loads (uploadPack).
static void uploadPending(bool wait) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const size_t allowance = wait ? SIZE_MAX : frameUploadBudget();
    size_t sent = 0;
    exchangeRest(wait);
    for (;;) {
        if (s_uploads.empty()) {
            Upload u;
            {
                std::unique_lock<std::mutex> lk(s_mu, std::defer_lock);
                if (wait) lk.lock();
                else if (!lk.try_lock()) break;
                auto loaded = [] { return s_decodedDemand == 0 && s_queue.empty() && !s_busy; };
                if (wait) s_cv.wait(lk, [&] { return s_decoded.size() > s_decodedPack || loaded(); });
                if ((wait && loaded()) || !takeDecoded(u, false)) break;
            }
            if (beginUpload(u)) s_uploads.push_back(std::move(u));
            continue;
        }
        Upload& u = s_uploads.front();
        if (u.item.request.kind == REST && !completionTarget(u)) {
            s_uploads.pop_front();
            continue;
        }
        if (sendLevels(u, 0, allowance, &sent)) {
            finishUpload(u, 0);
            if (wait && u.item.request.kind != REST) s_fresh.push_back(u.tex);
            s_uploads.pop_front();
        }
        if (!wait && (sent >= allowance || Clock::now() - start >= std::chrono::milliseconds(2))) break;
    }
}

// During a load, after the level's own: the whole pack's textures
// (SMS_TEXTURE_PACK_PRELOAD=all) for up to `ms`, so over the first loads the
// lot arrives without any frame sending it.
static void uploadPack(double ms) {
    using Clock = std::chrono::steady_clock;
    const auto until = Clock::now() + std::chrono::microseconds(int64_t(ms * 1000));
    size_t sent = 0;
    for (;;) {
        Upload u;
        {
            std::unique_lock<std::mutex> lk(s_mu);
            auto done = [] { return s_packQueue.empty() && !s_busyPack; };
            s_cv.wait_until(lk, until, [&] { return s_decodedPack > 0 || done(); });
            if (!takeDecoded(u, true)) break;
        }
        if (beginUpload(u)) {
            sendLevels(u, 0, SIZE_MAX, &sent);
            finishUpload(u, 0);
            s_fresh.push_back(u.tex);
        }
        if (Clock::now() >= until) break;
    }
}

// The replacements a load prepared are each drawn from once, a point into a
// 1x1 target, so the driver moves them into video memory during the load
// instead of at the level's first frame (which otherwise waits for that:
// about 60 ms for Delfino Plaza's on a GTX 1060).
static GLuint s_touchProg = 0, s_touchFbo = 0, s_touchTarget = 0, s_touchVao = 0;

static void touchFresh() {
    if (s_fresh.empty()) return;
    glcInvalidate();
    GLint fbo = 0, vp[4] = {};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_VIEWPORT, vp);
    if (!s_touchProg) {
        static const char* kVs = "#version 330 core\nvoid main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
        static const char* kFs = "#version 330 core\nuniform sampler2D t;\nout vec4 o;\n"
                                 "void main() { o = textureLod(t, vec2(0.5), 0.0); }\n";
        GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(v, 1, &kVs, nullptr);
        glShaderSource(f, 1, &kFs, nullptr);
        glCompileShader(v);
        glCompileShader(f);
        s_touchProg = glCreateProgram();
        glAttachShader(s_touchProg, v);
        glAttachShader(s_touchProg, f);
        glBindFragDataLocation(s_touchProg, 0, "o");
        glLinkProgram(s_touchProg);
        glDeleteShader(v);
        glDeleteShader(f);
        glUseProgram(s_touchProg);
        glUniform1i(glGetUniformLocation(s_touchProg, "t"), 0);
        glGenTextures(1, &s_touchTarget);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_touchTarget);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &s_touchFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, s_touchFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_touchTarget, 0);
        glGenVertexArrays(1, &s_touchVao);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s_touchFbo);
    glViewport(0, 0, 1, 1);
    glUseProgram(s_touchProg);
    glBindVertexArray(s_touchVao);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    for (GLuint t : s_fresh) {
        glBindTexture(GL_TEXTURE_2D, t);
        glDrawArrays(GL_POINTS, 0, 1);
    }
    s_fresh.clear();
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(fbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glcInvalidate();
}

void hiresPreload() {
    if (s_state != 1 || !preloading()) return;
    const auto start = std::chrono::steady_clock::now();
    uint32_t before = s_uploaded;
    uploadPending(true);
    if (preloadMode() == 2) uploadPack(1500);
    // The GL thread takes what was sent while the level still loads, so the
    // first frame does not wait for it.
    touchFresh();
    glt::finish();
    if (s_uploaded != before)
        logmsg("texture pack: prepared %u replacements before gameplay in %.1f ms (%zu MiB resident)",
               s_uploaded - before,
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), s_bytes >> 20);
}

// Once a display frame: over the memory budget (SMS_TEXTURE_PACK_MB, 1536
// by default), the replacements unused for longest are freed, down to three
// quarters of it. One sampled since the previous frame is never freed; a
// freed one is read again the next time its texture is. The whole pack
// (SMS_TEXTURE_PACK_PRELOAD=all) is kept.
void hiresEndFrame() {
    if (s_state == 1 && !syncLoading()) uploadPending(false);
    s_frame++;
    static size_t budget = 0;
    if (!budget) {
        const char* e = getenv("SMS_TEXTURE_PACK_MB");
        budget = size_t(e && atoi(e) > 0 ? atoi(e) : 1536) << 20;
    }
    if (s_bytes <= budget || preloadMode() == 2) return;
    std::vector<std::pair<uint32_t, const std::string*>> old;
    for (auto& kv : s_repl)
        if (kv.second.state == Replacement::READY && kv.second.used + 1 < s_frame) old.emplace_back(kv.second.used, &kv.first);
    std::sort(old.begin(), old.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    size_t freed = 0, n = 0;
    std::vector<std::string> names;
    for (auto& o : old) {
        if (s_bytes - freed <= budget / 4 * 3) break;
        freed += s_repl[*o.second].bytes;
        names.push_back(*o.second);
    }
    for (const std::string& nm : names) {
        Replacement& r = s_repl[nm];
        glcForgetTexture(r.tex);
        glDeleteTextures(1, &r.tex);
        s_repl.erase(nm);
        std::lock_guard<std::mutex> lk(s_mu);
        s_requested.erase(nm);
        n++;
    }
    s_bytes -= freed;
    if (n) logmsg("texture pack: freed %zu replacements (%zu MiB) over the %zu MiB budget", n, freed >> 20, budget >> 20);
}

void hiresShutdown() {
    s_state = 0;
    {
        std::lock_guard<std::mutex> lk(s_mu);
        std::lock_guard<std::mutex> lk2(s_restMu);
        s_stop = true;
    }
    s_cv.notify_all();
    s_restCv.notify_all();
    for (std::thread** t : {&s_worker, &s_completer})
        if (*t) {
            (*t)->join();
            delete *t;
            *t = nullptr;
        }
    s_queue.clear();
    s_packQueue.clear();
    s_packPending.clear();
    s_restQueue.clear();
    s_restDone.clear();
    s_restLater.clear();
    s_restDoneBytes = 0;
    s_decoded.clear();
    s_uploads.clear();
    s_requested.clear();
    s_decodedBytes = s_decodedDemand = s_decodedPack = s_decodedPackBytes = 0;
    s_busy = s_busyPack = s_busyRest = false;
    s_bytes = 0;
    for (auto& kv : s_repl)
        if (kv.second.tex) {
            glcForgetTexture(kv.second.tex);
            glDeleteTextures(1, &kv.second.tex);
        }
    s_repl.clear();
    if (!s_names.empty()) glDeleteTextures(GLsizei(s_names.size()), s_names.data());
    s_names.clear();
    s_fresh.clear();
    if (s_touchProg) {
        glDeleteProgram(s_touchProg);
        glDeleteFramebuffers(1, &s_touchFbo);
        glDeleteTextures(1, &s_touchTarget);
        glDeleteVertexArrays(1, &s_touchVao);
        s_touchProg = s_touchFbo = s_touchTarget = s_touchVao = 0;
    }
    s_index.clear();
    s_s3tc = s_bptc = false;
    s_state = -1;
}

uint32_t hiresUploadedCount() { return s_uploaded; }

HiresStats hiresStats() {
    std::lock_guard<std::mutex> lk(s_mu);
    std::lock_guard<std::mutex> lk2(s_restMu);
    size_t restUploads = 0;
    for (const Upload& u : s_uploads) restUploads += u.item.request.kind == REST;
    return {s_bytes, s_decodedBytes + s_restDoneBytes,
            s_queue.size() + s_packQueue.size() + s_decoded.size() + s_uploads.size() - restUploads + size_t(s_busy) +
                size_t(s_busyPack),
            s_uploaded,
            s_restLater.size() + s_restQueue.size() + s_restDone.size() + restUploads + size_t(s_busyRest)};
}

}  // namespace gx
