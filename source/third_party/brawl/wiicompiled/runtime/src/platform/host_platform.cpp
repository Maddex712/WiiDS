#include "platform/host_platform.h"

#include <cstdlib>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <pwd.h>
#endif

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <cstdio>
#endif

namespace RuntimePlatform {

std::optional<std::filesystem::path> ExecutableDirectory() noexcept {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::nullopt;
        }
        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0) {
        return std::nullopt;
    }
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        return std::nullopt;
    }
    path.resize(std::char_traits<char>::length(path.c_str()));
    std::error_code ec;
    const auto resolved = std::filesystem::weakly_canonical(path, ec);
    return (ec ? std::filesystem::path(path) : resolved).parent_path();
#elif defined(__ANDROID__)
    // The game is a library inside an APK, so there is no executable directory. The app unpacks
    // the files a desktop build keeps beside its executable (DSP ROM, pipeline cache, Wii
    // bootstrap NAND, CA bundle) and names that directory before SDL starts the game.
    if (const char* directory = std::getenv("WIICOMPILED_RUNTIME_DIR"); directory && *directory) {
        return std::filesystem::path(directory);
    }
    return std::nullopt;
#else
    return std::nullopt;
#endif
}

std::filesystem::path ApplicationDataDirectory(std::string_view applicationName) {
#if defined(_WIN32)
    PWSTR rawPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &rawPath)) && rawPath) {
        const std::filesystem::path directory = std::filesystem::path(rawPath) / applicationName;
        CoTaskMemFree(rawPath);
        return directory;
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / "Library" / "Application Support" / applicationName;
    }
    if (const passwd* user = getpwuid(getuid()); user && user->pw_dir && *user->pw_dir) {
        return std::filesystem::path(user->pw_dir) / "Library" / "Application Support" / applicationName;
    }
#elif defined(__ANDROID__)
    // The app's own storage (its external files directory, which a PC can reach over USB) already
    // belongs to this game alone, so it is used as-is rather than gaining an application subfolder.
    (void)applicationName;
    if (const char* directory = std::getenv("WIICOMPILED_DATA_DIR"); directory && *directory) {
        return std::filesystem::path(directory);
    }
#endif
    return std::filesystem::current_path() / applicationName;
}

std::filesystem::path LogDirectory(std::string_view applicationName) {
    return ApplicationDataDirectory(applicationName) / "Logs";
}

uint64_t CurrentProcessId() noexcept {
#if defined(_WIN32)
    return static_cast<uint64_t>(::GetCurrentProcessId());
#else
    return static_cast<uint64_t>(::getpid());
#endif
}

void ReportFrameWork(uint64_t workNanos) noexcept {
#if defined(__ANDROID__)
    // Android's performance hint API (ADPF, API 33), looked up at run time because the app's minimum is API 30.
    struct Manager;
    struct Session;
    using GetManagerFn = Manager* (*)();
    using CreateSessionFn = Session* (*)(Manager*, const int32_t*, size_t, int64_t);
    using ReportFn = int (*)(Session*, int64_t);
    static ReportFn report = nullptr;
    static Session* session = [] () -> Session* {
        // `adb shell setprop debug.wiids.adpf 0` turns the hints off, to compare runs with and without them.
        char off[PROP_VALUE_MAX] = {};
        if (__system_property_get("debug.wiids.adpf", off) > 0 && off[0] == '0') {
            std::printf("[platform] performance hints off (debug.wiids.adpf=0)\n");
            std::fflush(stdout);
            return nullptr;
        }
        void* android = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        auto getManager = android ? reinterpret_cast<GetManagerFn>(dlsym(android, "APerformanceHint_getManager")) : nullptr;
        auto createSession = android ? reinterpret_cast<CreateSessionFn>(dlsym(android, "APerformanceHint_createSession")) : nullptr;
        report = android ? reinterpret_cast<ReportFn>(dlsym(android, "APerformanceHint_reportActualWorkDuration")) : nullptr;
        Manager* manager = getManager && createSession && report ? getManager() : nullptr;
        const int32_t tid = gettid();
        Session* created = manager ? createSession(manager, &tid, 1, 16'666'667) : nullptr;
        std::printf("[platform] performance hint session for the game thread (tid %d): %s\n", tid,
                    created ? "on, target 16.7 ms" : "not available");
        std::fflush(stdout);
        return created;
    }();
    if (session && workNanos > 0) {
        report(session, static_cast<int64_t>(workNanos));
    }
#else
    (void)workNanos;
#endif
}

} // namespace RuntimePlatform
