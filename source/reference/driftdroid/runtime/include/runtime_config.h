#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <toml.hpp>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#else
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#endif

struct RuntimeUserConfig {
    std::optional<bool> widescreen;
    std::optional<int32_t> windowPosX;
    std::optional<int32_t> windowPosY;
    std::optional<uint32_t> windowWidth;
    std::optional<uint32_t> windowHeight;
    std::optional<float> resolutionMultiplier;
    std::optional<std::string> graphicsApi;
    // Android custom Vulkan driver, written by the launcher (android_gpu_driver.cpp).
    std::optional<std::string> gpuDriverDir;
    std::optional<std::string> gpuDriverLib;
    std::optional<std::string> displayMode;
    std::optional<uint32_t> frameInterpolationFps;
    std::optional<bool> skipUnreadyPipelines;
    std::optional<bool> disableCopyFilter;
    std::optional<bool> constantMatrixIndexing;
    std::optional<bool> lowDetailComputerKarts;
    std::optional<bool> thirtyFpsRaces;
    std::optional<std::string> vertexRepack;
    std::optional<bool> threadedGx;
    std::optional<bool> overlapFrameEncode;
    std::optional<bool> thermalAutoQuality;
    std::optional<bool> showShaderCompilation;
    std::optional<bool> hideSettingsButton;
    std::optional<bool> textureReplacements;
    std::optional<bool> textureDumps;
    std::optional<bool> showFps;
    std::optional<uint32_t> disabledPostProcessingPaths;
    std::optional<float> audioVolume;
    std::optional<float> audioMusicVolume;
    std::optional<float> audioSoundEffectsVolume;
    std::optional<float> audioUiVolume;
    std::optional<float> audioVoicesVolume;
    std::optional<bool> audioMuted;
    std::optional<bool> audioMixWorker;
    std::optional<bool> attenuateMusicWhenMediaPlays;
    std::optional<bool> networkEnabled;
    std::optional<std::string> nandRoot;
    std::optional<std::string> dvdRoot;
    // The one canonical Retro Rewind installation, owned and updated by the frontend. Setup records
    // it here instead of copying the pack, so an asset-only update is visible on the next launch.
    std::optional<std::string> retroRewindRoot;
    std::vector<std::string> overlayRoots;
    // Controller mappings use Wii/GameCube button names as keys and up to two
    // comma-separated SDL-style physical button names ("south", or
    // "dpad_up,left_shoulder") as values; pressing either bound button counts.
    std::array<std::optional<std::string>, 12> controllerButtons;
};

namespace RuntimeConfigFile {

inline constexpr const char* kConfigFileName = "Config.toml";
inline constexpr const char* kApplicationDirectoryName = "WiiCompiled";

inline bool IsThorDisplay() noexcept {
#if defined(__ANDROID__)
    const char* value = std::getenv("MKW_THOR_DISPLAY");
    return value && value[0] == '1' && value[1] == '\0';
#else
    return false;
#endif
}

#if defined(__ANDROID__)
// Android apps have no writable HOME/XDG-style environment variables and no meaningful
// "next to the executable" directory (the runtime is a .so loaded into a Java process, not a
// standalone executable) - the only per-app writable location the OS hands out is
// Context.getFilesDir(), which the Kotlin side must push down through JNI before any of the
// path functions below are called. See android/app/src/main/cpp/jni/paths.cpp (P5).
namespace AndroidPaths {
inline std::string& FilesDirStorage() {
    static std::string dir;
    return dir;
}
}  // namespace AndroidPaths

inline void SetAndroidFilesDir(std::string path) {
    AndroidPaths::FilesDirStorage() = std::move(path);
}

inline const std::string& AndroidFilesDir() {
    return AndroidPaths::FilesDirStorage();
}
#endif

// Portable layout. A directory holding kPortableMarkerFileName is a portable root; every piece of
// runtime user state (Config.toml, NAND, Cache, Logs) lives in <root>/UserData instead of
// %LOCALAPPDATA%. The marker is searched for from the executable's directory upwards, which is what
// makes an installation survive being moved or carried on removable media.
inline constexpr const char* kPortableMarkerFileName = "portable.txt";
inline constexpr const char* kPortableUserDataDirectoryName = "UserData";

// The installed layout puts products two levels below the root (<root>/Install/Base/game.exe). The
// bound is deliberately small so an unrelated marker far up a drive can never capture an ordinary
// installation.
inline constexpr int kPortableSearchDepth = 4;

inline std::string Trim(std::string_view text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

inline std::string RemoveComment(std::string_view line) {
    bool inSingle = false;
    bool inDouble = false;
    bool escaped = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (inDouble && ch == '\\' && !escaped) {
            escaped = true;
            continue;
        }
        if (ch == '\'' && !inDouble) {
            inSingle = !inSingle;
        } else if (ch == '"' && !inSingle && !escaped) {
            inDouble = !inDouble;
        } else if (ch == '#' && !inSingle && !inDouble) {
            return std::string(line.substr(0, i));
        }
        escaped = false;
    }
    return std::string(line);
}

inline bool IsSupportedResolutionMultiplier(float value) {
    // 0.5/0.75 exist for hardware whose GPU clock gets capped low by the OS/OEM power policy
    // (confirmed on-device: an efficient, well-batched frame - ~450 merged draw calls - still
    // only hit ~32fps at a 315MHz-capped GPU clock, pointing at fill-rate/shader cost, not draw
    // call overhead) - rendering below native resolution directly reduces that cost instead of
    // fighting a clock ceiling the app has no API to override.
    static constexpr std::array values{0.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
    return std::find(values.begin(), values.end(), value) != values.end();
}

// Must stay in step with the backend table in main.cpp, which is what actually
// maps these to AuroraBackend.
inline bool IsSupportedGraphicsApi(std::string_view value) {
#if defined(__SWITCH__)
    static constexpr std::array<std::string_view, 3> values{"auto", "opengles", "deko3d"};
#elif defined(__ANDROID__)
    static constexpr std::array<std::string_view, 3> values{"auto", "vulkan", "opengles"};
#else
    static constexpr std::array<std::string_view, 3> values{"auto", "d3d12", "vulkan"};
#endif
    return std::find(values.begin(), values.end(), value) != values.end();
}

inline bool IsSupportedDisplayMode(std::string_view value) {
    static constexpr std::array<std::string_view, 3> values{
        "windowed", "borderless", "exclusive",
    };
    return std::find(values.begin(), values.end(), value) != values.end();
}

// 240 was offered by an early build and is no longer supported; a saved 240 is
// migrated to 180 at the parse site.
inline bool IsSupportedFrameInterpolationFps(uint32_t value) {
    return value == 0 || value == 120 || value == 180;
}

inline std::optional<std::filesystem::path> ExecutableDirectory() {
#ifdef _WIN32
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
#elif defined(__ANDROID__)
    // No meaningful "next to the executable" concept for a .so loaded into a Java process, and
    // the portable-install marker-file search below doesn't apply to Android's sandboxed
    // per-app storage model - always nullopt (this also makes PortableRootDirectory() below
    // short-circuit to nullopt on Android, which is the intended behavior: "portable installs"
    // are a desktop-only concept).
    return std::nullopt;
#elif defined(__SWITCH__)
    // Same rationale as Android: there is no "next to the executable" concept for Switch homebrew
    // (hbloader always launches the .nro from a fixed sdmc:/ path, and "portable install" -
    // finding a marker file walking up from that location - is a desktop-only concept that
    // doesn't map onto Horizon OS's single sdmc:/switch/<app>/ layout). Also no /proc equivalent
    // to read the running executable's own path from even if this mattered.
    return std::nullopt;
#else
    // /proc/self/exe is a Linux-specific magic symlink to the running executable; readlink()
    // does not NUL-terminate and silently truncates if the buffer is too small, so this grows
    // the buffer until the result no longer fills it completely, the same doubling strategy as
    // the Windows branch above uses for GetModuleFileNameW.
    std::string buffer(256, '\0');
    for (;;) {
        const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) {
            return std::nullopt;
        }
        if (static_cast<size_t>(length) < buffer.size()) {
            buffer.resize(static_cast<size_t>(length));
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
}

// The portable root this executable lives under, or nullopt for a normal installation. The answer
// cannot change while the process runs, so it is resolved exactly once: every user-state path
// derives from it and they must not disagree with each other.
inline const std::optional<std::filesystem::path>& PortableRootDirectory() {
    static const std::optional<std::filesystem::path> root = []() -> std::optional<std::filesystem::path> {
        const auto executableDirectory = ExecutableDirectory();
        if (!executableDirectory) {
            return std::nullopt;
        }
        std::filesystem::path current = *executableDirectory;
        for (int level = 0; level <= kPortableSearchDepth; ++level) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(current / kPortableMarkerFileName, ec)) {
                return current;
            }
            const auto parent = current.parent_path();
            if (parent.empty() || parent == current) {
                break;
            }
            current = parent;
        }
        return std::nullopt;
    }();
    return root;
}

inline std::filesystem::path ApplicationDataDirectory() {
    if (const auto& portableRoot = PortableRootDirectory()) {
        return *portableRoot / kPortableUserDataDirectoryName;
    }
#ifdef _WIN32
    PWSTR rawPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &rawPath)) && rawPath) {
        const std::filesystem::path directory = std::filesystem::path(rawPath) / kApplicationDirectoryName;
        CoTaskMemFree(rawPath);
        return directory;
    }
#elif defined(__ANDROID__)
    // Set once from JNI at startup via SetAndroidFilesDir() (Context.getFilesDir()). Falling
    // through to std::filesystem::current_path() below if it was never set would silently write
    // into whatever directory the process happened to start in (an app has no useful "current
    // directory" to fall back to) - not fatal, but callers should ensure SetAndroidFilesDir() ran
    // during startup before relying on this path being correct.
    if (const std::string& filesDir = AndroidFilesDir(); !filesDir.empty()) {
        return std::filesystem::path(filesDir) / kApplicationDirectoryName;
    }
#elif defined(__SWITCH__)
    // Horizon OS has no environment variables and no per-user profile directories in the
    // XDG/FOLDERID sense - homebrew conventionally keeps its data on the SD card under
    // sdmc:/switch/<app>/, alongside every other homebrew app's own subdirectory (mirrors how
    // hbmenu itself organizes .nro apps). Fixed, not derived from anything at runtime, the same
    // way Android's AndroidFilesDir() is a fixed per-app sandbox path rather than something to
    // search for.
    return std::filesystem::path("sdmc:/switch") / kApplicationDirectoryName;
#else
    // XDG Base Directory spec equivalent of FOLDERID_LocalAppData: $XDG_DATA_HOME if set and
    // non-empty, otherwise its default of $HOME/.local/share.
    if (const char* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && *xdgDataHome) {
        return std::filesystem::path(xdgDataHome) / kApplicationDirectoryName;
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / kApplicationDirectoryName;
    }
#endif
    return std::filesystem::current_path() / kApplicationDirectoryName;
}

inline std::filesystem::path ResolveConfigPath() {
    return ApplicationDataDirectory() / kConfigFileName;
}

inline void EnsureConfigFile() {
    const std::filesystem::path path = ResolveConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec || std::filesystem::exists(path, ec)) {
        return;
    }

    std::ofstream output(path);
    if (!output) {
        return;
    }
    output << "# WiiCompiled user configuration\n"
              "# Set paths.dvd_root to an extracted Mario Kart Wii DATA directory.\n\n"
              "[video]\n"
              "widescreen = true\n"
              "resolution_multiplier = " << (IsThorDisplay() ? "0.0\n" : "1.0\n") <<
              "frame_interpolation_fps = 0\n"
              "display_mode = \"borderless\"\n"
              "graphics_api = \"auto\"\n"
              "skip_unready_pipelines = true\n"
              "disable_copy_filter = true\n"
              "show_fps = true\n"
              "# Dolphin-style custom textures. When enabled, the renderer indexes\n"
              "# texture_replacements/ next to this file at startup and substitutes\n"
              "# any tex1_<W>x<H>_<hash>[_<tlut hash>]_<format>.dds or .png it finds\n"
              "# there for the matching game texture. texture_dumps writes every\n"
              "# unmatched texture to Cache/texture_dumps under the name a\n"
              "# replacement would need. Both are read once, at startup.\n"
              "texture_replacements = false\n"
              "texture_dumps = false\n\n"
              "[audio]\n"
              "volume = 1.0\n"
              "music_volume = 1.0\n"
              "sound_effects_volume = 1.0\n"
              "ui_volume = 1.0\n"
              "voices_volume = 1.0\n"
              "muted = false\n"
              "attenuate_music_when_media_plays = false\n"
              "# Runs the AX/DSP voice mix on its own thread, joined before the\n"
              "# guest can observe it. Set to false to mix inline on the guest\n"
              "# thread exactly as the runtime did before.\n"
              "mix_worker = true\n\n"
              "[network]\n"
              "enabled = true\n\n"
              "[paths]\n"
              "# dvd_root = \"D:\\\\MarioKartWii\\\\DATA\"\n"
              "# nand_root = \"D:\\\\WiiNand\"\n"
              "# retro_rewind_root = \"D:\\\\RetroRewind\\\\RetroRewind6\"\n"
              "# overlay_roots = [\"D:\\\\RetroRewind\"]\n";
}

template <typename T>
inline std::optional<T> FindConfigValue(
    const toml::value& document, std::string_view section, std::string_view key) {
    try {
        return toml::find<T>(document, std::string(section), std::string(key));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::optional<uint32_t> FindConfigUint(
    const toml::value& document, std::string_view section, std::string_view key) {
    const auto value = FindConfigValue<int64_t>(document, section, key);
    if (!value || *value < 0 || static_cast<uint64_t>(*value) > UINT32_MAX) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(*value);
}

inline std::optional<int32_t> FindConfigInt(
    const toml::value& document, std::string_view section, std::string_view key) {
    const auto value = FindConfigValue<int64_t>(document, section, key);
    if (!value || *value < INT32_MIN || *value > INT32_MAX) {
        return std::nullopt;
    }
    return static_cast<int32_t>(*value);
}

inline std::optional<float> FindConfigFloat(
    const toml::value& document, std::string_view section, std::string_view key) {
    std::optional<double> value = FindConfigValue<double>(document, section, key);
    if (!value) {
        if (const auto integer = FindConfigValue<int64_t>(document, section, key)) {
            value = static_cast<double>(*integer);
        }
    }
    if (!value || !std::isfinite(*value) ||
        *value < -static_cast<double>(std::numeric_limits<float>::max()) ||
        *value > static_cast<double>(std::numeric_limits<float>::max())) {
        return std::nullopt;
    }
    return static_cast<float>(*value);
}

inline void AppendOverlayRoots(RuntimeUserConfig& config, const std::string& roots) {
    size_t begin = 0;
    while (begin < roots.size()) {
        const size_t end = roots.find(';', begin);
        std::string root = Trim(std::string_view(roots).substr(begin, end - begin));
        if (!root.empty()) {
            config.overlayRoots.push_back(std::move(root));
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
}

inline RuntimeUserConfig ParseConfigDocument(const toml::value& document) {
    RuntimeUserConfig config;

    static constexpr std::array<std::string_view, 12> buttonKeys = {
        "a", "b", "x", "y", "start", "z", "l", "r", "up", "down", "left", "right",
    };
    for (size_t index = 0; index < buttonKeys.size(); ++index) {
        config.controllerButtons[index] =
            FindConfigValue<std::string>(document, "controller", buttonKeys[index]);
    }

    config.widescreen = FindConfigValue<bool>(document, "video", "widescreen");
    config.windowPosX = FindConfigInt(document, "video", "window_x");
    config.windowPosY = FindConfigInt(document, "video", "window_y");
    if (auto value = FindConfigUint(document, "video", "window_width"); value && *value != 0) {
        config.windowWidth = *value;
    }
    if (auto value = FindConfigUint(document, "video", "window_height"); value && *value != 0) {
        config.windowHeight = *value;
    }
    if (auto value = FindConfigFloat(document, "video", "resolution_multiplier");
        value && IsSupportedResolutionMultiplier(*value)) {
        config.resolutionMultiplier = *value;
    }
    if (auto value = FindConfigValue<std::string>(document, "video", "graphics_api")) {
        if (IsSupportedGraphicsApi(*value)) {
            config.graphicsApi = *value;
        } else {
            std::cerr << "[runtime] Unknown video.graphics_api=\"" << *value
                      << "\", using the automatic backend" << std::endl;
        }
    }
    config.gpuDriverDir = FindConfigValue<std::string>(document, "video", "gpu_driver_dir");
    config.gpuDriverLib = FindConfigValue<std::string>(document, "video", "gpu_driver_lib");
    if (auto value = FindConfigValue<std::string>(document, "video", "display_mode");
        value && IsSupportedDisplayMode(*value)) {
        config.displayMode = *value;
    }
    if (auto value = FindConfigUint(document, "video", "frame_interpolation_fps")) {
        const uint32_t migrated = *value == 240u ? 180u : *value;
        if (IsSupportedFrameInterpolationFps(migrated)) {
            config.frameInterpolationFps = migrated;
        }
    }
    config.skipUnreadyPipelines = FindConfigValue<bool>(document, "video", "skip_unready_pipelines");
    config.disableCopyFilter = FindConfigValue<bool>(document, "video", "disable_copy_filter");
    config.constantMatrixIndexing = FindConfigValue<bool>(document, "video", "constant_matrix_indexing");
    config.lowDetailComputerKarts = FindConfigValue<bool>(document, "video", "low_detail_computer_karts");
    config.thirtyFpsRaces = FindConfigValue<bool>(document, "video", "thirty_fps_races");
    config.vertexRepack = FindConfigValue<std::string>(document, "video", "vertex_repack");
    config.threadedGx = FindConfigValue<bool>(document, "video", "threaded_gx");
    config.overlapFrameEncode = FindConfigValue<bool>(document, "video", "overlap_frame_encode");
    config.thermalAutoQuality = FindConfigValue<bool>(document, "video", "thermal_auto_quality");
    config.showShaderCompilation = FindConfigValue<bool>(document, "video", "show_shader_compilation");
    config.hideSettingsButton = FindConfigValue<bool>(document, "video", "hide_settings_button");
    config.showFps = FindConfigValue<bool>(document, "video", "show_fps");
    config.textureReplacements = FindConfigValue<bool>(document, "video", "texture_replacements");
    config.textureDumps = FindConfigValue<bool>(document, "video", "texture_dumps");
    if (auto value = FindConfigUint(document, "video", "disabled_post_processing_paths");
        value && (*value & ~0x10u) == 0) {
        config.disabledPostProcessingPaths = *value & 0x10u;
    }

    auto readVolume = [&](std::string_view key) -> std::optional<float> {
        auto value = FindConfigFloat(document, "audio", key);
        return value && *value >= 0.0f && *value <= 1.0f ? value : std::nullopt;
    };
    config.audioVolume = readVolume("volume");
    config.audioMusicVolume = readVolume("music_volume");
    config.audioSoundEffectsVolume = readVolume("sound_effects_volume");
    config.audioUiVolume = readVolume("ui_volume");
    config.audioVoicesVolume = readVolume("voices_volume");
    config.audioMuted = FindConfigValue<bool>(document, "audio", "muted");
    config.audioMixWorker = FindConfigValue<bool>(document, "audio", "mix_worker");
    config.attenuateMusicWhenMediaPlays =
        FindConfigValue<bool>(document, "audio", "attenuate_music_when_media_plays");
    config.networkEnabled = FindConfigValue<bool>(document, "network", "enabled");

    config.nandRoot = FindConfigValue<std::string>(document, "paths", "nand_root");
    config.dvdRoot = FindConfigValue<std::string>(document, "paths", "dvd_root");
    config.retroRewindRoot = FindConfigValue<std::string>(document, "paths", "retro_rewind_root");
    if (auto roots = FindConfigValue<std::vector<std::string>>(document, "paths", "overlay_roots")) {
        for (auto& root : *roots) {
            root = Trim(root);
            if (!root.empty()) {
                config.overlayRoots.push_back(std::move(root));
            }
        }
    } else if (auto roots = FindConfigValue<std::string>(document, "paths", "overlay_roots")) {
        AppendOverlayRoots(config, *roots);
    }

    return config;
}

inline RuntimeUserConfig ParseConfig(std::istream& input, std::string sourceName = "Config.toml") {
    try {
        return ParseConfigDocument(toml::parse(input, std::move(sourceName)));
    } catch (const std::exception& exception) {
        std::cerr << "[runtime-config] Invalid TOML; using built-in defaults: "
                  << exception.what() << std::endl;
        return {};
    }
}

inline RuntimeUserConfig LoadConfigFile() {
    EnsureConfigFile();
    std::ifstream file(ResolveConfigPath(), std::ios::binary);
    return file ? ParseConfig(file, ResolveConfigPath().string()) : RuntimeUserConfig{};
}

inline const RuntimeUserConfig& Get() {
    static RuntimeUserConfig config = LoadConfigFile();
    return config;
}

inline RuntimeUserConfig& Mutable() {
    return const_cast<RuntimeUserConfig&>(Get());
}

inline constexpr std::array<std::string_view, 12> kControllerButtonKeys = {
    "a", "b", "x", "y", "start", "z", "l", "r", "up", "down", "left", "right",
};

inline const std::optional<std::string>& ControllerButton(size_t index) {
    static const std::optional<std::string> empty;
    return index < Get().controllerButtons.size() ? Get().controllerButtons[index] : empty;
}

// Update one TOML value without discarding comments, unrelated settings, or
// user-specific paths. This is used by the in-game F10 settings bar.
// Whether the most recent WriteSetting succeeded, so the settings panel can report it truthfully.
inline std::atomic_bool& LastSettingWriteSucceeded() {
    static std::atomic_bool ok{true};
    return ok;
}

inline bool WriteSetting(std::string_view section, std::string_view key, std::string_view value) {
    // Serialize read/modify/commit so simultaneous setting writers cannot
    // overwrite each other's changes or share an unfinished temporary file.
    static std::mutex writeMutex;
    const std::lock_guard<std::mutex> lock(writeMutex);
    const auto path = ResolveConfigPath();
    std::vector<std::string> lines;
    {
        // The read handle must be closed before the file is reopened for writing below: the
        // Switch's SD card filesystem refuses to open a file for writing while it is still open
        // for reading, which made every settings change silently fail to save.
        std::ifstream input(path);
        std::error_code readError;
        if ((!input && std::filesystem::exists(path, readError)) || readError) {
            std::cerr << "[runtime-config] Unable to read " << path.string() << std::endl;
            LastSettingWriteSucceeded() = false;
            return false;
        }
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
        }
        if (input.bad()) {
            std::cerr << "[runtime-config] Failed while reading " << path.string() << std::endl;
            LastSettingWriteSucceeded() = false;
            return false;
        }
    }

    const std::string normalizedSection = Trim(section);
    const std::string normalizedKey = Trim(key);
    size_t sectionStart = lines.size();
    size_t sectionEnd = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string trimmed = Trim(RemoveComment(lines[i]));
        if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
            const std::string found = Trim(std::string_view(trimmed).substr(1, trimmed.size() - 2));
            if (sectionStart != lines.size()) {
                sectionEnd = i;
                break;
            }
            if (found == normalizedSection) {
                sectionStart = i;
            }
        }
    }

    const std::string replacement = normalizedKey + " = " + std::string(value);
    if (sectionStart == lines.size()) {
        if (!lines.empty() && !lines.back().empty()) {
            lines.emplace_back();
        }
        lines.emplace_back("[" + normalizedSection + "]");
        lines.push_back(replacement);
    } else {
        bool replaced = false;
        for (size_t i = sectionStart + 1; i < sectionEnd; ++i) {
            const std::string uncommented = Trim(RemoveComment(lines[i]));
            const size_t equals = uncommented.find('=');
            if (equals != std::string::npos && Trim(std::string_view(uncommented).substr(0, equals)) == normalizedKey) {
                lines[i] = replacement;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            // Append after the section's last real line rather than after the blank line that
            // separates it from the next header: this file is edited by hand and by the host
            // installer as well, and a key parked below the separator reads as if it belonged to
            // the next section. The host writer (Launcher/WiiCompiled.Setup/RuntimeConfiguration.cs)
            // applies exactly this rule.
            size_t insertAt = sectionEnd;
            while (insertAt > sectionStart + 1 && Trim(lines[insertAt - 1]).empty()) {
                --insertAt;
            }
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), replacement);
        }
    }

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
#if defined(__SWITCH__)
    // Horizon cannot rename over an existing file. Keep its previous writer
    // until a platform-specific replacement transaction is available.
    std::ofstream output(path, std::ios::trunc);
    for (const auto& outputLine : lines) output << outputLine << '\n';
    output.flush();
    const bool written = static_cast<bool>(output);
    output.close();
    LastSettingWriteSucceeded() = written && !output.fail();
    return LastSettingWriteSucceeded();
#else
    auto temporary = path;
    temporary += ".runtime-tmp";
    const auto failed = [&]() {
        std::cerr << "[runtime-config] Unable to commit " << path.string()
                  << "; keeping existing configuration" << std::endl;
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        LastSettingWriteSucceeded() = false;
        return false;
    };
    if (ec) return failed();
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        return failed();
    }
    for (const auto& outputLine : lines) {
        output << outputLine << '\n';
    }
    output.flush();
    const bool written = static_cast<bool>(output);
    output.close();
    if (!written || output.fail()) return failed();

    // Flush the sibling file all the way to disk before atomically publishing
    // it. Process death or a failed write/flush/rename leaves Config.toml intact.
#ifdef _WIN32
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return failed();
    const bool flushed = FlushFileBuffers(file) != 0;
    const bool closed = CloseHandle(file) != 0;
    if (!flushed || !closed || !MoveFileExW(temporary.c_str(), path.c_str(),
                                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return failed();
#else
    const int file = open(temporary.c_str(), O_WRONLY);
    if (file < 0) return failed();
    const bool flushed = fsync(file) == 0;
    const bool closed = close(file) == 0;
    if (!flushed || !closed || std::rename(temporary.c_str(), path.c_str()) != 0) return failed();
    const auto directory = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    const int directoryFd = open(directory.c_str(), O_RDONLY);
    if (directoryFd >= 0) {
        // The new file is already committed. Some filesystems do not support
        // directory fsync; report durability limits without undoing that commit.
        if (fsync(directoryFd) != 0)
            std::cerr << "[runtime-config] Directory sync unavailable for " << directory.string() << std::endl;
        close(directoryFd);
    }
#endif
    LastSettingWriteSucceeded() = true;
    return true;
#endif
}

inline std::string FormatString(std::string_view value) {
    return toml::format(toml::value(std::string(value)));
}

inline bool SetResolutionMultiplier(float value) {
    Mutable().resolutionMultiplier = value;
    std::ostringstream formatted;
    formatted << value;
    return WriteSetting("video", "resolution_multiplier", formatted.str());
}

inline bool SetWindowSize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return false;
    }
    Mutable().windowWidth = width;
    Mutable().windowHeight = height;
    const bool wroteWidth = WriteSetting("video", "window_width", std::to_string(width));
    const bool wroteHeight = WriteSetting("video", "window_height", std::to_string(height));
    return wroteWidth && wroteHeight;
}

inline bool SetWindowPosition(int32_t x, int32_t y) {
    Mutable().windowPosX = x;
    Mutable().windowPosY = y;
    const bool wroteX = WriteSetting("video", "window_x", std::to_string(x));
    const bool wroteY = WriteSetting("video", "window_y", std::to_string(y));
    return wroteX && wroteY;
}

inline bool SetFrameInterpolationFps(uint32_t value) {
    if (!IsSupportedFrameInterpolationFps(value)) {
        return false;
    }
    Mutable().frameInterpolationFps = value;
    return WriteSetting("video", "frame_interpolation_fps", std::to_string(value));
}

inline bool SetDisplayMode(std::string value) {
    if (!IsSupportedDisplayMode(value)) {
        return false;
    }
    Mutable().displayMode = value;
    return WriteSetting("video", "display_mode", FormatString(value));
}

inline bool SetSkipUnreadyPipelines(bool value) {
    Mutable().skipUnreadyPipelines = value;
    return WriteSetting("video", "skip_unready_pipelines", value ? "true" : "false");
}

inline bool SetThermalAutoQuality(bool value) {
    Mutable().thermalAutoQuality = value;
    return WriteSetting("video", "thermal_auto_quality", value ? "true" : "false");
}

inline bool SetThreadedGx(bool value) {
    Mutable().threadedGx = value;
    return WriteSetting("video", "threaded_gx", value ? "true" : "false");
}

inline bool SetConstantMatrixIndexing(bool value) {
    Mutable().constantMatrixIndexing = value;
    return WriteSetting("video", "constant_matrix_indexing", value ? "true" : "false");
}

// Computer karts use the game's split-screen models in single-player races. Applies from the next race.
inline bool SetLowDetailComputerKarts(bool value) {
    Mutable().lowDetailComputerKarts = value;
    return WriteSetting("video", "low_detail_computer_karts", value ? "true" : "false");
}

// "auto", "off", "characters" or "all"; see AuroraSetVertexRepackMode. Read once at startup.
inline bool SetVertexRepack(const std::string& value) {
    Mutable().vertexRepack = value;
    return WriteSetting("video", "vertex_repack", FormatString(value));
}

inline bool SetDisableCopyFilter(bool value) {
    Mutable().disableCopyFilter = value;
    return WriteSetting("video", "disable_copy_filter", value ? "true" : "false");
}

inline bool SetHideSettingsButton(bool value) {
    Mutable().hideSettingsButton = value;
    return WriteSetting("video", "hide_settings_button", value ? "true" : "false");
}

inline bool SetShowShaderCompilation(bool value) {
    Mutable().showShaderCompilation = value;
    return WriteSetting("video", "show_shader_compilation", value ? "true" : "false");
}

inline bool SetShowFps(bool value) {
    Mutable().showFps = value;
    return WriteSetting("video", "show_fps", value ? "true" : "false");
}

inline bool SetDisabledPostProcessingPaths(uint32_t value) {
    Mutable().disabledPostProcessingPaths = value;
    std::ostringstream formatted;
    formatted << "0x" << std::hex << std::uppercase << value;
    return WriteSetting("video", "disabled_post_processing_paths", formatted.str());
}

inline bool SetControllerButton(size_t index, std::string value) {
    if (index >= kControllerButtonKeys.size()) {
        return false;
    }
    Mutable().controllerButtons[index] = value;
    return WriteSetting("controller", kControllerButtonKeys[index], FormatString(value));
}

inline bool SetAudioVolume(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    Mutable().audioVolume = value;
    std::ostringstream formatted;
    formatted << value;
    return WriteSetting("audio", "volume", formatted.str());
}

inline bool SetMusicVolume(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    Mutable().audioMusicVolume = value;
    std::ostringstream formatted;
    formatted << value;
    return WriteSetting("audio", "music_volume", formatted.str());
}

inline bool SetSoundEffectsVolume(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    Mutable().audioSoundEffectsVolume = value;
    std::ostringstream formatted;
    formatted << value;
    return WriteSetting("audio", "sound_effects_volume", formatted.str());
}

inline bool SetUiVolume(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    Mutable().audioUiVolume = value;
    std::ostringstream formatted;
    formatted << value;
    return WriteSetting("audio", "ui_volume", formatted.str());
}

inline bool SetVoicesVolume(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    Mutable().audioVoicesVolume = value;
    std::ostringstream formatted;
    formatted << value;
    return WriteSetting("audio", "voices_volume", formatted.str());
}

inline bool SetAudioMuted(bool value) {
    Mutable().audioMuted = value;
    return WriteSetting("audio", "muted", value ? "true" : "false");
}

inline bool SetAudioMixWorker(bool value) {
    Mutable().audioMixWorker = value;
    return WriteSetting("audio", "mix_worker", value ? "true" : "false");
}

inline bool SetAttenuateMusicWhenMediaPlays(bool value) {
    Mutable().attenuateMusicWhenMediaPlays = value;
    return WriteSetting("audio", "attenuate_music_when_media_plays", value ? "true" : "false");
}

inline bool WidescreenEnabled(bool fallback = false) {
    return Get().widescreen.value_or(fallback);
}

// Updates the live value too: SCGetAspectRatio_HLE (hle/sc.cpp) answers from it, and
// ApplyPendingMkwAspectMode (dynamic_aspect.cpp) re-runs the game's own aspect setup against it,
// so a change from the settings sidebar takes effect without restarting.
inline bool SetWidescreen(bool value) {
    Mutable().widescreen = value;
    return WriteSetting("video", "widescreen", value ? "true" : "false");
}

inline bool WindowPosition(int32_t& x, int32_t& y) {
    if (!Get().windowPosX || !Get().windowPosY) {
        return false;
    }
    x = *Get().windowPosX;
    y = *Get().windowPosY;
    return true;
}

inline uint32_t WindowWidth(uint32_t fallback) {
    return Get().windowWidth.value_or(fallback);
}

inline uint32_t WindowHeight(uint32_t fallback) {
    return Get().windowHeight.value_or(fallback);
}

inline float ResolutionMultiplier(float fallback = 1.0f) {
    // Auto follows the upper SDL surface's actual pixel size (1920x1080 on
    // Thor), rather than multiplying the guest's 640x528 EFB by an approximate
    // "2x" scale. Existing explicit settings remain authoritative.
    return std::max(0.0f, Get().resolutionMultiplier.value_or(IsThorDisplay() ? 0.0f : fallback));
}

inline float AudioVolume(float fallback = 1.0f) {
    return std::clamp(Get().audioVolume.value_or(fallback), 0.0f, 1.0f);
}

inline float MusicVolume(float fallback = 1.0f) {
    return std::clamp(Get().audioMusicVolume.value_or(fallback), 0.0f, 1.0f);
}

inline float SoundEffectsVolume(float fallback = 1.0f) {
    return std::clamp(Get().audioSoundEffectsVolume.value_or(fallback), 0.0f, 1.0f);
}

inline float UiVolume(float fallback = 1.0f) {
    return std::clamp(Get().audioUiVolume.value_or(fallback), 0.0f, 1.0f);
}

inline float VoicesVolume(float fallback = 1.0f) {
    return std::clamp(Get().audioVoicesVolume.value_or(fallback), 0.0f, 1.0f);
}

inline bool AudioMuted(bool fallback = false) {
    return Get().audioMuted.value_or(fallback);
}

// Off-thread AX/DSP mix. Default on; false restores the fully synchronous mix.
inline bool AudioMixWorkerEnabled(bool fallback = true) {
    return Get().audioMixWorker.value_or(fallback);
}

inline bool AttenuateMusicWhenMediaPlays(bool fallback = false) {
    return Get().attenuateMusicWhenMediaPlays.value_or(fallback);
}

inline uint32_t FrameInterpolationFps(uint32_t fallback = 0) {
    return Get().frameInterpolationFps.value_or(fallback);
}

inline bool SkipUnreadyPipelines(bool fallback = true) {
    return Get().skipUnreadyPipelines.value_or(fallback);
}

inline bool ThermalAutoQuality(bool fallback = false) {
    return Get().thermalAutoQuality.value_or(fallback);
}

inline bool ThreadedGx(bool fallback = false) {
    return Get().threadedGx.value_or(fallback);
}

// No settings-menu entry: an escape hatch for the always-overlapped frame encode, read at startup.
inline bool OverlapFrameEncode(bool fallback = true) {
    return Get().overlapFrameEncode.value_or(fallback);
}

// Races run in the game's own 30 FPS mode. Applies from the next race.
inline bool SetThirtyFpsRaces(bool value) {
    Mutable().thirtyFpsRaces = value;
    return WriteSetting("video", "thirty_fps_races", value ? "true" : "false");
}

inline bool ThirtyFpsRaces(bool fallback = false) {
    return Get().thirtyFpsRaces.value_or(fallback);
}

inline bool ConstantMatrixIndexing(bool fallback = false) {
    return Get().constantMatrixIndexing.value_or(fallback);
}

inline bool LowDetailComputerKarts(bool fallback = false) {
    return Get().lowDetailComputerKarts.value_or(fallback);
}

inline std::string VertexRepack(std::string fallback = "auto") {
    return Get().vertexRepack.value_or(std::move(fallback));
}

inline bool DisableCopyFilter(bool fallback = true) {
    return Get().disableCopyFilter.value_or(fallback);
}

inline bool HideSettingsButton(bool fallback = false) {
    return Get().hideSettingsButton.value_or(fallback);
}

inline bool ShowShaderCompilation(bool fallback = true) {
    return Get().showShaderCompilation.value_or(fallback);
}

inline bool ShowFps(bool fallback = true) {
    return Get().showFps.value_or(fallback);
}

inline bool TextureReplacements(bool fallback = false) {
    return Get().textureReplacements.value_or(fallback);
}

// Dumping only produces the names a replacement would need, so main.cpp
// gates it on TextureReplacements() as well.
inline bool TextureDumps(bool fallback = false) {
    return Get().textureDumps.value_or(fallback);
}

inline uint32_t DisabledPostProcessingPaths(uint32_t fallback = 0) {
    return Get().disabledPostProcessingPaths.value_or(fallback) & 0x10u;
}

inline std::string GraphicsApi(std::string fallback = "auto") {
    return Get().graphicsApi.value_or(std::move(fallback));
}

inline std::string GpuDriverDir(std::string fallback = "") {
    return Get().gpuDriverDir.value_or(std::move(fallback));
}

inline std::string GpuDriverLib(std::string fallback = "") {
    return Get().gpuDriverLib.value_or(std::move(fallback));
}

inline std::string DisplayMode(std::string fallback = "borderless") {
    return Get().displayMode.value_or(std::move(fallback));
}

inline bool NetworkEnabled(bool fallback = true) {
    return Get().networkEnabled.value_or(fallback);
}

inline std::string NandRoot(std::string fallback = "") {
    return Get().nandRoot.value_or(std::move(fallback));
}

inline std::string DvdRoot(std::string fallback = "") {
    return Get().dvdRoot.value_or(std::move(fallback));
}

// The one resolver for configured paths. A relative value means the same thing
// everywhere it can be configured: relative to the config file that named it,
// never to the process working directory (docs/WHEELWIZARD_CONTRACT.md).
inline std::filesystem::path ResolveRelativeTo(const std::filesystem::path& base,
                                               const std::string& value) {
    std::filesystem::path path(value);
    if (path.is_relative()) {
        path = base / path;
    }
    return path.lexically_normal();
}

inline std::filesystem::path ResolveRelativeToConfig(const std::string& value) {
    return ResolveRelativeTo(ResolveConfigPath().parent_path(), value);
}

// The extracted DATA directory. Empty when nothing is configured.
inline std::filesystem::path ResolvedDvdRoot() {
    const std::string configured = DvdRoot();
#if defined(__SWITCH__)
    // The first-boot importer (switch_rom_import.cpp) extracts the disc next to Config.toml.
    if (configured.empty()) {
        return ResolveRelativeToConfig("DATA");
    }
#endif
    return configured.empty() ? std::filesystem::path{} : ResolveRelativeToConfig(configured);
}

/// The canonical Retro Rewind installation the frontend owns, or "" when none is recorded.
inline std::string RetroRewindRoot(std::string fallback = "") {
    return Get().retroRewindRoot.value_or(std::move(fallback));
}

inline const std::vector<std::string>& OverlayRoots() {
    return Get().overlayRoots;
}

inline void LogLoadedConfig() {
    static const bool logged = [] {
        const auto& config = Get();
        const auto configPath = ResolveConfigPath();
        std::cout << "[runtime-config] " << configPath.string();
        if (!std::filesystem::exists(configPath)) {
            std::cout << " not found; using built-in defaults";
        } else {
            std::cout << " loaded";
            if (config.widescreen) {
                std::cout << " widescreen=" << (*config.widescreen ? "true" : "false");
            }
            if (config.windowWidth || config.windowHeight) {
                std::cout << " window=" << config.windowWidth.value_or(0) << "x"
                          << config.windowHeight.value_or(0);
            }
            if (config.resolutionMultiplier) {
                std::cout << " resolution_multiplier=" << *config.resolutionMultiplier;
            }
            if (config.dvdRoot) {
                std::cout << " dvd_root=" << *config.dvdRoot;
            }
            if (config.graphicsApi) {
                std::cout << " graphics_api=" << *config.graphicsApi;
            }
            if (config.frameInterpolationFps) {
                std::cout << " frame_interpolation_fps=" << *config.frameInterpolationFps;
            }
            if (config.skipUnreadyPipelines) {
                std::cout << " skip_unready_pipelines=" << (*config.skipUnreadyPipelines ? "true" : "false");
            }
            if (config.disableCopyFilter) {
                std::cout << " disable_copy_filter=" << (*config.disableCopyFilter ? "true" : "false");
            }
            if (config.showFps) {
                std::cout << " show_fps=" << (*config.showFps ? "true" : "false");
            }
            if (config.textureReplacements) {
                std::cout << " texture_replacements=" << (*config.textureReplacements ? "true" : "false");
            }
            if (config.textureDumps) {
                std::cout << " texture_dumps=" << (*config.textureDumps ? "true" : "false");
            }
            if (config.audioVolume) {
                std::cout << " audio_volume=" << *config.audioVolume;
            }
            if (config.audioMuted) {
                std::cout << " audio_muted=" << (*config.audioMuted ? "true" : "false");
            }
            if (config.networkEnabled) {
                std::cout << " network_enabled=" << (*config.networkEnabled ? "true" : "false");
            }
            if (config.nandRoot) {
                std::cout << " nand_root=" << *config.nandRoot;
            }
            if (config.retroRewindRoot) {
                std::cout << " retro_rewind_root=" << *config.retroRewindRoot;
            }
        }
        std::cout << std::endl;
        return true;
    }();
    (void)logged;
}

} // namespace RuntimeConfigFile
