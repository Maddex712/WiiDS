// Post-process chain applied at present time. See include/aurora/postfx.h.
#include "../activity.hpp"
#include "postfx.hpp"

#include <aurora/postfx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include <webgpu/webgpu_cpp.h>

#include "../internal.hpp"
#include "gpu_prof.hpp"

namespace aurora::webgpu::postfx {
namespace {
Module Log("aurora::postfx");

constexpr uint32_t BuiltinSlotStride = 256; // minUniformBufferOffsetAlignment upper bound

struct BuiltinBlock {
  float outputSize[2];
  float sourceSize[2];
  float viewport[4];
  float surfaceSize[2];
  uint32_t frame;
  uint32_t pad;
};
static_assert(sizeof(BuiltinBlock) == 48);

constexpr std::string_view Prelude = R"(
struct AuroraPostFx {
    output_size: vec2f,
    source_size: vec2f,
    viewport: vec4f,
    surface_size: vec2f,
    frame: u32,
};
@group(0) @binding(0) var<uniform> aurora_postfx: AuroraPostFx;
@group(0) @binding(2) var aurora_linear: sampler;
struct AuroraPostFxVertex {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};
@vertex
fn aurora_postfx_vs(@builtin(vertex_index) vtx: u32) -> AuroraPostFxVertex {
    let p = vec2f(f32((vtx << 1u) & 2u), f32(vtx & 2u));
    var out: AuroraPostFxVertex;
    out.position = vec4f(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    out.uv = p;
    return out;
}
)";

struct PassDesc {
  std::string entryPoint;
  AuroraPostFxSize width;
  AuroraPostFxSize height;
  bool hdr;
  uint32_t inputCount;
  std::array<uint32_t, AURORA_POSTFX_MAX_INPUTS> inputs;

  bool operator==(const PassDesc& o) const {
    return entryPoint == o.entryPoint && width.mode == o.width.mode && width.value == o.width.value &&
           height.mode == o.height.mode && height.value == o.height.value && hdr == o.hdr &&
           inputCount == o.inputCount && inputs == o.inputs;
  }
};

// Shared between the API (any thread) and the render worker.
std::mutex g_mutex;
std::string g_pendingSource;
std::vector<PassDesc> g_pendingPasses;
uint64_t g_pendingGeneration = 0;
std::vector<uint8_t> g_pendingUniforms;
uint64_t g_uniformGeneration = 0;
bool g_failed = false;

// Render worker state.
struct Target {
  wgpu::Texture texture;
  wgpu::TextureView view;
  uint32_t width = 0;
  uint32_t height = 0;
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
};
struct BuiltPass {
  PassDesc desc;
  wgpu::RenderPipeline pipeline;
  wgpu::BindGroupLayout layout;
  Target target;
};
uint64_t g_builtGeneration = 0;
uint64_t g_builtUniformGeneration = 0;
std::string g_moduleSource;
wgpu::ShaderModule g_module;
std::map<std::tuple<std::string, uint32_t, wgpu::TextureFormat>, std::pair<wgpu::RenderPipeline, wgpu::BindGroupLayout>>
    g_pipelineCache;
std::vector<BuiltPass> g_passes;
bool g_ready = false;
wgpu::Sampler g_linearSampler;
wgpu::Buffer g_builtinBuffer;
wgpu::Buffer g_userBuffer;
uint64_t g_userBufferSize = 0;
std::vector<uint8_t> g_uniformScratch;
wgpu::BindGroup g_finalBindGroup;
uint32_t g_frame = 0;

wgpu::TextureFormat target_format(const PassDesc& desc, bool last) {
  if (!last && desc.hdr) {
    return wgpu::TextureFormat::RGBA16Float;
  }
  return g_graphicsConfig.surfaceConfiguration.format;
}

bool wait_error_scope(const char* what) {
  bool ok = true;
  std::string message;
  const auto future = g_device.PopErrorScope(
      wgpu::CallbackMode::WaitAnyOnly,
      [&](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView msg) {
        if (status == wgpu::PopErrorScopeStatus::Success && type != wgpu::ErrorType::NoError) {
          ok = false;
          message = std::string(msg.data, msg.length);
        }
      });
  g_instance.WaitAny(future, UINT64_MAX);
  if (!ok) {
    Log.error("{} failed: {}", what, message);
  }
  return ok;
}

std::pair<wgpu::RenderPipeline, wgpu::BindGroupLayout> get_pipeline(const PassDesc& desc, wgpu::TextureFormat format) {
  const auto key = std::make_tuple(desc.entryPoint, desc.inputCount, format);
  if (const auto it = g_pipelineCache.find(key); it != g_pipelineCache.end()) {
    return it->second;
  }
  std::vector<wgpu::BindGroupLayoutEntry> entries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex,
          .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(BuiltinBlock)},
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Fragment,
          .buffer = {.type = wgpu::BufferBindingType::Uniform},
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler = {.type = wgpu::SamplerBindingType::Filtering},
      },
  };
  for (uint32_t i = 0; i < desc.inputCount; ++i) {
    entries.push_back(wgpu::BindGroupLayoutEntry{
        .binding = 3 + i,
        .visibility = wgpu::ShaderStage::Fragment,
        .texture = {.sampleType = wgpu::TextureSampleType::Float, .viewDimension = wgpu::TextureViewDimension::e2D},
    });
  }
  const wgpu::BindGroupLayoutDescriptor layoutDesc{
      .label = "PostFx bind group layout",
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  auto layout = g_device.CreateBindGroupLayout(&layoutDesc);
  const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &layout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&pipelineLayoutDesc);
  const wgpu::ColorTargetState colorTarget{.format = format, .writeMask = wgpu::ColorWriteMask::All};
  const wgpu::FragmentState fragment{
      .module = g_module,
      .entryPoint = desc.entryPoint.c_str(),
      .targetCount = 1,
      .targets = &colorTarget,
  };
  const wgpu::RenderPipelineDescriptor pipelineDesc{
      .label = "PostFx pipeline",
      .layout = pipelineLayout,
      .vertex = {.module = g_module, .entryPoint = "aurora_postfx_vs"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .multisample = {.count = 1, .mask = UINT32_MAX},
      .fragment = &fragment,
  };
  const ::aurora::webgpu::CreateTimer createTimer_195{"pipeline postfx.cpp:195"}; // 0223 (AURORA_CREATE_LOG)
  auto pipeline = g_device.CreateRenderPipeline(&pipelineDesc);
  auto result = std::make_pair(std::move(pipeline), std::move(layout));
  g_pipelineCache.emplace(key, result);
  return result;
}

bool build(const std::string& source, const std::vector<PassDesc>& passes) {
  g_passes.clear();
  if (source.empty() || passes.empty()) {
    return false;
  }
  if (!g_linearSampler) {
    const wgpu::SamplerDescriptor samplerDesc{
        .label = "PostFx linear sampler",
        .addressModeU = wgpu::AddressMode::ClampToEdge,
        .addressModeV = wgpu::AddressMode::ClampToEdge,
        .addressModeW = wgpu::AddressMode::ClampToEdge,
        .magFilter = wgpu::FilterMode::Linear,
        .minFilter = wgpu::FilterMode::Linear,
        .mipmapFilter = wgpu::MipmapFilterMode::Nearest,
    };
    g_linearSampler = g_device.CreateSampler(&samplerDesc);
    const wgpu::BufferDescriptor bufferDesc{
        .label = "PostFx builtin uniforms",
        .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform,
        .size = BuiltinSlotStride * AURORA_POSTFX_MAX_PASSES,
    };
    g_builtinBuffer = g_device.CreateBuffer(&bufferDesc);
  }
  for (size_t i = 0; i < passes.size(); ++i) {
    for (uint32_t j = 0; j < passes[i].inputCount; ++j) {
      if (passes[i].inputs[j] > i) {
        Log.error("Pass {} ({}) reads the output of pass {}, which has not run yet", i, passes[i].entryPoint,
                  passes[i].inputs[j] - 1);
        return false;
      }
    }
  }

  g_device.PushErrorScope(wgpu::ErrorFilter::Validation);
  if (source != g_moduleSource || !g_module) {
    g_pipelineCache.clear();
    std::string code{Prelude};
    code += source;
    wgpu::ShaderSourceWGSL wgsl{};
    wgsl.code = code.c_str();
    const wgpu::ShaderModuleDescriptor moduleDesc{.nextInChain = &wgsl, .label = "PostFx module"};
    g_module = g_device.CreateShaderModule(&moduleDesc);
    g_moduleSource = source;
  }
  for (size_t i = 0; i < passes.size(); ++i) {
    const auto format = target_format(passes[i], i + 1 == passes.size());
    auto [pipeline, layout] = get_pipeline(passes[i], format);
    g_passes.push_back({.desc = passes[i], .pipeline = pipeline, .layout = layout});
  }
  if (!wait_error_scope("PostFx shader/pipeline creation")) {
    g_module = {};
    g_moduleSource.clear();
    g_pipelineCache.clear();
    g_passes.clear();
    return false;
  }
  Log.info("Post-process chain ready ({} passes)", g_passes.size());
  return true;
}

uint32_t resolve_extent(const AuroraPostFxSize& size, uint32_t viewportExtent, uint32_t sourceExtent,
                        uint32_t inputExtent) {
  double v = 0.0;
  switch (size.mode) {
  case AURORA_POSTFX_SIZE_VIEWPORT:
    v = size.value * viewportExtent;
    break;
  case AURORA_POSTFX_SIZE_SOURCE:
  default:
    v = size.value * sourceExtent;
    break;
  case AURORA_POSTFX_SIZE_INPUT:
    v = size.value * inputExtent;
    break;
  case AURORA_POSTFX_SIZE_ABSOLUTE:
    v = size.value;
    break;
  }
  return static_cast<uint32_t>(std::clamp(std::lround(v), 1l, 8192l));
}

void ensure_target(Target& target, uint32_t width, uint32_t height, wgpu::TextureFormat format) {
  if (target.texture && target.width == width && target.height == height && target.format == format) {
    return;
  }
  const wgpu::TextureDescriptor desc{
      .label = "PostFx target",
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
      .size = {width, height, 1},
      .format = format,
  };
  target.texture = g_device.CreateTexture(&desc);
  target.view = target.texture.CreateView();
  target.width = width;
  target.height = height;
  target.format = format;
}

wgpu::BindGroup make_bind_group(const BuiltPass& pass, uint32_t index, const std::vector<wgpu::TextureView>& views) {
  std::vector<wgpu::BindGroupEntry> entries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .buffer = g_builtinBuffer,
          .offset = uint64_t{index} * BuiltinSlotStride,
          .size = sizeof(BuiltinBlock),
      },
      wgpu::BindGroupEntry{.binding = 1, .buffer = g_userBuffer, .size = g_userBufferSize},
      wgpu::BindGroupEntry{.binding = 2, .sampler = g_linearSampler},
  };
  for (uint32_t i = 0; i < pass.desc.inputCount; ++i) {
    entries.push_back(wgpu::BindGroupEntry{.binding = 3 + i, .textureView = views[pass.desc.inputs[i]]});
  }
  const wgpu::BindGroupDescriptor desc{
      .label = "PostFx bind group",
      .layout = pass.layout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  return g_device.CreateBindGroup(&desc);
}

} // namespace

bool prepare(const wgpu::CommandEncoder& encoder, const Viewport& viewport) {
  std::vector<uint8_t> uniforms;
  bool uniformsChanged = false;
  {
    std::lock_guard lock{g_mutex};
    if (g_builtGeneration != g_pendingGeneration) {
      const activity::Scope busy{activity::Thread::RenderWorker, "building the post-process chain"};
      g_builtGeneration = g_pendingGeneration;
      g_ready = build(g_pendingSource, g_pendingPasses);
      g_failed = !g_ready && !g_pendingPasses.empty();
    }
    if (g_builtUniformGeneration != g_uniformGeneration) {
      g_builtUniformGeneration = g_uniformGeneration;
      uniforms = g_pendingUniforms;
      uniformsChanged = true;
    }
  }
  if (uniformsChanged || !g_userBuffer) {
    const uint64_t size = std::max<uint64_t>(16, (uniforms.size() + 15) & ~uint64_t{15});
    if (!g_userBuffer || size != g_userBufferSize) {
      const wgpu::BufferDescriptor desc{
          .label = "PostFx user uniforms",
          .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform,
          .size = size,
      };
      g_userBuffer = g_device.CreateBuffer(&desc);
      g_userBufferSize = size;
    }
    uniforms.resize(size, 0);
    g_queue.WriteBuffer(g_userBuffer, 0, uniforms.data(), uniforms.size());
  }
  g_finalBindGroup = {};
  if (!g_ready || g_passes.empty() || viewport.width < 1.f || viewport.height < 1.f) {
    return false;
  }

  const auto& source = present_source();
  const uint32_t srcW = source.size.width;
  const uint32_t srcH = source.size.height;
  const uint32_t vpW = std::max(1u, static_cast<uint32_t>(std::lround(viewport.width)));
  const uint32_t vpH = std::max(1u, static_cast<uint32_t>(std::lround(viewport.height)));
  const double srcAspect = srcH ? static_cast<double>(srcW) / srcH : 1.0;

  std::vector<wgpu::TextureView> views{source.view};
  std::vector<std::pair<uint32_t, uint32_t>> sizes{{srcW, srcH}};
  g_uniformScratch.assign(BuiltinSlotStride * g_passes.size(), 0);
  for (size_t i = 0; i < g_passes.size(); ++i) {
    auto& pass = g_passes[i];
    const bool last = i + 1 == g_passes.size();
    uint32_t w = vpW;
    uint32_t h = vpH;
    if (!last) {
      const auto in = pass.desc.inputCount > 0 ? sizes[pass.desc.inputs[0]] : sizes[0];
      const auto& dw = pass.desc.width;
      const auto& dh = pass.desc.height;
      if (dw.mode == AURORA_POSTFX_SIZE_ASPECT && dh.mode == AURORA_POSTFX_SIZE_ASPECT) {
        w = srcW;
        h = srcH;
      } else if (dw.mode == AURORA_POSTFX_SIZE_ASPECT) {
        h = resolve_extent(dh, vpH, srcH, in.second);
        w = resolve_extent({AURORA_POSTFX_SIZE_ABSOLUTE, static_cast<float>(dw.value * h * srcAspect)}, 0, 0, 0);
      } else if (dh.mode == AURORA_POSTFX_SIZE_ASPECT) {
        w = resolve_extent(dw, vpW, srcW, in.first);
        h = resolve_extent({AURORA_POSTFX_SIZE_ABSOLUTE, static_cast<float>(dh.value * w / srcAspect)}, 0, 0, 0);
      } else {
        w = resolve_extent(dw, vpW, srcW, in.first);
        h = resolve_extent(dh, vpH, srcH, in.second);
      }
      ensure_target(pass.target, w, h, target_format(pass.desc, false));
      views.push_back(pass.target.view);
    }
    sizes.emplace_back(w, h);
    const BuiltinBlock block{
        .outputSize = {static_cast<float>(w), static_cast<float>(h)},
        .sourceSize = {static_cast<float>(srcW), static_cast<float>(srcH)},
        .viewport = {viewport.left, viewport.top, viewport.width, viewport.height},
        .surfaceSize = {static_cast<float>(g_graphicsConfig.surfaceConfiguration.width),
                        static_cast<float>(g_graphicsConfig.surfaceConfiguration.height)},
        .frame = g_frame,
    };
    std::memcpy(g_uniformScratch.data() + i * BuiltinSlotStride, &block, sizeof(block));
  }
  g_queue.WriteBuffer(g_builtinBuffer, 0, g_uniformScratch.data(), g_uniformScratch.size());
  ++g_frame;

  for (size_t i = 0; i + 1 < g_passes.size(); ++i) {
    const auto& pass = g_passes[i];
    const auto bindGroup = make_bind_group(pass, static_cast<uint32_t>(i), views);
    const wgpu::RenderPassColorAttachment attachment{
        .view = pass.target.view,
        .loadOp = wgpu::LoadOp::Clear,
        .storeOp = wgpu::StoreOp::Store,
    };
    const wgpu::RenderPassDescriptor passDesc{
        .label = "PostFx pass",
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment,
        .timestampWrites = gpu_prof::pass_writes(fmt::format("PostFx {}", i)),
    };
    const auto rp = encoder.BeginRenderPass(&passDesc);
    rp.SetPipeline(pass.pipeline);
    rp.SetBindGroup(0, bindGroup, 0, nullptr);
    rp.Draw(3);
    rp.End();
  }
  g_finalBindGroup = make_bind_group(g_passes.back(), static_cast<uint32_t>(g_passes.size() - 1), views);
  return true;
}

void draw(const wgpu::RenderPassEncoder& pass) {
  pass.SetPipeline(g_passes.back().pipeline);
  pass.SetBindGroup(0, g_finalBindGroup, 0, nullptr);
  pass.Draw(3);
}

void shutdown() {
  g_passes.clear();
  g_pipelineCache.clear();
  g_module = {};
  g_moduleSource.clear();
  g_linearSampler = {};
  g_builtinBuffer = {};
  g_userBuffer = {};
  g_finalBindGroup = {};
  g_ready = false;
  g_builtGeneration = 0;
  g_builtUniformGeneration = 0;
}

} // namespace aurora::webgpu::postfx

void aurora_postfx_set_chain(const char* wgsl, const AuroraPostFxPass* passes, uint32_t passCount) {
  using namespace aurora::webgpu::postfx;
  std::string source = wgsl != nullptr ? wgsl : "";
  std::vector<PassDesc> descs;
  if (!source.empty() && passes != nullptr) {
    passCount = std::min<uint32_t>(passCount, AURORA_POSTFX_MAX_PASSES);
    for (uint32_t i = 0; i < passCount; ++i) {
      const auto& p = passes[i];
      PassDesc d{
          .entryPoint = p.entryPoint != nullptr ? p.entryPoint : "",
          .width = p.width,
          .height = p.height,
          .hdr = p.hdr,
          .inputCount = std::min<uint32_t>(p.inputCount, AURORA_POSTFX_MAX_INPUTS),
          .inputs = {},
      };
      for (uint32_t j = 0; j < d.inputCount; ++j) {
        d.inputs[j] = p.inputs[j];
      }
      descs.push_back(std::move(d));
    }
  }
  if (descs.empty()) {
    source.clear();
  }
  std::lock_guard lock{g_mutex};
  if (source == g_pendingSource && descs == g_pendingPasses) {
    return;
  }
  g_pendingSource = std::move(source);
  g_pendingPasses = std::move(descs);
  g_failed = false;
  ++g_pendingGeneration;
}

void aurora_postfx_set_uniforms(const void* data, uint32_t size) {
  using namespace aurora::webgpu::postfx;
  size = std::min<uint32_t>(size, AURORA_POSTFX_MAX_UNIFORM_SIZE);
  const auto* bytes = static_cast<const uint8_t*>(data);
  std::lock_guard lock{g_mutex};
  if (g_pendingUniforms.size() == size && (size == 0 || std::memcmp(g_pendingUniforms.data(), bytes, size) == 0)) {
    return;
  }
  g_pendingUniforms.assign(bytes, bytes + size);
  ++g_uniformGeneration;
}

bool aurora_postfx_active() {
  using namespace aurora::webgpu::postfx;
  std::lock_guard lock{g_mutex};
  return !g_pendingPasses.empty() && !g_failed;
}
