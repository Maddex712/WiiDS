#pragma once
#include <stddef.h>

struct GameEventFlag;
struct GalaxyCometTimePaper;

// Configured exactly once, from the package-internal Activity's scoped paths,
// before MobileApp creates settings, NAND, Vulkan caches or any game objects.
void port_kaizo_configure(const char* internalPath, const char* externalPath);
bool port_kaizo_enabled();
void port_kaizo_apply_flags(GameEventFlag* flags, size_t count);
const GalaxyCometTimePaper* port_kaizo_comet_table(int dome, const GalaxyCometTimePaper* original);
