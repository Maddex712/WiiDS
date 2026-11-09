#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

// Small host-services boundary for functionality that must not leak Win32
// assumptions into runtime or game code. Guest execution, virtual memory, and
// cooperative contexts remain outside this layer until dedicated macOS
// prototypes establish a safe abstraction.
namespace RuntimePlatform {

std::optional<std::filesystem::path> ExecutableDirectory() noexcept;

// Returns the platform's conventional per-user application-data directory.
// It does not create the directory, leaving that policy to the caller.
std::filesystem::path ApplicationDataDirectory(std::string_view applicationName);

// The root for per-run diagnostics. Keeping this here ensures log placement
// follows the same host convention as configuration and other user data.
std::filesystem::path LogDirectory(std::string_view applicationName);

uint64_t CurrentProcessId() noexcept;

// Android: reports how long the calling thread's last frame of work took (everything but the wait for
// the next retrace) to the system's performance hint service (ADPF), against a 16.7 ms target. The
// power HAL then raises clocks and moves the thread to faster cores when frames run long; without it
// the AYN Thor keeps the game on its middle cores and parks the prime core. A no-op elsewhere.
void ReportFrameWork(uint64_t workNanos) noexcept;

} // namespace RuntimePlatform
