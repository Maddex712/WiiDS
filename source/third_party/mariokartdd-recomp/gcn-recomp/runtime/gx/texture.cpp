// GX texture decoding (all formats -> RGBA8), TLUT (palette) memory, and the
// guest-side texture cache.
#include <algorithm>
#include "../runtime.h"
#include "gx.h"
#include "render.h"
#include "texture.h"
#include "texture_pack.h"
#include <unordered_map>

namespace gx {

static uint8_t g_tlut_mem[0x80000];  // TMEM upper half (palettes)
static uint32_t g_next_tex_id = 1;
uint32_t g_frame_counter;

static inline uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return r | (g << 8) | (b << 16) | (a << 24);
}
static inline uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

static inline uint32_t decode_565(uint16_t v) {
    uint32_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    return rgba((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255);
}
static inline uint32_t decode_5a3(uint16_t v) {
    if (v & 0x8000) {
        uint32_t r = (v >> 10) & 0x1F, g = (v >> 5) & 0x1F, b = v & 0x1F;
        return rgba((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255);
    }
    uint32_t a = (v >> 12) & 7, r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
    return rgba(r * 17, g * 17, b * 17, (a << 5) | (a << 2) | (a >> 1));
}
static inline uint32_t decode_ia8(uint16_t v) {
    uint32_t a = v >> 8, i = v & 0xFF;
    return rgba(i, i, i, a);
}

static inline uint32_t decode_tlut(const uint8_t* pal, uint32_t idx, uint32_t tlut_fmt) {
    uint16_t v = be16(pal + idx * 2);
    switch (tlut_fmt) {
    case 0: return decode_ia8(v);
    case 1: return decode_565(v);
    default: return decode_5a3(v);
    }
}

struct FmtInfo { uint32_t bw, bh, bpp; };  // block width/height in texels, bits per texel
static FmtInfo fmt_info(uint32_t fmt) {
    switch (fmt) {
    case TF_I4: case TF_CI4: case TF_CMPR: return {8, 8, 4};
    case TF_I8: case TF_IA4: case TF_CI8: return {8, 4, 8};
    case TF_IA8: case TF_RGB565: case TF_RGB5A3: case TF_CI14X2: return {4, 4, 16};
    case TF_RGBA8: return {4, 4, 32};
    default: return {8, 8, 4};
    }
}

uint32_t texture_size_bytes(uint32_t fmt, uint32_t w, uint32_t h) {
    FmtInfo fi = fmt_info(fmt);
    uint32_t bx = (w + fi.bw - 1) / fi.bw, by = (h + fi.bh - 1) / fi.bh;
    return bx * by * fi.bw * fi.bh * fi.bpp / 8;
}

static void decode_dxt_block(const uint8_t* src, uint32_t* dst, uint32_t stride, uint32_t w, uint32_t h, uint32_t x0, uint32_t y0) {
    uint16_t c0 = be16(src), c1 = be16(src + 2);
    uint32_t pal[4];
    pal[0] = decode_565(c0);
    pal[1] = decode_565(c1);
    auto ch = [](uint32_t c, int s) { return (c >> s) & 0xFF; };
    if (c0 > c1) {
        pal[2] = rgba((2 * ch(pal[0], 0) + ch(pal[1], 0)) / 3, (2 * ch(pal[0], 8) + ch(pal[1], 8)) / 3,
                      (2 * ch(pal[0], 16) + ch(pal[1], 16)) / 3, 255);
        pal[3] = rgba((ch(pal[0], 0) + 2 * ch(pal[1], 0)) / 3, (ch(pal[0], 8) + 2 * ch(pal[1], 8)) / 3,
                      (ch(pal[0], 16) + 2 * ch(pal[1], 16)) / 3, 255);
    } else {
        pal[2] = rgba((ch(pal[0], 0) + ch(pal[1], 0)) / 2, (ch(pal[0], 8) + ch(pal[1], 8)) / 2,
                      (ch(pal[0], 16) + ch(pal[1], 16)) / 2, 255);
        pal[3] = rgba((ch(pal[0], 0) + ch(pal[1], 0)) / 2, (ch(pal[0], 8) + ch(pal[1], 8)) / 2,
                      (ch(pal[0], 16) + ch(pal[1], 16)) / 2, 0);
    }
    for (uint32_t y = 0; y < 4; y++) {
        uint8_t bits = src[4 + y];
        for (uint32_t x = 0; x < 4; x++) {
            uint32_t px = x0 + x, py = y0 + y;
            if (px < w && py < h) dst[py * stride + px] = pal[(bits >> (6 - 2 * x)) & 3];
        }
    }
}

// Decode one mip level. `src` points to the level's data in guest memory.
static void decode_level(const uint8_t* src, uint32_t fmt, uint32_t w, uint32_t h, const uint8_t* pal,
                         uint32_t tlut_fmt, uint32_t* dst) {
    FmtInfo fi = fmt_info(fmt);
    uint32_t bx_count = (w + fi.bw - 1) / fi.bw, by_count = (h + fi.bh - 1) / fi.bh;
    for (uint32_t by = 0; by < by_count; by++) {
        for (uint32_t bx = 0; bx < bx_count; bx++) {
            uint32_t x0 = bx * fi.bw, y0 = by * fi.bh;
            auto put = [&](uint32_t x, uint32_t y, uint32_t c) {
                uint32_t px = x0 + x, py = y0 + y;
                if (px < w && py < h) dst[py * w + px] = c;
            };
            switch (fmt) {
            case TF_I4:
                for (uint32_t y = 0; y < 8; y++)
                    for (uint32_t x = 0; x < 8; x++) {
                        uint8_t b = src[y * 4 + x / 2];
                        uint32_t i = (x & 1) ? (b & 0xF) : (b >> 4);
                        i *= 17;
                        put(x, y, rgba(i, i, i, i));
                    }
                src += 32;
                break;
            case TF_I8:
                for (uint32_t y = 0; y < 4; y++)
                    for (uint32_t x = 0; x < 8; x++) {
                        uint32_t i = src[y * 8 + x];
                        put(x, y, rgba(i, i, i, i));
                    }
                src += 32;
                break;
            case TF_IA4:
                for (uint32_t y = 0; y < 4; y++)
                    for (uint32_t x = 0; x < 8; x++) {
                        uint8_t b = src[y * 8 + x];
                        uint32_t a = (b >> 4) * 17, i = (b & 0xF) * 17;
                        put(x, y, rgba(i, i, i, a));
                    }
                src += 32;
                break;
            case TF_IA8:
                for (uint32_t y = 0; y < 4; y++)
                    for (uint32_t x = 0; x < 4; x++) put(x, y, decode_ia8(be16(src + (y * 4 + x) * 2)));
                src += 32;
                break;
            case TF_RGB565:
                for (uint32_t y = 0; y < 4; y++)
                    for (uint32_t x = 0; x < 4; x++) put(x, y, decode_565(be16(src + (y * 4 + x) * 2)));
                src += 32;
                break;
            case TF_RGB5A3:
                for (uint32_t y = 0; y < 4; y++)
                    for (uint32_t x = 0; x < 4; x++) put(x, y, decode_5a3(be16(src + (y * 4 + x) * 2)));
                src += 32;
                break;
            case TF_RGBA8:
                for (uint32_t y = 0; y < 4; y++)
                    for (uint32_t x = 0; x < 4; x++) {
                        uint32_t k = (y * 4 + x) * 2;
                        uint32_t a = src[k], r = src[k + 1], g = src[32 + k], b = src[32 + k + 1];
                        put(x, y, rgba(r, g, b, a));
                    }
                src += 64;
                break;
            case TF_CI4:
                for (uint32_t y = 0; y < 8; y++)
                    for (uint32_t x = 0; x < 8; x++) {
                        uint8_t b = src[y * 4 + x / 2];
                        put(x, y, decode_tlut(pal, (x & 1) ? (b & 0xF) : (b >> 4), tlut_fmt));
                    }
                src += 32;
                break;
            case TF_CI8:
                for (uint32_t y = 0; y < 4; y++)
                    for (uint32_t x = 0; x < 8; x++) put(x, y, decode_tlut(pal, src[y * 8 + x], tlut_fmt));
                src += 32;
                break;
            case TF_CI14X2:
                for (uint32_t y = 0; y < 4; y++)
                    for (uint32_t x = 0; x < 4; x++)
                        put(x, y, decode_tlut(pal, be16(src + (y * 4 + x) * 2) & 0x3FFF, tlut_fmt));
                src += 32;
                break;
            case TF_CMPR:
                for (uint32_t sb = 0; sb < 4; sb++) {
                    decode_dxt_block(src, dst, w, w, h, x0 + (sb & 1) * 4, y0 + (sb >> 1) * 4);
                    src += 8;
                }
                break;
            default:
                for (uint32_t y = 0; y < fi.bh; y++)
                    for (uint32_t x = 0; x < fi.bw; x++) put(x, y, rgba(255, 0, 255, 255));
                src += fi.bw * fi.bh * fi.bpp / 8;
                break;
            }
        }
    }
}

// FNV-1a over 8-byte words, for texture change detection. Every texture a frame samples
// is hashed once per frame, several megabytes in a race, and a single FNV chain is bound
// by the latency of its multiply: four independent lanes run in parallel and are folded
// together at the end, which is about three times the throughput on the same data.
static uint64_t hash_bytes(const uint8_t* p, size_t n, uint64_t seed) {
    constexpr uint64_t kPrime = 0x100000001b3ull;
    uint64_t h0 = 0xcbf29ce484222325ull ^ seed, h1 = h0 ^ 0x9e3779b97f4a7c15ull,
             h2 = h0 ^ 0x3c6ef372fe94f82bull, h3 = h0 ^ 0xdaa66d2b78dd3b40ull;
    size_t i = 0;
    for (; i + 32 <= n; i += 32) {
        uint64_t w0, w1, w2, w3;
        memcpy(&w0, p + i, 8);
        memcpy(&w1, p + i + 8, 8);
        memcpy(&w2, p + i + 16, 8);
        memcpy(&w3, p + i + 24, 8);
        h0 = (h0 ^ w0) * kPrime; h0 ^= h0 >> 29;
        h1 = (h1 ^ w1) * kPrime; h1 ^= h1 >> 29;
        h2 = (h2 ^ w2) * kPrime; h2 ^= h2 >> 29;
        h3 = (h3 ^ w3) * kPrime; h3 ^= h3 >> 29;
    }
    uint64_t h = (h0 ^ (h1 * kPrime)) ^ ((h2 ^ (h3 * kPrime)) * kPrime);
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        h = (h ^ w) * kPrime;
        h ^= h >> 29;
    }
    for (; i < n; i++) h = (h ^ p[i]) * kPrime;
    return h;
}

void tlut_load(uint32_t src_addr, uint32_t tmem_off, uint32_t bytes) {
    if (tmem_off + bytes > sizeof(g_tlut_mem)) bytes = sizeof(g_tlut_mem) - tmem_off;
    memcpy(g_tlut_mem + tmem_off, phys_ptr(src_addr), bytes);
}

// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------
// A cached texture is its texels -- keyed by where they are, their format and shape --
// plus, for the indexed formats, one decode per palette those texels have been drawn
// with. The palette is identified by the entries the texels actually use, because a
// game loads what it likes: one game's font is CI4, uses four entries, and loads a
// sixteen-entry palette whose other twelve are whatever lies past its table in memory,
// different on every load. Hashing the whole palette made each of those loads a new
// texture; keying on the palette's TMEM slot alone (as this once did) made the game's
// two passes over the font -- a palette that renders the glyphs black for the drop
// shadow, then one that renders them white -- share a decode, and the shadow came out
// white. Each pass lands in the same frame, so the variants have to coexist.
struct Variant {
    uint64_t pal_hash;
    std::shared_ptr<TexData> tex;
};
struct CacheEntry {
    uint64_t hash;             // the texels
    uint32_t last_frame;
    uint32_t checked_frame;
    uint64_t used[4];          // palette indices the texels reference (CI4, CI8)
    std::vector<Variant> variants;
};
static void drop_variants(CacheEntry& e);  // below, with Batch::dead_textures
static void trim_variants(CacheEntry& e);
struct EfbCopyEntry {
    std::shared_ptr<TexData> tex;
    uint32_t last_frame;
    uint32_t checked_frame;
    uint32_t w, h, fmt;
    uint64_t ram;              // efb_copy_fingerprint of the destination when copied
};

struct Key {
    uint32_t addr, fmt, w, h, levels, tlut_off, tlut_fmt;
    bool operator==(const Key& o) const { return memcmp(this, &o, sizeof(Key)) == 0; }
};
struct KeyHash {
    size_t operator()(const Key& k) const { return hash_bytes((const uint8_t*)&k, sizeof(k), 0); }
};

static std::unordered_map<Key, CacheEntry, KeyHash> g_cache;
static std::unordered_map<uint32_t, EfbCopyEntry> g_efb_copies;  // by address

// A copy is kept on the GPU and never written to RAM, so the RAM at its destination is
// whatever was there before. If that changes, the game has put something else there -- a
// texture loaded into memory the copy's buffer used to occupy -- and the address no longer
// names the copy. Without this check a lookup kept returning the copy, and kept it from
// expiring, so the new texture was drawn as the old copy for as long as it was in use.
// The fingerprint covers w*h/2 bytes, the smallest copy (4 bits a texel): the first 4 KB
// in full and a word every 256 bytes after that, which is enough for anything that
// rewrites the region and costs a few kilobytes a frame per copy.
static uint64_t efb_copy_fingerprint(uint32_t addr, uint32_t w, uint32_t h) {
    uint32_t n = w * h / 2;
    if (addr >= RAM_SIZE) return 0;
    if (n > RAM_SIZE - addr) n = RAM_SIZE - addr;
    const uint8_t* p = mem_ptr(0x80000000u | addr);
    const uint32_t head = n < 4096 ? n : 4096;
    uint64_t h64 = hash_bytes(p, head, n);
    for (uint32_t o = head; o + 8 <= n; o += 256) h64 = hash_bytes(p + o, 8, h64);
    return h64;
}

// The id a copy of this geometry to this address was last given, so that it keeps it. A
// copy to the same place with the same geometry is the same target being redrawn, and the
// renderer re-renders into the texture it already has; minting a fresh id every frame
// instead stranded one GL texture per copy per frame -- around 2.5 MB a frame here, since
// EFB textures are exempt from eviction. Kept apart from g_efb_copies, which holds only
// the latest copy at an address: a game that copies several targets of different sizes
// through one scratch buffer every frame (a reflection, then a depth copy of the scene,
// then a sliver of it) replaced the entry at that address with each, and every copy was
// new again the next frame, five GL textures allocated and later freed per frame.
struct EfbCopyIdKey {
    uint32_t addr, w, h, fmt;
    bool operator==(const EfbCopyIdKey& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
};
struct EfbCopyIdKeyHash {
    size_t operator()(const EfbCopyIdKey& k) const { return hash_bytes((const uint8_t*)&k, sizeof(k), 0); }
};
struct EfbCopyId {
    std::shared_ptr<TexData> tex;
    uint32_t last_frame;
};
static std::unordered_map<EfbCopyIdKey, EfbCopyId, EfbCopyIdKeyHash> g_efb_copy_ids;

uint32_t texture_register_efb_copy(uint32_t addr, uint32_t w, uint32_t h, uint32_t fmt) {
    addr &= 0x03FFFFFF;
    auto it = g_efb_copies.find(addr);
    if (it != g_efb_copies.end() && it->second.w == w && it->second.h == h && it->second.fmt == fmt) {
        it->second.last_frame = it->second.checked_frame = g_frame_counter;
        it->second.ram = efb_copy_fingerprint(addr, w, h);
        return it->second.tex->id;
    }
    EfbCopyEntry e{};
    EfbCopyId& id = g_efb_copy_ids[EfbCopyIdKey{addr, w, h, fmt}];
    if (!id.tex) {
        id.tex = std::make_shared<TexData>();
        id.tex->id = g_next_tex_id++;
        id.tex->width = w;
        id.tex->height = h;
    }
    id.last_frame = g_frame_counter;
    e.tex = id.tex;
    e.w = w; e.h = h; e.fmt = fmt;
    e.last_frame = e.checked_frame = g_frame_counter;
    e.ram = efb_copy_fingerprint(addr, w, h);
    g_efb_copies[addr] = e;
    return e.tex->id;
}

// Which palette entries the texels of an indexed texture reference.
static void scan_used_indices(const uint8_t* src, uint32_t total, uint32_t fmt, uint64_t used[4]) {
    memset(used, 0, 4 * sizeof(uint64_t));
    if (fmt == TF_CI4) {
        uint32_t m = 0;
        for (uint32_t i = 0; i < total; i++) m |= (1u << (src[i] >> 4)) | (1u << (src[i] & 15));
        used[0] = m;
    } else if (fmt == TF_CI8) {
        for (uint32_t i = 0; i < total; i++) used[src[i] >> 6] |= 1ull << (src[i] & 63);
    } else {
        // CI14X2: 16384 entries; the whole palette stands for it.
        used[0] = used[1] = used[2] = used[3] = ~0ull;
    }
}

// An EFB copy read back through a palette. A game can copy the frame as intensity and then
// bind the same memory as CI4 or CI8 with a TLUT loaded: each texel's intensity becomes an
// index, and the palette turns the picture into false colour -- a heat map, say. On
// hardware this is just memory read under another format. Here a copy stays on the GPU in
// the shape it was copied in, so the renderer does the lookup itself: the palette is
// decoded into a 16x1 or 256x1 texture beside the copy, and the shader turns the copy's
// value into the index (shadergen's efb_pal_mask). Only a copy with as many bits a texel
// as the index is read this way (a 4-bit copy as CI4, an 8-bit one as CI8); any other
// pairing reorders texels within a block and is left as the copy.
struct PalEntry {
    std::shared_ptr<TexData> tex;
    uint32_t last_frame;
};
static std::unordered_map<uint64_t, PalEntry> g_efb_palettes;  // by palette contents

static uint32_t copy_bits(uint32_t fmt) {
    switch (fmt & 15) {
    case 0: return 4;                                    // I4, R4
    case 1: case 7: case 8: case 9: case 10: return 8;   // I8, A8, R8, G8, B8
    default: return 0;
    }
}

static void efb_copy_palette(const TexParams& p, uint32_t copy_fmt, TexLookup& r,
                             std::vector<std::shared_ptr<TexData>>& new_textures) {
    const uint32_t bits = p.fmt == TF_CI4 ? 4 : p.fmt == TF_CI8 ? 8 : 0;
    if (!bits || copy_bits(copy_fmt) != bits) return;
    const uint32_t entries = 1u << bits;
    if (p.tlut_off + entries * 2 > sizeof(g_tlut_mem)) return;
    const uint8_t* pal = g_tlut_mem + p.tlut_off;
    const uint64_t key = hash_bytes(pal, entries * 2, (uint64_t)p.tlut_fmt << 8 | bits);
    PalEntry& e = g_efb_palettes[key];
    if (!e.tex) {
        e.tex = std::make_shared<TexData>();
        e.tex->id = g_next_tex_id++;
        e.tex->width = entries;
        e.tex->height = 1;
        e.tex->hash = key;
        std::vector<uint32_t> px(entries);
        for (uint32_t i = 0; i < entries; i++) px[i] = decode_tlut(pal, i, p.tlut_fmt);
        e.tex->levels.push_back(std::move(px));
        new_textures.push_back(e.tex);
    }
    e.last_frame = g_frame_counter;
    r.pal_id = e.tex->id;
    r.pal_bits = bits;
}

TexLookup texture_lookup(const TexParams& p, std::vector<std::shared_ptr<TexData>>& new_textures) {
    uint32_t addr = p.addr & 0x03FFFFFF;
    // EFB copies at this address take priority (the copy isn't written back to RAM),
    // unless the RAM there has changed since, which means the copy has been replaced.
    static const bool hash_always = getenv("GCN_TEXHASH_ALWAYS") != nullptr;
    auto eit = g_efb_copies.find(addr);
    if (eit != g_efb_copies.end()) {
        EfbCopyEntry& c = eit->second;
        if (c.checked_frame != g_frame_counter || hash_always) {
            c.checked_frame = g_frame_counter;
            if (efb_copy_fingerprint(addr, c.w, c.h) != c.ram) {
                g_efb_copies.erase(eit);
                eit = g_efb_copies.end();
            }
        }
        if (eit != g_efb_copies.end()) {
            c.last_frame = g_frame_counter;
            TexLookup r{c.tex->id, true};
            r.efb_fmt = c.fmt;
            efb_copy_palette(p, c.fmt, r, new_textures);
            return r;
        }
    }
    uint32_t tlut_entries = 0;
    if (p.fmt == TF_CI4) tlut_entries = 16;
    else if (p.fmt == TF_CI8) tlut_entries = 256;
    else if (p.fmt == TF_CI14X2) tlut_entries = 16384;
    const bool indexed = tlut_entries != 0;
    const uint32_t tlut_off = indexed && p.tlut_off < sizeof(g_tlut_mem) ? p.tlut_off : 0;
    const uint8_t* pal = g_tlut_mem + tlut_off;
    if (indexed) tlut_entries = std::min<uint32_t>(tlut_entries, (uint32_t)(sizeof(g_tlut_mem) - tlut_off) / 2);

    Key k{addr, p.fmt, p.width, p.height, p.levels, indexed ? tlut_off : 0, indexed ? p.tlut_fmt : 0};
    CacheEntry& e = g_cache[k];
    e.last_frame = g_frame_counter;

    // Compute data size over all levels.
    uint32_t total = 0, w = p.width, h = p.height;
    for (uint32_t l = 0; l < p.levels; l++) {
        total += texture_size_bytes(p.fmt, w, h);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    if (addr + total > RAM_SIZE) return {0, false};
    const uint8_t* src = phys_ptr(addr);

    // The texels are checked once per frame, on the assumption that the game does not
    // rewrite a texture between two draws of the same frame. GCN_TEXHASH_ALWAYS=1 checks
    // on every lookup instead; if an artifact disappears under it, the game is doing
    // exactly that and the texture needs telling apart some other way.
    if (e.variants.empty() || e.checked_frame != g_frame_counter || hash_always) {
        const uint64_t hsh = hash_bytes(src, total, 0);
        e.checked_frame = g_frame_counter;
        if (e.variants.empty() || hsh != e.hash) {
            drop_variants(e);
            e.hash = hsh;
            if (indexed) scan_used_indices(src, total, p.fmt, e.used);
        }
    }

    // The palette, as far as these texels can see it.
    uint64_t pal_hash = 0;
    if (indexed) {
        if (p.fmt == TF_CI14X2) {
            pal_hash = hash_bytes(pal, tlut_entries * 2, 0);
        } else {
            for (uint32_t i = 0; i < tlut_entries; i++)
                if (e.used[i >> 6] & (1ull << (i & 63))) pal_hash = hash_bytes(pal + i * 2, 2, pal_hash ^ i);
        }
    }
    // Replacement names include a contiguous palette span, including gaps not sampled
    // by the original texture. Invalidate those cache entries too when packs are enabled.
    const uint64_t render_pal_hash = pal_hash;
    static const bool managed_packs = [] {
        const char* paths = getenv("WIIDS_NATIVE_TEXTURE_PATHS");
        return paths && *paths;
    }();
    if (indexed && managed_packs) pal_hash = wiids::textures::paletteIdentity(pal_hash, src,
        texture_size_bytes(p.fmt, p.width, p.height), p.fmt, pal, tlut_entries * 2);
    for (const Variant& v : e.variants)
        if (v.pal_hash == pal_hash) return {v.tex->id, false};

    if (managed_packs) trim_variants(e);
    auto td = std::make_shared<TexData>();
    td->id = g_next_tex_id++;
    td->width = p.width;
    td->height = p.height;
    td->hash = e.hash ^ (render_pal_hash * 0x9E3779B97F4A7C15ull);
    static wiids::textures::Pack pack;
    wiids::textures::Image replacement;
    if (pack.replace(src, texture_size_bytes(p.fmt, p.width, p.height), p.fmt, p.width, p.height,
                     p.levels, indexed ? pal : nullptr, tlut_entries * 2, td, replacement)) {
        td->width = replacement.width;
        td->height = replacement.height;
        td->levels = std::move(replacement.levels);
        e.variants.push_back({pal_hash, td});
        new_textures.push_back(td);
        return {td->id, false};
    }
    w = p.width; h = p.height;
    for (uint32_t l = 0; l < p.levels; l++) {
        std::vector<uint32_t> px(w * h);
        decode_level(src, p.fmt, w, h, pal, p.tlut_fmt, px.data());
        td->levels.push_back(std::move(px));
        src += texture_size_bytes(p.fmt, w, h);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    e.variants.push_back({pal_hash, td});
    new_textures.push_back(td);
    // GCN_TEXLOG=1 reports every texture decoded: where it came from, its shape, and how
    // much of its top level is anything but black -- which tells a texture the game never
    // wrote (all zero) from one the shader is mishandling.
    static const bool texlog = getenv("GCN_TEXLOG") != nullptr;
    if (texlog) {
        uint32_t nonzero = 0;
        for (uint32_t v : td->levels[0]) nonzero += (v & 0x00FFFFFF) != 0;
        fprintf(stderr, "[tex] f%u id=%u hash=%016llx addr=%08X fmt=%u %ux%u levels=%u nonblack=%u/%zu",
                g_frame_counter, td->id, (unsigned long long)td->hash, addr, p.fmt, p.width, p.height, p.levels,
                nonzero, td->levels[0].size());
        if (indexed) {
            // The palette entries the texels use, decoded: which ones a glyph lands on is
            // what decides what colour it comes out.
            fprintf(stderr, " tlut@%X fmt=%u variant=%zu:", p.tlut_off, p.tlut_fmt, e.variants.size());
            for (uint32_t i = 0; i < 16 && i < tlut_entries; i++)
                if (p.fmt == TF_CI14X2 || (e.used[i >> 6] & (1ull << (i & 63))))
                    fprintf(stderr, " %u=%08X", i, decode_tlut(pal, i, p.tlut_fmt));
        }
        fprintf(stderr, "\n");
    }
    return {td->id, false};
}

void texture_invalidate_efb_copy(uint32_t addr) { g_efb_copies.erase(addr & 0x03FFFFFF); }

// Decoded textures let go of, for the renderer (Batch::dead_textures). EFB copies are not
// here: the renderer makes their GL textures itself and ages them itself.
static std::vector<uint32_t> g_dead;
static void trim_variants(CacheEntry& e) {
    // Retired IDs are freed by the renderer a batch late, after their last draw.
    if (e.variants.size() < 16) return;
    g_dead.push_back(e.variants.front().tex->id);
    e.variants.erase(e.variants.begin());
}
static void drop_variants(CacheEntry& e) {
    for (const Variant& v : e.variants) g_dead.push_back(v.tex->id);
    e.variants.clear();
}
void texture_take_dead(std::vector<uint32_t>& out) {
    out.insert(out.end(), g_dead.begin(), g_dead.end());
    g_dead.clear();
}

// Drop decoded textures the game has stopped referencing. Each entry holds every mip
// level as RGBA8, so a long race would otherwise accumulate hundreds of megabytes.
void texture_evict() {
    static constexpr uint32_t kIdleFrames = 240;  // ~8 s at 30 fps
    if ((g_frame_counter & 63) != 0) return;
    for (auto it = g_cache.begin(); it != g_cache.end();) {
        if (g_frame_counter - it->second.last_frame > kIdleFrames) {
            drop_variants(it->second);
            it = g_cache.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = g_efb_palettes.begin(); it != g_efb_palettes.end();) {
        if (g_frame_counter - it->second.last_frame > kIdleFrames) {
            g_dead.push_back(it->second.tex->id);
            it = g_efb_palettes.erase(it);
        } else {
            ++it;
        }
    }
    // EFB copies. A copy is never written to RAM, so one dropped while the game still
    // means to use it cannot be had again: the address decodes as whatever RAM holds there.
    // A game can render a set of textures once -- sixty frames of a spinning emblem, made
    // while a menu loads -- and show them long after, and dropping copies idle for a few
    // seconds lost the lot before the first was drawn. So an idle copy goes only when the
    // RAM at its destination has changed (the game has put something else there), or when
    // there are more than kMaxCopies, oldest first: a game can also copy out dozens of
    // sprites a frame to addresses that rotate, and that map would otherwise grow without
    // bound, with a GL texture per entry.
    static constexpr size_t kMaxCopies = 512;
    for (auto it = g_efb_copies.begin(); it != g_efb_copies.end();) {
        const EfbCopyEntry& c = it->second;
        if (g_frame_counter - c.last_frame > kIdleFrames && efb_copy_fingerprint(it->first, c.w, c.h) != c.ram)
            it = g_efb_copies.erase(it);
        else
            ++it;
    }
    if (g_efb_copies.size() > kMaxCopies) {
        std::vector<std::pair<uint32_t, uint32_t>> by_age;  // (last frame, address)
        for (auto& kv : g_efb_copies) by_age.push_back({kv.second.last_frame, kv.first});
        std::sort(by_age.begin(), by_age.end());
        for (size_t i = 0; i < by_age.size() - kMaxCopies; i++) g_efb_copies.erase(by_age[i].second);
    }
    // A copy's id outlives its entry for a while (see g_efb_copy_ids), but not an entry
    // that still names it. When it goes, its GL texture goes with it: the front end alone
    // decides when a copy is dead, as for decoded textures.
    for (auto it = g_efb_copy_ids.begin(); it != g_efb_copy_ids.end();) {
        auto live = g_efb_copies.find(it->first.addr);
        const bool held = live != g_efb_copies.end() && live->second.tex == it->second.tex;
        if (!held && g_frame_counter - it->second.last_frame > kIdleFrames) {
            g_dead.push_back(it->second.tex->id);
            it = g_efb_copy_ids.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace gx
