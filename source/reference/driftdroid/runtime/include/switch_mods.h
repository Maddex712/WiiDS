#pragma once

// Mods on the Switch: each folder in sdmc:/switch/WiiCompiled/Mods is one Riivolution-style mod
// (replacement game files, usually with a riivolution/*.xml), served through the same overlay
// pipeline as Retro Rewind. The same model as the Android launcher's "Mods..." (ModManager.kt):
// a ".disabled" file inside a mod turns it off, ".mod_order" in the Mods folder keeps the priority
// order, first wins. Changes apply the next time the game starts.
#if defined(__SWITCH__)

#include <string>
#include <vector>

namespace SwitchMods {

inline constexpr const char* kFolder = "sdmc:/switch/WiiCompiled/Mods";

struct Mod {
    std::string name;
    std::string path;
    bool enabled = true;
};

// Every mod folder, highest priority first.
std::vector<Mod> List();
void SetEnabled(const Mod& mod, bool enabled);
void SaveOrder(const std::vector<Mod>& mods);
bool Delete(const Mod& mod);
// The folders to serve files from, highest priority first.
std::vector<std::string> EnabledRoots();

}  // namespace SwitchMods
#endif
