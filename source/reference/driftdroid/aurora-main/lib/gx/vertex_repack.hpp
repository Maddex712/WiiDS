#pragma once

#include "gx.hpp"

#include <vector>

// Adreno geometry workaround ("vertex explosion": stretched meshes and smeared textures on
// Snapdragon 8 Elite class GPUs). Ported from KartPad's kartpad_vertex_repack.
//
// In the normal path the vertex shader reads 1-byte matrix and array indices out of an
// odd-stride, unaligned vertex stream and then performs a second, dependent storage-buffer
// read into the attribute arrays. Qualcomm drivers get that wrong. When enabled, this module
// resolves every indexed attribute on the CPU and rewrites each vertex as 4-byte-aligned direct
// data (matrix indices in zero-padded 32-bit slots; position, normal and texcoords as
// big-endian f32; colors as their original bytes), so the shader performs only aligned,
// non-dependent reads from one buffer. The matrix palette lookup itself is unchanged.
namespace aurora::gx::vertex_repack {

// Requested mode, see AuroraSetVertexRepackMode. Only read once, at the first draw.
void set_requested_mode(int mode) noexcept;

// Process-constant decision, resolved from the requested mode and the adapter at the first draw.
bool enabled() noexcept;
bool all_draws() noexcept;

// Whether the current GX state and primitive take the repacked path.
inline bool applies(GXPrimitive prim) noexcept {
  return prim != GX_LINES && prim != GX_LINESTRIP && prim != GX_POINTS && enabled() &&
         (g_gxState.vtxDesc[GX_VA_PNMTXIDX] == GX_DIRECT || all_draws());
}

// Rewrites a source vertex layout into the repacked layout in place; returns the new stride.
u8 repacked_layout(std::array<AttrConfig, MaxVtxAttr>& attrs) noexcept;

// Repacks `count` vertices of format `fmt` (source stride `srcStride`) using the live
// attribute arrays. The returned buffer is reused by the next call on this thread.
const std::vector<u8>& repack(GXVtxFmt fmt, const u8* vertices, u16 count, u32 srcStride);

// Layout-explicit entry used by repack().
void repack_with_layout(const std::array<AttrConfig, MaxVtxAttr>& src, u32 srcStride,
                        const std::array<AttrConfig, MaxVtxAttr>& dst, u32 dstStride, const u8* vertices,
                        u16 count, const std::array<AttrArray, MaxVtxAttr>& arrays, std::vector<u8>& out);

// Rate-limited "repack active" evidence line, emitted when a repacked pipeline is first resolved.
void note_pipeline(u64 sourceHash, u64 repackedHash, u32 srcStride, u32 dstStride) noexcept;

} // namespace aurora::gx::vertex_repack
