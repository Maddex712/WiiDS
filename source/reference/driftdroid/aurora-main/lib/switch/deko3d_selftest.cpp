// TEMP deko3d bring-up: renders small isolated feature tests and logs the resulting pixel, so one
// launch shows which WebGPU feature the deko3d backend gets wrong.
#include "../webgpu/gpu.hpp"

#include <array>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace aurora::webgpu {
namespace {

constexpr uint32_t kSize = 8;

struct Rgba {
  uint8_t r, g, b, a;
};

bool wait_map(const wgpu::Buffer& buffer, uint64_t size) {
  bool done = false;
  bool ok = false;
  buffer.MapAsync(wgpu::MapMode::Read, 0, size, wgpu::CallbackMode::AllowProcessEvents,
                  [&](wgpu::MapAsyncStatus status, wgpu::StringView message) {
                    done = true;
                    ok = status == wgpu::MapAsyncStatus::Success;
                    if (!ok) {
                      std::fprintf(stderr, "[selftest]   map status=%d: %.*s\n", int(status),
                                   int(message.length), message.data);
                    }
                  });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!done && std::chrono::steady_clock::now() < deadline) {
    g_device.Tick();
    g_instance.ProcessEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ok;
}

wgpu::ShaderModule make_module(const char* code) {
  wgpu::ShaderSourceWGSL source{};
  source.code = code;
  wgpu::ShaderModuleDescriptor desc{.nextInChain = &source};
  return g_device.CreateShaderModule(&desc);
}

wgpu::Buffer make_buffer(wgpu::BufferUsage usage, const void* data, size_t size) {
  wgpu::BufferDescriptor desc{.usage = usage | wgpu::BufferUsage::CopyDst, .size = (size + 3) & ~size_t(3)};
  wgpu::Buffer buffer = g_device.CreateBuffer(&desc);
  g_queue.WriteBuffer(buffer, 0, data, size);
  return buffer;
}

struct TestSetup {
  wgpu::RenderPipeline pipeline;
  std::function<void(wgpu::RenderPassEncoder&)> draw;
  bool depth = false;
  float depthClear = 1.0f;
};

void run_test(const char* name, Rgba expect, const std::function<TestSetup(wgpu::TextureFormat)>& make) {
  const wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8Unorm;
  g_device.PushErrorScope(wgpu::ErrorFilter::Validation);
  TestSetup setup = make(format);
  wgpu::TextureDescriptor texDesc{
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc,
      .size = {kSize, kSize, 1},
      .format = format,
  };
  wgpu::Texture target = g_device.CreateTexture(&texDesc);
  wgpu::Texture depth;
  wgpu::RenderPassColorAttachment color{
      .view = target.CreateView(),
      .loadOp = wgpu::LoadOp::Clear,
      .storeOp = wgpu::StoreOp::Store,
      .clearValue = {0, 0, 0, 1},
  };
  wgpu::RenderPassDepthStencilAttachment depthAttachment{};
  wgpu::RenderPassDescriptor passDesc{.colorAttachmentCount = 1, .colorAttachments = &color};
  if (setup.depth) {
    wgpu::TextureDescriptor depthDesc{
        .usage = wgpu::TextureUsage::RenderAttachment,
        .size = {kSize, kSize, 1},
        .format = wgpu::TextureFormat::Depth32Float,
    };
    depth = g_device.CreateTexture(&depthDesc);
    depthAttachment.view = depth.CreateView();
    depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
    depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
    depthAttachment.depthClearValue = setup.depthClear;
    passDesc.depthStencilAttachment = &depthAttachment;
  }
  wgpu::BufferDescriptor readDesc{.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
                                  .size = kSize * kSize * 4};
  wgpu::Buffer readback = g_device.CreateBuffer(&readDesc);

  wgpu::CommandEncoder encoder = g_device.CreateCommandEncoder();
  {
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
    if (setup.pipeline) {
      pass.SetPipeline(setup.pipeline);
      setup.draw(pass);
    }
    pass.End();
  }
  wgpu::TexelCopyTextureInfo src{.texture = target};
  wgpu::TexelCopyBufferInfo dst{.layout = {.bytesPerRow = kSize * 4, .rowsPerImage = kSize}, .buffer = readback};
  wgpu::Extent3D extent{kSize, kSize, 1};
  encoder.CopyTextureToBuffer(&src, &dst, &extent);
  wgpu::CommandBuffer commands = encoder.Finish();
  g_queue.Submit(1, &commands);
  bool scopeDone = false;
  g_device.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents,
                         [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView message) {
                           scopeDone = true;
                           if (type != wgpu::ErrorType::NoError) {
                             std::fprintf(stderr, "[selftest]   %s validation: %.*s\n", name,
                                          int(message.length), message.data);
                           }
                         });
  for (int i = 0; i < 1000 && !scopeDone; ++i) {
    g_instance.ProcessEvents();
  }

  if (!wait_map(readback, kSize * kSize * 4)) {
    std::fprintf(stderr, "[selftest] %-22s MAP FAILED\n", name);
    return;
  }
  const auto* px = static_cast<const uint8_t*>(readback.GetConstMappedRange(0, kSize * kSize * 4));
  const auto at = [&](uint32_t x, uint32_t y) { return px + (y * kSize + x) * 4; };
  const uint8_t* c = at(kSize / 2, kSize / 2);
  auto near = [](uint8_t a, uint8_t b) { return (a > b ? a - b : b - a) <= 8; };
  const bool pass = near(c[0], expect.r) && near(c[1], expect.g) && near(c[2], expect.b);
  std::fprintf(stderr, "[selftest] %-22s %s center=%02x%02x%02x%02x expect=%02x%02x%02x corners=%02x%02x%02x %02x%02x%02x\n",
               name, pass ? "PASS" : "FAIL", c[0], c[1], c[2], c[3], expect.r, expect.g, expect.b, at(0, 0)[0],
               at(0, 0)[1], at(0, 0)[2], at(kSize - 1, kSize - 1)[0], at(kSize - 1, kSize - 1)[1],
               at(kSize - 1, kSize - 1)[2]);
  readback.Unmap();
}

wgpu::RenderPipeline make_pipeline(const wgpu::ShaderModule& module, wgpu::TextureFormat format,
                                   wgpu::PipelineLayout layout = nullptr, bool depth = false,
                                   wgpu::CompareFunction depthCompare = wgpu::CompareFunction::Always) {
  wgpu::ColorTargetState target{.format = format};
  wgpu::FragmentState fragment{.module = module, .entryPoint = "fs_main", .targetCount = 1, .targets = &target};
  wgpu::DepthStencilState ds{
      .format = wgpu::TextureFormat::Depth32Float,
      .depthWriteEnabled = wgpu::OptionalBool::True,
      .depthCompare = depthCompare,
  };
  wgpu::RenderPipelineDescriptor desc{
      .layout = layout,
      .vertex = {.module = module, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .depthStencil = depth ? &ds : nullptr,
      .fragment = &fragment,
  };
  return g_device.CreateRenderPipeline(&desc);
}

constexpr const char* kFullscreen = R"(
var<private> pos = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
)";

} // namespace

void run_deko3d_selftest() {
  std::fprintf(stderr, "[selftest] begin\n");

  run_test("clear-only", {0, 0, 0, 255}, [](wgpu::TextureFormat) { return TestSetup{}; });

  run_test("const-vertex-index", {255, 0, 0, 255}, [](wgpu::TextureFormat f) {
    auto m = make_module((std::string(kFullscreen) + R"(
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f { return vec4f(pos[i], 0.5, 1.0); }
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(1.0, 0.0, 0.0, 1.0); }
)").c_str());
    TestSetup s{.pipeline = make_pipeline(m, f)};
    s.draw = [](wgpu::RenderPassEncoder& p) { p.Draw(3); };
    return s;
  });

  run_test("vertex-buffer-attr", {0, 0, 255, 255}, [](wgpu::TextureFormat f) {
    auto m = make_module(R"(
@vertex fn vs_main(@location(0) p: vec2f) -> @builtin(position) vec4f { return vec4f(p, 0.5, 1.0); }
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(0.0, 0.0, 1.0, 1.0); }
)");
    wgpu::VertexAttribute attr{.format = wgpu::VertexFormat::Float32x2, .offset = 0, .shaderLocation = 0};
    wgpu::VertexBufferLayout vbl{.stepMode = wgpu::VertexStepMode::Vertex, .arrayStride = 8, .attributeCount = 1, .attributes = &attr};
    wgpu::ColorTargetState target{.format = f};
    wgpu::FragmentState fragment{.module = m, .entryPoint = "fs_main", .targetCount = 1, .targets = &target};
    wgpu::RenderPipelineDescriptor desc{
        .vertex = {.module = m, .entryPoint = "vs_main", .bufferCount = 1, .buffers = &vbl},
        .fragment = &fragment,
    };
    static const float verts[] = {-1, -1, 3, -1, -1, 3};
    auto vb = make_buffer(wgpu::BufferUsage::Vertex, verts, sizeof(verts));
    TestSetup s{.pipeline = g_device.CreateRenderPipeline(&desc)};
    s.draw = [vb](wgpu::RenderPassEncoder& p) { p.SetVertexBuffer(0, vb); p.Draw(3); };
    return s;
  });

  run_test("ubo-color", {0, 255, 0, 255}, [](wgpu::TextureFormat f) {
    auto m = make_module((std::string(kFullscreen) + R"(
@group(0) @binding(0) var<uniform> col: vec4f;
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f { return vec4f(pos[i], 0.5, 1.0); }
@fragment fn fs_main() -> @location(0) vec4f { return col; }
)").c_str());
    auto pipeline = make_pipeline(m, f);
    static const float col[] = {0, 1, 0, 1};
    auto ub = make_buffer(wgpu::BufferUsage::Uniform, col, sizeof(col));
    wgpu::BindGroupEntry e{.binding = 0, .buffer = ub};
    wgpu::BindGroupDescriptor bgd{.layout = pipeline.GetBindGroupLayout(0), .entryCount = 1, .entries = &e};
    auto bg = g_device.CreateBindGroup(&bgd);
    TestSetup s{.pipeline = pipeline};
    s.draw = [bg](wgpu::RenderPassEncoder& p) { p.SetBindGroup(0, bg); p.Draw(3); };
    return s;
  });

  run_test("ubo-dynamic-offset", {0, 0, 255, 255}, [](wgpu::TextureFormat f) {
    auto m = make_module((std::string(kFullscreen) + R"(
@group(0) @binding(0) var<uniform> col: vec4f;
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f { return vec4f(pos[i], 0.5, 1.0); }
@fragment fn fs_main() -> @location(0) vec4f { return col; }
)").c_str());
    wgpu::BindGroupLayoutEntry le{.binding = 0, .visibility = wgpu::ShaderStage::Fragment,
                                  .buffer = {.type = wgpu::BufferBindingType::Uniform, .hasDynamicOffset = true, .minBindingSize = 16}};
    wgpu::BindGroupLayoutDescriptor bgld{.entryCount = 1, .entries = &le};
    auto bgl = g_device.CreateBindGroupLayout(&bgld);
    wgpu::PipelineLayoutDescriptor pld{.bindGroupLayoutCount = 1, .bindGroupLayouts = &bgl};
    auto pipeline = make_pipeline(m, f, g_device.CreatePipelineLayout(&pld));
    std::vector<float> data(128, 0.0f);
    data[0] = 1; data[3] = 1;          // red at offset 0
    data[64 + 2] = 1; data[64 + 3] = 1; // blue at offset 256
    auto ub = make_buffer(wgpu::BufferUsage::Uniform, data.data(), data.size() * 4);
    wgpu::BindGroupEntry e{.binding = 0, .buffer = ub, .size = 16};
    wgpu::BindGroupDescriptor bgd{.layout = bgl, .entryCount = 1, .entries = &e};
    auto bg = g_device.CreateBindGroup(&bgd);
    TestSetup s{.pipeline = pipeline};
    s.draw = [bg](wgpu::RenderPassEncoder& p) { uint32_t off = 256; p.SetBindGroup(0, bg, 1, &off); p.Draw(3); };
    return s;
  });

  const char* ssboShader = R"(
@group(0) @binding(0) var<storage, read> vbuf: array<u32>;
struct VOut { @builtin(position) pos: vec4f, @location(0) col: vec4f };
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> VOut {
  var o: VOut;
  o.pos = vec4f(bitcast<f32>(vbuf[i * 2u]), bitcast<f32>(vbuf[i * 2u + 1u]), 0.5, 1.0);
  o.col = vec4f(f32(vbuf[6]) / 255.0, f32(vbuf[7]) / 255.0, f32(arrayLength(&vbuf)) / 255.0, 1.0);
  return o;
}
@fragment fn fs_main(in: VOut) -> @location(0) vec4f { return in.col; }
)";
  auto ssbo_data = [](std::vector<uint32_t>& v, size_t base) {
    const float p[] = {-1, -1, 3, -1, -1, 3};
    for (int k = 0; k < 6; ++k) std::memcpy(&v[base + k], &p[k], 4);
    v[base + 6] = 0x40;
    v[base + 7] = 0x80;
  };
  // arrayLength = 8 -> blue 0x08
  run_test("ssbo-read", {0x40, 0x80, 0x08, 255}, [&](wgpu::TextureFormat f) {
    auto pipeline = make_pipeline(make_module(ssboShader), f);
    std::vector<uint32_t> v(8);
    ssbo_data(v, 0);
    auto sb = make_buffer(wgpu::BufferUsage::Storage, v.data(), v.size() * 4);
    wgpu::BindGroupEntry e{.binding = 0, .buffer = sb};
    wgpu::BindGroupDescriptor bgd{.layout = pipeline.GetBindGroupLayout(0), .entryCount = 1, .entries = &e};
    auto bg = g_device.CreateBindGroup(&bgd);
    TestSetup s{.pipeline = pipeline};
    s.draw = [bg](wgpu::RenderPassEncoder& p) { p.SetBindGroup(0, bg); p.Draw(3); };
    return s;
  });

  run_test("ssbo-binding-offset", {0x40, 0x80, 0x08, 255}, [&](wgpu::TextureFormat f) {
    auto pipeline = make_pipeline(make_module(ssboShader), f);
    std::vector<uint32_t> v(64 + 8, 0);
    ssbo_data(v, 64);
    auto sb = make_buffer(wgpu::BufferUsage::Storage, v.data(), v.size() * 4);
    wgpu::BindGroupEntry e{.binding = 0, .buffer = sb, .offset = 256, .size = 32};
    wgpu::BindGroupDescriptor bgd{.layout = pipeline.GetBindGroupLayout(0), .entryCount = 1, .entries = &e};
    auto bg = g_device.CreateBindGroup(&bgd);
    TestSetup s{.pipeline = pipeline};
    s.draw = [bg](wgpu::RenderPassEncoder& p) { p.SetBindGroup(0, bg); p.Draw(3); };
    return s;
  });

  // Game-sized buffers: vertices at the far end of a 32 MiB storage buffer bound whole.
  run_test("ssbo-32mib-far-end", {0x40, 0x80, 0x00, 255}, [&](wgpu::TextureFormat f) {
    auto m = make_module(R"(
@group(0) @binding(0) var<storage, read> vbuf: array<u32>;
struct VOut { @builtin(position) pos: vec4f, @location(0) col: vec4f };
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> VOut {
  var o: VOut;
  let base = arrayLength(&vbuf) - 8u;
  o.pos = vec4f(bitcast<f32>(vbuf[base + i * 2u]), bitcast<f32>(vbuf[base + i * 2u + 1u]), 0.5, 1.0);
  o.col = vec4f(f32(vbuf[base + 6u]) / 255.0, f32(vbuf[base + 7u]) / 255.0, select(0.0, 1.0, arrayLength(&vbuf) != 8388608u), 1.0);
  return o;
}
@fragment fn fs_main(in: VOut) -> @location(0) vec4f { return in.col; }
)");
    auto pipeline = make_pipeline(m, f);
    constexpr size_t kWords = 8388608; // 32 MiB
    wgpu::BufferDescriptor desc{.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, .size = kWords * 4};
    auto sb = g_device.CreateBuffer(&desc);
    std::vector<uint32_t> tail(8);
    ssbo_data(tail, 0);
    g_queue.WriteBuffer(sb, (kWords - 8) * 4, tail.data(), tail.size() * 4);
    wgpu::BindGroupEntry e{.binding = 0, .buffer = sb};
    wgpu::BindGroupDescriptor bgd{.layout = pipeline.GetBindGroupLayout(0), .entryCount = 1, .entries = &e};
    auto bg = g_device.CreateBindGroup(&bgd);
    TestSetup s{.pipeline = pipeline};
    s.draw = [bg](wgpu::RenderPassEncoder& p) { p.SetBindGroup(0, bg); p.Draw(3); };
    return s;
  });
  // Same data at 3 MiB into a 32 MiB buffer, indexed from the start (length-independent).
  run_test("ssbo-32mib-3mib-in", {0x40, 0x80, 0x00, 255}, [&](wgpu::TextureFormat f) {
    auto m = make_module(R"(
@group(0) @binding(0) var<storage, read> vbuf: array<u32>;
struct VOut { @builtin(position) pos: vec4f, @location(0) col: vec4f };
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> VOut {
  var o: VOut;
  let base = 786432u;
  o.pos = vec4f(bitcast<f32>(vbuf[base + i * 2u]), bitcast<f32>(vbuf[base + i * 2u + 1u]), 0.5, 1.0);
  o.col = vec4f(f32(vbuf[base + 6u]) / 255.0, f32(vbuf[base + 7u]) / 255.0, 0.0, 1.0);
  return o;
}
@fragment fn fs_main(in: VOut) -> @location(0) vec4f { return in.col; }
)");
    auto pipeline = make_pipeline(m, f);
    wgpu::BufferDescriptor desc{.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, .size = 8388608u * 4};
    auto sb = g_device.CreateBuffer(&desc);
    std::vector<uint32_t> data(8);
    ssbo_data(data, 0);
    g_queue.WriteBuffer(sb, 786432u * 4, data.data(), data.size() * 4);
    wgpu::BindGroupEntry e{.binding = 0, .buffer = sb};
    wgpu::BindGroupDescriptor bgd{.layout = pipeline.GetBindGroupLayout(0), .entryCount = 1, .entries = &e};
    auto bg = g_device.CreateBindGroup(&bgd);
    TestSetup s{.pipeline = pipeline};
    s.draw = [bg](wgpu::RenderPassEncoder& p) { p.SetBindGroup(0, bg); p.Draw(3); };
    return s;
  });

  run_test("draw-first-vertex", {255, 255, 0, 255}, [](wgpu::TextureFormat f) {
    auto m = make_module(R"(
var<private> pos = array<vec2f, 6>(vec2f(0.0), vec2f(0.0), vec2f(0.0), vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f { return vec4f(pos[i], 0.5, 1.0); }
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(1.0, 1.0, 0.0, 1.0); }
)");
    TestSetup s{.pipeline = make_pipeline(m, f)};
    s.draw = [](wgpu::RenderPassEncoder& p) { p.Draw(3, 1, 3, 0); };
    return s;
  });

  const char* indexedShader = R"(
var<private> pos = array<vec2f, 6>(vec2f(0.0), vec2f(0.0), vec2f(0.0), vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f { return vec4f(pos[i], 0.5, 1.0); }
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(1.0, 0.0, 1.0, 1.0); }
)";
  run_test("indexed-u16", {255, 0, 255, 255}, [&](wgpu::TextureFormat f) {
    static const uint16_t idx[] = {3, 4, 5, 0};
    auto ib = make_buffer(wgpu::BufferUsage::Index, idx, sizeof(idx));
    TestSetup s{.pipeline = make_pipeline(make_module(indexedShader), f)};
    s.draw = [ib](wgpu::RenderPassEncoder& p) { p.SetIndexBuffer(ib, wgpu::IndexFormat::Uint16); p.DrawIndexed(3); };
    return s;
  });
  run_test("indexed-u32-firstidx", {255, 0, 255, 255}, [&](wgpu::TextureFormat f) {
    static const uint32_t idx[] = {0, 0, 0, 3, 4, 5};
    auto ib = make_buffer(wgpu::BufferUsage::Index, idx, sizeof(idx));
    TestSetup s{.pipeline = make_pipeline(make_module(indexedShader), f)};
    s.draw = [ib](wgpu::RenderPassEncoder& p) { p.SetIndexBuffer(ib, wgpu::IndexFormat::Uint32); p.DrawIndexed(3, 1, 3, 0); };
    return s;
  });
  run_test("indexed-basevertex", {255, 0, 255, 255}, [&](wgpu::TextureFormat f) {
    static const uint16_t idx[] = {0, 1, 2, 0};
    auto ib = make_buffer(wgpu::BufferUsage::Index, idx, sizeof(idx));
    TestSetup s{.pipeline = make_pipeline(make_module(indexedShader), f)};
    s.draw = [ib](wgpu::RenderPassEncoder& p) { p.SetIndexBuffer(ib, wgpu::IndexFormat::Uint16); p.DrawIndexed(3, 1, 0, 3); };
    return s;
  });

  run_test("depth-less-clear1", {0, 255, 255, 255}, [](wgpu::TextureFormat f) {
    auto m = make_module((std::string(kFullscreen) + R"(
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f { return vec4f(pos[i], 0.5, 1.0); }
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(0.0, 1.0, 1.0, 1.0); }
)").c_str());
    TestSetup s{.pipeline = make_pipeline(m, f, nullptr, true, wgpu::CompareFunction::Less), .depth = true};
    s.draw = [](wgpu::RenderPassEncoder& p) { p.Draw(3); };
    return s;
  });
  run_test("depth-greater-clear0", {0, 255, 255, 255}, [](wgpu::TextureFormat f) {
    auto m = make_module((std::string(kFullscreen) + R"(
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f { return vec4f(pos[i], 0.5, 1.0); }
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(0.0, 1.0, 1.0, 1.0); }
)").c_str());
    TestSetup s{.pipeline = make_pipeline(m, f, nullptr, true, wgpu::CompareFunction::Greater), .depth = true, .depthClear = 0.0f};
    s.draw = [](wgpu::RenderPassEncoder& p) { p.Draw(3); };
    return s;
  });

  run_test("w-not-one", {255, 128, 0, 255}, [](wgpu::TextureFormat f) {
    auto m = make_module((std::string(kFullscreen) + R"(
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f { return vec4f(pos[i] * 4.0, 2.0, 4.0); }
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(1.0, 0.5, 0.0, 1.0); }
)").c_str());
    TestSetup s{.pipeline = make_pipeline(m, f)};
    s.draw = [](wgpu::RenderPassEncoder& p) { p.Draw(3); };
    return s;
  });

  run_test("texture-sample", {0, 200, 100, 255}, [](wgpu::TextureFormat f) {
    auto m = make_module((std::string(kFullscreen) + R"(
@group(0) @binding(0) var s: sampler;
@group(0) @binding(1) var t: texture_2d<f32>;
struct VOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f };
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> VOut { var o: VOut; o.pos = vec4f(pos[i], 0.5, 1.0); o.uv = pos[i] * 0.5 + 0.5; return o; }
@fragment fn fs_main(in: VOut) -> @location(0) vec4f { return textureSample(t, s, in.uv); }
)").c_str());
    auto pipeline = make_pipeline(m, f);
    wgpu::TextureDescriptor td{.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
                               .size = {2, 2, 1}, .format = wgpu::TextureFormat::RGBA8Unorm};
    auto tex = g_device.CreateTexture(&td);
    const uint8_t texels[] = {0, 200, 100, 255, 0, 200, 100, 255, 0, 200, 100, 255, 0, 200, 100, 255};
    wgpu::TexelCopyTextureInfo dst{.texture = tex};
    wgpu::TexelCopyBufferLayout layout{.bytesPerRow = 8, .rowsPerImage = 2};
    wgpu::Extent3D ext{2, 2, 1};
    g_queue.WriteTexture(&dst, texels, sizeof(texels), &layout, &ext);
    auto sampler = g_device.CreateSampler();
    std::array entries{wgpu::BindGroupEntry{.binding = 0, .sampler = sampler},
                       wgpu::BindGroupEntry{.binding = 1, .textureView = tex.CreateView()}};
    wgpu::BindGroupDescriptor bgd{.layout = pipeline.GetBindGroupLayout(0), .entryCount = entries.size(), .entries = entries.data()};
    auto bg = g_device.CreateBindGroup(&bgd);
    TestSetup s{.pipeline = pipeline};
    s.draw = [bg](wgpu::RenderPassEncoder& p) { p.SetBindGroup(0, bg); p.Draw(3); };
    return s;
  });

  run_test("two-groups-vs-ubo", {128, 0, 128, 255}, [](wgpu::TextureFormat f) {
    auto m = make_module((std::string(kFullscreen) + R"(
@group(0) @binding(0) var<storage, read> vbuf: array<u32>;
@group(1) @binding(0) var<uniform> ubuf: vec4f;
struct VOut { @builtin(position) pos: vec4f, @location(0) col: vec4f };
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> VOut { var o: VOut; o.pos = vec4f(pos[vbuf[i]], 0.5, 1.0); o.col = ubuf; return o; }
@fragment fn fs_main(in: VOut) -> @location(0) vec4f { return in.col; }
)").c_str());
    auto pipeline = make_pipeline(m, f);
    static const uint32_t order[] = {0, 1, 2, 0};
    static const float col[] = {0.5f, 0, 0.5f, 1};
    auto sb = make_buffer(wgpu::BufferUsage::Storage, order, sizeof(order));
    auto ub = make_buffer(wgpu::BufferUsage::Uniform, col, sizeof(col));
    wgpu::BindGroupEntry e0{.binding = 0, .buffer = sb};
    wgpu::BindGroupEntry e1{.binding = 0, .buffer = ub};
    wgpu::BindGroupDescriptor d0{.layout = pipeline.GetBindGroupLayout(0), .entryCount = 1, .entries = &e0};
    wgpu::BindGroupDescriptor d1{.layout = pipeline.GetBindGroupLayout(1), .entryCount = 1, .entries = &e1};
    auto bg0 = g_device.CreateBindGroup(&d0);
    auto bg1 = g_device.CreateBindGroup(&d1);
    TestSetup s{.pipeline = pipeline};
    s.draw = [bg0, bg1](wgpu::RenderPassEncoder& p) { p.SetBindGroup(0, bg0); p.SetBindGroup(1, bg1); p.Draw(3); };
    return s;
  });

  // aurora's XFB copy/present pipeline, verbatim shader, with and without a sub-rect viewport.
  auto copyTest = [&](const char* name, bool subViewport) {
    run_test(name, {0, 200, 100, 255}, [subViewport](wgpu::TextureFormat f) {
      auto m = make_module(R"(
@group(0) @binding(0)
var efb_sampler: sampler;
@group(0) @binding(1)
var efb_texture: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

var<private> pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(-1.0, 1.0),
    vec2(-1.0, -3.0),
    vec2(3.0, 1.0),
);
var<private> uvs: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(0.0, 0.0),
    vec2(0.0, 2.0),
    vec2(2.0, 0.0),
);

@vertex
fn vs_main(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4<f32>(pos[vtxIdx], 0.0, 1.0);
    out.uv = uvs[vtxIdx];
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let color = textureSample(efb_texture, efb_sampler, in.uv);
    return vec4(color.rgb, 1.0);
}
)");
      const std::array bglEntries{
          wgpu::BindGroupLayoutEntry{.binding = 0, .visibility = wgpu::ShaderStage::Fragment,
                                     .sampler = {.type = wgpu::SamplerBindingType::Filtering}},
          wgpu::BindGroupLayoutEntry{.binding = 1, .visibility = wgpu::ShaderStage::Fragment,
                                     .texture = {.sampleType = wgpu::TextureSampleType::Float,
                                                 .viewDimension = wgpu::TextureViewDimension::e2D}},
      };
      wgpu::BindGroupLayoutDescriptor bgld{.entryCount = bglEntries.size(), .entries = bglEntries.data()};
      auto bgl = g_device.CreateBindGroupLayout(&bgld);
      wgpu::PipelineLayoutDescriptor pld{.bindGroupLayoutCount = 1, .bindGroupLayouts = &bgl};
      wgpu::ColorTargetState target{.format = f, .writeMask = wgpu::ColorWriteMask::All};
      wgpu::FragmentState fragment{.module = m, .entryPoint = "fs_main", .targetCount = 1, .targets = &target};
      wgpu::RenderPipelineDescriptor desc{
          .layout = g_device.CreatePipelineLayout(&pld),
          .vertex = {.module = m, .entryPoint = "vs_main"},
          .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
          .multisample = {.count = 1, .mask = UINT32_MAX},
          .fragment = &fragment,
      };
      wgpu::TextureDescriptor td{.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
                                 .size = {2, 2, 1}, .format = wgpu::TextureFormat::RGBA8Unorm};
      auto tex = g_device.CreateTexture(&td);
      const uint8_t texels[] = {0, 200, 100, 255, 0, 200, 100, 255, 0, 200, 100, 255, 0, 200, 100, 255};
      wgpu::TexelCopyTextureInfo dst{.texture = tex};
      wgpu::TexelCopyBufferLayout layout{.bytesPerRow = 8, .rowsPerImage = 2};
      wgpu::Extent3D ext{2, 2, 1};
      g_queue.WriteTexture(&dst, texels, sizeof(texels), &layout, &ext);
      wgpu::SamplerDescriptor sd{.magFilter = wgpu::FilterMode::Linear, .minFilter = wgpu::FilterMode::Linear};
      std::array entries{wgpu::BindGroupEntry{.binding = 0, .sampler = g_device.CreateSampler(&sd)},
                         wgpu::BindGroupEntry{.binding = 1, .textureView = tex.CreateView()}};
      wgpu::BindGroupDescriptor bgd{.layout = bgl, .entryCount = entries.size(), .entries = entries.data()};
      auto bg = g_device.CreateBindGroup(&bgd);
      TestSetup s{.pipeline = g_device.CreateRenderPipeline(&desc)};
      s.draw = [bg, subViewport](wgpu::RenderPassEncoder& p) {
        p.SetBindGroup(0, bg);
        if (subViewport) {
          p.SetViewport(0.f, 0.f, 8.f, 5.f, 0.f, 1.f); // top 5 rows: center (4,4) inside, (7,7) outside
        }
        p.Draw(3);
      };
      return s;
    });
  };
  // aurora's palette conversion: R16Sint index texture (textureLoad + textureDimensions) -> TLUT.
  run_test("palette-r16sint-tlut", {30, 160, 220, 255}, [](wgpu::TextureFormat f) {
    auto m = make_module(R"(
struct VertexOutput { @builtin(position) pos: vec4f, @location(0) uv: vec2f };
var<private> positions: array<vec2f, 3> = array(vec2f(-1.0, 1.0), vec2f(-1.0, -3.0), vec2f(3.0, 1.0));
var<private> uvs: array<vec2f, 3> = array(vec2f(0.0, 0.0), vec2f(0.0, 2.0), vec2f(2.0, 0.0));
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
  var out: VertexOutput; out.pos = vec4f(positions[vi], 0.0, 1.0); out.uv = uvs[vi]; return out;
}
@group(0) @binding(0) var src_samp: sampler;
@group(0) @binding(1) var src: texture_2d<i32>;
@group(0) @binding(2) var tlut: texture_2d<f32>;
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
  let texSize = vec2f(textureDimensions(src));
  let coord = vec2i(floor(in.uv * texSize));
  let idx = textureLoad(src, coord, 0).r;
  return textureLoad(tlut, vec2i(idx, 0), 0);
}
)");
    const std::array bglEntries{
        wgpu::BindGroupLayoutEntry{.binding = 0, .visibility = wgpu::ShaderStage::Fragment,
                                   .sampler = {.type = wgpu::SamplerBindingType::NonFiltering}},
        wgpu::BindGroupLayoutEntry{.binding = 1, .visibility = wgpu::ShaderStage::Fragment,
                                   .texture = {.sampleType = wgpu::TextureSampleType::Sint}},
        wgpu::BindGroupLayoutEntry{.binding = 2, .visibility = wgpu::ShaderStage::Fragment,
                                   .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat}},
    };
    wgpu::BindGroupLayoutDescriptor bgld{.entryCount = bglEntries.size(), .entries = bglEntries.data()};
    auto bgl = g_device.CreateBindGroupLayout(&bgld);
    wgpu::PipelineLayoutDescriptor pld{.bindGroupLayoutCount = 1, .bindGroupLayouts = &bgl};
    auto pipeline = make_pipeline(m, f, g_device.CreatePipelineLayout(&pld));
    // 4x4 index texture, every texel = 5.
    wgpu::TextureDescriptor itd{.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
                                .size = {4, 4, 1}, .format = wgpu::TextureFormat::R16Sint};
    auto idxTex = g_device.CreateTexture(&itd);
    std::vector<int16_t> idx(16, 5);
    wgpu::TexelCopyTextureInfo d1{.texture = idxTex};
    wgpu::TexelCopyBufferLayout l1{.bytesPerRow = 8, .rowsPerImage = 4};
    wgpu::Extent3D e1{4, 4, 1};
    g_queue.WriteTexture(&d1, idx.data(), idx.size() * 2, &l1, &e1);
    // 16x1 TLUT, entry 5 = (30,160,220).
    wgpu::TextureDescriptor ttd{.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
                                .size = {16, 1, 1}, .format = wgpu::TextureFormat::RGBA8Unorm};
    auto tlut = g_device.CreateTexture(&ttd);
    std::vector<uint8_t> pal(64, 0);
    for (int i = 0; i < 16; ++i) { pal[i * 4 + 3] = 255; pal[i * 4] = 128; pal[i * 4 + 1] = 128; pal[i * 4 + 2] = 128; }
    pal[5 * 4] = 30; pal[5 * 4 + 1] = 160; pal[5 * 4 + 2] = 220;
    wgpu::TexelCopyTextureInfo d2{.texture = tlut};
    wgpu::TexelCopyBufferLayout l2{.bytesPerRow = 64, .rowsPerImage = 1};
    wgpu::Extent3D e2{16, 1, 1};
    g_queue.WriteTexture(&d2, pal.data(), pal.size(), &l2, &e2);
    std::array entries{wgpu::BindGroupEntry{.binding = 0, .sampler = g_device.CreateSampler()},
                       wgpu::BindGroupEntry{.binding = 1, .textureView = idxTex.CreateView()},
                       wgpu::BindGroupEntry{.binding = 2, .textureView = tlut.CreateView()}};
    wgpu::BindGroupDescriptor bgd{.layout = bgl, .entryCount = entries.size(), .entries = entries.data()};
    auto bg = g_device.CreateBindGroup(&bgd);
    TestSetup s{.pipeline = pipeline};
    s.draw = [bg](wgpu::RenderPassEncoder& p) { p.SetBindGroup(0, bg); p.Draw(3); };
    return s;
  });

  copyTest("copy-shader-exact", false);
  copyTest("copy-shader-viewport", true);

  std::fprintf(stderr, "[selftest] end\n");
}

} // namespace aurora::webgpu
