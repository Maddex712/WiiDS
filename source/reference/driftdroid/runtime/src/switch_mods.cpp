#if defined(__SWITCH__)

#include "switch_mods.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace SwitchMods {
namespace {

constexpr const char* kDisabledMarker = ".disabled";
constexpr const char* kOrderFile = ".mod_order";

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<std::string> ReadOrder() {
    std::vector<std::string> names;
    const std::string path = std::string(kFolder) + "/" + kOrderFile;
    if (FILE* file = std::fopen(path.c_str(), "rb")) {
        char line[512];
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            std::string name(line);
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) {
                name.pop_back();
            }
            if (!name.empty()) {
                names.push_back(std::move(name));
            }
        }
        std::fclose(file);
    }
    return names;
}

// A mod unpacked one level too deep ("MyMod/MyMod/riivolution") still works.
std::string OverlayRoot(const std::string& path) {
    std::error_code ec;
    if (fs::is_directory(fs::path(path) / "riivolution", ec)) {
        return path;
    }
    std::vector<fs::path> children;
    for (fs::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) {
            children.push_back(it->path());
        }
    }
    if (children.size() == 1 && fs::is_directory(children[0] / "riivolution", ec)) {
        return children[0].string();
    }
    return path;
}

}  // namespace

std::vector<Mod> List() {
    std::vector<Mod> found;
    std::error_code ec;
    for (fs::directory_iterator it(kFolder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) {
            continue;
        }
        Mod mod;
        mod.name = it->path().filename().string();
        mod.path = std::string(kFolder) + "/" + mod.name;
        mod.enabled = !fs::exists(it->path() / kDisabledMarker, ec);
        found.push_back(std::move(mod));
    }
    std::sort(found.begin(), found.end(), [](const Mod& a, const Mod& b) { return Lower(a.name) < Lower(b.name); });

    // Saved order first, anything new after it by name.
    std::vector<Mod> ordered;
    for (const std::string& name : ReadOrder()) {
        const auto it = std::find_if(found.begin(), found.end(), [&](const Mod& m) { return m.name == name; });
        if (it != found.end()) {
            ordered.push_back(std::move(*it));
            found.erase(it);
        }
    }
    for (Mod& mod : found) {
        ordered.push_back(std::move(mod));
    }
    return ordered;
}

void SetEnabled(const Mod& mod, bool enabled) {
    const std::string marker = mod.path + "/" + kDisabledMarker;
    if (enabled) {
        std::remove(marker.c_str());
    } else if (FILE* file = std::fopen(marker.c_str(), "wb")) {
        std::fclose(file);
    }
}

void SaveOrder(const std::vector<Mod>& mods) {
    const std::string path = std::string(kFolder) + "/" + kOrderFile;
    if (FILE* file = std::fopen(path.c_str(), "wb")) {
        for (const Mod& mod : mods) {
            std::fprintf(file, "%s\n", mod.name.c_str());
        }
        std::fclose(file);
    }
}

bool Delete(const Mod& mod) {
    std::error_code ec;
    fs::remove_all(mod.path, ec);
    return !fs::exists(mod.path, ec);
}

std::vector<std::string> EnabledRoots() {
    std::vector<std::string> roots;
    for (const Mod& mod : List()) {
        if (mod.enabled) {
            roots.push_back(OverlayRoot(mod.path));
        }
    }
    return roots;
}

}  // namespace SwitchMods
#endif
