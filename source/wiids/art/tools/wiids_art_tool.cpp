// Host test for wiids_art.
//   wiids_art_tool nsmb <extractedDiscRoot> <outDir>
//   wiids_art_tool sms  <game.iso> <outDir>
#include "wiids_art_profiles.h"

#include <cstdio>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    if (argc != 4) { std::fprintf(stderr, "usage: nsmb|sms <input> <outDir>\n"); return 2; }
    try {
        const std::string mode = argv[1];
        std::string result;
        if (mode == "nsmb") result = wiids::art::BuildNsmbHud(argv[2], argv[3]);
        else if (mode == "sms") result = wiids::art::BuildSunshineHud(argv[2], argv[3]);
        else if (mode == "mkw") result = wiids::art::BuildMarioKartArt(argv[2], argv[3]);
        else if (mode == "casesmkw") result = wiids::art::BuildMarioKartCases(argv[2], argv[3]);
        else if (mode == "casesnsmb") result = wiids::art::BuildNsmbCases(argv[2], argv[3]);
        else if (mode == "casessms") result = wiids::art::BuildSunshineCases(argv[2], argv[3]);
        else if (mode == "rrmaps") result = wiids::art::BuildRetroRewindMaps(argv[2], argv[3]);
        else if (mode == "maps") result = wiids::art::BuildMarioKartMaps(argv[2], argv[3]);
        else if (mode.compare(0, 3, "map") == 0) result = wiids::art::BuildMarioKartMap(argv[2], std::stoi(mode.substr(3)), argv[3]);
        else if (mode == "banner") result = wiids::art::BuildBannerArt(argv[2], argv[3]);
        else if (mode == "mkwaudio") result = wiids::art::BuildMarioKartAudio(argv[2], argv[3]);
        else { std::fprintf(stderr, "unknown mode\n"); return 2; }
        std::printf("ok: %s\n", result.c_str());
        return 0;
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
}
