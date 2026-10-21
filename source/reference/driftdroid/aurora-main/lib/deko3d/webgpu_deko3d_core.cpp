// Hand-written core of the webgpu.h-on-deko3d shim. Real,
// deko3d-backed implementations of the webgpu.h C ABI go here, one at a time, each removed from
// gen_webgpu_stubs.py's generated set (IMPLEMENTED_ELSEWHERE) as it lands. Everything not yet
// implemented lives in webgpu_deko3d_stubs.generated.cpp instead.
//
// Why a C ABI shim and not a hand-rolled C++ wrapper: aurora-main's existing ~17,500 lines of
// wgpu:: code call into Dawn's real webgpu_cpp.h, which is itself just a thin trampoline over
// these same exported C functions (e.g. wgpu::Instance::CreateSurface calls
// wgpuInstanceCreateSurface). Implementing this C ABI against deko3d - instead of Dawn's Vulkan/
// D3D/Metal backends - means the entire existing rendering/pipeline/texture call graph in
// aurora.cpp and friends keeps compiling completely unchanged. No parallel Switch-only rendering
// path to maintain, no #ifdef forking at 17,500 call sites.
#include "webgpu_deko3d_stub_support.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <webgpu/webgpu.h>

#include "../dawn/BackendBinding.hpp"

namespace aurora::deko3d {

void webgpu_stub_unimplemented(const char* functionName) {
  std::fprintf(stderr, "[webgpu-deko3d] not yet implemented: %s\n", functionName);
  std::abort();
}

} // namespace aurora::deko3d

// WGPUInstance is an opaque handle (typedef struct WGPUInstanceImpl*) everywhere else in
// webgpu.h/webgpu_cpp.h - this is the one place its real layout is defined. Deliberately minimal
// for this first slice: just enough for wgpu::Instance's RAII (constructor AddRef, destructor
// Release) to be correct. Real device/adapter enumeration (the deko3d DkDevice this instance will
// eventually hand out, matching the mechanics already verified on hardware in Phase 3a) is the
// next increment, not yet wired up here.
struct WGPUInstanceImpl {
  int refCount = 1;
};

// gpu.cpp's create_surface() calls this (declared in ../dawn/BackendBinding.hpp, which it
// #includes regardless of platform) to get a platform-specific wgpu::ChainedStruct surface
// source descriptor - BackendBinding.cpp's real implementation switches on SDL window properties
// to build one of Dawn's X11/Wayland/Win32/Android/Cocoa-specific surface-source types, none of
// which apply here (BackendBinding.cpp itself is excluded from the Switch build - see
// aurora_core.cmake). Since wgpuInstanceCreateSurface below is OUR implementation, not Dawn's, it
// doesn't need to inspect this descriptor's contents at all: Switch has exactly one window
// (nwindowGetDefault()), so a bare non-null placeholder is enough to satisfy create_surface()'s
// null check - wgpuInstanceCreateSurface always binds to nwindowGetDefault() directly regardless
// of what's passed in.
namespace aurora::webgpu::utils {
std::shared_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptor(SDL_Window*) {
  return std::make_shared<wgpu::ChainedStruct>();
}
} // namespace aurora::webgpu::utils

extern "C" {

WGPUInstance wgpuCreateInstance(WGPU_NULLABLE WGPUInstanceDescriptor const* descriptor) {
  (void)descriptor;
  return new WGPUInstanceImpl();
}

void wgpuInstanceAddRef(WGPUInstance instance) {
  if (instance != nullptr) {
    ++instance->refCount;
  }
}

void wgpuInstanceRelease(WGPUInstance instance) {
  if (instance != nullptr && --instance->refCount == 0) {
    delete instance;
  }
}

} // extern "C"
