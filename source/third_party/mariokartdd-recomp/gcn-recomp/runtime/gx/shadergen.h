#pragma once
#include "render.h"
#include <cstring>
#include <string>

namespace gx {

struct ShaderKey {
    uint32_t genmode;
    uint32_t order[8];
    uint32_t cenv[16], aenv[16];
    uint32_t indcmd[16];
    uint32_t ksel[8];
    uint32_t alpha_func;
    uint32_t iref;
    uint32_t fog;
    uint32_t num_texgens;
    uint32_t efb_has_alpha;
    uint32_t efb_tex_mask;
    // EFB copies read back through a palette (PixelState::tex_pal): bit m for a map whose
    // copy is indexed by 4 bits (CI4), bit 8+m by 8 (CI8).
    uint32_t efb_pal_mask;
    // How each EFB map's copy was converted from the frame's colour, 4 bits a map
    // (EfbConv), for an eye that samples its own grab of the frame in the copy's place:
    // the grab is colour and has to be converted the same way.
    uint32_t efb_conv;
    uint32_t ztex;  // bp[0xF5]: the Z texture's format (bits 0-1) and operation (bits 2-3); 0 when off
    uint32_t dst_alpha;  // dst_alpha_mode(): 0 off, 1 the constant alpha, 2 that and the TEV's as source 1
    // Set by the renderer, not make_shader_key: a draw an eye gives a foreground layer's game
    // depth (u_vr 4), whose depth is then worked out per fragment (see the vertex shader).
    uint32_t fg_depth;
    bool operator==(const ShaderKey& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
};

struct ShaderKeyHash {
    size_t operator()(const ShaderKey& k) const {
        const uint32_t* p = (const uint32_t*)&k;
        size_t h = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(k) / 4; i++) h = (h ^ p[i]) * 1099511628211ull;
        return h;
    }
};

// The conversions an EFB colour copy makes that the shader repeats on an eye's grab:
// none, a 4-bit intensity (I4), an 8-bit one (I8), or intensity with the frame's alpha
// (IA4, IA8); or one channel of the frame's -- its alpha (A8), its red at 4 bits (R4) or 8
// (R8), its green or blue -- in every component, or two of them read back as IA8 (RG8, GB8),
// or the colour made opaque (RGB565). See kCopyFS in render_gl.cpp, which makes them for
// the copy itself.
enum EfbConv : uint32_t {
    EfbConvNone = 0, EfbConvI4 = 1, EfbConvI8 = 2, EfbConvIA = 3, EfbConvA8 = 4, EfbConvR4 = 5, EfbConvR8 = 6,
    EfbConvG8 = 7, EfbConvB8 = 8, EfbConvRG8 = 9, EfbConvGB8 = 10, EfbConvOpaque = 11,
};
EfbConv efb_conv_for(uint32_t copy_fmt);
ShaderKey make_shader_key(const PixelState& st);
// Clears what gen_pixel_shader does not read, so that keys generating the same source are
// equal. make_shader_key's keys already are; a key read from an older cache file is not.
void canonicalize_shader_key(ShaderKey& k);
// The konstant each of the 16 stages reads (u_kstage), from its selectors (bp[0xF6..0xFD])
// and the konstant registers as the shader takes them (u_konst's layout: four registers,
// r g b a each).
void stage_konsts(const uint32_t* bp, const int konst[16], int out[64]);

// Bumped whenever a key field's meaning or the shader cache's record layout changes. With
// the key's size beside it, so that a field added to ShaderKey counts as a change by itself,
// it is the format of both the cache file (render_gl.cpp) and a game's key list
// (render_set_shader_keys, tools/shader_keys.cpp), and either one in another format is
// not read.
constexpr uint32_t kShaderKeyVersion = 3;
constexpr uint32_t kShaderKeyFormat = kShaderKeyVersion << 16 | (uint32_t)sizeof(ShaderKey);
// A key list: this, kShaderKeyFormat, then the canonical keys, each once.
constexpr uint32_t kShaderKeysMagic = 0x4B534347;  // "GCSK"
// Whether the GL has dual-source blending (core in desktop 3.3, GL_EXT_blend_func_extended on
// ES); set by render_init before any shader is made.
extern bool g_dual_source;
// GXSetDstAlpha (bp[0x42]: the alpha in bits 0-7, enabled by bit 8): with alpha updates on
// and an EFB that has alpha, the alpha written is that constant rather than the TEV's,
// which still decides the alpha test and the blend. 0 when it does not apply; 1 when the
// shader writes the constant as its alpha and the blend reads no source alpha; 2 when the
// blend does read it, which then comes from a second output (dual-source blending). Where
// the GL has no dual-source blending, case 2 is left as 0 and the TEV's alpha is stored.
uint32_t dst_alpha_mode(const uint32_t* bp);
std::string gen_vertex_shader();
// GCN_GLSL_VERSION, plus what writing gl_ClipDistance needs; defines GCN_CLIP where it can.
std::string glsl_header_with_clip();
std::string gen_pixel_shader(const ShaderKey& k);

}  // namespace gx
