#include "thermal_quality.h"

#include "runtime_log.h"

#include <aurora/gfx.h>

#include <array>
#include <atomic>

namespace ThermalQuality {
namespace {

// Thresholds from Android's own thermal guidance: act above 0.95, hold between 0.85 and 0.95, and
// only recover below 0.85 - https://developer.android.com/games/optimize/adpf/thermal
constexpr float kThrottlingImminent = 0.95f;
constexpr float kRecoveryCeiling = 0.85f;

// Pixel counts relative to the user's setting: 1.0, ~72%, ~49%, ~30%.
constexpr std::array kFactors{1.0f, 0.85f, 0.7f, 0.55f};

// Recovering costs a resolution change (and a visible blink on some drivers), so only step back up
// after the device has stayed cool for several reports rather than oscillating on one good sample.
constexpr int kCoolReportsBeforeRecovery = 3;

std::atomic_bool g_enabled{true};
std::atomic<float> g_lastHeadroom{-1.0f};
std::atomic<size_t> g_step{0};
int g_coolReports = 0;

void ApplyStep(size_t step) noexcept {
    const float factor = kFactors[step];
    g_step.store(step, std::memory_order_relaxed);
    aurora_set_thermal_render_factor(factor);
    RT_LOGF(RT_TAG_RUNTIME, "thermal: render scale factor now %.2f (step %zu)\n",
            static_cast<double>(factor), step);
}

} // namespace

void ReportHeadroom(float headroom) noexcept {
    g_lastHeadroom.store(headroom, std::memory_order_relaxed);
    if (!g_enabled.load(std::memory_order_relaxed) || !(headroom > 0.0f)) {
        // A NaN or missing reading means the device cannot tell us; leave quality alone.
        return;
    }

    const size_t step = g_step.load(std::memory_order_relaxed);
    if (headroom >= kThrottlingImminent) {
        g_coolReports = 0;
        if (step + 1 < kFactors.size()) {
            ApplyStep(step + 1);
        }
        return;
    }
    if (headroom <= kRecoveryCeiling && step > 0) {
        if (++g_coolReports >= kCoolReportsBeforeRecovery) {
            g_coolReports = 0;
            ApplyStep(step - 1);
        }
        return;
    }
    // In the hold band: neither hotter nor reliably cooler.
    g_coolReports = 0;
}

void SetEnabled(bool enabled) noexcept {
    if (g_enabled.exchange(enabled, std::memory_order_relaxed) == enabled) {
        return;
    }
    if (!enabled && g_step.load(std::memory_order_relaxed) != 0) {
        g_coolReports = 0;
        ApplyStep(0);
    }
}

bool Enabled() noexcept { return g_enabled.load(std::memory_order_relaxed); }

float CurrentFactor() noexcept { return kFactors[g_step.load(std::memory_order_relaxed)]; }

float LastHeadroom() noexcept { return g_lastHeadroom.load(std::memory_order_relaxed); }

} // namespace ThermalQuality
