#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace gx {

struct TexData;

enum TexFmt : uint32_t {
    TF_I4 = 0, TF_I8 = 1, TF_IA4 = 2, TF_IA8 = 3, TF_RGB565 = 4, TF_RGB5A3 = 5, TF_RGBA8 = 6,
    TF_CI4 = 8, TF_CI8 = 9, TF_CI14X2 = 10, TF_CMPR = 14,
};

struct TexParams {
    uint32_t addr, fmt, width, height, levels;
    uint32_t tlut_off, tlut_fmt;
};

struct TexLookup {
    uint32_t id;
    bool efb;
    // For an EFB copy: the format it was copied in (EfbCopyCmd::format, +0x10 when an
    // intensity copy), and, when the game reads it back through a palette (bound as CI4
    // or CI8 over a copy of as many bits a texel), that palette decoded as a 16x1 or
    // 256x1 texture and the index's width in bits. 0 otherwise.
    uint32_t efb_fmt = 0;
    uint32_t pal_id = 0;
    uint32_t pal_bits = 0;
};

extern uint32_t g_frame_counter;

TexLookup texture_lookup(const TexParams& p, std::vector<std::shared_ptr<TexData>>& new_textures);
uint32_t texture_register_efb_copy(uint32_t addr, uint32_t w, uint32_t h, uint32_t fmt);
void texture_invalidate_efb_copy(uint32_t addr);

// Drop decoded textures the game has stopped referencing. Call once per frame.
void texture_evict();
// Moves the ids of textures dropped since the last call into `out` (see Batch::dead_textures).
void texture_take_dead(std::vector<uint32_t>& out);
void tlut_load(uint32_t src_addr, uint32_t tmem_off, uint32_t bytes);
uint32_t texture_size_bytes(uint32_t fmt, uint32_t w, uint32_t h);

}  // namespace gx
