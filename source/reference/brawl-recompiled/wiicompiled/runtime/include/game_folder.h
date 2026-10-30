#pragma once

#include <filesystem>
#include <string>

// The player's own copy of New Super Mario Bros. Wii, extracted with Dolphin ("Extract Entire Disc"):
// a folder holding files/ and sys/. This port runs only the USA disc, revision 1 (SMNE01 rev 1);
// other revisions place the game's code elsewhere.
namespace GameFolder {

// The extracted game at `path` (or its DATA subfolder); empty when there is none.
std::filesystem::path Find(const std::filesystem::path& path);

// Why the game at `root` can't be used ("" when it can): not an extracted disc, or another game or
// revision, read from sys/boot.bin.
std::string Problem(const std::filesystem::path& root);

// At startup, before anything reads the disc. Windows: a game folder that is missing or wrong is
// asked for, with the Dolphin steps and a folder picker (a "game" folder next to the program is
// picked up by itself), and saved to Config.toml. Elsewhere the app supplies the folder.
// Returns false when the player gave up (the program then quits).
bool EnsureConfigured();

} // namespace GameFolder
