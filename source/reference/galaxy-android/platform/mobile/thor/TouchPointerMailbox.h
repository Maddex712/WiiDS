#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>

namespace mobile_thor {
// UI thread writes one indivisible coordinate pair; only the input thread touches the game pad.
class TouchPointerMailbox {
    std::atomic<uint64_t> state{0};
public:
    void clear() { state.store(0, std::memory_order_release); }
    void set(float x, float y, bool down) {
        if (!down || !std::isfinite(x) || !std::isfinite(y)) { clear(); return; }
        const auto encode = [](float v) -> uint64_t {
            v = v < -1.f ? -1.f : v > 1.f ? 1.f : v;
            return static_cast<uint64_t>((v + 1.f) * 32767.5f + .5f);
        };
        state.store((uint64_t{1} << 32) | encode(x) | (encode(y) << 16), std::memory_order_release);
    }
    bool read(float& x, float& y) const {
        const uint64_t value = state.load(std::memory_order_acquire);
        if (!(value & (uint64_t{1} << 32))) return false;
        x = static_cast<float>(value & 65535) / 32767.5f - 1.f;
        y = static_cast<float>((value >> 16) & 65535) / 32767.5f - 1.f;
        return true;
    }
};
}
