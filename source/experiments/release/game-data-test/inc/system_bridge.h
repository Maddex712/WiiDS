#pragma once
#include <string_view>
#include <cstdint>
inline void SetRuntimeExitCode(int) {}
namespace RuntimeCrash { inline void WriteCrashArtifacts(std::string_view, std::string_view = {}, const uint32_t* = nullptr) noexcept {} }
