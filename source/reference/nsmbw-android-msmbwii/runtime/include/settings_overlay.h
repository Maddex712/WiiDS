#pragma once

#include <aurora/aurora.h>
#include <aurora/event.h>

#include <string>

namespace settings_overlay {
// Apply persistent controller settings once Aurora has discovered host devices.
void InitializeRuntimeSettings() noexcept;
// Draw the F10 settings bar before each Aurora present.
void HandleEvents(const AuroraEvent* events) noexcept;
void Draw() noexcept;
bool StartupScreenVisible() noexcept;
void NotifyStrapInputAccepted() noexcept;
void AdvancePresentedFrame() noexcept;
// Put host controllers back to a neutral state before the process ends.
void ReleaseControllers() noexcept;
// FPS counter on or off (the Android menu sets it through JNI); saved to Config.toml.
void SetShowFps(bool show) noexcept;
bool ShowFps() noexcept;
// A frame-time graph under the FPS counter, off by default. Saved to Config.toml.
void SetFrameGraph(bool show) noexcept;
bool FrameGraph() noexcept;
// V-Sync: presents wait for the display, and the game's clock follows its refresh (hle/vi.cpp).
// Off by default. Saved to Config.toml.
void SetVSync(bool enabled) noexcept;
bool VSync() noexcept;
// Animated tiles (spinning coins, ? blocks...), on by default. Saved to Config.toml.
void SetSpinningCoins(bool enabled) noexcept;
bool SpinningCoins() noexcept;
// 120 FPS presentation by frame interpolation (game logic stays 60 Hz); saved to Config.toml.
void SetHighFrameRate(bool on) noexcept;
bool HighFrameRate() noexcept;
// Render scale: 1 = 480 lines, 0 = the window's (screen's) own resolution. Saved to Config.toml.
void SetRenderScale(float scale) noexcept;
float RenderScale() noexcept;
// DisplaySettings' aspect (4:3, 16:9, Fill), including a change not yet applied. Saved to Config.toml.
void SetAspect(int32_t mode) noexcept;
int32_t Aspect() noexcept;
// The settings window's Controller tab (netplay/netplay_ui.cpp), for devices with no F10 key (the
// AYN Thor): the twelve GameCube buttons the game reads through its virtual Wii Remote, each
// changed by pressing the controller's own button, saved to Config.toml like the F10 overlay's.
int ControllerButtonCount() noexcept;
// What drives button `index` on port 1 ("South", "South / Left trigger"), "None", or "" without a
// controller.
std::string ControllerBindingLabel(int index);
void BeginControllerCapture(int index) noexcept;
// The button waiting for a press, or -1.
int ControllerCapturing() noexcept;
// Takes the next new press (button, trigger or stick); gives up after 10 seconds.
void PollControllerCapture() noexcept;
void ResetControllerMapping() noexcept;
} // namespace settings_overlay
