// Switch counterpart of lib/dawn/BackendBinding.cpp (excluded from this build): the surface is
// always the single libnx default window. Dawn's EGL swapchain is patched to accept it through the
// Android native-window surface source.
#include "../dawn/BackendBinding.hpp"

#include <memory>
#include <switch.h>
#include <webgpu/webgpu_cpp.h>

namespace aurora::webgpu::utils {

std::shared_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptor(SDL_Window*) {
  auto descriptor = std::make_shared<wgpu::SurfaceSourceAndroidNativeWindow>();
  descriptor->window = nwindowGetDefault();
  return descriptor;
}

} // namespace aurora::webgpu::utils
