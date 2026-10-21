#pragma once

#include "runtime_config.h"
#include "host_file_copy.h"
#include "runtime_log.h"
#include "system_bridge.h"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace RuntimeNandPath {

inline std::optional<std::filesystem::path> ExistingDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    if (!path.empty() && std::filesystem::is_directory(path, ec) && !ec) {
        return path;
    }
    return std::nullopt;
}

[[noreturn]] inline void FailNandRoot(const char* message, const std::filesystem::path& path = {}) {
    if (path.empty()) {
        RT_LOGF(RT_TAG_NAND, "ERROR: %s\n", message);
    } else {
        RT_LOGF(RT_TAG_NAND, "ERROR: %s: %s\n", message, path.string().c_str());
    }
    RT_LOGF(RT_TAG_NAND, "Set [paths] nand_root in Config.toml.\n");
    std::string details = message ? message : "The configured NAND could not be initialized.";
    if (!path.empty()) {
        details += "\n\nPath: ";
        details += path.string();
    }
    details += "\n\nSet [paths] nand_root in Config.toml and try again.";
    // Same fatal idiom as the DVD and OS paths: crash artifacts first so the run
    // folder always has them, then a non-zero exit code, the popup, and the
    // "already reported" latch so the atexit handler does not stack a second
    // generic report on top of this one.
    RuntimeCrash::WriteCrashArtifacts("nand_root", details);
    SetRuntimeExitCode(EXIT_FAILURE);
    ShowRuntimeFatalPopup("NAND initialization failed", details);
    MarkFatalErrorReported();
    std::exit(EXIT_FAILURE);
}

inline std::filesystem::path ResolveConfiguredPath(const std::string& value) {
    return RuntimeConfigFile::ResolveRelativeToConfig(value);
}

inline std::string PathStringWithoutTrailingSeparators(std::filesystem::path path) {
    std::string text = path.string();
    while (!text.empty()) {
        const char tail = text.back();
        if (tail != '\\' && tail != '/') {
            break;
        }
        text.pop_back();
    }
    return text;
}

inline std::filesystem::path ManagedNandRootPath() {
    return RuntimeConfigFile::ApplicationDataDirectory() / "NAND";
}

inline std::optional<std::filesystem::path> BootstrapPayloadPath() {
    if (auto executableDirectory = RuntimeConfigFile::ExecutableDirectory()) {
        const auto adjacent = *executableDirectory / "wii_bootstrap";
        if (ExistingDirectory(adjacent / "shared2" / "wc24")) {
            return adjacent;
        }
    }

#if defined(__ANDROID__) || defined(__SWITCH__)
    // Same reasoning as the dsp_coef.bin lookup in ax_mix.cpp: no executable directory and no
    // checked-out source tree on-device, so the bootstrap payload (runtime/assets/wii on desktop)
    // is instead staged alongside Config.toml, debug-only, the same way DiscData/dsp_coef.bin are.
    // Switch shares this branch with Android rather than getting its own: both have
    // ExecutableDirectory() == nullopt (see runtime_config.h) for the same underlying reason (no
    // "next to the executable" concept), so both need the payload staged next to their config the
    // same way.
    const auto androidPayload = RuntimeConfigFile::ApplicationDataDirectory() / "wii_bootstrap";
    if (ExistingDirectory(androidPayload / "shared2" / "wc24")) {
        return androidPayload;
    }
#endif

    // This makes developer-tree launches work without changing their release layout.
    for (auto base = std::filesystem::current_path(); !base.empty();) {
        const auto candidate = base / "runtime" / "assets" / "wii";
        if (ExistingDirectory(candidate / "shared2" / "wc24")) {
            return candidate;
        }
        const auto parent = base.parent_path();
        if (parent == base) {
            break;
        }
        base = parent;
    }
    return std::nullopt;
}

inline bool CopyBootstrapFile(const std::filesystem::path& sourceRoot,
                              const std::filesystem::path& destinationRoot,
                              const std::filesystem::path& relativePath,
                              std::error_code& ec) {
    const auto source = sourceRoot / relativePath;
    const auto destination = destinationRoot / relativePath;
    if (std::filesystem::exists(destination, ec)) {
        return !ec;
    }

    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        return false;
    }
    HostFileCopy::CopyFile(source, destination, std::filesystem::copy_options::none, ec);
    return !ec;
}

// Create these WC24 files only for a new profile; never overwrite user data.
constexpr std::string_view kBootstrapFiles[] = {
    "shared2/wc24/misc.bin",
    "shared2/wc24/nwc24dl.bin",
    "shared2/wc24/nwc24fl.bin",
    "shared2/wc24/nwc24fls.bin",
    "shared2/wc24/nwc24msg.cbk",
    "shared2/wc24/nwc24msg.cfg",
    "shared2/wc24/mbox/Readme.txt",
    "shared2/wc24/mbox/wc24recv.ctl",
    "shared2/wc24/mbox/wc24recv.mbx",
    "shared2/wc24/mbox/wc24send.ctl",
    "shared2/wc24/mbox/wc24send.mbx",
};

// Add first-run WC24 files only when the NAND has none yet.
inline bool SeedMissingBootstrapFiles(const std::filesystem::path& root) {
    const auto payload = BootstrapPayloadPath();
    if (!payload) {
        return false;
    }
    std::error_code ec;
    for (const std::string_view file : kBootstrapFiles) {
        const std::filesystem::path relativePath{std::string(file)};
        ec.clear();
        if (!CopyBootstrapFile(*payload, root, relativePath, ec)) {
            RT_LOG(RT_TAG_NAND) << "could not create " << (root / relativePath).string() << std::endl;
            return false;
        }
    }
    return true;
}

inline std::filesystem::path CreateManagedNandRoot() {
    const std::filesystem::path root = ManagedNandRootPath();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec || !std::filesystem::is_directory(root, ec)) {
        FailNandRoot("Unable to create managed NAND root", root);
    }

    if (!SeedMissingBootstrapFiles(root)) {
        FailNandRoot("Unable to initialize managed NAND", root);
    }

    const auto marker = root / ".mkw_recompiled_managed_nand";
    ec.clear();
    if (!std::filesystem::exists(marker, ec)) {
        std::ofstream markerFile(marker, std::ios::trunc);
        if (!markerFile) {
            FailNandRoot("Managed NAND root is not writable", root);
        }
        markerFile << "version=1\n";
        markerFile.close();
        if (!markerFile) {
            FailNandRoot("Unable to finish managed NAND initialization", root);
        }
    }

    RT_LOG(RT_TAG_NAND) << "using managed NAND root: " << root.string() << std::endl;
    return root;
}

inline std::filesystem::path DiscoverNandRootPath() {
    const std::string configPath = RuntimeConfigFile::NandRoot();
    if (!configPath.empty()) {
        const auto path = ResolveConfiguredPath(configPath);
        if (auto existing = ExistingDirectory(path)) {
            // Seed only a new configured NAND so existing frontend data stays unchanged.
            if (!SeedMissingBootstrapFiles(*existing)) {
                RT_LOG(RT_TAG_NAND) << "first-run WC24 seeding failed for the configured NAND root" << std::endl;
            }
            return *existing;
        }
        FailNandRoot("Configured NAND root is not an existing directory", path);
    }
    return CreateManagedNandRoot();
}

inline std::string DiscoverNandRootString() {
    return PathStringWithoutTrailingSeparators(DiscoverNandRootPath());
}

} // namespace RuntimeNandPath
