#include "efb_ram.hpp"

#include "frame_packet.hpp"
#include "hash.hpp"
#include "recording.hpp"
#include "tex_copy_conv.hpp"
#include "texture.hpp"

#include "../gx/gx.hpp"
#include "../gx/texture.hpp"
#include "../internal.hpp"
#include "../webgpu/gpu.hpp"
#include "../webgpu/gpu_prof.hpp"
#include "frame.hpp"

#include <aurora/efb_ram.h>
#include <aurora/mem_stats.h>

#include <absl/container/flat_hash_map.h>
#include <magic_enum.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace aurora::gfx::efb_ram {
namespace {
Module Log("aurora::gfx::efb_ram");

using webgpu::g_device;

// ---------------------------------------------------------------------------------------------
// Shaders. Texel layouts follow the GX memory format: 4bpp in 8x8 tiles, 8bpp in 8x4 tiles,
// 16bpp in 4x4 tiles (32 bytes each), RGBA8/Z24X8 in 4x4 tiles of 64 bytes (AR pairs, then GB
// pairs). Multi-byte texels are big-endian. Copy textures hold Aurora's conversion of the EFB
// (tex_copy_conv.cpp), which already matches the semantics of the format read as a texture
// (e.g. Z16 = intensity hi byte, alpha lo byte, as IA8 reads it), so encoding is a repack.
// ---------------------------------------------------------------------------------------------

constexpr std::string_view ShaderCommon = R"(
struct Params {
    fmt: u32,
    width: u32,     // texels (native)
    height: u32,
    srcWidth: u32,  // copy texture size (may be upscaled)
    srcHeight: u32,
    byteSize: u32,
    byteOffset: u32,
    pad: u32,
};

// 0: 4bpp (8x8), 1: 8bpp (8x4), 2: 16bpp (4x4), 3: 32bpp (4x4, 64-byte tiles)
fn fmt_kind(fmt: u32) -> u32 {
    switch fmt {
        case 0x00u, 0x20u, 0x30u: { return 0u; }
        case 0x01u, 0x02u, 0x22u, 0x27u, 0x28u, 0x29u, 0x2Au, 0x11u, 0x39u, 0x3Au: { return 1u; }
        case 0x03u, 0x04u, 0x05u, 0x23u, 0x2Bu, 0x2Cu, 0x13u, 0x3Cu: { return 2u; }
        default: { return 3u; }
    }
}
fn tile_w(kind: u32) -> u32 { return select(4u, 8u, kind <= 1u); }
fn tile_h(kind: u32) -> u32 { return select(4u, 8u, kind == 0u); }
)";

constexpr std::string_view EncodeShader = R"(
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var<storage, read_write> dst: array<u32>;
@group(0) @binding(2) var<uniform> p: Params;

fn fetch(x: u32, y: u32) -> vec4f {
    let cx = min(x, p.width - 1u);
    let cy = min(y, p.height - 1u);
    let sx = min(u32((f32(cx) + 0.5) * f32(p.srcWidth) / f32(p.width)), p.srcWidth - 1u);
    let sy = min(u32((f32(cy) + 0.5) * f32(p.srcHeight) / f32(p.height)), p.srcHeight - 1u);
    return textureLoad(src, vec2i(i32(sx), i32(sy)), 0);
}
fn u8v(v: f32) -> u32 { return u32(clamp(round(v * 255.0), 0.0, 255.0)); }
fn u4v(v: f32) -> u32 { return u32(clamp(round(v * 15.0), 0.0, 15.0)); }

// Byte `sel` of a 16-bit texel, or of the AR/GB half of an RGBA8 texel.
fn texel_byte(c: vec4f, sel: u32, half: u32) -> u32 {
    switch p.fmt {
        case 0x04u: { // RGB565
            let v = ((u8v(c.r) >> 3u) << 11u) | ((u8v(c.g) >> 2u) << 5u) | (u8v(c.b) >> 3u);
            return select(v & 0xFFu, v >> 8u, sel == 0u);
        }
        case 0x05u: { // RGB5A3
            let a = u8v(c.a);
            var v: u32;
            if (a >= 0xE0u) {
                v = 0x8000u | ((u8v(c.r) >> 3u) << 10u) | ((u8v(c.g) >> 3u) << 5u) | (u8v(c.b) >> 3u);
            } else {
                v = ((a >> 5u) << 12u) | ((u8v(c.r) >> 4u) << 8u) | ((u8v(c.g) >> 4u) << 4u) | (u8v(c.b) >> 4u);
            }
            return select(v & 0xFFu, v >> 8u, sel == 0u);
        }
        case 0x06u, 0x16u, 0x26u: { // RGBA8: AR then GB
            if (half == 0u) { return select(u8v(c.r), u8v(c.a), sel == 0u); }
            return select(u8v(c.b), u8v(c.g), sel == 0u);
        }
        default: { // IA8-like: [alpha-ish, intensity-ish] as Aurora's copy texture stores them
            return select(u8v(c.r), u8v(c.a), sel == 0u);
        }
    }
}

fn byte8(c: vec4f) -> u32 {
    switch p.fmt {
        case 0x02u, 0x22u: { return (u4v(c.a) << 4u) | u4v(c.r); } // IA4 / RA4
        default: { return u8v(c.r); }                              // I8, A8, R8, G8, B8, Z8...
    }
}

fn encode_byte(off: u32) -> u32 {
    let kind = fmt_kind(p.fmt);
    let tw = tile_w(kind);
    let th = tile_h(kind);
    let tilesPerRow = (p.width + tw - 1u) / tw;
    let tileBytes = select(32u, 64u, kind == 3u);
    let tile = off / tileBytes;
    let r = off % tileBytes;
    let tx = (tile % tilesPerRow) * tw;
    let ty = (tile / tilesPerRow) * th;
    if (kind == 0u) {
        let t = r * 2u;
        let a = fetch(tx + t % tw, ty + t / tw);
        let b = fetch(tx + (t + 1u) % tw, ty + (t + 1u) / tw);
        return (u4v(a.r) << 4u) | u4v(b.r);
    }
    if (kind == 1u) {
        return byte8(fetch(tx + r % tw, ty + r / tw));
    }
    let half = r / 32u;
    let t = (r % 32u) / 2u;
    return texel_byte(fetch(tx + t % tw, ty + t / tw), r % 2u, half);
}

@compute @workgroup_size(64)
fn cs_main(@builtin(global_invocation_id) gid: vec3u) {
    let word = gid.x + gid.y * 65535u * 64u;
    if (word * 4u >= p.byteSize) { return; }
    let base = word * 4u;
    dst[word] = encode_byte(base) | (encode_byte(base + 1u) << 8u) | (encode_byte(base + 2u) << 16u) |
                (encode_byte(base + 3u) << 24u);
}
)";

constexpr std::string_view DecodeShader = R"(
@group(0) @binding(0) var<storage, read> mem: array<u32>;
@group(0) @binding(1) var<uniform> p: Params;

var<private> positions: array<vec2f, 3> = array(vec2f(-1.0, 1.0), vec2f(-1.0, -3.0), vec2f(3.0, 1.0));
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
    return vec4f(positions[vi], 0.0, 1.0);
}

fn rd(o: u32) -> u32 {
    let a = o + p.byteOffset;
    if (a >= p.byteSize) { return 0u; }
    return (mem[a >> 2u] >> ((a & 3u) * 8u)) & 0xFFu;
}
fn rd16(o: u32) -> u32 { return (rd(o) << 8u) | rd(o + 1u); }
fn x4(v: u32) -> f32 { return f32(v * 17u) / 255.0; }
fn x5(v: u32) -> f32 { return f32((v << 3u) | (v >> 2u)) / 255.0; }
fn x6(v: u32) -> f32 { return f32((v << 2u) | (v >> 4u)) / 255.0; }
fn x3(v: u32) -> f32 { return f32((v << 5u) | (v << 2u) | (v >> 1u)) / 255.0; }
fn n8(v: u32) -> f32 { return f32(v) / 255.0; }

@fragment fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let x = u32(pos.x);
    let y = u32(pos.y);
    let kind = fmt_kind(p.fmt);
    let tw = tile_w(kind);
    let th = tile_h(kind);
    let tilesPerRow = (p.width + tw - 1u) / tw;
    let tile = (y / th) * tilesPerRow + x / tw;
    let t = (y % th) * tw + x % tw;
    switch p.fmt {
        case 0x00u: { // I4
            let b = rd(tile * 32u + t / 2u);
            let i = x4(select(b & 0xFu, b >> 4u, (t & 1u) == 0u));
            return vec4f(i, i, i, i);
        }
        case 0x01u: { // I8
            let i = n8(rd(tile * 32u + t));
            return vec4f(i, i, i, i);
        }
        case 0x02u: { // IA4
            let b = rd(tile * 32u + t);
            let i = x4(b & 0xFu);
            return vec4f(i, i, i, x4(b >> 4u));
        }
        case 0x03u: { // IA8
            let o = tile * 32u + t * 2u;
            let i = n8(rd(o + 1u));
            return vec4f(i, i, i, n8(rd(o)));
        }
        case 0x04u: { // RGB565
            let v = rd16(tile * 32u + t * 2u);
            return vec4f(x5(v >> 11u), x6((v >> 5u) & 0x3Fu), x5(v & 0x1Fu), 1.0);
        }
        case 0x05u: { // RGB5A3
            let v = rd16(tile * 32u + t * 2u);
            if ((v & 0x8000u) != 0u) {
                return vec4f(x5((v >> 10u) & 0x1Fu), x5((v >> 5u) & 0x1Fu), x5(v & 0x1Fu), 1.0);
            }
            return vec4f(x4((v >> 8u) & 0xFu), x4((v >> 4u) & 0xFu), x4(v & 0xFu), x3((v >> 12u) & 0x7u));
        }
        default: { // RGBA8
            let o = tile * 64u + t * 2u;
            return vec4f(n8(rd(o + 1u)), n8(rd(o + 32u)), n8(rd(o + 33u)), n8(rd(o)));
        }
    }
}
)";

struct Params {
  uint32_t fmt = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t srcWidth = 0;
  uint32_t srcHeight = 0;
  uint32_t byteSize = 0;
  uint32_t byteOffset = 0;
  uint32_t pad = 0;
};
static_assert(sizeof(Params) == 32);

wgpu::ComputePipeline g_encodePipeline;
wgpu::BindGroupLayout g_encodeLayout;
wgpu::RenderPipeline g_decodePipeline;
wgpu::BindGroupLayout g_decodeLayout;

// ---------------------------------------------------------------------------------------------
// State. Copies are recorded and textures resolved on the FIFO thread; readbacks complete on
// whatever thread services Dawn callbacks; writebacks run on the caller of apply().
// ---------------------------------------------------------------------------------------------

// A GPU buffer recorded on the FIFO thread but created and used only on the render worker
// (Dawn device calls from the FIFO thread race with the render worker).
struct LazyBuffer {
  wgpu::Buffer buffer;
  uint64_t size = 0;
  wgpu::BufferUsage usage = wgpu::BufferUsage::None;
  const char* label = "";
  const wgpu::Buffer& get() {
    if (!buffer) {
      const wgpu::BufferDescriptor desc{.label = label, .usage = usage, .size = size};
      buffer = g_device.CreateBuffer(&desc);
    }
    return buffer;
  }
};
using LazyBufferRef = std::shared_ptr<LazyBuffer>;

struct Entry {
  const uint8_t* dest = nullptr;
  uint32_t size = 0;
  GXTexFmt fmt = GX_TF_I8;
  uint32_t width = 0;
  uint32_t height = 0;
  TextureHandle copy;   // the copy's GPU texture (Aurora's, possibly upscaled)
  LazyBufferRef bytes;  // GX-format bytes of the copy
  uint64_t seq = 0;
  uint64_t encodedSeq = 0;
  HashType ramHash = 0; // memory contents we expect (pre-copy, or our writeback)
  uint32_t verifiedFrame = UINT32_MAX;
};

struct Readback {
  wgpu::Buffer buffer;        // render worker / finish_maps only
  std::vector<uint8_t> data;  // filled by finish_maps
  int mapStatus = 0;          // set by the map callback: 1 mapped, 2 failed (0219)
  const uint8_t* dest = nullptr;
  uint32_t size = 0;
  GXTexFmt fmt = GX_TF_I8;
  uint32_t width = 0;
  uint32_t height = 0;
  HashType preHash = 0;   // memory when the game issued the copy (aurora_efb_ram_guest_copy), else hostHash
  HashType hostHash = 0;  // memory when Aurora executed the copy
  bool guestKnown = false;
  uint64_t seq = 0;
  bool ok = false;
};

struct Decoded {
  TextureHandle handle;
  uint64_t signature = 0;
  uint32_t lastUsedFrame = 0;
};

struct DecodedKey {
  const void* data;
  uint32_t fmt, width, height;
  bool operator==(const DecodedKey&) const = default;
  template <typename H>
  friend H AbslHashValue(H h, const DecodedKey& k) {
    return H::combine(std::move(h), k.data, k.fmt, k.width, k.height);
  }
};

// Lock order (0191). With ImplicitDeviceSynchronization every Dawn call takes the device lock,
// and AllowSpontaneous map callbacks run inside whatever Dawn call notices the map finished
// (often Queue::Submit on the render worker), i.e. with the device lock held. So:
//  - the readback map callback only takes leaf locks (g_landedMutex, g_poolMutex), which are
//    never held across a Dawn call;
//  - g_mutex is never taken by a map callback, and no Dawn object is created or released while
//    it is held (textures/buffers that die under it are moved out and dropped after unlocking),
//    so a guest thread waiting for it (DCInvalidateRange -> cpu_read) never waits behind a
//    device lock held through a pipeline compile.
// Before, the FIFO thread held g_mutex while creating a decoded texture (device lock) while the
// render worker held the device lock in Submit and its map callback waited for g_mutex: a
// deadlock that froze the game when entering a room or a transition that first decodes a texture
// from EFB copy memory (the Sealed Room from the roof, mirror warps).
// 0219: the map callback makes no Dawn call at all (it only records the status and queues the
// readback in g_mapped); GetConstMappedRange/Unmap run in finish_maps (render worker after each
// frame, and aurora_efb_ram_apply), holding no lock. A callback that called Unmap deadlocked inside
// Dawn when it fired from the present-timing thread's WaitAny poll while the render worker held the
// device lock in a Dawn call waiting for that same event to finish (the game's render loop stopped
// for good: LM frame interpolation runs, Bogmire's arena, ~3 min). The pool holds each buffer's size
// so no Dawn call (GetSize) is made under g_poolMutex either.
std::mutex g_mutex;
std::mutex g_landedMutex; // g_landed only (leaf lock: map callbacks)
std::atomic_bool g_enabled{true};
std::atomic_bool g_readbackAll{false};
std::atomic_bool g_scaledDepthReads{true};
std::vector<Entry> g_entries; // oldest first
absl::flat_hash_map<uint64_t, LazyBufferRef> g_bytePool; // (dest ^ size) -> bytes buffer, reused per range
absl::flat_hash_map<DecodedKey, Decoded> g_decoded;
std::vector<std::pair<const uint8_t*, uint32_t>> g_cpuReadRanges;
std::vector<std::pair<const uint8_t*, uint32_t>> g_watchRanges; // read back even when disabled
std::deque<std::shared_ptr<Readback>> g_landed; // guarded by g_landedMutex
std::deque<std::shared_ptr<Readback>> g_mapped; // map callback done, not yet copied out (g_landedMutex)
std::mutex g_poolMutex;  // readback buffer pool (leaf lock, no Dawn call under it)
std::vector<std::pair<wgpu::Buffer, uint32_t>> g_readbackPool; // buffer, size
absl::flat_hash_map<const uint8_t*, std::pair<uint32_t, HashType>> g_written; // our last writeback per dest
// Memory at each destination when the game issued its newest copy there (aurora_efb_ram_guest_copy),
// newest last. Aurora executes the copy later on another thread (a frame-interpolation replay can
// be game frames behind, more so on a busy CPU), when the game may have freed and reused the range.
struct GuestCopy {
  const uint8_t* dest;
  uint32_t size;
  HashType hash;
  std::chrono::steady_clock::time_point when; // diagnostics: how late Aurora executes it
};
std::vector<GuestCopy> g_guestCopies;
constexpr size_t MaxGuestCopies = 128;
uint64_t g_seq = 0;
AuroraEfbRamStats g_stats{};
AuroraEfbRamWritebackFn g_writebackFn = nullptr;
void* g_writebackUser = nullptr;

constexpr size_t MaxEntries = 64;

struct CpuTimer {
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  ~CpuTimer() {
    g_stats.cpuNs += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
  }
};

bool overlaps(const uint8_t* a, uint32_t as, const uint8_t* b, uint32_t bs) { return a < b + bs && b < a + as; }

HashType hash_range(const uint8_t* p, uint32_t size) {
  g_stats.hashedBytes += size;
  return xxh3_hash_s(p, size);
}

uint32_t align4(uint32_t v) { return (v + 3u) & ~3u; }

uint8_t g_generation = 0;
bool g_replaying = false;
bool g_noReadback = false;  // a repeated replay of a tape: its copies were read back already
std::array<std::optional<std::vector<Entry>>, 4> g_entrySlots;

LazyBufferRef bytes_buffer(const uint8_t* dest, uint32_t size) {
  const uint64_t key = reinterpret_cast<uintptr_t>(dest) ^ (static_cast<uint64_t>(size) << 40) ^
                       (static_cast<uint64_t>(g_generation) << 62);
  auto& buf = g_bytePool[key];
  if (!buf) {
    buf = std::make_shared<LazyBuffer>(LazyBuffer{
        .size = align4(size),
        .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst,
        .label = "EFB copy RAM bytes",
    });
  }
  return buf;
}

// Render worker only.
wgpu::Buffer params_buffer(const Params& params) {
  const wgpu::BufferDescriptor desc{
      .label = "EFB copy RAM params",
      .usage = wgpu::BufferUsage::Uniform,
      .size = sizeof(Params),
      .mappedAtCreation = true,
  };
  auto buf = g_device.CreateBuffer(&desc);
  std::memcpy(buf.GetMappedRange(0, sizeof(Params)), &params, sizeof(Params));
  buf.Unmap();
  return buf;
}

// Encode `copy` (width x height native texels) into `bytes`. Runs on the render worker.
void run_encode(const wgpu::CommandEncoder& cmd, const TextureHandle& copy, const wgpu::Buffer& bytes, GXTexFmt fmt,
                uint32_t width, uint32_t height, uint32_t size, const wgpu::Buffer& paramsBuf) {
  const std::array entries{
      wgpu::BindGroupEntry{.binding = 0, .textureView = copy->sampleTextureView},
      wgpu::BindGroupEntry{.binding = 1, .buffer = bytes, .size = align4(size)},
      wgpu::BindGroupEntry{.binding = 2, .buffer = paramsBuf, .size = sizeof(Params)},
  };
  const wgpu::BindGroupDescriptor bgDesc{
      .label = "EFB copy encode bind group",
      .layout = g_encodeLayout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bgDesc);
  const wgpu::ComputePassDescriptor passDesc{
      .label = "EFB copy to RAM encode",
      .timestampWrites = webgpu::gpu_prof::pass_writes("EFB copy RAM encode"),
  };
  const auto pass = cmd.BeginComputePass(&passDesc);
  pass.SetPipeline(g_encodePipeline);
  pass.SetBindGroup(0, bindGroup);
  const uint32_t words = align4(size) / 4;
  const uint32_t groups = (words + 63) / 64;
  pass.DispatchWorkgroups(std::min<uint32_t>(groups, 65535), (groups + 65534) / 65535);
  pass.End();
  (void)fmt;
  (void)width;
  (void)height;
}

void run_decode(const wgpu::CommandEncoder& cmd, const wgpu::Buffer& bytes, uint32_t bytesSize,
                const wgpu::Buffer& paramsBuf, const TextureHandle& dst) {
  const std::array entries{
      wgpu::BindGroupEntry{.binding = 0, .buffer = bytes, .size = align4(bytesSize)},
      wgpu::BindGroupEntry{.binding = 1, .buffer = paramsBuf, .size = sizeof(Params)},
  };
  const wgpu::BindGroupDescriptor bgDesc{
      .label = "EFB copy decode bind group",
      .layout = g_decodeLayout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bgDesc);
  const std::array colorAttachments{
      wgpu::RenderPassColorAttachment{
          .view = dst->attachmentTextureView,
          .loadOp = wgpu::LoadOp::Clear,
          .storeOp = wgpu::StoreOp::Store,
          .clearValue = {0.0, 0.0, 0.0, 0.0},
      },
  };
  const wgpu::RenderPassDescriptor passDesc{
      .label = "EFB copy RAM decode",
      .colorAttachmentCount = colorAttachments.size(),
      .colorAttachments = colorAttachments.data(),
      .timestampWrites = webgpu::gpu_prof::pass_writes("EFB copy RAM decode"),
  };
  const auto pass = cmd.BeginRenderPass(&passDesc);
  pass.SetPipeline(g_decodePipeline);
  pass.SetBindGroup(0, bindGroup);
  pass.Draw(3);
  pass.End();
}

// Records the encode of `e` into the pass op list (caller holds g_mutex).
PassOp make_encode_op(Entry& e) {
  e.encodedSeq = e.seq;
  ++g_stats.encodes;
  const Params params{
      .fmt = static_cast<uint32_t>(e.fmt),
      .width = e.width,
      .height = e.height,
      .srcWidth = e.copy->size.width,
      .srcHeight = e.copy->size.height,
      .byteSize = e.size,
  };
  return [copy = e.copy, bytes = e.bytes, fmt = e.fmt, w = e.width, h = e.height, size = e.size,
          params](const wgpu::CommandEncoder& cmd, detail::FramePacket&) {
    run_encode(cmd, copy, bytes->get(), fmt, w, h, size, params_buffer(params));
  };
}

bool supported_texture_format(u32 fmt) {
  switch (fmt) {
  case GX_TF_I4:
  case GX_TF_I8:
  case GX_TF_IA4:
  case GX_TF_IA8:
  case GX_TF_RGB565:
  case GX_TF_RGB5A3:
  case GX_TF_RGBA8:
    return true;
  default:
    return false;
  }
}

// Render worker only. The pool lock is a leaf lock (map callbacks take it with the device lock
// held), so the new buffer is created after releasing it.
wgpu::Buffer acquire_readback_buffer(uint32_t size) {
  {
    std::lock_guard lock{g_poolMutex};
    for (auto it = g_readbackPool.begin(); it != g_readbackPool.end(); ++it) {
      if (it->second >= align4(size)) {
        auto buf = std::move(it->first);
        g_readbackPool.erase(it);
        return buf;
      }
    }
  }
  const wgpu::BufferDescriptor desc{
      .label = "EFB copy RAM readback",
      .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
      .size = align4(size),
  };
  return g_device.CreateBuffer(&desc);
}
} // namespace

void initialize() {
  const std::string encodeSrc = std::string{ShaderCommon} + std::string{EncodeShader};
  const std::string decodeSrc = std::string{ShaderCommon} + std::string{DecodeShader};
  {
    const wgpu::ShaderSourceWGSL wgsl{wgpu::ShaderSourceWGSL::Init{.code = encodeSrc.c_str()}};
    const wgpu::ShaderModuleDescriptor modDesc{.nextInChain = &wgsl, .label = "EFB copy RAM encode"};
    const auto module = g_device.CreateShaderModule(&modDesc);
    const std::array entries{
        wgpu::BindGroupLayoutEntry{
            .binding = 0,
            .visibility = wgpu::ShaderStage::Compute,
            .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                        .viewDimension = wgpu::TextureViewDimension::e2D},
        },
        wgpu::BindGroupLayoutEntry{
            .binding = 1,
            .visibility = wgpu::ShaderStage::Compute,
            .buffer = {.type = wgpu::BufferBindingType::Storage},
        },
        wgpu::BindGroupLayoutEntry{
            .binding = 2,
            .visibility = wgpu::ShaderStage::Compute,
            .buffer = {.type = wgpu::BufferBindingType::Uniform},
        },
    };
    const wgpu::BindGroupLayoutDescriptor layoutDesc{
        .label = "EFB copy RAM encode layout", .entryCount = entries.size(), .entries = entries.data()};
    g_encodeLayout = g_device.CreateBindGroupLayout(&layoutDesc);
    const wgpu::PipelineLayoutDescriptor plDesc{.bindGroupLayoutCount = 1, .bindGroupLayouts = &g_encodeLayout};
    const wgpu::ComputePipelineDescriptor desc{
        .label = "EFB copy RAM encode",
        .layout = g_device.CreatePipelineLayout(&plDesc),
        .compute = {.module = module, .entryPoint = "cs_main"},
    };
    const ::aurora::webgpu::CreateTimer createTimer_565{"pipeline efb_ram.cpp:565"}; // 0223 (AURORA_CREATE_LOG)
    g_encodePipeline = g_device.CreateComputePipeline(&desc);
  }
  {
    const wgpu::ShaderSourceWGSL wgsl{wgpu::ShaderSourceWGSL::Init{.code = decodeSrc.c_str()}};
    const wgpu::ShaderModuleDescriptor modDesc{.nextInChain = &wgsl, .label = "EFB copy RAM decode"};
    const auto module = g_device.CreateShaderModule(&modDesc);
    const std::array entries{
        wgpu::BindGroupLayoutEntry{
            .binding = 0,
            .visibility = wgpu::ShaderStage::Fragment,
            .buffer = {.type = wgpu::BufferBindingType::ReadOnlyStorage},
        },
        wgpu::BindGroupLayoutEntry{
            .binding = 1,
            .visibility = wgpu::ShaderStage::Fragment,
            .buffer = {.type = wgpu::BufferBindingType::Uniform},
        },
    };
    const wgpu::BindGroupLayoutDescriptor layoutDesc{
        .label = "EFB copy RAM decode layout", .entryCount = entries.size(), .entries = entries.data()};
    g_decodeLayout = g_device.CreateBindGroupLayout(&layoutDesc);
    const wgpu::PipelineLayoutDescriptor plDesc{.bindGroupLayoutCount = 1, .bindGroupLayouts = &g_decodeLayout};
    constexpr std::array targets{wgpu::ColorTargetState{.format = wgpu::TextureFormat::RGBA8Unorm}};
    const wgpu::FragmentState fragment{
        .module = module, .entryPoint = "fs_main", .targetCount = targets.size(), .targets = targets.data()};
    const wgpu::RenderPipelineDescriptor desc{
        .label = "EFB copy RAM decode",
        .layout = g_device.CreatePipelineLayout(&plDesc),
        .vertex = {.module = module, .entryPoint = "vs_main"},
        .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
        .fragment = &fragment,
    };
    const ::aurora::webgpu::CreateTimer createTimer_597{"pipeline efb_ram.cpp:597"}; // 0223 (AURORA_CREATE_LOG)
    g_decodePipeline = g_device.CreateRenderPipeline(&desc);
  }
}

void shutdown() {
  // Dropped after the locks are released (lock order above).
  std::vector<Entry> entries;
  absl::flat_hash_map<uint64_t, LazyBufferRef> bytePool;
  absl::flat_hash_map<DecodedKey, Decoded> decoded;
  std::vector<std::pair<wgpu::Buffer, uint32_t>> readbackPool;
  std::deque<std::shared_ptr<Readback>> landed, mapped;
  {
    std::lock_guard lock{g_mutex};
    entries.swap(g_entries);
    bytePool.swap(g_bytePool);
    decoded.swap(g_decoded);
  }
  {
    std::lock_guard poolLock{g_poolMutex};
    readbackPool.swap(g_readbackPool);
  }
  {
    std::lock_guard landedLock{g_landedMutex};
    landed.swap(g_landed);
    mapped.swap(g_mapped);
  }
  g_encodePipeline = {};
  g_encodeLayout = {};
  g_decodePipeline = {};
  g_decodeLayout = {};
}

void finish_maps() {
  std::deque<std::shared_ptr<Readback>> mapped;
  {
    std::lock_guard lock{g_landedMutex};
    mapped.swap(g_mapped);
  }
  if (mapped.empty()) {
    return;
  }
  // No lock held across the Dawn calls (GetConstMappedRange, Unmap, buffer release).
  for (auto& rb : mapped) {
    rb->ok = false;
    if (rb->mapStatus == 1 && rb->buffer) {
      const auto* p = static_cast<const uint8_t*>(rb->buffer.GetConstMappedRange(0, align4(rb->size)));
      if (p != nullptr) {
        rb->data.assign(p, p + rb->size);
        rb->ok = true;
      }
      rb->buffer.Unmap();
      std::lock_guard lock{g_poolMutex};
      if (g_readbackPool.size() < 16) {
        g_readbackPool.emplace_back(std::move(rb->buffer), align4(rb->size));
      }
    }
    rb->buffer = {};
  }
  std::lock_guard lock{g_landedMutex};
  for (auto& rb : mapped) {
    g_landed.push_back(std::move(rb));
  }
}

bool enabled() noexcept { return g_enabled.load(std::memory_order_relaxed); }

bool supports_copy_format(GXTexFmt fmt) noexcept {
  return tex_copy_conv::needs_conversion(fmt) || fmt == GX_TF_RGBA8 || fmt == GX_TF_RGB5A3;
}

PassOp on_copy(const void* destPtr, GXTexFmt fmt, uint32_t width, uint32_t height, const TextureHandle& copy) {
  if (!copy || !supports_copy_format(fmt) || width == 0 || height == 0) {
    return {};
  }
  const auto* dest = static_cast<const uint8_t*>(destPtr);
  bool watched = false;
  {
    std::lock_guard lock{g_mutex};
    for (const auto& [p, s] : g_watchRanges) {
      watched = watched || (dest >= p && dest < p + s);
    }
  }
  if (!enabled() && !watched) {
    return {};
  }
  const auto size = static_cast<uint32_t>(GXGetTexBufferSize(width, height, fmt, GX_FALSE, 0));
  if (size == 0) {
    return {};
  }

  std::vector<Entry> dropped; // released after g_mutex (declared before the lock)
  std::lock_guard lock{g_mutex};
  const CpuTimer timer;
  ++g_stats.copies;
  // A copy overwrites its whole range: older copies entirely inside it are gone.
  for (auto it = g_entries.begin(); it != g_entries.end();) {
    if (it->dest >= dest && it->dest + it->size <= dest + size) {
      dropped.push_back(std::move(*it));
      it = g_entries.erase(it);
    } else {
      ++it;
    }
  }
  if (g_entries.size() >= MaxEntries) {
    dropped.push_back(std::move(g_entries.front()));
    g_entries.erase(g_entries.begin());
  }
  auto& e = g_entries.emplace_back(Entry{
      .dest = dest,
      .size = size,
      .fmt = fmt,
      .width = width,
      .height = height,
      .copy = copy,
      .bytes = bytes_buffer(dest, size),
      .seq = ++g_seq,
  });
  // Memory still holds what was there before the copy until a writeback lands.
  e.ramHash = hash_range(dest, size);
  e.verifiedFrame = current_frame();

  bool readback = watched || g_readbackAll.load(std::memory_order_relaxed);
  // Frame interpolation replays a frame several times; only its first replay reads its copies back
  // (as the console did once). Later replays would write the frame's copies into memory the game
  // may have reused since (a GBH screen capture buffer later holding model data).

  if (g_noReadback) {
    readback = watched;
  }
  for (const auto& [p, s] : g_cpuReadRanges) {
    readback = readback || (!g_noReadback && overlaps(p, s, dest, size));
  }
  if (!readback) {
    return {}; // encoded lazily if a texture reads it with another format
  }

  ++g_stats.readbacks;
  auto encode = make_encode_op(e);
  // The writeback may only land if the game has not written the range since it issued the copy.
  // e.ramHash is the memory now, which can already hold what the game put there after the copy
  // (it freed the buffer and the heap reused it while this frame waited for its replay).
  HashType preHash = e.ramHash;
  bool guestKnown = false;
  for (auto it = g_guestCopies.rbegin(); it != g_guestCopies.rend(); ++it) {
    if (it->dest == dest && it->size == size) {
      preHash = it->hash;
      guestKnown = true;
      const double lagMs =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - it->when).count();
      static double s_maxLagMs = 100.0;
      if (lagMs > s_maxLagMs) {
        s_maxLagMs = lagMs;
        Log.info("read-back EFB copy to {} (+{:#x}) executed {:.0f} ms after the game issued it",
                 static_cast<const void*>(dest), size, lagMs);
      }
      const auto ours = g_written.find(dest);
      const bool oursNow = ours != g_written.end() && ours->second.first == size && ours->second.second == e.ramHash;
      if (preHash != e.ramHash && !oursNow && ++g_stats.lateChanged <= 20) {
        Log.info("EFB copy to {} (+{:#x}) executed {:.0f} ms after the game issued it; the game wrote the range "
                 "in between",
                 static_cast<const void*>(dest), size, lagMs);
      }
      break;
    }
  }
  auto rb = std::make_shared<Readback>(Readback{
      .dest = dest,
      .size = size,
      .fmt = fmt,
      .width = width,
      .height = height,
      .preHash = preHash,
      .hostHash = e.ramHash,
      .guestKnown = guestKnown,
      .seq = e.seq,
  });
  return [encode = std::move(encode), bytes = e.bytes, rb](const wgpu::CommandEncoder& cmd,
                                                           detail::FramePacket& frame) {
    encode(cmd, frame);
    rb->buffer = acquire_readback_buffer(rb->size);
    cmd.CopyBufferToBuffer(bytes->get(), 0, rb->buffer, 0, align4(rb->size));
    frame.afterSubmitCallbacks.emplace_back([rb] {
      rb->buffer.MapAsync(
          wgpu::MapMode::Read, 0, align4(rb->size), wgpu::CallbackMode::AllowSpontaneous,
          [rb](wgpu::MapAsyncStatus status, wgpu::StringView) {
            // Spontaneous: runs inside whatever Dawn call noticed the map, on any thread. No Dawn
            // call here (0219, see the lock order notes) and only the leaf lock g_landedMutex.
            rb->mapStatus = status == wgpu::MapAsyncStatus::Success ? 1 : 2;
            if (status != wgpu::MapAsyncStatus::Success && status != wgpu::MapAsyncStatus::CallbackCancelled &&
                status != wgpu::MapAsyncStatus::Aborted) {
              Log.warn("readback map failed: {}", magic_enum::enum_name(status));
            }
            std::lock_guard lock{g_landedMutex};
            g_mapped.push_back(rb);
          });
    });
  };
}

namespace {
// What resolve_texture must do outside g_mutex (lock order, see g_mutex): entries and textures that
// went away are released, and Aurora's copy textures for dropped copies evicted, after unlocking.
struct Deferred {
  std::vector<Entry> entries;
  std::vector<TextureHandle> textures;
  std::vector<const void*> evict;
};

// resolve_texture's work under g_mutex. A decoded texture that does not exist yet is not created
// here: `needTexture` asks the caller to create it unlocked and call again with it in `created`.
TextureHandle resolve_locked(const GXTexObj_& obj, const uint8_t* data, u32 fmt, u32 width, u32 height,
                             uint32_t texSize, TextureHandle& created, bool& needTexture, Deferred& out) {
  if (g_entries.empty()) {
    return {};
  }
  const CpuTimer timer;
  const uint32_t frame = current_frame();
  // Drop copies whose memory the CPU (or DMA) has written since.
  bool any = false;
  for (auto it = g_entries.begin(); it != g_entries.end();) {
    if (!overlaps(it->dest, it->size, data, texSize)) {
      ++it;
      continue;
    }
    if (it->verifiedFrame != frame && !g_replaying) {
      it->verifiedFrame = frame;
      if (hash_range(it->dest, it->size) != it->ramHash) {
        ++g_stats.cpuOverwrites;
        const void* dest = it->dest;
        out.entries.push_back(std::move(*it));
        it = g_entries.erase(it);
        // Aurora's GPU association for the pointer goes too, unless a newer copy owns it.
        if (std::none_of(g_entries.begin(), g_entries.end(), [&](const Entry& e) { return e.dest == dest; })) {
          out.evict.push_back(dest);
        }
        continue;
      }
    }
    any = true;
    ++it;
  }
  if (!any) {
    return {};
  }

  // Exactly the copy as copied: Aurora's (upscaled) copy texture is right.
  const auto last = std::find_if(g_entries.rbegin(), g_entries.rend(),
                                 [&](const Entry& e) { return overlaps(e.dest, e.size, data, texSize); });
  if (last->dest == data && static_cast<u32>(last->fmt) == fmt && last->width == width && last->height == height) {
    return {};
  }
  // A Z16 copy read as IA8 of the same size: Aurora's copy texture already holds that view
  // (tex_copy_conv FragZ16: I = depth bits 23..16, A = 15..8), at the internal resolution.
  if (g_scaledDepthReads.load(std::memory_order_relaxed) && last->dest == data &&
      static_cast<u32>(last->fmt) == GX_TF_Z16 && fmt == GX_TF_IA8 && last->width == width &&
      last->height == height) {
    return {};
  }
  if (!supported_texture_format(fmt)) {
    return {}; // palette/CMPR reads keep Aurora's reinterpretation
  }

  // Which copies make up the texture's bytes, oldest first.
  std::vector<Entry*> parts;
  uint64_t signature = 0;
  for (auto& e : g_entries) {
    if (overlaps(e.dest, e.size, data, texSize)) {
      parts.push_back(&e);
      signature = signature * 1000003u + e.seq;
    }
  }
  auto& dec = g_decoded[DecodedKey{data, fmt, width, height}];
  dec.lastUsedFrame = frame;
  if (!dec.handle) {
    if (!created) {
      Log.info("texture {} {}x{} fmt {:#x} reads EFB copy memory ({} copies, newest {} {}x{} fmt {:#x}): decoding",
               obj.data, width, height, fmt, parts.size(), static_cast<const void*>(last->dest), last->width,
               last->height, static_cast<u32>(last->fmt));
      needTexture = true;
      return {};
    }
    dec.handle = std::move(created);
  }
  if (dec.signature == signature) {
    return dec.handle;
  }

  std::vector<PassOp> ops;
  for (auto* e : parts) {
    if (e->encodedSeq != e->seq) {
      ops.push_back(make_encode_op(*e));
    }
  }
  LazyBufferRef src;
  uint32_t srcSize = 0;
  uint32_t srcOffset = 0;
  const auto* single = parts.size() == 1 && parts[0]->dest <= data && data + texSize <= parts[0]->dest + parts[0]->size
                           ? parts[0]
                           : nullptr;
  if (single != nullptr) {
    src = single->bytes;
    srcSize = single->size;
    srcOffset = static_cast<uint32_t>(data - single->dest);
  } else {
    // Memory under the texture, then each copy's bytes over it in copy order.
    src = std::make_shared<LazyBuffer>(LazyBuffer{
        .size = align4(texSize),
        .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
        .label = "EFB copy RAM staging",
    });
    auto mem = std::make_shared<std::vector<uint8_t>>(data, data + texSize);
    mem->resize(align4(texSize));
    ops.push_back([src, mem](const wgpu::CommandEncoder& cmd, detail::FramePacket&) {
      // Through a mapped-at-creation copy buffer recorded into the same encoder, so the upload is
      // ordered with the copies below (queue.WriteBuffer would land before the whole frame).
      const wgpu::BufferDescriptor desc{
          .label = "EFB copy RAM staging upload",
          .usage = wgpu::BufferUsage::CopySrc,
          .size = mem->size(),
          .mappedAtCreation = true,
      };
      const auto upload = g_device.CreateBuffer(&desc);
      std::memcpy(upload.GetMappedRange(0, mem->size()), mem->data(), mem->size());
      upload.Unmap();
      cmd.CopyBufferToBuffer(upload, 0, src->get(), 0, mem->size());
    });
    srcSize = texSize;
    for (auto* e : parts) {
      const uint8_t* lo = std::max(e->dest, data);
      const uint8_t* hi = std::min(e->dest + e->size, data + texSize);
      const auto from = static_cast<uint32_t>(lo - e->dest) & ~3u;
      const auto to = static_cast<uint32_t>(lo - data) & ~3u;
      const auto len = align4(static_cast<uint32_t>(hi - lo));
      ops.push_back([bytes = e->bytes, src, from, to, len](const wgpu::CommandEncoder& cmd, detail::FramePacket&) {
        cmd.CopyBufferToBuffer(bytes->get(), from, src->get(), to, len);
      });
    }
  }
  const Params params{
      .fmt = fmt,
      .width = width,
      .height = height,
      .byteSize = srcSize,
      .byteOffset = srcOffset,
  };
  ops.push_back([src, srcSize, params, dst = dec.handle](const wgpu::CommandEncoder& cmd, detail::FramePacket&) {
    run_decode(cmd, src->get(), srcSize, params_buffer(params), dst);
  });
  if (!queue_pre_pass_ops(std::move(ops))) {
    return {};
  }
  dec.signature = signature;
  ++g_stats.decodes;

  TextureHandle result = dec.handle;
  // Forget decoded textures nobody has used for a while.
  if (g_decoded.size() > 32) {
    absl::erase_if(g_decoded, [&](auto& kv) {
      if (frame - kv.second.lastUsedFrame <= 600) {
        return false;
      }
      out.textures.push_back(std::move(kv.second.handle));
      return true;
    });
  }
  return result;
}
} // namespace

TextureHandle resolve_texture(const GXTexObj_& obj) {
  if (!enabled() || !obj.has_data()) {
    return {};
  }
  const auto* data = static_cast<const uint8_t*>(obj.data);
  const u32 fmt = obj.format();
  const u32 width = obj.width();
  const u32 height = obj.height();
  const auto texSize = static_cast<uint32_t>(gx::texture::texture_source_size(fmt, width, height, 1));
  if (texSize == 0) {
    return {};
  }
  Deferred deferred;
  TextureHandle created;
  TextureHandle result;
  for (int attempt = 0; attempt < 2; ++attempt) {
    bool needTexture = false;
    {
      std::lock_guard lock{g_mutex};
      result = resolve_locked(obj, data, fmt, width, height, texSize, created, needTexture, deferred);
    }
    if (!needTexture) {
      break;
    }
    created = new_conv_texture(width, height, fmt, "EFB copy RAM decoded texture");
  }
  for (const void* dest : deferred.evict) {
    gx::evict_copy_texture(dest);
  }
  return result;
}

void set_generation(uint8_t generation, bool replaying, bool noReadback) {
  std::lock_guard lock{g_mutex};
  g_generation = generation & 3;
  g_replaying = replaying;
  g_noReadback = noReadback;
}

void save_slot(uint8_t slot) {
  std::optional<std::vector<Entry>> old; // released after g_mutex
  std::lock_guard lock{g_mutex};
  old = std::move(g_entrySlots[slot & 3]);
  g_entrySlots[slot & 3] = g_entries;
}

void restore_slot(uint8_t slot) {
  std::vector<Entry> old; // released after g_mutex
  std::lock_guard lock{g_mutex};
  if (g_entrySlots[slot & 3]) {
    old = std::move(g_entries);
    g_entries = *g_entrySlots[slot & 3];
  }
}

void sweep(std::vector<const void*>& evict) {
  // Released after g_mutex (lock order: no Dawn object is released under it).
  std::vector<Entry> dropped;
  std::vector<LazyBufferRef> buffers;
  {
    std::lock_guard lock{g_mutex};
    const uint32_t frame = current_frame();
    if (!g_enabled.load(std::memory_order_relaxed)) {
      return;
    }
    // (Unlike resolve_texture, also while frame interpolation replays tapes: those restore their
    // copies from the replay slots, which lose an evicted destination too. The memory is checked
    // as the game has it now, ahead of the replays; a copy the game still uses is not written by
    // the CPU.)
    for (auto it = g_entries.begin(); it != g_entries.end();) {
      if (it->verifiedFrame == frame || hash_range(it->dest, it->size) == it->ramHash) {
        it->verifiedFrame = frame;
        ++it;
        continue;
      }
      ++g_stats.cpuOverwrites;
      const uint8_t* dest = it->dest;
      dropped.push_back(std::move(*it));
      it = g_entries.erase(it);
      if (std::none_of(g_entries.begin(), g_entries.end(), [&](const Entry& e) { return e.dest == dest; })) {
        evict.push_back(dest);
        g_written.erase(dest);
      }
    }
    for (auto& slot : g_entrySlots) {
      if (!slot) {
        continue;
      }
      for (auto it = slot->begin(); it != slot->end();) {
        if (std::find(evict.begin(), evict.end(), it->dest) != evict.end()) {
          dropped.push_back(std::move(*it));
          it = slot->erase(it);
        } else {
          ++it;
        }
      }
    }
    // A byte buffer only the pool holds belongs to no tracked copy (nor a replay slot's).
    for (auto it = g_bytePool.begin(); it != g_bytePool.end();) {
      if (it->second.use_count() == 1) {
        buffers.push_back(std::move(it->second));
        g_bytePool.erase(it++);
      } else {
        ++it;
      }
    }
  }
}

} // namespace aurora::gfx::efb_ram

using namespace aurora::gfx::efb_ram;

void aurora_efb_ram_set_enabled(bool enabled) {
  if (enabled == g_enabled.exchange(enabled)) {
    return;
  }
  std::vector<Entry> entries; // released after g_mutex
  absl::flat_hash_map<DecodedKey, Decoded> decoded;
  std::lock_guard lock{g_mutex};
  entries.swap(g_entries);
  decoded.swap(g_decoded);
  g_cpuReadRanges.clear();
}

bool aurora_efb_ram_enabled() { return enabled(); }

void aurora_efb_ram_set_readback_all(bool enabled) { g_readbackAll = enabled; }
void aurora_efb_ram_set_scaled_depth_reads(bool enabled) { g_scaledDepthReads = enabled; }

void aurora_efb_ram_cpu_read(const void* ptr, uint32_t size) {
  if (!enabled() || size == 0) {
    return;
  }
  {
    std::lock_guard lock{g_mutex};
    const auto* p = static_cast<const uint8_t*>(ptr);
    bool known = false;
    bool copied = false;
    for (const auto& [rp, rs] : g_cpuReadRanges) {
      known = known || (p >= rp && p + size <= rp + rs);
    }
    for (const auto& e : g_entries) {
      copied = copied || overlaps(e.dest, e.size, p, size);
    }
    // Only ranges that hold EFB copies matter; remember a bounded number of them.
    if (!known && copied) {
      if (g_cpuReadRanges.size() >= 32) {
        g_cpuReadRanges.erase(g_cpuReadRanges.begin());
      }
      g_cpuReadRanges.emplace_back(p, size);
      Log.info("CPU reads EFB copy memory at {} (+{:#x}); copies there are read back", ptr, size);
    }
  }
  aurora_efb_ram_apply();
}

void aurora_efb_ram_guest_copy(const void* ptr, uint32_t width, uint32_t height, uint32_t format) {
  const auto fmt = static_cast<GXTexFmt>(format);
  if (!enabled() || !supports_copy_format(fmt) || width == 0 || height == 0) {
    return;
  }
  const auto size = static_cast<uint32_t>(GXGetTexBufferSize(width, height, fmt, GX_FALSE, 0));
  if (size == 0) {
    return;
  }
  const auto* dest = static_cast<const uint8_t*>(ptr);
  std::lock_guard lock{g_mutex};
  const CpuTimer timer;
  const aurora::HashType h = hash_range(dest, size);
  std::erase_if(g_guestCopies, [&](const GuestCopy& g) { return g.dest == dest; });
  if (g_guestCopies.size() >= MaxGuestCopies) {
    g_guestCopies.erase(g_guestCopies.begin());
  }
  g_guestCopies.push_back({dest, size, h, std::chrono::steady_clock::now()});
}

void aurora_efb_ram_watch(const void* ptr, uint32_t size) {
  std::lock_guard lock{g_mutex};
  g_watchRanges.emplace_back(static_cast<const uint8_t*>(ptr), size);
}

uint32_t aurora_efb_ram_apply() {
  finish_maps();
  std::deque<std::shared_ptr<Readback>> landed;
  {
    std::lock_guard lock{g_landedMutex};
    landed.swap(g_landed);
  }
  if (landed.empty()) {
    return 0;
  }
  std::sort(landed.begin(), landed.end(), [](const auto& a, const auto& b) { return a->seq < b->seq; });
  uint32_t written = 0;
  for (auto& rb : landed) {
    bool wrote = false;
    if (rb->ok) {
      std::lock_guard lock{g_mutex};
      const CpuTimer timer;
      auto* dest = const_cast<uint8_t*>(rb->dest);
      const aurora::HashType now = hash_range(dest, rb->size);
      const auto prev = g_written.find(rb->dest);
      const bool ours = prev != g_written.end() && prev->second.first == rb->size && prev->second.second == now;
      if (now == rb->preHash || ours) {
        std::memcpy(dest, rb->data.data(), rb->size);
        const aurora::HashType h = hash_range(dest, rb->size);
        g_written[rb->dest] = {rb->size, h};
        for (auto& e : g_entries) {
          if (e.dest == rb->dest && e.size == rb->size) {
            e.ramHash = h;
          }
        }
        ++g_stats.writebacks;
        ++written;
        wrote = true;
      } else {
        ++g_stats.skipped;
        if (rb->guestKnown && now == rb->hostHash) {
          // Unchanged since Aurora executed the copy, but changed since the game issued it: the
          // range was reused in between. Before aurora_efb_ram_guest_copy this was written over.
          if (++g_stats.staleSkipped <= 20) {
            Log.warn("late EFB copy readback to {} (+{:#x}) dropped: the game wrote the range after issuing the "
                     "copy (writing it would have overwritten the new contents)",
                     static_cast<const void*>(dest), rb->size);
          }
        }
      }
    }
    if (wrote && g_writebackFn != nullptr) {
      g_writebackFn(rb->dest, rb->size, rb->fmt, rb->width, rb->height, g_writebackUser);
    }
  }
  return written;
}

void aurora_efb_ram_set_writeback_callback(AuroraEfbRamWritebackFn fn, void* userdata) {
  g_writebackFn = fn;
  g_writebackUser = userdata;
}

void aurora_efb_ram_get_stats(AuroraEfbRamStats* out) {
  std::lock_guard lock{g_mutex};
  *out = g_stats;
}

// 0250: cache sizes for aurora_mem_stats.
namespace aurora::gfx::efb_ram {
void mem_stats(AuroraMemStats& out) {
  std::lock_guard lock{g_mutex};
  out.efbEntries = g_entries.size();
  out.efbPoolBuffers = g_bytePool.size();
  out.efbPoolBytes = 0;
  for (const auto& [_, buf] : g_bytePool) {
    if (buf && buf->buffer) {
      out.efbPoolBytes += buf->size;
    }
  }
  out.efbDecoded = g_decoded.size();
  out.efbWritten = g_written.size();
  out.efbGuestCopies = g_guestCopies.size();
}
} // namespace aurora::gfx::efb_ram
