#pragma once
#include <array>
#include <cmath>
#include <mutex>

namespace mobile_thor {
// UI producer, input-thread consumer. Keep ordered deltas: +edge then -motion must move back
// immediately, even when both arrived between game frames. No independent cursor to go stale.
class TrackpadMailbox {
    struct Delta { float x, y; };
    std::mutex mutex;
    std::array<Delta, 256> pending{};
    unsigned count = 0;
    bool held = false;
    void clearLocked() { count = 0; held = false; }
    static float clamp(float x) { return x < -1.f ? -1.f : x > 1.f ? 1.f : x; }
public:
    enum { Begin = 0, Move = 1, End = 2, Cancel = 3 };
    void clear() { std::lock_guard<std::mutex> lock(mutex); clearLocked(); }
    void send(float dx, float dy, int phase) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!std::isfinite(dx) || !std::isfinite(dy) || phase < Begin || phase > Cancel) { clearLocked(); return; }
        if (phase == Cancel) { clearLocked(); return; }
        if (phase == Begin) { if (held) clearLocked(); held = true; return; }
        if (!held) return;
        if (phase == End) { held = false; return; }
        if (count == pending.size()) { clearLocked(); return; } // refuse an overloaded gesture
        pending[count++] = {dx, dy};
    }
    bool apply(float& x, float& y) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!std::isfinite(x) || !std::isfinite(y)) { clearLocked(); return false; }
        const bool active = held || count != 0;
        for (unsigned i = 0; i < count; ++i) {
            x = clamp(x + pending[i].x);
            y = clamp(y + pending[i].y);
        }
        count = 0;
        return active;
    }
};
}
