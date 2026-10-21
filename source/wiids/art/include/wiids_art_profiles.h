#pragma once
// One entry point per game's art set. Each reads only the player's imported disc data and writes
// PNGs plus manifest.json into `out`; throws std::runtime_error with a player-facing reason.
#include "wiids_art.h"

#include <functional>
#include <string>

namespace wiids::art {

// discRoot: extracted NSMB Wii disc (sys/ + files/). Output: the NSMB app's ThorHud folder.
std::string BuildNsmbHud(const std::string& discRoot, const std::string& out);
// isoPath: the 1:1 Super Mario Sunshine ISO. Output: the Sunshine app's ThorHud folder.
std::string BuildSunshineHud(const std::string& isoPath, const std::string& out);
// discRoot: extracted PAL Mario Kart Wii disc. Output: the ThorMap folder (HUD, menu, faces, items).
std::string BuildMarioKartArt(const std::string& discRoot, const std::string& out);
// Launcher music (the disc's title theme) and UI sounds into out (ThorMap/launcher-audio).
std::string BuildMarioKartAudio(const std::string& discRoot, const std::string& out);
// A Wii disc's channel banner pictures (files/opening.bnr) as banner_*.png / icon_*.png.
std::string BuildBannerArt(const std::string& bannerPath, const std::string& out);
// Launcher cases composed from each disc into `out` (ThorMap/launcher), then artwork.json updated.
std::string BuildMarioKartCases(const std::string& discRoot, const std::string& out);
std::string BuildNsmbCases(const std::string& discRoot, const std::string& out);   // NSMB and MSMBWii
std::string BuildSunshineCases(const std::string& isoPath, const std::string& out);
std::string UpdateArtworkManifest(const std::string& out);
// Overhead race maps (course_NN.png/.json) for the Mario Kart lower screen, into the ThorMap folder.
std::string BuildMarioKartMap(const std::string& discRoot, int id, const std::string& out);
std::string BuildMarioKartMaps(const std::string& discRoot, const std::string& out);
// Retro Rewind maps for every track in an installed pack (RetroRewind6/), into out
// (ThorMap/retro_rewind): course_<key>.png/.json and maps.json. Resumable and incremental.
std::string BuildRetroRewindMaps(const std::string& packRoot, const std::string& out, const std::function<bool()>& cancelled = {});

} // namespace wiids::art
