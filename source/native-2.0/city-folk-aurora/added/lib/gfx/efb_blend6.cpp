#include "efb_blend6.hpp"

#include "frame_packet.hpp"
#include "../gx/gx.hpp"
#include "../webgpu/gpu.hpp"
#include "../webgpu/gpu_prof.hpp"
#include "../logging.hpp"

#include <absl/container/flat_hash_map.h>
#include <fmt/format.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace aurora::gfx::efb_blend6 {
namespace {
using webgpu::g_device;
Module Log("aurora::gfx::efb_blend6");

// A single-sampled target is copied into a scratch texture and quantized back in one full-screen
// pass. A multisampled one is quantized per sample into the scratch, then copied back by a second
// pass that also redoes the MSAA resolve.
//
// Quantization per channel, as the EFB stores an RGBA6 pixel after blending: the 8-bit result,
// dithered with the 2x2 ordered matrix (RGB only, when the pass's blended draws dither; the same
// formula as the in-shader quantization of unblended draws, Aurora 0010), truncated to 6 bits and
// widened back ((v << 2) | (v >> 4)). A channel that already holds a widened 6-bit value is a
// stored pixel (unblended draws are quantized in their shader; earlier passes were resolved) and is
// kept: re-dithering it could move it by one step. Alpha is truncated (never dithered).
std::string shader_source(bool multisampled, bool dither, bool quantize) {
  std::string s;
  s += multisampled ? "@group(0) @binding(0) var src: texture_multisampled_2d<f32>;\n"
                    : "@group(0) @binding(0) var src: texture_2d<f32>;\n";
  s += "@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
       "  let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
       "  return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);\n"
       "}\n";
  s += multisampled ? "@fragment fn fs_main(@builtin(position) pos: vec4f, @builtin(sample_index) si: u32) -> "
                      "@location(0) vec4f {\n  let c = textureLoad(src, vec2i(pos.xy), i32(si));\n"
                    : "@fragment fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {\n"
                      "  let c = textureLoad(src, vec2i(pos.xy), 0);\n";
  if (!quantize) {
    s += "  return c;\n}\n";
    return s;
  }
  s += "  let q8 = vec4i(round(clamp(c, vec4f(0.0), vec4f(1.0)) * 255.0));\n"
       "  let hi = q8 >> vec4u(2u);\n"
       "  let wid = (hi << vec4u(2u)) | (hi >> vec4u(4u));\n"
       "  var rgb = q8.rgb;\n";
  if (dither) {
    s += "  let dp = vec2u(pos.xy) & vec2u(1u);\n"
         "  let dq = i32(select(select(0u, 2u, dp.x == 1u), select(3u, 1u, dp.x == 1u), dp.y == 1u));\n"
         "  rgb = select(rgb - (rgb >> vec3u(6u)) + vec3i(dq), rgb, q8.rgb == wid.rgb);\n";
  }
  s += "  let qn = rgb >> vec3u(2u);\n"
       "  let out = (qn << vec3u(2u)) | (qn >> vec3u(4u));\n"
       "  return vec4f(vec4i(out, wid.a)) / 255.0;\n"
       "}\n";
  return s;
}

struct PipelineKey {
  wgpu::TextureFormat format;
  uint32_t samples;
  uint8_t mode; // 0 copy back, 1 quantize, 2 quantize + dither

  bool operator==(const PipelineKey& rhs) const {
    return format == rhs.format && samples == rhs.samples && mode == rhs.mode;
  }
  template <typename H>
  friend H AbslHashValue(H h, const PipelineKey& k) {
    return H::combine(std::move(h), static_cast<uint32_t>(k.format), k.samples, k.mode);
  }
};
struct Pipeline {
  wgpu::RenderPipeline pipeline;
  wgpu::BindGroupLayout layout;
};
absl::flat_hash_map<PipelineKey, Pipeline> g_pipelines;

const Pipeline& pipeline_for(const PipelineKey& key) {
  auto it = g_pipelines.find(key);
  if (it != g_pipelines.end()) {
    return it->second;
  }
  const bool ms = key.samples > 1;
  const auto src = shader_source(ms, key.mode == 2, key.mode != 0);
  const wgpu::ShaderSourceWGSL wgsl{wgpu::ShaderSourceWGSL::Init{.code = src.c_str()}};
  const wgpu::ShaderModuleDescriptor modDesc{.nextInChain = &wgsl, .label = "EFB blend6 resolve"};
  const auto module = g_device.CreateShaderModule(&modDesc);
  const wgpu::BindGroupLayoutEntry entry{
      .binding = 0,
      .visibility = wgpu::ShaderStage::Fragment,
      .texture =
          {
              .sampleType = wgpu::TextureSampleType::UnfilterableFloat,
              .viewDimension = wgpu::TextureViewDimension::e2D,
              .multisampled = ms,
          },
  };
  const wgpu::BindGroupLayoutDescriptor layoutDesc{.label = "EFB blend6 resolve", .entryCount = 1, .entries = &entry};
  Pipeline p;
  p.layout = g_device.CreateBindGroupLayout(&layoutDesc);
  const wgpu::PipelineLayoutDescriptor plDesc{.bindGroupLayoutCount = 1, .bindGroupLayouts = &p.layout};
  const std::array targets{wgpu::ColorTargetState{.format = key.format}};
  const wgpu::FragmentState fragment{
      .module = module, .entryPoint = "fs_main", .targetCount = targets.size(), .targets = targets.data()};
  const wgpu::RenderPipelineDescriptor desc{
      .label = "EFB blend6 resolve",
      .layout = g_device.CreatePipelineLayout(&plDesc),
      .vertex = {.module = module, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .multisample = {.count = key.samples},
      .fragment = &fragment,
  };
  const ::aurora::webgpu::CreateTimer createTimer_123{"pipeline efb_blend6.cpp:123"}; // 0223 (AURORA_CREATE_LOG)
  p.pipeline = g_device.CreateRenderPipeline(&desc);
  return g_pipelines.emplace(key, std::move(p)).first->second;
}

struct Scratch {
  wgpu::Texture texture;
  wgpu::TextureView view;
  wgpu::Extent3D size;
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  uint32_t samples = 0;
};
Scratch g_scratch;

const Scratch& scratch_for(const wgpu::Extent3D& size, wgpu::TextureFormat format, uint32_t samples) {
  if (!g_scratch.texture || g_scratch.size.width != size.width || g_scratch.size.height != size.height ||
      g_scratch.format != format || g_scratch.samples != samples) {
    const wgpu::TextureDescriptor desc{
        .label = "EFB blend6 scratch",
        .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding |
                 (samples == 1 ? wgpu::TextureUsage::CopyDst : wgpu::TextureUsage::None),
        .dimension = wgpu::TextureDimension::e2D,
        .size = {size.width, size.height, 1},
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = samples,
    };
    g_scratch.texture = g_device.CreateTexture(&desc);
    g_scratch.view = g_scratch.texture.CreateView();
    g_scratch.size = {size.width, size.height, 1};
    g_scratch.format = format;
    g_scratch.samples = samples;
  }
  return g_scratch;
}

// AURORA_EFB_BLEND6_TIMING=1 (needs TimestampQuery): GPU time of the resolves, logged as the
// average per resolve and per frame-equivalent every ~600 resolves.
struct TimingReadback {
  wgpu::QuerySet querySet;
  wgpu::Buffer resolve;
  wgpu::Buffer readback;
};
std::mutex g_timingMutex;
std::vector<std::shared_ptr<TimingReadback>> g_timingPool;
std::atomic<uint64_t> g_timingNs{0};
std::atomic<uint32_t> g_timingCount{0};
std::atomic<uint64_t> g_resolvedPixels{0};
std::atomic<uint32_t> g_mixedDither{0}; // resolves whose pass mixed dithered and undithered blends

bool timing_on() {
  static const bool on = std::getenv("AURORA_EFB_BLEND6_TIMING") != nullptr &&
                         g_device.HasFeature(wgpu::FeatureName::TimestampQuery);
  return on;
}

std::shared_ptr<TimingReadback> acquire_timing() {
  {
    std::lock_guard lock{g_timingMutex};
    if (!g_timingPool.empty()) {
      auto t = std::move(g_timingPool.back());
      g_timingPool.pop_back();
      return t;
    }
  }
  auto t = std::make_shared<TimingReadback>();
  const wgpu::QuerySetDescriptor qd{.label = "EFB blend6 timing", .type = wgpu::QueryType::Timestamp, .count = 2};
  t->querySet = g_device.CreateQuerySet(&qd);
  const wgpu::BufferDescriptor rd{.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc, .size = 16};
  t->resolve = g_device.CreateBuffer(&rd);
  const wgpu::BufferDescriptor bd{.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst, .size = 16};
  t->readback = g_device.CreateBuffer(&bd);
  return t;
}

void finish_timing(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame, std::shared_ptr<TimingReadback> t) {
  cmd.ResolveQuerySet(t->querySet, 0, 2, t->resolve, 0);
  cmd.CopyBufferToBuffer(t->resolve, 0, t->readback, 0, 16);
  // Mapped once this frame's commands are submitted.
  frame.afterSubmitCallbacks.emplace_back([t] {
    t->readback.MapAsync(wgpu::MapMode::Read, 0, 16, wgpu::CallbackMode::AllowSpontaneous,
                         [t](wgpu::MapAsyncStatus status, wgpu::StringView) {
      if (status == wgpu::MapAsyncStatus::Success) {
        const auto* ts = static_cast<const uint64_t*>(t->readback.GetConstMappedRange(0, 16));
        if (ts != nullptr && ts[1] > ts[0]) {
          g_timingNs += ts[1] - ts[0];
          const uint32_t n = ++g_timingCount;
          if (n % 600 == 0) {
            const double ms = static_cast<double>(g_timingNs.exchange(0)) / 1e6;
            const uint64_t px = g_resolvedPixels.exchange(0);
            g_timingCount = 0;
            Log.info("[efb-blend6] {} resolves: {:.4f} ms each ({:.2f} Mpx each, {} with mixed dither)", n, ms / n,
                     static_cast<double>(px) / n / 1e6, g_mixedDither.exchange(0));
          }
        }
        t->readback.Unmap();
      }
      std::lock_guard lock{g_timingMutex};
      g_timingPool.push_back(t);
    });
  });
}

wgpu::BindGroup bind_group(const Pipeline& p, const wgpu::TextureView& view) {
  const wgpu::BindGroupEntry entry{.binding = 0, .textureView = view};
  const wgpu::BindGroupDescriptor desc{.label = "EFB blend6 resolve", .layout = p.layout, .entryCount = 1, .entries = &entry};
  return g_device.CreateBindGroup(&desc);
}

// Flags carried past a pass that ended in a tiny copy (EFB peek probes) to the next pass on the
// same target.
struct Pending {
  WGPUTextureView view = nullptr;
  uint8_t flags = 0;
};
Pending g_pending;
// 0212: the last pass's resolve, run (or dropped) at the next op (before_op).
struct Deferred {
  const detail::RenderPass* pass = nullptr;
  uint8_t flags = 0;
};
Deferred g_deferred;
uint64_t g_skipped = 0;
void resolve(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame, const detail::RenderPass& pass, uint8_t flags);
} // namespace

void after_pass(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame, const detail::RenderPass& pass) {
  if (pass.colorAttachmentCount == 0) {
    return;
  }
  const auto& color = pass.colorAttachments[SceneColorAttachmentIndex];
  uint8_t flags = pass.efbBlend6;
  if (g_pending.flags != 0 && g_pending.view == color.view.Get()) {
    flags |= g_pending.flags;
    g_pending = {};
  }
  if (flags == 0) {
    return;
  }
  if (!color.view || (color.format != wgpu::TextureFormat::RGBA8Unorm && color.format != wgpu::TextureFormat::BGRA8Unorm)) {
    return;
  }
  // A pass ended by a copy of a few pixels (GXPeekARGB's 4x4 probe) is not quantized: peek answers
  // are converted to the EFB format by the caller anyway. The next pass on this target takes its
  // flags over and quantizes everything when it ends.
  if (pass.resolveTarget && !pass.snapshotColorDst && pass.resolveRect.width * pass.resolveRect.height <= 64) {
    g_pending = {color.view.Get(), flags};
    return;
  }
  // 0212: nothing in this pass reads its color after it (no color copy, no snapshot, no post-op):
  // resolve before the next op, or not at all when that op clears this color target completely.
  static const bool sNoDefer = std::getenv("AURORA_EFB_BLEND6_NODEFER") != nullptr; // A/B
  if (!sNoDefer && !pass.snapshotColorDst && !pass.colorPostOps &&
      (!pass.resolveTarget || gx::is_depth_format(pass.resolveFormat))) {
    g_deferred = {&pass, flags};
    return;
  }
  resolve(cmd, frame, pass, flags);
}

namespace {
void resolve(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame, const detail::RenderPass& pass, uint8_t flags) {
  const auto& color = pass.colorAttachments[SceneColorAttachmentIndex];
  const uint32_t samples = std::max(pass.msaaSamples, 1u);
  const auto& scratch = scratch_for(color.size, color.format, samples);
  const Pipeline quantize = pipeline_for({color.format, samples, static_cast<uint8_t>((flags & 2) != 0 ? 2 : 1)});
  const Pipeline copyBack = pipeline_for({color.format, samples, 0});

  std::shared_ptr<TimingReadback> timing;
  std::array<wgpu::PassTimestampWrites, 2> tw{};
  if (timing_on()) {
    timing = acquire_timing();
    tw[0] = {.querySet = timing->querySet, .beginningOfPassWriteIndex = 0};
    tw[1] = {.querySet = timing->querySet, .endOfPassWriteIndex = 1};
    tw[0].endOfPassWriteIndex = wgpu::kQuerySetIndexUndefined;
    tw[1].beginningOfPassWriteIndex = wgpu::kQuerySetIndexUndefined;
    g_resolvedPixels += static_cast<uint64_t>(color.size.width) * color.size.height;
    if (flags == 3) {
      ++g_mixedDither;
    }
  }

  const auto runPass = [&](const char* label, const wgpu::TextureView& target, const wgpu::TextureView& resolveTarget,
                           const Pipeline& p, const wgpu::TextureView& src, const wgpu::PassTimestampWrites* writes) {
    const wgpu::RenderPassColorAttachment att{
        .view = target,
        .resolveTarget = resolveTarget,
        .loadOp = wgpu::LoadOp::Load,
        .storeOp = wgpu::StoreOp::Store,
    };
    const wgpu::RenderPassDescriptor desc{
        .label = label,
        .colorAttachmentCount = 1,
        .colorAttachments = &att,
        .timestampWrites = writes != nullptr ? writes : webgpu::gpu_prof::pass_writes(label),
    };
    const auto rp = cmd.BeginRenderPass(&desc);
    rp.SetPipeline(p.pipeline);
    rp.SetBindGroup(0, bind_group(p, src));
    rp.Draw(3);
    rp.End();
  };
  if (samples == 1 && pass.copySourceTexture && pass.copySourceView.Get() == color.view.Get()) {
    // Single-sampled target: a plain texture copy into the scratch, then one quantizing pass back.
    const wgpu::TexelCopyTextureInfo src{.texture = pass.copySourceTexture};
    const wgpu::TexelCopyTextureInfo dst{.texture = scratch.texture};
    const wgpu::Extent3D size{color.size.width, color.size.height, 1};
    {
      const webgpu::gpu_prof::Zone zone{cmd, "EFB blend6 copy"};
      cmd.CopyTextureToTexture(&src, &dst, &size);
    }
    if (timing) {
      tw[0].endOfPassWriteIndex = 1;
    }
    runPass("EFB blend6 quantize", color.view, {}, quantize, scratch.view, timing ? &tw[0] : nullptr);
  } else {
    // Multisampled: quantize every sample into the scratch, then copy it back (and resolve).
    runPass("EFB blend6 quantize", scratch.view, {}, quantize, color.view, timing ? &tw[0] : nullptr);
    runPass("EFB blend6 store", color.view, color.resolveView, copyBack, scratch.view, timing ? &tw[1] : nullptr);
  }
  if (timing) {
    finish_timing(cmd, frame, std::move(timing));
  }
}
} // namespace

void before_op(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame, const detail::RenderPass* next) {
  if (g_deferred.pass == nullptr) {
    return;
  }
  const auto deferred = std::exchange(g_deferred, Deferred{});
  if (next != nullptr && next->sealed && !next->discardable && next->ramPreOps.empty() && next->paletteConvs.empty() &&
      next->colorAttachmentCount > 0) {
    const auto& prevColor = deferred.pass->colorAttachments[SceneColorAttachmentIndex];
    const auto& color = next->colorAttachments[SceneColorAttachmentIndex];
    const bool clears = color.loadOp == wgpu::LoadOp::Clear || (color.loadOp == wgpu::LoadOp::Undefined && color.clear);
    if (clears && color.view.Get() == prevColor.view.Get() && color.resolveView.Get() == prevColor.resolveView.Get()) {
      ++g_skipped;
      return; // every quantized pixel would be cleared before anything reads it
    }
  }
  resolve(cmd, frame, *deferred.pass, deferred.flags);
}

void flush(const wgpu::CommandEncoder& cmd, detail::FramePacket& frame) { before_op(cmd, frame, nullptr); }

void initialize() {
  // Built up front: creating them at the first resolve, on the render worker, held the device
  // while the pipeline-cache workers waited and draws timed out (~260 dropped draws once).
  const auto format = webgpu::g_graphicsConfig.surfaceConfiguration.format;
  if (format != wgpu::TextureFormat::RGBA8Unorm && format != wgpu::TextureFormat::BGRA8Unorm) {
    return;
  }
  const uint32_t msaa = std::max(webgpu::g_graphicsConfig.msaaSamples, 1u);
  for (const uint32_t samples : {1u, msaa}) {
    for (uint8_t mode = 0; mode < 3; ++mode) {
      pipeline_for({format, samples, mode});
    }
  }
}

void shutdown() {
  g_pipelines.clear();
  g_scratch = {};
  std::lock_guard lock{g_timingMutex};
  g_timingPool.clear();
}

} // namespace aurora::gfx::efb_blend6
