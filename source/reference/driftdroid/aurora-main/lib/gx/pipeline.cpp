#include "pipeline.hpp"

#include "../webgpu/gpu.hpp"
#include "gx_fmt.hpp"
#include "shader_info.hpp"
#include "tracy/Tracy.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>

#include <absl/container/flat_hash_map.h>

namespace aurora::gx {
static Module Log("aurora::gx");

namespace {
struct ShaderConfigHash {
  size_t operator()(const ShaderConfig& config) const noexcept { return static_cast<size_t>(xxh3_hash(config)); }
};

struct CachedShaderModule {
  std::condition_variable ready;
  wgpu::ShaderModule module;
  bool compiling = true;
};

std::mutex sShaderModuleCacheMutex;
absl::flat_hash_map<ShaderConfig, std::shared_ptr<CachedShaderModule>, ShaderConfigHash> sShaderModuleCache;

// Entries live only while a pipeline is being built from them. Dawn keeps a shader module's
// parsed program (Tint's AST and semantic data, megabytes for a GX shader) for as long as the
// application holds a reference to the module, and frees it when the last one is dropped; the
// pipelines keep the module itself alive. Holding every module here for the whole session meant a
// run that had to compile a few hundred shaders - a new or reset shader cache, or an update that
// changed the generated shader text - ran the process out of memory (std::bad_alloc inside Tint
// after about 12 minutes on Switch). So this map only joins callers that want the same shader at
// the same moment; the last one to finish removes the entry. A later request for the same shader
// builds its text again and Dawn hands back the module it already has.
std::shared_ptr<CachedShaderModule> acquire_shader_module(const ShaderConfig& config) {
  std::shared_ptr<CachedShaderModule> entry;
  {
    std::unique_lock lock{sShaderModuleCacheMutex};
    const auto it = sShaderModuleCache.find(config);
    if (it == sShaderModuleCache.end()) {
      entry = std::make_shared<CachedShaderModule>();
      sShaderModuleCache.emplace(config, entry);
    } else {
      entry = it->second;
      entry->ready.wait(lock, [&] { return !entry->compiling; });
      return entry;
    }
  }

  auto module = build_shader(config);
  {
    std::lock_guard lock{sShaderModuleCacheMutex};
    entry->module = module;
    entry->compiling = false;
  }
  entry->ready.notify_all();
  return entry;
}

void release_shader_module(const ShaderConfig& config, std::shared_ptr<CachedShaderModule>& entry) {
  std::lock_guard lock{sShaderModuleCacheMutex};
  // The map holds one reference and this caller one; anyone else still building holds more.
  if (entry.use_count() == 2) {
    const auto it = sShaderModuleCache.find(config);
    if (it != sShaderModuleCache.end() && it->second == entry) {
      sShaderModuleCache.erase(it);
    }
  }
  entry.reset();
}
} // namespace

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config) {
  ZoneScoped;
  auto shader = acquire_shader_module(config.shaderConfig);
  auto pipeline = build_pipeline(config, {}, shader->module, "GX Pipeline");
  release_shader_module(config.shaderConfig, shader);
  return pipeline;
}

void clear_shader_module_cache() {
  std::lock_guard lock{sShaderModuleCacheMutex};
  sShaderModuleCache.clear();
}

void render(const DrawData& data, const wgpu::RenderPassEncoder& pass, DrawEncodeState& state,
            bool requireReadyPipeline, const gfx::Range* uniformRangeOverride) {
  if (!gfx::bind_pipeline(data.pipeline, pass, state.currentPipeline, requireReadyPipeline)) {
    return;
  }

  // An interpolated presentation slot re-encodes the identical draw with only this range replaced; overriding here avoids copying the whole DrawData per draw per slot.
  const gfx::Range& uniformRange = uniformRangeOverride != nullptr ? *uniformRangeOverride : data.uniformRange;
  const std::array offsets{uniformRange.offset};
  pass.SetBindGroup(1, gfx::g_uniformBindGroup, offsets.size(), offsets.data());
  // Resolved when the draw was recorded; see GXBindGroups.
  if (data.bindGroups.resolvedTextureBindGroup != nullptr &&
      data.bindGroups.resolvedTextureBindGroup != state.boundTextureBindGroup) {
    wgpuRenderPassEncoderSetBindGroup(pass.Get(), 2, data.bindGroups.resolvedTextureBindGroup, 0, nullptr);
    state.boundTextureBindGroup = data.bindGroups.resolvedTextureBindGroup;
  }
  if (data.dstAlpha != UINT32_MAX) {
    const wgpu::Color color{0.f, 0.f, 0.f, data.dstAlpha / 255.f};
    pass.SetBlendConstant(&color);
  }
  if (!state.indexBufferBound) {
    // Bound once for the pass; draws select their range with firstIndex below.
    pass.SetIndexBuffer(gfx::g_indexBuffer, wgpu::IndexFormat::Uint16, 0, wgpu::kWholeSize);
    state.indexBufferBound = true;
  }
  pass.DrawIndexed(data.indexCount, data.instanceCount,
                   static_cast<uint32_t>(data.idxRange.offset / sizeof(uint16_t)));
}
} // namespace aurora::gx
