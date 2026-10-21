// Custom Vulkan driver loading on Android (Turnip, or a newer Qualcomm Adreno driver).
//
// Many Snapdragon phones ship Adreno Vulkan drivers years out of date, and several of the
// rendering faults players report on them are driver bugs rather than ours. Emulators solve this
// the same way: libadrenotools loads a user-supplied driver .so into its own linker namespace and
// hooks the system Vulkan loader to use it. Dawn normally dlopen()s libvulkan.so itself, so our
// Dawn build carries a small patch (dawn::native::vulkan::SetVulkanLibraryOverride) that makes it
// take the handle adrenotools returns instead.
//
// The launcher installs driver packages under the app's private files dir and writes
// video.gpu_driver_dir / video.gpu_driver_lib into Config.toml. Any failure here falls back to
// the system driver: a bad package must never stop the game from starting.

#if defined(__ANDROID__) && defined(MKW_HAVE_ADRENOTOOLS)

#include "android_gpu_driver.h"

#include "runtime_config.h"
#include "runtime_log.h"

#include <adrenotools/driver.h>

#include <dlfcn.h>
#include <sys/stat.h>

#include <string>

namespace dawn::native::vulkan {
// Weak so a build against the stock Dawn package (which lacks the patch) still links; the feature
// then reports itself unavailable instead of failing the link.
__attribute__((weak)) void SetVulkanLibraryOverride(void* dlHandle);
} // namespace dawn::native::vulkan

namespace {

// adrenotools installs its hook libraries from the app's nativeLibraryDir, which with legacy
// (extracted) JNI packaging is simply the directory this library was loaded from.
std::string NativeLibraryDir() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&NativeLibraryDir), &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    std::string path = info.dli_fname;
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
}

bool IsFile(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // namespace

void LoadAndroidCustomGpuDriver() {
    std::string driverDir = RuntimeConfigFile::GpuDriverDir("");
    const std::string driverLib = RuntimeConfigFile::GpuDriverLib("");
    if (driverDir.empty() || driverLib.empty()) {
        return; // system driver selected
    }
    if (driverDir.back() != '/') {
        driverDir += '/';
    }
    if (dawn::native::vulkan::SetVulkanLibraryOverride == nullptr) {
        RT_LOGF(RT_TAG_RUNTIME,
                "custom GPU driver requested, but this build's Dawn cannot take one; using the "
                "system driver\n");
        return;
    }
    if (!IsFile(driverDir + driverLib)) {
        RT_LOGF(RT_TAG_RUNTIME, "custom GPU driver %s%s is missing; using the system driver\n",
                driverDir.c_str(), driverLib.c_str());
        return;
    }
    // The hook libraries sit next to this library when the APK's native libraries are extracted
    // on install. They are not with the default packaging (libraries stay inside the APK), so the
    // launcher also copies them to gpu_drivers/hooks/, beside the driver folders.
    std::string hookDir = NativeLibraryDir();
    if (hookDir.empty() || !IsFile(hookDir + "libmain_hook.so")) {
        const auto parent = driverDir.find_last_of('/', driverDir.size() - 2);
        hookDir = parent == std::string::npos ? std::string{} : driverDir.substr(0, parent + 1) + "hooks/";
    }
    if (hookDir.empty() || !IsFile(hookDir + "libmain_hook.so")) {
        RT_LOGF(RT_TAG_RUNTIME,
                "custom GPU driver hooks not found next to the runtime (%s); using the system "
                "driver\n",
                hookDir.c_str());
        return;
    }
    // tmpLibDir is only consulted below API 29, and this app's minSdk is 29.
    void* handle = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, nullptr, hookDir.c_str(),
                                              driverDir.c_str(), driverLib.c_str(), nullptr, nullptr);
    if (handle == nullptr) {
        RT_LOGF(RT_TAG_RUNTIME, "custom GPU driver %s failed to load (%s); using the system driver\n",
                driverLib.c_str(), dlerror());
        return;
    }
    if (dlsym(handle, "vkGetInstanceProcAddr") == nullptr) {
        RT_LOGF(RT_TAG_RUNTIME,
                "custom GPU driver %s exports no vkGetInstanceProcAddr; using the system driver\n",
                driverLib.c_str());
        return;
    }
    // Deliberately never dlclose()d: Dawn keeps using it for the life of the process.
    dawn::native::vulkan::SetVulkanLibraryOverride(handle);
    RT_LOGF(RT_TAG_RUNTIME, "custom GPU driver loaded: %s%s\n", driverDir.c_str(), driverLib.c_str());
}

#endif
