#if defined(__SWITCH__)

#include "switch_bundled_files.h"

#include <switch.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "runtime_config.h"

namespace fs = std::filesystem;

namespace {

struct CopyStats {
    int copied = 0;
    int present = 0;
    int failed = 0;
};

bool CopyFile(const fs::path& from, const fs::path& to) {
    std::FILE* in = std::fopen(from.string().c_str(), "rb");
    if (in == nullptr) {
        return false;
    }
    // Written under a temporary name and renamed, so a copy cut short by a power-off is not
    // mistaken for the finished file on the next launch.
    const fs::path partial = to.string() + ".part";
    std::FILE* out = std::fopen(partial.string().c_str(), "wb");
    if (out == nullptr) {
        std::fclose(in);
        return false;
    }
    std::vector<char> buffer(256 * 1024);
    bool ok = true;
    for (;;) {
        const size_t n = std::fread(buffer.data(), 1, buffer.size(), in);
        if (n == 0) {
            ok = std::ferror(in) == 0;
            break;
        }
        if (std::fwrite(buffer.data(), 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    std::fclose(in);
    ok = std::fclose(out) == 0 && ok;
    std::error_code ec;
    if (ok) {
        fs::rename(partial, to, ec);
        ok = !ec;
    }
    if (!ok) {
        fs::remove(partial, ec);
    }
    return ok;
}

void CopyTree(const fs::path& from, const fs::path& to, CopyStats& stats) {
    std::error_code ec;
    fs::create_directories(to, ec);
    for (auto it = fs::directory_iterator(from, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path name = it->path().filename();
        std::error_code typeEc;
        if (it->is_directory(typeEc)) {
            CopyTree(it->path(), to / name, stats);
        } else if (fs::exists(to / name, typeEc)) {
            ++stats.present;
        } else if (CopyFile(it->path(), to / name)) {
            ++stats.copied;
        } else {
            ++stats.failed;
        }
    }
}

}  // namespace

std::string SwitchInstallBundledFiles() {
    if (R_FAILED(romfsInit())) {
        return "bundled files: no RomFS in this NRO (dsp_coef.bin and wii_bootstrap must be beside Config.toml)";
    }
    CopyStats stats;
    CopyTree("romfs:/", RuntimeConfigFile::ApplicationDataDirectory(), stats);
    romfsExit();
    return "bundled files: " + std::to_string(stats.copied) + " installed, " + std::to_string(stats.present) +
           " already present, " + std::to_string(stats.failed) + " failed";
}

#endif  // __SWITCH__
