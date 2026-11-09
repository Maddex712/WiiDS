#pragma once

// The graphics adapter aurora picked and whether its GPU driver workarounds are on, recorded once
// after aurora_initialize so the Android menu can show which GPU the game detected.

#include <mutex>
#include <string>

namespace GraphicsInfo {

inline std::mutex g_mutex;
inline std::string g_name;
inline bool g_compatActive = false;

inline void Set(std::string name, bool compatActive) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_name = std::move(name);
    g_compatActive = compatActive;
}

inline std::string Name() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_name;
}

inline bool g_integrated = false;
inline void SetIntegrated(bool integrated) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_integrated = integrated;
}
inline bool Integrated() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_integrated;
}

inline bool CompatActive() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_compatActive;
}

} // namespace GraphicsInfo
