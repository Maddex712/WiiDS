#pragma once
#include <filesystem>
#include <cstdlib>
namespace RuntimeNandPath {
inline std::filesystem::path DiscoverNandRootPath() {
 const char* name=std::getenv("THOR_RESERVE_TEST_NAND_NAME");
 return std::filesystem::path(std::getenv("THOR_RESERVE_TEST_ROOT")) / (name?name:"NAND");
}
}
