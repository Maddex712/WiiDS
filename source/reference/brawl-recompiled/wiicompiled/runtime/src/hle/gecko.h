// Gecko/Ocarina codes (.gct), installed the way Project+'s Brawl Mod Loader (USB Loader GX) installs them.
#pragma once

#include <cstdint>
#include <string>

struct CpuContext;

namespace Gecko {

// Before the game's entry point runs: the code list, gc.txt's pokes and the handler hook. No-op without codes.
void Install();
// Which build this game runs, for network play to check that everyone runs the same one: "vanilla" without codes,
// otherwise the codes' SD folder and a hash of the code list.
std::string BuildIdentity();
// A problem with how the mods are laid out on the SD card, for the player (empty when there is none).
const std::string& SetupProblem();
// VISetNextFrameBuffer_HLE stands in for the guest function whose blr the VBI hook replaces.
void RunVbiHook(CpuContext* ctx);

} // namespace Gecko
