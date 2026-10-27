#include "lsfg.hpp"

#if defined(__ANDROID__) && defined(WEBGPU_DAWN) && defined(DAWN_ENABLE_BACKEND_VULKAN)

#include "gpu.hpp"
#include "../internal.hpp"

#include <android/hardware_buffer.h>
#include <android/log.h>
#include <dawn/native/VulkanBackend.h>
#include <dlfcn.h>
#include <linux/sync_file.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// The handle of the Vulkan driver the process loaded, when it is a custom one (the runtime's
// android_vulkan_override.cpp); generation then runs on the same driver as the game.
extern "C" __attribute__((weak)) void* wiids_vulkan_library();

namespace aurora::lsfg {
namespace {
Module Log("aurora::lsfg");

// The C API of libbloodframe.so (bb_lsfg.h).
struct BbLsfgConfig {
  void* vk_get_instance_proc_addr;
  const char* dll_path;
  uint8_t device_uuid[16];
  uint8_t driver_uuid[16];
  float flow_scale;
  int32_t performance;
  int32_t generation_count;
  int32_t shared_format;
  int32_t hdr;
  int32_t fp16;
};
struct BbLsfgInfo {
  uint64_t input_ahb_usage;
  uint64_t output_ahb_usage;
  int32_t sync_fd_ok;
  int32_t input_copy;
  int32_t output_copy;
};
struct BbLsfgPresent {
  int32_t out_ready_fd[3];
  int32_t batch_complete_fd;
  int32_t gpu_synced;
};
struct Api {
  int (*init)(const BbLsfgConfig*, BbLsfgInfo*, char*, size_t);
  int (*create_context)(AHardwareBuffer*, AHardwareBuffer*, AHardwareBuffer* const*, uint32_t, uint32_t,
                        uint32_t, int32_t, char*, size_t);
  int (*present)(int, int, uint32_t, BbLsfgPresent*, char*, size_t);
  int (*wait)(int, uint64_t);
  void (*destroy_context)(int);
  void (*shutdown)();
  int (*diagnostics)(char*, size_t);
};

constexpr int32_t kSharedFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr uint32_t kMaxSlots = 3;
constexpr int32_t kLayoutUndefined = VK_IMAGE_LAYOUT_UNDEFINED;
constexpr int32_t kLayoutGeneral = VK_IMAGE_LAYOUT_GENERAL;

struct SharedImage {
  AHardwareBuffer* buffer = nullptr;
  wgpu::SharedTextureMemory memory;
  wgpu::Texture texture;
  wgpu::TextureView view;
  wgpu::BindGroup bindGroup;
};

std::mutex g_configMutex;
std::string g_dllPath;
float g_flowScale = 0.75f;
bool g_performance = true;
uint64_t g_configGeneration = 0;
std::string g_status;

// Encode-phase state (one frame at a time; see lsfg.hpp).
void* g_library = nullptr;
Api g_api{};
bool g_failed = false;
uint64_t g_appliedGeneration = 0;
bool g_runtime = false;
uint32_t g_runtimeSlots = 0;
BbLsfgInfo g_info{};
int g_context = -1;
uint32_t g_width = 0;
uint32_t g_height = 0;
std::array<SharedImage, 2> g_inputs;
std::array<SharedImage, kMaxSlots> g_outputs;
wgpu::Sampler g_sampler;
uint64_t g_frameIndex = 0;
uint32_t g_inputIndex = 0;
int g_batchCompleteFd = -1;  // the last present is done with both inputs
int g_outputsReadFd = -1;    // our copies out of the outputs are done
std::array<int, kMaxSlots> g_readyFds{-1, -1, -1};
uint32_t g_generated = 0;
// g_generated for other threads (the settings overlay), cleared whenever generation stops.
std::atomic_uint32_t g_activeFrames{0};
// First WebGPU error caught by pop_error_scope(); acted on at the next begin_input().
std::mutex g_scopeMutex;
std::string g_scopeError;
std::atomic_bool g_scopeFailed{false};

// The runtime log is in the app's private files; mirror generation state to logcat as well.
void logcat(int priority, const std::string& message) {
  __android_log_write(priority, "WiiDS-LSFG", message.c_str());
}

void close_fd(int& fd) noexcept {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

// One sync file signalled when both are (either may be -1). Takes ownership of both.
int merge_fds(int a, int b) noexcept {
  if (a < 0) return b;
  if (b < 0) return a;
  sync_merge_data data{};
  std::strncpy(data.name, "wiids-lsfg", sizeof(data.name) - 1);
  data.fd2 = b;
  const int merged = ioctl(a, SYNC_IOC_MERGE, &data) == 0 ? data.fence : -1;
  if (merged < 0) {
    // Keep the ordering by waiting here instead; -1 then means "already signalled".
    Log.warn("sync file merge failed; waiting on the CPU");
    std::array<pollfd, 2> fds{pollfd{.fd = a, .events = POLLIN}, pollfd{.fd = b, .events = POLLIN}};
    for (auto& fd : fds) poll(&fd, 1, 1000);
  }
  ::close(a);
  ::close(b);
  return merged;
}

void set_status(std::string message) {
  Log.warn("Lossless Scaling frame generation off: {}", message);
  logcat(ANDROID_LOG_WARN, "off: " + message);
  std::lock_guard lock(g_configMutex);
  g_status = std::move(message);
}

wgpu::SharedFence import_fence(int& fd) noexcept {
  if (fd < 0) return {};
  wgpu::SharedFenceSyncFDDescriptor syncFd;
  syncFd.handle = fd;
  wgpu::SharedFenceDescriptor descriptor;
  descriptor.nextInChain = &syncFd;
  auto fence = webgpu::g_device.ImportSharedFence(&descriptor);
  // Dawn keeps its own duplicate of the descriptor's handle.
  close_fd(fd);
  return fence;
}

// The end-of-access fence as a sync file we own (-1 when there is none).
int export_end_fence(const wgpu::SharedTextureMemoryEndAccessState& state) noexcept {
  int result = -1;
  for (size_t i = 0; i < state.fenceCount; ++i) {
    wgpu::SharedFenceSyncFDExportInfo syncFd;
    wgpu::SharedFenceExportInfo info;
    info.nextInChain = &syncFd;
    state.fences[i].ExportInfo(&info);
    if (info.type != wgpu::SharedFenceType::SyncFD || syncFd.handle < 0) continue;
    result = merge_fds(result, ::dup(syncFd.handle));
  }
  return result;
}

bool begin_access(SharedImage& image, int& fenceFd, bool initialized, int32_t oldLayout,
                  int32_t newLayout) noexcept {
  wgpu::SharedTextureMemoryVkImageLayoutBeginState layout;
  layout.oldLayout = oldLayout;
  layout.newLayout = newLayout;
  wgpu::SharedTextureMemoryBeginAccessDescriptor descriptor;
  descriptor.nextInChain = &layout;
  descriptor.concurrentRead = false;
  descriptor.initialized = initialized;
  const auto fence = import_fence(fenceFd);
  const uint64_t signaled = 1;
  // This Dawn leaves the count members uninitialised, so the no-fence case (the first frame) must
  // zero them itself or BeginAccess rejects stack garbage.
  descriptor.fenceCount = fence ? 1 : 0;
  descriptor.fences = fence ? &fence : nullptr;
  descriptor.signaledValueCount = fence ? 1 : 0;
  descriptor.signaledValues = fence ? &signaled : nullptr;
  return image.memory.BeginAccess(image.texture, &descriptor) == wgpu::Status::Success;
}

int end_access(SharedImage& image) noexcept {
  wgpu::SharedTextureMemoryVkImageLayoutEndState layout;
  wgpu::SharedTextureMemoryEndAccessState state;
  state.nextInChain = &layout;
  if (image.memory.EndAccess(image.texture, &state) != wgpu::Status::Success) return -1;
  return export_end_fence(state);
}

bool load_library() noexcept {
  if (g_library != nullptr) return true;
  g_library = dlopen("libbloodframe.so", RTLD_NOW | RTLD_LOCAL);
  if (g_library == nullptr) {
    set_status(std::string("libbloodframe.so is missing from this build (") + dlerror() + ")");
    return false;
  }
  const auto symbol = [](const char* name) { return dlsym(g_library, name); };
  g_api.init = reinterpret_cast<decltype(g_api.init)>(symbol("bb_lsfg_init"));
  g_api.create_context = reinterpret_cast<decltype(g_api.create_context)>(symbol("bb_lsfg_create_context"));
  g_api.present = reinterpret_cast<decltype(g_api.present)>(symbol("bb_lsfg_present"));
  g_api.wait = reinterpret_cast<decltype(g_api.wait)>(symbol("bb_lsfg_wait"));
  g_api.destroy_context = reinterpret_cast<decltype(g_api.destroy_context)>(symbol("bb_lsfg_destroy_context"));
  g_api.shutdown = reinterpret_cast<decltype(g_api.shutdown)>(symbol("bb_lsfg_shutdown"));
  g_api.diagnostics = reinterpret_cast<decltype(g_api.diagnostics)>(symbol("bb_lsfg_diagnostics"));
  if (!g_api.init || !g_api.create_context || !g_api.present || !g_api.wait || !g_api.destroy_context ||
      !g_api.shutdown) {
    set_status("libbloodframe.so does not export the expected bb_lsfg API");
    return false;
  }
  return true;
}

// vkGetInstanceProcAddr of the driver the game runs on, and that driver's device/driver UUIDs.
bool host_device(void*& getInstanceProcAddr, uint8_t (&deviceUuid)[16], uint8_t (&driverUuid)[16]) noexcept {
  void* vulkan = wiids_vulkan_library != nullptr ? wiids_vulkan_library() : nullptr;
  if (vulkan == nullptr) vulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
  getInstanceProcAddr = vulkan != nullptr ? dlsym(vulkan, "vkGetInstanceProcAddr") : nullptr;
  if (getInstanceProcAddr == nullptr) return false;

  const WGPUDevice device = webgpu::g_device.Get();
  const VkInstance instance = dawn::native::vulkan::GetInstance(device);
  const auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
      dawn::native::vulkan::GetInstanceProcAddr(device, "vkEnumeratePhysicalDevices"));
  const auto properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
      dawn::native::vulkan::GetInstanceProcAddr(device, "vkGetPhysicalDeviceProperties2"));
  if (instance == VK_NULL_HANDLE || enumerate == nullptr || properties2 == nullptr) return false;
  uint32_t count = 0;
  enumerate(instance, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  enumerate(instance, &count, devices.data());
  wgpu::AdapterInfo adapter;
  webgpu::g_device.GetAdapterInfo(&adapter);
  for (const auto physical : devices) {
    VkPhysicalDeviceIDProperties ids{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 props{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &ids};
    properties2(physical, &props);
    if (devices.size() == 1 ||
        (props.properties.vendorID == adapter.vendorID && props.properties.deviceID == adapter.deviceID)) {
      std::memcpy(deviceUuid, ids.deviceUUID, sizeof(deviceUuid));
      std::memcpy(driverUuid, ids.driverUUID, sizeof(driverUuid));
      return true;
    }
  }
  return false;
}

void destroy_context() noexcept {
  if (g_context >= 0) {
    g_api.wait(g_context, 1'000'000'000ull);
    g_api.destroy_context(g_context);
    g_context = -1;
  }
  // Dawn holds its own reference to each buffer for as long as its objects live.
  const auto release = [](SharedImage& image) {
    AHardwareBuffer* buffer = image.buffer;
    image = {};
    if (buffer != nullptr) AHardwareBuffer_release(buffer);
  };
  for (auto& image : g_inputs) release(image);
  for (auto& image : g_outputs) release(image);
  close_fd(g_batchCompleteFd);
  close_fd(g_outputsReadFd);
  for (auto& fd : g_readyFds) close_fd(fd);
  g_width = g_height = 0;
  g_frameIndex = 0;
  g_generated = 0;
}

void destroy_runtime() noexcept {
  g_activeFrames.store(0, std::memory_order_relaxed);
  destroy_context();
  if (g_runtime) {
    g_api.shutdown();
    g_runtime = false;
    g_runtimeSlots = 0;
  }
}

void fail(std::string message) noexcept {
  g_activeFrames.store(0, std::memory_order_relaxed);
  destroy_runtime();
  g_failed = true;
  set_status(std::move(message));
}

bool ensure_runtime(uint32_t slots) noexcept {
  uint64_t generation;
  std::string dllPath;
  float flowScale;
  bool performance;
  {
    std::lock_guard lock(g_configMutex);
    generation = g_configGeneration;
    dllPath = g_dllPath;
    flowScale = g_flowScale;
    performance = g_performance;
  }
  if (generation != g_appliedGeneration) {
    // New settings: start over (a failure in the old ones no longer applies).
    destroy_runtime();
    g_failed = false;
    g_appliedGeneration = generation;
  }
  if (g_failed || dllPath.empty()) return false;
  if (g_runtime && g_runtimeSlots == slots) return true;
  destroy_runtime();
  if (!load_library()) {
    g_failed = true;
    return false;
  }
  if (webgpu::g_graphicsConfig.surfaceConfiguration.format != wgpu::TextureFormat::RGBA8Unorm) {
    fail("the display surface is not RGBA8");
    return false;
  }
  BbLsfgConfig config{};
  if (!host_device(config.vk_get_instance_proc_addr, config.device_uuid, config.driver_uuid)) {
    fail("the game's Vulkan device could not be identified");
    return false;
  }
  config.dll_path = dllPath.c_str();
  config.flow_scale = flowScale;
  config.performance = performance ? 1 : 0;
  config.generation_count = static_cast<int32_t>(slots);
  config.shared_format = kSharedFormat;
  config.hdr = 0;
  config.fp16 = 1;
  char error[512]{};
  if (g_api.init(&config, &g_info, error, sizeof(error)) != 0) {
    fail(error[0] != 0 ? error : "bb_lsfg_init failed");
    return false;
  }
  g_runtime = true;
  g_runtimeSlots = slots;
  if (g_api.diagnostics != nullptr) {
    char line[768]{};
    g_api.diagnostics(line, sizeof(line));
    Log.info("Lossless Scaling frame generation ready ({} generated per frame): {}", slots, line);
    logcat(ANDROID_LOG_INFO, "ready: " + std::string(line));
  }
  {
    std::lock_guard lock(g_configMutex);
    g_status.clear();
  }
  return true;
}

bool make_image(SharedImage& image, uint32_t width, uint32_t height, uint64_t usage, bool sampled) noexcept {
  AHardwareBuffer_Desc desc{
      .width = width,
      .height = height,
      .layers = 1,
      .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
      .usage = usage | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT,
  };
  if (AHardwareBuffer_allocate(&desc, &image.buffer) != 0) return false;
  wgpu::SharedTextureMemoryAHardwareBufferDescriptor ahb;
  ahb.handle = image.buffer;
  wgpu::SharedTextureMemoryDescriptor descriptor;
  descriptor.nextInChain = &ahb;
  descriptor.label = "LSFG shared image";
  image.memory = webgpu::g_device.ImportSharedTextureMemory(&descriptor);
  if (!image.memory) return false;
  image.texture = image.memory.CreateTexture();
  if (!image.texture) return false;
  image.view = image.texture.CreateView();
  if (sampled) image.bindGroup = webgpu::create_copy_bind_group(image.view, g_sampler);
  return true;
}

bool ensure_context(uint32_t width, uint32_t height) noexcept {
  if (g_context >= 0 && g_width == width && g_height == height) return true;
  destroy_context();
  if (!g_sampler) {
    const wgpu::SamplerDescriptor samplerDescriptor{
        .label = "LSFG output sampler",
        .magFilter = wgpu::FilterMode::Linear,
        .minFilter = wgpu::FilterMode::Linear,
    };
    g_sampler = webgpu::g_device.CreateSampler(&samplerDescriptor);
  }
  for (auto& image : g_inputs) {
    if (!make_image(image, width, height, g_info.input_ahb_usage, false)) {
      fail("could not allocate the shared input images");
      return false;
    }
  }
  std::array<AHardwareBuffer*, kMaxSlots> outputs{};
  for (uint32_t i = 0; i < g_runtimeSlots; ++i) {
    if (!make_image(g_outputs[i], width, height, g_info.output_ahb_usage, true)) {
      fail("could not allocate the shared output images");
      return false;
    }
    outputs[i] = g_outputs[i].buffer;
  }
  char error[512]{};
  g_context = g_api.create_context(g_inputs[0].buffer, g_inputs[1].buffer, outputs.data(), g_runtimeSlots, width,
                                   height, kSharedFormat, error, sizeof(error));
  if (g_context < 0) {
    fail(error[0] != 0 ? error : "bb_lsfg_create_context failed");
    return false;
  }
  g_width = width;
  g_height = height;
  g_frameIndex = 0;
  return true;
}

} // namespace

void configure(const char* dllPath, float flowScale, bool performance) noexcept {
  std::lock_guard lock(g_configMutex);
  g_dllPath = dllPath != nullptr ? dllPath : "";
  g_flowScale = flowScale < 0.25f ? 0.25f : flowScale > 1.0f ? 1.0f : flowScale;
  g_performance = performance;
  ++g_configGeneration;
  g_status.clear();
}

const char* status() noexcept {
  // The returned pointer stays valid until the next failure or configure().
  std::lock_guard lock(g_configMutex);
  return g_status.c_str();
}

bool enabled() noexcept {
  std::lock_guard lock(g_configMutex);
  return !g_dllPath.empty();
}

uint32_t active_frames() noexcept { return g_activeFrames.load(std::memory_order_relaxed); }

void push_error_scope() noexcept {
  webgpu::g_device.PushErrorScope(wgpu::ErrorFilter::Validation);
  webgpu::g_device.PushErrorScope(wgpu::ErrorFilter::Internal);
}

void pop_error_scope() noexcept {
  for (int scope = 0; scope < 2; ++scope) {
    webgpu::g_device.PopErrorScope(
        wgpu::CallbackMode::AllowSpontaneous,
        [](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView message) {
          if (status != wgpu::PopErrorScopeStatus::Success || type == wgpu::ErrorType::NoError) return;
          std::lock_guard lock(g_scopeMutex);
          if (g_scopeError.empty()) g_scopeError = std::string(std::string_view(message));
          g_scopeFailed.store(true, std::memory_order_release);
        });
  }
}

bool begin_input(uint32_t width, uint32_t height, uint32_t slots, wgpu::TextureView& view) noexcept {
  if (g_scopeFailed.exchange(false, std::memory_order_acquire)) {
    std::string error;
    {
      std::lock_guard lock(g_scopeMutex);
      error.swap(g_scopeError);
    }
    fail("WebGPU rejected a generated frame: " + error);
    return false;
  }
  slots = slots > kMaxSlots ? kMaxSlots : slots;
  if (slots == 0 || !ensure_runtime(slots) || !ensure_context(width, height)) return false;
  g_inputIndex = static_cast<uint32_t>(g_frameIndex % 2);
  auto& input = g_inputs[g_inputIndex];
  // The last present read both inputs (this one as its previous frame): wait for it before the
  // real frame overwrites this one. Contents are replaced, so the old layout is irrelevant.
  if (!begin_access(input, g_batchCompleteFd, false, kLayoutUndefined, kLayoutUndefined)) {
    fail("the shared input image could not be acquired");
    return false;
  }
  view = input.view;
  return true;
}

uint32_t submit_input(uint32_t slots) noexcept {
  if (g_context < 0) return 0;
  slots = slots > g_runtimeSlots ? g_runtimeSlots : slots;
  int inputFd = end_access(g_inputs[g_inputIndex]);
  // The present also overwrites the outputs, so it waits for our copies out of them.
  const int semaphoreFd = merge_fds(inputFd, g_outputsReadFd);
  g_outputsReadFd = -1;
  // The first frame has no previous one to interpolate from: it only starts the history.
  const uint32_t count = g_frameIndex == 0 ? 0 : slots;
  BbLsfgPresent present{};
  char error[512]{};
  if (g_api.present(g_context, semaphoreFd, count, &present, error, sizeof(error)) != 0) {
    fail(error[0] != 0 ? error : "bb_lsfg_present failed");
    return 0;
  }
  close_fd(g_batchCompleteFd);
  g_batchCompleteFd = present.batch_complete_fd;
  for (uint32_t i = 0; i < kMaxSlots; ++i) {
    close_fd(g_readyFds[i]);
    g_readyFds[i] = i < count ? present.out_ready_fd[i] : -1;
    if (i >= count && present.out_ready_fd[i] >= 0) ::close(present.out_ready_fd[i]);
  }
  ++g_frameIndex;
  g_generated = count;
  if (count != 0 && g_activeFrames.exchange(count, std::memory_order_relaxed) != count) {
    Log.info("Lossless Scaling frame generation active: {} generated per frame", count);
    logcat(ANDROID_LOG_INFO, "active: " + std::to_string(count) + " generated per frame");
  }
  return count;
}

wgpu::BindGroup begin_output(uint32_t index) noexcept {
  if (g_context < 0 || index >= g_generated) return {};
  auto& output = g_outputs[index];
  // The framegen device releases its outputs in GENERAL to the external queue family.
  if (!begin_access(output, g_readyFds[index], true, kLayoutGeneral, kLayoutGeneral)) {
    fail("a generated frame could not be acquired");
    return {};
  }
  return output.bindGroup;
}

void end_output(uint32_t index) noexcept {
  if (g_context < 0 || index >= g_generated) return;
  g_outputsReadFd = merge_fds(g_outputsReadFd, end_access(g_outputs[index]));
}

void shutdown() noexcept {
  destroy_runtime();
  g_sampler = {};
  if (g_library != nullptr) {
    dlclose(g_library);
    g_library = nullptr;
  }
}

} // namespace aurora::lsfg

#else

namespace aurora::lsfg {
void configure(const char*, float, bool) noexcept {}
const char* status() noexcept { return "Lossless Scaling frame generation needs Android with Vulkan"; }
bool enabled() noexcept { return false; }
uint32_t active_frames() noexcept { return 0; }
void push_error_scope() noexcept {}
void pop_error_scope() noexcept {}
bool begin_input(uint32_t, uint32_t, uint32_t, wgpu::TextureView&) noexcept { return false; }
uint32_t submit_input(uint32_t) noexcept { return 0; }
wgpu::BindGroup begin_output(uint32_t) noexcept { return {}; }
void end_output(uint32_t) noexcept {}
void shutdown() noexcept {}
} // namespace aurora::lsfg

#endif
