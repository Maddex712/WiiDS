#include "wiids_config.h"
#include "toml.hpp"
#include <stdexcept>

namespace wiids::disc {
std::string ConfiguredNand(const std::string& config) {
    if (config.size() >= (1u << 20)) throw std::runtime_error("Config is too large to check safely.");
    const auto document = toml::parse_str(config);
    if (!document.contains("paths")) return {};
    const auto& paths = document.at("paths");
    if (!paths.is_table()) throw std::runtime_error("Config paths must be a table.");
    if (!paths.contains("nand_root")) return {};
    const auto path = toml::get<std::string>(paths.at("nand_root"));
    if (path.find('\0') != std::string::npos) throw std::runtime_error("Config NAND path contains a NUL.");
    return path;
}
}
