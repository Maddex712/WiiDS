#pragma once

// Thermal quality scaling: keeps the frame rate up on a hot phone by rendering fewer pixels,
// instead of letting the SoC throttle into a slideshow at full resolution. Android reports how
// close the device is to throttling (PowerManager.getThermalHeadroom, API 31) and the Kotlin side
// forwards it here; see ThermalMonitor.kt.
//
// Only the render resolution moves - never the user's saved setting, and never the game's own
// framebuffer size, which aurora clamps to.

namespace ThermalQuality {

// Headroom is 0..1+ where >= 1.0 means throttling is imminent. Called from the UI thread.
void ReportHeadroom(float headroom) noexcept;

// Off means the render factor snaps back to 1.0 and stays there.
void SetEnabled(bool enabled) noexcept;
bool Enabled() noexcept;

// 1.0 when untouched; 0.85/0.7/0.55 as it steps down. For the settings screen.
float CurrentFactor() noexcept;

// Last headroom Android reported, or a negative value if it never has (API < 31, or no report yet).
float LastHeadroom() noexcept;

} // namespace ThermalQuality
