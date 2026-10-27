// The Dawn half of custom GPU drivers (android_gpu_driver.cpp), without a patched Dawn.
//
// The stock Dawn package loads Vulkan with dlopen("libvulkan.so") from DynamicLib. The Android
// custom-driver build renames DynamicLib's dlopen/dlclose references in a copy of the Dawn
// archive to the two functions below (rr-6.13.4/build_native_drivers.py, llvm-objcopy
// --redefine-sym), so only Dawn's own library loading passes through here. Once a driver has been
// loaded through libadrenotools, Dawn's request for the Vulkan loader gets that driver's handle
// instead, which is what the upstream Dawn patch's SetVulkanLibraryOverride does.
//
// A link-wide --wrap=dlopen is not usable: libadrenotools' linkernsbypass reads the machine code
// at &dlopen to find the linker's internal loader, and a wrap would hand it this file's code.

#if defined(__ANDROID__) && defined(MKW_HAVE_ADRENOTOOLS)

#include <dlfcn.h>

#include <atomic>
#include <cstring>

namespace {

std::atomic<void*> g_vulkanOverride{nullptr};

bool IsVulkanLoader(const char* name) {
    if (name == nullptr) {
        return false;
    }
    const char* slash = std::strrchr(name, '/');
    const char* base = slash != nullptr ? slash + 1 : name;
    return std::strcmp(base, "libvulkan.so") == 0 || std::strcmp(base, "libvulkan.so.1") == 0;
}

} // namespace

extern "C" void* wiids_dawn_dlopen(const char* name, int flags) {
    if (void* handle = g_vulkanOverride.load(std::memory_order_acquire); handle != nullptr && IsVulkanLoader(name)) {
        return handle;
    }
    return dlopen(name, flags);
}

extern "C" int wiids_dawn_dlclose(void* handle) {
    // The driver stays loaded for the life of the process (see android_gpu_driver.cpp).
    if (handle != nullptr && handle == g_vulkanOverride.load(std::memory_order_acquire)) {
        return 0;
    }
    return dlclose(handle);
}

// The custom driver's handle, or null on the system driver. Aurora's Lossless Scaling frame
// generation (aurora-main/lib/webgpu/lsfg.cpp) runs on the same driver as the game through it.
extern "C" void* wiids_vulkan_library() {
    return g_vulkanOverride.load(std::memory_order_acquire);
}

namespace dawn::native::vulkan {
// Defined here rather than in Dawn; android_gpu_driver.cpp declares it weak.
void SetVulkanLibraryOverride(void* dlHandle) {
    g_vulkanOverride.store(dlHandle, std::memory_order_release);
}
} // namespace dawn::native::vulkan

#endif
