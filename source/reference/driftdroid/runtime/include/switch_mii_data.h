#pragma once

// Mii data on the Switch, as in the Android launcher's "Mii data": export or replace the game's
// Mii database (RFL_DB.dat) and add single Miis (74-byte .rcd/.mii/.miigx files). There is no file
// picker here, so everything goes through one folder on the card: sdmc:/switch/WiiCompiled/Mii.
#if defined(__SWITCH__) || defined(MII_DATA_TEST_MAIN)

#include <filesystem>
#include <string>

namespace SwitchMiiData {

struct Result {
    bool ok = false;
    std::string message;
};

// `database` is the game's RFL_DB.dat, `folder` the exchange folder.
Result Export(const std::filesystem::path& database, const std::filesystem::path& folder);
Result Replace(const std::filesystem::path& database, const std::filesystem::path& folder);
Result AddSingleMiis(const std::filesystem::path& database, const std::filesystem::path& folder);

}  // namespace SwitchMiiData
#endif
