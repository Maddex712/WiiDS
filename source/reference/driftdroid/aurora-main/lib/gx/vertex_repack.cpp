#include "vertex_repack.hpp"

#include "../internal.hpp"
#include "../webgpu/gpu.hpp"

#include <dolphin/gx/GXAurora.h>

#include <atomic>
#include <bit>
#include <cstring>

namespace aurora::gx::vertex_repack {
namespace {
Module Log("aurora::gx::fifo");

std::atomic<int> g_requestedMode{AURORA_VERTEX_REPACK_OFF};

bool is_index_attr(u32 attr) noexcept {
  return attr == GX_VA_PNMTXIDX || (attr >= GX_VA_TEX0MTXIDX && attr <= GX_VA_TEX7MTXIDX);
}
bool is_color_attr(u32 attr) noexcept { return attr == GX_VA_CLR0 || attr == GX_VA_CLR1; }

u32 color_size(u8 type) noexcept {
  switch (type) {
  case GX_RGB565:
  case GX_RGBA4:
    return 2;
  case GX_RGB8:
  case GX_RGBA6:
    return 3;
  default:
    return 4;
  }
}

u32 comp_size(u8 type) noexcept {
  switch (type) {
  case GX_U8:
  case GX_S8:
    return 1;
  case GX_U16:
  case GX_S16:
    return 2;
  default:
    return 4;
  }
}

u32 read16(const u8* p, bool le) noexcept { return le ? (p[0] | (p[1] << 8)) : ((p[0] << 8) | p[1]); }
u32 read32(const u8* p, bool le) noexcept {
  return le ? (u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24))
            : ((u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]));
}

// Same arithmetic as the generated WGSL fetch_{u8,s8,u16,s16,f32}_N helpers.
float decode_component(const u8* p, u8 type, u8 frac, bool le) noexcept {
  const float scale = static_cast<float>(1u << (frac & 31u));
  switch (type) {
  case GX_U8:
    return static_cast<float>(p[0]) / scale;
  case GX_S8:
    return static_cast<float>(static_cast<s8>(p[0])) / scale;
  case GX_U16:
    return static_cast<float>(read16(p, le)) / scale;
  case GX_S16:
    return static_cast<float>(static_cast<s16>(read16(p, le))) / scale;
  default:
    return std::bit_cast<float>(read32(p, le));
  }
}

void write_f32_be(u8* p, float value) noexcept {
  const u32 bits = std::bit_cast<u32>(value);
  p[0] = static_cast<u8>(bits >> 24);
  p[1] = static_cast<u8>(bits >> 16);
  p[2] = static_cast<u8>(bits >> 8);
  p[3] = static_cast<u8>(bits);
}

// Resolves one element of an attribute: a pointer to `bytes` bytes plus their byte order,
// or nullptr when an indexed read would fall outside the array (the packer then leaves zeros).
const u8* element(const AttrConfig& src, u32 attr, const u8* srcBase, u32 indexSlot, u32 bytes,
                  const std::array<AttrArray, MaxVtxAttr>& arrays, bool& le) noexcept {
  if (src.attrType == GX_DIRECT) {
    le = false; // The vertex stream is always big endian.
    return srcBase + indexSlot * bytes;
  }
  const bool index16 = src.attrType == GX_INDEX16;
  const u8* indexPtr = srcBase + indexSlot * (index16 ? 2u : 1u);
  const u32 index = index16 ? read16(indexPtr, false) : indexPtr[0];
  const auto& array = arrays[attr];
  const u64 offset = static_cast<u64>(index) * src.stride;
  if (array.data == nullptr || offset + bytes > array.size) return nullptr;
  le = array.le;
  return static_cast<const u8*>(array.data) + offset;
}

u32 dst_size(const AttrConfig& d, u32 attr) noexcept {
  if (is_index_attr(attr)) return 4;
  if (is_color_attr(attr)) return (color_size(d.compType) + 3u) & ~3u;
  return 4u * d.cnt;
}

int active_mode() noexcept {
  static const int mode = [] {
    const int requested = g_requestedMode.load(std::memory_order_relaxed);
    const int adreno = webgpu::g_adapterAdrenoModel;
    // 1 = PNMTXIDX-direct (skinned character) draws; 2 = every triangle draw.
    // The automatic modes act on Adreno 8xx, the family the stretched geometry is confirmed on, and
    // on the Adreno 750, where characters go missing instead: there the character repack is paired
    // with the constant matrix lookup (see AuroraAdapterWantsConstantMatrixIndexing).
    const bool automatic = requested == AURORA_VERTEX_REPACK_AUTO_CHARACTERS || requested == AURORA_VERTEX_REPACK_AUTO_ALL;
    int result = AURORA_VERTEX_REPACK_OFF;
    if (automatic) {
      if (adreno >= 800 && adreno < 1000) {
        result = requested == AURORA_VERTEX_REPACK_AUTO_ALL ? AURORA_VERTEX_REPACK_ALL : AURORA_VERTEX_REPACK_CHARACTERS;
      } else if (adreno == 750) {
        result = AURORA_VERTEX_REPACK_CHARACTERS;
      }
    } else if (requested == AURORA_VERTEX_REPACK_CHARACTERS || requested == AURORA_VERTEX_REPACK_ALL) {
      result = requested;
    }
    Log.info("vertex repack mode={} source={} adreno={}",
             result == AURORA_VERTEX_REPACK_ALL ? "all_draws" : (result == AURORA_VERTEX_REPACK_CHARACTERS ? "characters" : "off"),
             automatic ? "auto" : "config", adreno);
    return result;
  }();
  return mode;
}
} // namespace

void set_requested_mode(int mode) noexcept { g_requestedMode.store(mode, std::memory_order_relaxed); }

bool enabled() noexcept { return active_mode() >= AURORA_VERTEX_REPACK_CHARACTERS; }
bool all_draws() noexcept { return active_mode() == AURORA_VERTEX_REPACK_ALL; }

u8 repacked_layout(std::array<AttrConfig, MaxVtxAttr>& attrs) noexcept {
  u32 offset = 0;
  for (u32 attr = GX_VA_PNMTXIDX; attr <= GX_VA_TEX7; ++attr) {
    auto& d = attrs[attr];
    if (d.attrType == GX_NONE) continue;
    d.attrType = GX_DIRECT;
    d.stride = 0;
    d.le = false;
    d.nrmIndexCount = 1;
    if (!is_index_attr(attr) && !is_color_attr(attr)) {
      d.compType = GX_F32;
      d.frac = 0;
    }
    d.offset = static_cast<u8>(offset);
    offset += dst_size(d, attr);
  }
  return static_cast<u8>(offset);
}

void repack_with_layout(const std::array<AttrConfig, MaxVtxAttr>& src, u32 srcStride,
                        const std::array<AttrConfig, MaxVtxAttr>& dst, u32 dstStride, const u8* vertices,
                        u16 count, const std::array<AttrArray, MaxVtxAttr>& arrays, std::vector<u8>& out) {
  // Already-repacked layouts (every attribute direct, same type, count and offset) decode to the
  // same bytes, so copy them. Common in all-draws mode, where it saves a per-component decode.
  bool identical = srcStride == dstStride;
  for (u32 attr = GX_VA_PNMTXIDX; identical && attr <= GX_VA_TEX7; ++attr) {
    const auto& s = src[attr];
    const auto& d = dst[attr];
    if (s.attrType == GX_NONE) continue;
    identical = s.attrType == GX_DIRECT && d.attrType == GX_DIRECT && s.compType == d.compType &&
                s.cnt == d.cnt && s.offset == d.offset && (s.compType == GX_F32 || is_color_attr(attr));
  }
  if (identical) {
    out.assign(vertices, vertices + static_cast<size_t>(count) * dstStride);
    return;
  }
  out.assign(static_cast<size_t>(count) * dstStride, 0);
  for (u32 v = 0; v < count; ++v) {
    const u8* in = vertices + static_cast<size_t>(v) * srcStride;
    u8* o = out.data() + static_cast<size_t>(v) * dstStride;
    for (u32 attr = GX_VA_PNMTXIDX; attr <= GX_VA_TEX7; ++attr) {
      const auto& s = src[attr];
      if (s.attrType == GX_NONE) continue;
      const u8* srcBase = in + s.offset;
      u8* dstBase = o + dst[attr].offset;
      if (is_index_attr(attr)) {
        dstBase[0] = srcBase[0]; // Matrix-index attributes are always direct bytes.
        continue;
      }
      bool le = false;
      if (is_color_attr(attr)) {
        const u32 bytes = color_size(s.compType);
        const u8* p = element(s, attr, srcBase, 0, bytes, arrays, le);
        if (p == nullptr) continue;
        // Packed 16/24-bit colors are decoded with the attribute's byte order; the
        // repacked stream is big endian, so reverse little-endian sources.
        const bool packed = s.compType == GX_RGB565 || s.compType == GX_RGBA4 || s.compType == GX_RGBA6;
        for (u32 b = 0; b < bytes; ++b) dstBase[b] = (packed && le) ? p[bytes - 1 - b] : p[b];
        continue;
      }
      const u32 csize = comp_size(s.compType);
      // Indexed GX_NRM_NBT3 stores one index per 3-component group; everything else is one element.
      const bool nbt3 = attr == GX_VA_NRM && s.cnt == 9 && s.nrmIndexCount == 3 && s.attrType != GX_DIRECT;
      const u32 groups = nbt3 ? 3u : 1u;
      const u32 perGroup = s.cnt / groups;
      for (u32 g = 0; g < groups; ++g) {
        const u8* p = nbt3 ? element(s, attr, srcBase, g, perGroup * csize, arrays, le)
                           : (g == 0 ? element(s, attr, srcBase, 0, perGroup * csize, arrays, le) : nullptr);
        if (p == nullptr) continue;
        for (u32 c = 0; c < perGroup; ++c) {
          write_f32_be(dstBase + 4u * (g * perGroup + c), decode_component(p + c * csize, s.compType, s.frac, le));
        }
      }
    }
  }
}

const std::vector<u8>& repack(GXVtxFmt fmt, const u8* vertices, u16 count, u32 srcStride) {
  thread_local std::vector<u8> out;
  std::array<AttrConfig, MaxVtxAttr> src{};
  populate_vertex_layout(src, fmt);
  auto dst = src;
  const u32 dstStride = repacked_layout(dst);
  repack_with_layout(src, srcStride, dst, dstStride, vertices, count, g_gxState.arrays, out);
  return out;
}

void note_pipeline(u64 sourceHash, u64 repackedHash, u32 srcStride, u32 dstStride) noexcept {
  static std::array<u64, 24> seen{};
  static unsigned reports = 0;
  if (reports >= seen.size()) return;
  for (unsigned i = 0; i < reports; ++i) {
    if (seen[i] == sourceHash) return;
  }
  seen[reports++] = sourceHash;
  Log.info("vertex repack active source={:016x} pipeline={:016x} src_stride={} dst_stride={} report={}/24",
           sourceHash, repackedHash, srcStride, dstStride, reports);
}

} // namespace aurora::gx::vertex_repack
