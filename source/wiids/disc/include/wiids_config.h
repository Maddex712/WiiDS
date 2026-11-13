#pragma once
#include <string>

namespace wiids::disc {
// Strictly parse the same TOML dialect as the runtimes. Missing paths.nand_root means default.
// Invalid syntax/type fails closed so storage operations cannot overlook configured saves.
std::string ConfiguredNand(const std::string& config);
}
