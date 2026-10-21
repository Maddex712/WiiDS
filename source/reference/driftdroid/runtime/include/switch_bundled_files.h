#pragma once

#if defined(__SWITCH__)
#include <string>

// The NRO carries the files the game needs beside Config.toml (dsp_coef.bin, wii_bootstrap/) and a
// starter shader cache (Cache/) in its RomFS. This copies any that are missing to
// sdmc:/switch/WiiCompiled/, so a release is the single .nro file. Existing files are never
// overwritten. Returns a one-line summary for the log.
std::string SwitchInstallBundledFiles();
#endif
