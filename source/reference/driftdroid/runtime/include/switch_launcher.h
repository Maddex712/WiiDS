#pragma once

#if defined(__SWITCH__)
#include <aurora/aurora.h>

#include <string>

// DriftDroid's Switch launcher: a small ImGui front end that runs after the graphics are up and
// before the game starts. It installs the game (picks and unpacks the player's disc image), and
// has a browser for the SD card. It opens on every launch; Play starts the game.

// Loads the Switch system fonts into ImGui. Pass as AuroraConfig::imGuiInitCallback.
void SwitchLauncherInitFonts(const AuroraWindowSize* size);

// Runs the launcher. Returns true when the game should start, false when the
// player chose Quit (the caller should shut down and return).
// On true, an aurora frame has already been begun: the caller must mark its frame as active.
bool SwitchRunLauncher();

// The Retro Rewind pack folder (the one holding Binaries/Code.pul) on the SD card, or empty when
// it is not there. Looks where a player would put it: unzipped next to the game data, or at the
// root of the card exactly as on a Wii (sdmc:/RetroRewind6 beside sdmc:/riivolution).
std::string SwitchFindRetroRewindRoot();
#endif
