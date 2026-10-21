#pragma once
#include <filesystem>
#include <cstdlib>
namespace RuntimeConfigFile { inline std::filesystem::path ResolvedDvdRoot() { return std::getenv("GD_ROOT"); } }
