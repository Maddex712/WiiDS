#include "settings_overlay.h"
#if defined(__ANDROID__)
#include "android_touch_overlay_bridge.h"
#endif
#include "audio_backend.h"
#include "controller_mapping_wizard.h"
#include "game_graphics_options.h"
#include "music_attenuation.h"
#include "music_player.h"
#include "thermal_quality.h"
#include "runtime_config.h"
#include "runtime_log.h"
#include "runtime_product.h"
#include "memory.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <bitset>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#endif

#include <dolphin/pad.h>
#include <dolphin/vi.h>
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <aurora/imgui.h>
#include <dolphin/gx/GXAurora.h>
#include "thor_settings_assets.h"

extern "C" int g_gxFrameCount;
// dynamic_aspect.cpp
void RequestMkwAspectMode(bool widescreen);

// Defined in runtime/src/hle/audio/ax_mix.cpp. That header is private to the HLE
// directory and is not on this target's include path.
namespace AxDspHle {
void SetMixWorkerEnabled(bool enabled);
}

void RuntimeSetLowDetailComputerKarts(bool enabled) noexcept;  // hle/low_detail_karts.cpp
void RuntimeSetThirtyFpsRaces(bool enabled) noexcept;          // hle/frame_rate_lock.cpp

namespace settings_overlay {
namespace {

const char* GraphicsApiDisplayName() {
    switch (aurora_get_backend()) {
    case BACKEND_D3D11: return "Direct3D 11";
    case BACKEND_D3D12: return "Direct3D 12";
    case BACKEND_METAL: return "Metal";
    case BACKEND_VULKAN: return "Vulkan";
    case BACKEND_OPENGL: return "OpenGL";
    case BACKEND_OPENGLES: return "OpenGL ES";
    case BACKEND_WEBGPU: return "WebGPU";
    case BACKEND_NULL: return "Null";
    case BACKEND_DEKO3D: return "deko3d";
    case BACKEND_AUTO: return "Automatic";
    }
    return "Unknown";
}

bool g_topBarVisible = false;
int g_controllerPort = 0;
float g_resolutionScale = RuntimeConfigFile::ResolutionMultiplier(1.0f);
int g_audioVolumePercent = static_cast<int>(std::lround(RuntimeConfigFile::AudioVolume(1.0f) * 100.0f));
int g_musicVolumePercent = static_cast<int>(std::lround(RuntimeConfigFile::MusicVolume(1.0f) * 100.0f));
int g_soundEffectsVolumePercent =
    static_cast<int>(std::lround(RuntimeConfigFile::SoundEffectsVolume(1.0f) * 100.0f));
int g_uiVolumePercent = static_cast<int>(std::lround(RuntimeConfigFile::UiVolume(1.0f) * 100.0f));
int g_voicesVolumePercent = static_cast<int>(std::lround(RuntimeConfigFile::VoicesVolume(1.0f) * 100.0f));
bool g_audioMuted = RuntimeConfigFile::AudioMuted(false);
bool g_audioMixWorker = RuntimeConfigFile::AudioMixWorkerEnabled(true);
bool g_attenuateMusicWhenMediaPlays = RuntimeConfigFile::AttenuateMusicWhenMediaPlays(false);
#if defined(__ANDROID__)
constexpr const char* kMediaMonitorWaitingText = "Waiting for media playback detection...";
constexpr const char* kMediaMonitorUnavailableText = "Media playback detection is unavailable.";
#else
constexpr const char* kMediaMonitorWaitingText = "Waiting for Windows Media Control...";
constexpr const char* kMediaMonitorUnavailableText = "Windows Media Control is unavailable.";
#endif
int ConfiguredFrameInterpolationMode() {
    switch (RuntimeConfigFile::FrameInterpolationFps(0)) {
    case 120:
        return 1;
    case 180:
        return RuntimeConfigFile::IsThorDisplay() ? 1 : 2;
    default:
        return 0;
    }
}
int g_frameInterpolationMode = ConfiguredFrameInterpolationMode();
int ParseDisplayModeConfig(const std::string& mode) {
    if (mode == "borderless") {
        return static_cast<int>(AURORA_DISPLAY_MODE_BORDERLESS);
    }
    if (mode == "exclusive") {
        return static_cast<int>(AURORA_DISPLAY_MODE_EXCLUSIVE);
    }
    return static_cast<int>(AURORA_DISPLAY_MODE_WINDOWED);
}

int g_displayMode = ParseDisplayModeConfig(RuntimeConfigFile::DisplayMode("borderless"));
bool g_skipUnreadyPipelines = RuntimeConfigFile::SkipUnreadyPipelines(true);
bool g_disableCopyFilter = RuntimeConfigFile::DisableCopyFilter(true);
bool g_constantMatrixIndexing = RuntimeConfigFile::ConstantMatrixIndexing(false);
// Computer karts load the game's own split-screen models in single-player races: far fewer draws
// and no driver animation per kart. On by default on Switch, where a full race is CPU-bound.
#if defined(__SWITCH__)
constexpr bool kLowDetailComputerKartsDefault = true;
#else
constexpr bool kLowDetailComputerKartsDefault = false;
#endif
bool g_lowDetailComputerKarts = RuntimeConfigFile::LowDetailComputerKarts(kLowDetailComputerKartsDefault);
bool g_thirtyFpsRaces = RuntimeConfigFile::ThirtyFpsRaces(false);

// video.vertex_repack: the Adreno 8xx "vertex explosion" workaround. Fixed for the process once
// rendering starts, so the settings entry only takes effect on the next launch.
constexpr std::array<const char*, 4> kVertexRepackConfigValues{"auto", "off", "characters", "all"};
int ParseVertexRepackConfig(const std::string& value) {
    for (size_t i = 0; i < kVertexRepackConfigValues.size(); ++i) {
        if (value == kVertexRepackConfigValues[i]) {
            return static_cast<int>(i);
        }
    }
    return 0;
}
int g_vertexRepackChoice = ParseVertexRepackConfig(RuntimeConfigFile::VertexRepack("auto"));

AuroraVertexRepackMode VertexRepackModeForChoice(int choice) {
    switch (choice) {
    case 1:
        return AURORA_VERTEX_REPACK_OFF;
    case 2:
        return AURORA_VERTEX_REPACK_CHARACTERS;
    case 3:
        return AURORA_VERTEX_REPACK_ALL;
    default:
        // Repacking every draw is what fixes tracks and menus too and is confirmed good in the
        // original game. In Retro Rewind it made other karts and item boxes vanish for one
        // Adreno 840 tester, so automatic stays with the character-only form there.
        return RuntimeProduct::IsRetroRewind() ? AURORA_VERTEX_REPACK_AUTO_CHARACTERS
                                               : AURORA_VERTEX_REPACK_AUTO_ALL;
    }
}
#if defined(__SWITCH__)
// On by default on Switch: measured 18 -> 22 FPS in a 12-kart race at stock clock.
bool g_threadedGx = RuntimeConfigFile::ThreadedGx(true);
#else
bool g_threadedGx = RuntimeConfigFile::ThreadedGx(false);
#endif
bool g_thermalAutoQuality = RuntimeConfigFile::ThermalAutoQuality(false);
bool g_showFps = RuntimeConfigFile::ShowFps(true);
bool g_showShaderCompilation = RuntimeConfigFile::ShowShaderCompilation(!RuntimeConfigFile::IsThorDisplay());
bool g_hideSettingsButton = RuntimeConfigFile::HideSettingsButton(false);
bool g_widescreen = RuntimeConfigFile::WidescreenEnabled(true);
uint32_t g_disabledPostProcessingPaths = RuntimeConfigFile::DisabledPostProcessingPaths(0);
std::array<int32_t, PAD_MAX_CONTROLLERS> g_configuredControllerIndices = [] {
    std::array<int32_t, PAD_MAX_CONTROLLERS> indices{};
    indices.fill(std::numeric_limits<int32_t>::min());
    return indices;
}();

struct ControllerButtonItem {
    const char* configKey;
    const char* label;
    PADButton padButton;
};

constexpr std::array<ControllerButtonItem, PAD_BUTTON_COUNT> kControllerButtons = {{
    {"a", "A", PAD_BUTTON_A},
    {"b", "B", PAD_BUTTON_B},
    {"x", "X", PAD_BUTTON_X},
    {"y", "Y", PAD_BUTTON_Y},
    {"start", "Start", PAD_BUTTON_START},
    {"z", "Z", PAD_TRIGGER_Z},
    {"l", "L", PAD_TRIGGER_L},
    {"r", "R", PAD_TRIGGER_R},
    {"up", "D-pad Up", PAD_BUTTON_UP},
    {"down", "D-pad Down", PAD_BUTTON_DOWN},
    {"left", "D-pad Left", PAD_BUTTON_LEFT},
    {"right", "D-pad Right", PAD_BUTTON_RIGHT},
}};

struct NativeButtonItem {
    const char* configName;
    const char* label;
    uint32_t nativeButton;
};

constexpr std::array<NativeButtonItem, SDL_GAMEPAD_BUTTON_COUNT + 1> kNativeButtons = {{
    {"unmapped", "Unmapped / analog trigger", PAD_NATIVE_BUTTON_INVALID},
    {"south", "South (A / Cross)", SDL_GAMEPAD_BUTTON_SOUTH},
    {"east", "East (B / Circle)", SDL_GAMEPAD_BUTTON_EAST},
    {"west", "West (X / Square)", SDL_GAMEPAD_BUTTON_WEST},
    {"north", "North (Y / Triangle)", SDL_GAMEPAD_BUTTON_NORTH},
    {"back", "Back / Select", SDL_GAMEPAD_BUTTON_BACK},
    {"guide", "Guide / Home", SDL_GAMEPAD_BUTTON_GUIDE},
    {"start", "Start / Options", SDL_GAMEPAD_BUTTON_START},
    {"left_stick", "Left stick click", SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"right_stick", "Right stick click", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {"left_shoulder", "Left shoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"right_shoulder", "Right shoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"dpad_up", "D-pad Up", SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"dpad_down", "D-pad Down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"dpad_left", "D-pad Left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"dpad_right", "D-pad Right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"misc1", "Misc 1 / Share", SDL_GAMEPAD_BUTTON_MISC1},
    {"right_paddle1", "Right paddle 1", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1},
    {"left_paddle1", "Left paddle 1", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1},
    {"right_paddle2", "Right paddle 2", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2},
    {"left_paddle2", "Left paddle 2", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2},
    {"touchpad", "Touchpad", SDL_GAMEPAD_BUTTON_TOUCHPAD},
    {"misc2", "Misc 2", SDL_GAMEPAD_BUTTON_MISC2},
    {"misc3", "Misc 3 / GC L click", SDL_GAMEPAD_BUTTON_MISC3},
    {"misc4", "Misc 4 / GC R click", SDL_GAMEPAD_BUTTON_MISC4},
    {"misc5", "Misc 5", SDL_GAMEPAD_BUTTON_MISC5},
    {"misc6", "Misc 6", SDL_GAMEPAD_BUTTON_MISC6},
}};

// Classic Controller Pro layout, indexed like kControllerButtons: the SNES-style
// diamond (A right, B bottom, X top, Y left) with digital bumpers driving the GC
// triggers and Z on Back/Select (the same home the NSO GC default gives it).
constexpr std::array<const char*, PAD_BUTTON_COUNT> kClassicProPreset = {
    "east",           // A
    "south",          // B
    "north",          // X
    "west",           // Y
    "start",          // Start
    "back",           // Z
    "left_shoulder",  // L
    "right_shoulder", // R
    "dpad_up", "dpad_down", "dpad_left", "dpad_right",
};

struct ResolutionItem {
    const char* label;
    float scale;
};

using Clock = std::chrono::steady_clock;

constexpr auto kCursorAutoHideDelay = std::chrono::seconds(5);
Clock::time_point g_lastMouseActivity{Clock::now()};
bool g_cursorHidden = false;

constexpr std::array<std::string_view, 3> kDisplayModeConfigNames = {
    "windowed", "borderless", "exclusive",
};

uint64_t g_presentedFrame = 0;
std::atomic_bool g_strapInputAccepted = false;
std::atomic_uint64_t g_startupDismissFrame = UINT64_MAX;
// The cover stays up until the warning screen's scene has really gone (NotifyBootSceneExited).
// Counting frames from the accepted input alone lifted it early on slow starts - the Switch showed
// the warning screen stuttering through its fade. The long count is only a fallback.
constexpr uint64_t kStrapTransitionCoverFrames = 600;
constexpr uint64_t kSceneExitCoverFrames = 6;
std::atomic_bool g_bootSceneExited = false;
// The same two limits in wall time. Frames alone kept the cover up far too long whenever the first
// frames after the warning screen were slow (reported on Switch: the intro's sound started under
// the splash and the intro then appeared part-way through), and the 600-frame fallback is half a
// minute at 20 FPS.
constexpr int64_t kStrapTransitionCoverMs = 4000;
constexpr int64_t kSceneExitCoverMs = 150;
std::atomic_int64_t g_startupDismissAtMs = INT64_MAX;

int64_t StartupClockMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

// Nothing below 1x: the renderer cannot draw smaller than the game's own framebuffer, and the
// old 0.5x/0.75x entries crashed on the way into a race.
constexpr std::array<ResolutionItem, 8> kResolutions = {{
    {"Auto (window size)", 0.0f},
    {"Native (1x)", 1.0f}, {"1.5x", 1.5f}, {"2x", 2.0f},
    {"3x", 3.0f}, {"4x", 4.0f}, {"6x", 6.0f}, {"8x", 8.0f},
}};

constexpr std::array<uint32_t, 3> kFrameInterpolationTargetFps{0, 120, 180};

const char* ResolutionLabel(const ResolutionItem& resolution) {
    return RuntimeConfigFile::IsThorDisplay() && resolution.scale == 0.0f
        ? "1080p (upper display)" : resolution.label;
}

bool IsHighResolutionScale(float scale) {
    return std::fabs(scale - 6.0f) < 0.001f || std::fabs(scale - 8.0f) < 0.001f;
}

bool IsHighFrameRateMode() {
    return kFrameInterpolationTargetFps[static_cast<size_t>(g_frameInterpolationMode)] > 60;
}

void SetResolutionScale(float scale) {
    g_resolutionScale = scale;
    VISetFrameBufferScale(scale);
    RuntimeConfigFile::SetResolutionMultiplier(scale);
}

void LimitResolutionForFrameRate() {
    if (IsHighFrameRateMode() && IsHighResolutionScale(g_resolutionScale)) {
        SetResolutionScale(4.0f);
    }
}

const NativeButtonItem* FindNativeButton(std::string value) {
    const auto it = std::find_if(kNativeButtons.begin(), kNativeButtons.end(), [&](const NativeButtonItem& item) {
        return value == item.configName;
    });
    return it == kNativeButtons.end() ? nullptr : &*it;
}

struct ControllerBindingPair {
    std::string primary;
    std::string secondary;
};

std::string TrimBindingToken(const std::string& token) {
    const size_t begin = token.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return {};
    }
    const size_t end = token.find_last_not_of(" \t");
    return token.substr(begin, end - begin + 1);
}

// Config values hold up to two comma-separated button names ("dpad_up" or
// "dpad_up,left_shoulder"); pressing either one counts as the GC button.
ControllerBindingPair SplitControllerBinding(const std::string& value) {
    const size_t comma = value.find(',');
    if (comma == std::string::npos) {
        return {TrimBindingToken(value), {}};
    }
    return {TrimBindingToken(value.substr(0, comma)), TrimBindingToken(value.substr(comma + 1))};
}

const NativeButtonItem& NativeButtonForValue(uint32_t nativeButton) {
    const auto it = std::find_if(kNativeButtons.begin(), kNativeButtons.end(), [&](const NativeButtonItem& item) {
        return nativeButton == item.nativeButton;
    });
    return it == kNativeButtons.end() ? kNativeButtons.front() : *it;
}

void SetTopBarVisible(bool visible) {
    if (g_topBarVisible == visible) {
        return;
    }
    g_topBarVisible = visible;
    PADBlockInput(visible);
#if defined(__ANDROID__)
    AndroidNotifySettingsVisibilityChanged(visible);
#endif
}

void ApplyConfiguredMappings() {
    for (uint32_t port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
        const int32_t controllerIndex = PADGetIndexForPort(port);
        if (controllerIndex == g_configuredControllerIndices[port]) {
            continue;
        }
        g_configuredControllerIndices[port] = controllerIndex;
        if (controllerIndex < 0) {
            continue;
        }

        uint32_t count = 0;
        if (PADGetButtonMappings(port, &count) == nullptr || count != PAD_BUTTON_COUNT) {
            continue;
        }
#if defined(__SWITCH__)
        // Requested directly: "i want the L and R button to do the same as ZL and ZR, use item and
        // well, drift". aurora's default puts GC L/R on the analog triggers (ZL/ZR) only and Z on
        // the right bumper, so the bumpers join as second bindings and Z gives its bumper up (the
        // game also looks behind on X). A binding saved in Config.toml still wins below.
        for (const uint16_t padButton : {PAD_TRIGGER_Z, PAD_TRIGGER_L, PAD_TRIGGER_R}) {
            const auto it = std::find_if(kControllerButtons.begin(), kControllerButtons.end(),
                                         [&](const auto& item) { return item.padButton == padButton; });
            if (it == kControllerButtons.end() ||
                RuntimeConfigFile::ControllerButton(static_cast<size_t>(it - kControllerButtons.begin()))) {
                continue;
            }
            if (padButton == PAD_TRIGGER_Z) {
                PADSetButtonMapping(port, PADButtonMapping{PAD_NATIVE_BUTTON_INVALID, padButton});
            } else {
                PADSetAltButtonMapping(
                    port, PADButtonMapping{static_cast<uint32_t>(padButton == PAD_TRIGGER_L
                                                                     ? SDL_GAMEPAD_BUTTON_LEFT_SHOULDER
                                                                     : SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
                                           padButton});
            }
        }
#endif
        for (size_t i = 0; i < kControllerButtons.size(); ++i) {
            const auto& configured = RuntimeConfigFile::ControllerButton(i);
            if (!configured) {
                continue;
            }
            const ControllerBindingPair binding = SplitControllerBinding(*configured);
            if (const NativeButtonItem* native = FindNativeButton(binding.primary)) {
                PADSetButtonMapping(port, PADButtonMapping{native->nativeButton, kControllerButtons[i].padButton});
            } else {
                RT_LOG(RT_TAG_CONFIG) << "Unknown controller." << kControllerButtons[i].configKey
                          << " button '" << binding.primary << "'" << std::endl;
            }
            uint32_t altNative = PAD_NATIVE_BUTTON_INVALID;
            if (!binding.secondary.empty()) {
                if (const NativeButtonItem* native = FindNativeButton(binding.secondary)) {
                    altNative = native->nativeButton;
                } else {
                    RT_LOG(RT_TAG_CONFIG) << "Unknown controller." << kControllerButtons[i].configKey
                              << " secondary button '" << binding.secondary << "'" << std::endl;
                }
            }
            PADSetAltButtonMapping(port, PADButtonMapping{altNative, kControllerButtons[i].padButton});
        }
    }
}

bool g_thorFocusNextPrimary = false;
float g_thorArtRowHeight = 66.0f;

void SetThorFontPixels(float pixels) {
    const float size = ImGui::GetFontSize();
    if (size > 0) ImGui::SetWindowFontScale(ImGui::GetCurrentWindow()->FontWindowScale * pixels / size);
}

void FocusThorLastItem(bool force = false) {
    if (!RuntimeConfigFile::IsThorDisplay() || (!g_thorFocusNextPrimary && !force)) return;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::FocusWindow(window);
    ImGui::SetFocusID(ImGui::GetItemID(), window);
    ImGuiContext& context = *ImGui::GetCurrentContext();
    context.NavInitRequest = false;
    context.NavInitResult.ID = 0;
    ImGui::SetNavCursorVisible(true);
    g_thorFocusNextPrimary = false;
}

struct ThorRowTextBounds {
    float minX = 0, maxX = 0;
    unsigned lines = 1;
};

ThorRowTextBounds MeasureThorRowText(const char* text, const char* end) {
    ThorRowTextBounds bounds;
    float pen = 0;
    ImFont* font = ImGui::GetFont();
    while (text < end) {
        unsigned codepoint = 0;
        const int bytes = ImTextCharFromUtf8(&codepoint, text, end);
        if (bytes <= 0) break;
        text += bytes;
        if (codepoint == '\n') { pen = 0; ++bounds.lines; continue; }
        if (codepoint == '\r') continue;
        const ImFontGlyph* glyph = codepoint <= 0xFFFF ? font->FindGlyph(static_cast<ImWchar>(codepoint))
                                                       : font->FallbackGlyph;
        if (!glyph) continue;
        bounds.minX = std::min(bounds.minX, pen + glyph->X0);
        bounds.maxX = std::max(bounds.maxX, pen + glyph->X1);
        pen += glyph->AdvanceX;
        bounds.maxX = std::max(bounds.maxX, pen);
    }
    return bounds;
}

void DrawThorRowText(const char* text, const char* end, const ThorRowTextBounds& bounds,
                     float left, float width, float centerY, float height, bool rightAligned) {
    ImFont* font = ImGui::GetFont();
    const float span = bounds.maxX - bounds.minX;
    float pixels = std::min(ImGui::GetFontSize(), height / bounds.lines);
    if (span > 0) pixels = std::min(pixels, width * font->FontSize / span);
    const float scale = pixels / font->FontSize;
    const float x = rightAligned ? left + width - bounds.maxX * scale : left - bounds.minX * scale;
    ImGui::GetWindowDrawList()->AddText(font, pixels, ImVec2(x, centerY - pixels * bounds.lines * 0.5f),
        ImGui::GetColorU32(ImGuiCol_Text), text, end);
}

bool ThorArtButton(const char* label, const char* value = nullptr, ImVec2 size = ImVec2(0, 0), bool takeFocus = false) {
    ImGui::PushID(label);
    if (size.x <= 0) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0) size.y = std::max(g_thorArtRowHeight, ImGui::GetFrameHeight());
    const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + size.x, min.y + size.y);
    const ImGuiID id = ImGui::GetID("##MkwChoice");
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const bool pressed = ImGui::Button("##MkwChoice", size);
    if ((ImGui::GetCurrentContext()->CurrentItemFlags & (ImGuiItemFlags_NoNav | ImGuiItemFlags_Disabled)) == 0)
        FocusThorLastItem(takeFocus);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    const bool focused = ImGui::GetCurrentContext()->NavId == id ||
                         ImGui::GetCurrentContext()->ActiveId == id;
    const float scale = std::clamp(ImGui::GetIO().DisplaySize.y / 1080.0f, 0.65f, 1.5f);
    const ImGuiID motionId = ImHashStr("##ThorFocusLift", 0, id);
    const float previous = ImGui::GetStateStorage()->GetFloat(motionId, 0);
    const float step = std::max(0.0f, ImGui::GetIO().DeltaTime) / 0.10f;
    const float motion = focused ? std::min(1.0f, previous + step) : std::max(0.0f, previous - step);
    ImGui::GetStateStorage()->SetFloat(motionId, motion);
    const float lift = 2.0f * scale * motion;
    // The original texture includes transparent padding outside its bevel;
    // allow for that inset so the visible frame also has a rounded corner.
    thor_settings_assets::DrawButton(focused ? "button_focus" : "button", ImGui::GetWindowDrawList(),
        ImVec2(min.x, min.y - lift), ImVec2(max.x, max.y - lift), std::min(26.0f * scale, size.y * 0.28f));
    const float padding = std::min(30.0f * scale, size.x * 0.10f);
    const float available = std::max(1.0f, size.x - padding * 2);
    const float centerY = min.y + size.y * 0.5f - lift;
    const float textHeight = std::max(1.0f, size.y - 12.0f * scale);
    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    const ThorRowTextBounds labelBounds = MeasureThorRowText(label, labelEnd);
    float labelWidth = available;
    if (value) {
        // Dynamic controller names can be longer than their assigned row.
        // Reserve disjoint label/value regions and fit original glyph ink and
        // advances inside each, without changing the button's hit/nav rectangle.
        const float gap = std::min(24.0f * scale, available * 0.10f);
        const float remaining = available - gap;
        const float naturalLabelWidth = (labelBounds.maxX - labelBounds.minX) *
                                        ImGui::GetFontSize() / ImGui::GetFont()->FontSize;
        labelWidth = std::min(naturalLabelWidth, remaining * 0.55f);
        const char* valueEnd = value + std::strlen(value);
        DrawThorRowText(value, valueEnd, MeasureThorRowText(value, valueEnd),
            min.x + padding + labelWidth + gap, remaining - labelWidth, centerY, textHeight, true);
    }
    DrawThorRowText(label, labelEnd, labelBounds, min.x + padding, labelWidth, centerY, textHeight, false);
    ImGui::PopID();
    return pressed;
}

bool SettingsCheckbox(const char* label, bool* value) {
    if (!RuntimeConfigFile::IsThorDisplay() || !thor_settings_assets::ready) return ImGui::Checkbox(label, value);
    if (!ThorArtButton(label, *value ? "On" : "Off")) return false;
    *value = !*value;
    ImGui::MarkItemEdited(ImGui::GetItemID());
    return true;
}

void DrawThorControllerAssignment();
bool ThorVolumeControl(const char* label, int* value);

bool SettingsVolumeControl(const char* label, int* value) {
    if (RuntimeConfigFile::IsThorDisplay()) return ThorVolumeControl(label, value);
    return ImGui::SliderInt(label, value, 0, 100, "%d%%");
}

void DrawControllerSettings() {
#if defined(__ANDROID__)
    if (!RuntimeConfigFile::IsThorDisplay()) {
    // Lives here (not under Graphics, where it was originally placed) since it's an input
    // setting, not a display one - confirmed directly that it wasn't found under Graphics. The
    // actual show/hide + persistence lives Kotlin-side (TouchControlsOverlay.kt) as a per-device
    // UI preference, same as its drag/resize/hide-per-button state - this checkbox is just the
    // settings-menu entry point into that, plus a live mirror so it displays the right state
    // (seeded via nativeSetTouchControlsVisibleCache, updated on every toggle here). Most useful
    // right after a real controller auto-connects and hides the touch overlay
    // (TouchControlsOverlay.setControllerConnected) but the player wants it back anyway.
    if (SettingsCheckbox("Touch controls", &g_androidTouchControlsVisibleCache)) {
        AndroidSetTouchOverlayVisible(g_androidTouchControlsVisibleCache);
    }
    // Requested feature (GitHub issue #4): holding an item behind you for protection while also
    // drifting/tricking is hard on touch controls since both need a thumb free. Double-tapping
    // the on-screen A button locks it "held" so that thumb frees up; double-tapping again
    // releases it. Off by default - opt-in, same reasoning as everything else in this section
    // being a per-device UI preference rather than a game setting. Actual detection/hold logic
    // lives in TouchButtonView.kt; this is just the settings-menu entry point plus a live mirror
    // of its current state (seeded via nativeSetDoubleTapAutoHoldCache).
    if (SettingsCheckbox("Double-tap A to auto-hold acceleration", &g_androidDoubleTapAutoHoldCache)) {
        AndroidSetDoubleTapAutoHold(g_androidDoubleTapAutoHoldCache);
    }
    ImGui::Separator();
    }
#endif
    for (int port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
        const std::string label = "Port " + std::to_string(port + 1);
        ImGui::RadioButton(label.c_str(), &g_controllerPort, port);
        if (port == 0) FocusThorLastItem();
        if (port + 1 < PAD_MAX_CONTROLLERS) {
            ImGui::SameLine();
        }
    }

    ImGui::Separator();
    controller_mapping_wizard::DrawSetupList();
    const uint32_t controllerCount = PADCount();
    if (controllerCount == 0) {
        ImGui::TextDisabled("No controller connected");
        return;
    }

    const char* currentName = PADGetName(static_cast<uint32_t>(g_controllerPort));
    if (RuntimeConfigFile::IsThorDisplay()) {
        DrawThorControllerAssignment();
    } else {
    ImGui::Text("Assigned: %s", currentName != nullptr ? currentName : "None");
    if (ImGui::BeginMenu("Assign connected controller")) {
        for (uint32_t index = 0; index < controllerCount; ++index) {
            const char* name = PADGetNameForControllerIndex(index);
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::MenuItem(name != nullptr ? name : "Unknown controller")) {
                PADSetPortForIndex(index, static_cast<uint32_t>(g_controllerPort));
                g_configuredControllerIndices.fill(std::numeric_limits<int32_t>::min());
                ApplyConfiguredMappings();
            }
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    }

    uint32_t mappingCount = 0;
    PADButtonMapping* mappings = PADGetButtonMappings(static_cast<uint32_t>(g_controllerPort), &mappingCount);
    if (mappings == nullptr || mappingCount != PAD_BUTTON_COUNT) {
        ImGui::TextDisabled("Assign a controller to edit its buttons");
        return;
    }

    uint32_t altMappingCount = 0;
    PADButtonMapping* altMappings =
        PADGetAltButtonMappings(static_cast<uint32_t>(g_controllerPort), &altMappingCount);

    const auto writeBinding = [](size_t index, uint32_t primaryNative, uint32_t altNative) {
        std::string value = NativeButtonForValue(primaryNative).configName;
        if (altNative != PAD_NATIVE_BUTTON_INVALID) {
            value += ',';
            value += NativeButtonForValue(altNative).configName;
        }
        RuntimeConfigFile::SetControllerButton(index, value);
    };

    // Which rows show the second-binding combo without one being bound yet;
    // reset when the user switches ports so a stale "+" click doesn't linger.
    static std::array<bool, PAD_BUTTON_COUNT> altRowExpanded{};
    static int altRowExpandedPort = -1;
    if (altRowExpandedPort != g_controllerPort) {
        altRowExpandedPort = g_controllerPort;
        altRowExpanded.fill(false);
    }

    ImGui::SeparatorText("Presets");
    if (ImGui::Button("GameCube")) {
        const uint32_t port = static_cast<uint32_t>(g_controllerPort);
        PADRestoreDefaultMapping(port);
        uint32_t restoredCount = 0;
        if (PADButtonMapping* restored = PADGetButtonMappings(port, &restoredCount)) {
            for (size_t i = 0; i < kControllerButtons.size(); ++i) {
                const auto it = std::find_if(restored, restored + restoredCount, [&](const PADButtonMapping& mapping) {
                    return mapping.padButton == kControllerButtons[i].padButton;
                });
                if (it != restored + restoredCount) {
                    RuntimeConfigFile::SetControllerButton(i, NativeButtonForValue(it->nativeButton).configName);
                }
            }
        }
        altRowExpanded.fill(false);
        PADSerializeMappings();
        mappings = PADGetButtonMappings(port, &mappingCount);
    }
    ImGui::SameLine();
    if (ImGui::Button("Classic Controller Pro")) {
        const uint32_t port = static_cast<uint32_t>(g_controllerPort);
        for (size_t i = 0; i < kControllerButtons.size(); ++i) {
            if (const NativeButtonItem* native = FindNativeButton(kClassicProPreset[i])) {
                PADSetButtonMapping(port, PADButtonMapping{native->nativeButton, kControllerButtons[i].padButton});
                PADSetAltButtonMapping(port,
                                       PADButtonMapping{PAD_NATIVE_BUTTON_INVALID, kControllerButtons[i].padButton});
                RuntimeConfigFile::SetControllerButton(i, kClassicProPreset[i]);
            }
        }
        altRowExpanded.fill(false);
        PADSerializeMappings();
        mappings = PADGetButtonMappings(port, &mappingCount);
    }

    ImGui::SeparatorText("Button mapping");
    float mappingWidth = 190.0f;
    if (RuntimeConfigFile::IsThorDisplay()) {
        const float scale = std::clamp(ImGui::GetIO().DisplaySize.y / 1080.0f, 0.65f, 1.5f);
        mappingWidth = 600.0f * scale;
        for (const auto& candidate : kNativeButtons) {
            mappingWidth = std::max(mappingWidth, ImGui::CalcTextSize(candidate.label).x +
                ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2);
        }
    }
    for (size_t i = 0; i < kControllerButtons.size(); ++i) {
        auto mappingIt = std::find_if(mappings, mappings + mappingCount, [&](const PADButtonMapping& mapping) {
            return mapping.padButton == kControllerButtons[i].padButton;
        });
        if (mappingIt == mappings + mappingCount) {
            continue;
        }
        PADButtonMapping* altIt = nullptr;
        if (altMappings != nullptr && altMappingCount == PAD_BUTTON_COUNT) {
            const auto it = std::find_if(altMappings, altMappings + altMappingCount, [&](const PADButtonMapping& mapping) {
                return mapping.padButton == kControllerButtons[i].padButton;
            });
            if (it != altMappings + altMappingCount) {
                altIt = it;
            }
        }

        const NativeButtonItem& current = NativeButtonForValue(mappingIt->nativeButton);
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(mappingWidth);
        if (ImGui::BeginCombo("##primary", current.label)) {
            for (const auto& candidate : kNativeButtons) {
                const bool selected = candidate.nativeButton == mappingIt->nativeButton;
                if (ImGui::Selectable(candidate.label, selected)) {
                    const uint32_t port = static_cast<uint32_t>(g_controllerPort);
                    PADSetButtonMapping(port, PADButtonMapping{candidate.nativeButton, kControllerButtons[i].padButton});
                    writeBinding(i, candidate.nativeButton,
                                 altIt != nullptr ? altIt->nativeButton : PAD_NATIVE_BUTTON_INVALID);
                    PADSerializeMappings();
                    mappings = PADGetButtonMappings(port, &mappingCount);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (altIt != nullptr) {
            const bool altBound = altIt->nativeButton != PAD_NATIVE_BUTTON_INVALID;
            if (!altBound && !altRowExpanded[i]) {
                ImGui::SameLine();
                if (ImGui::SmallButton("+")) {
                    altRowExpanded[i] = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Add a second binding; pressing either one works");
                }
            } else {
                ImGui::SameLine();
                ImGui::TextUnformatted("or");
                ImGui::SameLine();
                const char* altLabel = altBound ? NativeButtonForValue(altIt->nativeButton).label : "None";
                ImGui::SetNextItemWidth(mappingWidth);
                if (ImGui::BeginCombo("##alt", altLabel)) {
                    for (const auto& candidate : kNativeButtons) {
                        const bool isNone = candidate.nativeButton == PAD_NATIVE_BUTTON_INVALID;
                        const bool selected = candidate.nativeButton == altIt->nativeButton;
                        if (ImGui::Selectable(isNone ? "None" : candidate.label, selected)) {
                            const uint32_t port = static_cast<uint32_t>(g_controllerPort);
                            PADSetAltButtonMapping(
                                port, PADButtonMapping{candidate.nativeButton, kControllerButtons[i].padButton});
                            writeBinding(i, mappingIt->nativeButton, candidate.nativeButton);
                            if (isNone) {
                                altRowExpanded[i] = false;
                            }
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(kControllerButtons[i].label);
        ImGui::PopID();
    }

}

void DrawAudioSettings(bool basicOnly = false, bool advancedOnly = false) {
    if (!advancedOnly) {
    ImGui::SetNextItemWidth(RuntimeConfigFile::IsThorDisplay() ? ImGui::GetContentRegionAvail().x * 0.60f : 220.0f);
    const bool masterChanged = SettingsVolumeControl("Master", &g_audioVolumePercent);
    FocusThorLastItem();
    if (masterChanged) {
        const float volume = static_cast<float>(g_audioVolumePercent) / 100.0f;
        AudioBackend::Instance().SetMasterVolume(volume);
        RuntimeConfigFile::SetAudioVolume(volume);
    }
    if (SettingsVolumeControl("Music", &g_musicVolumePercent)) {
        const float volume = static_cast<float>(g_musicVolumePercent) / 100.0f;
        MusicAttenuation::SetMusicVolume(volume);
        RuntimeConfigFile::SetMusicVolume(volume);
    }
    if (SettingsVolumeControl("Sound Effects", &g_soundEffectsVolumePercent)) {
        const float volume = static_cast<float>(g_soundEffectsVolumePercent) / 100.0f;
        MusicAttenuation::SetSoundEffectsVolume(volume);
        RuntimeConfigFile::SetSoundEffectsVolume(volume);
    }
    if (SettingsVolumeControl("UI", &g_uiVolumePercent)) {
        const float volume = static_cast<float>(g_uiVolumePercent) / 100.0f;
        MusicAttenuation::SetUiVolume(volume);
        RuntimeConfigFile::SetUiVolume(volume);
    }
    if (SettingsVolumeControl("Voices", &g_voicesVolumePercent)) {
        const float volume = static_cast<float>(g_voicesVolumePercent) / 100.0f;
        MusicAttenuation::SetVoicesVolume(volume);
        RuntimeConfigFile::SetVoicesVolume(volume);
    }
    if (SettingsCheckbox("Mute", &g_audioMuted)) {
        AudioBackend::Instance().SetMuted(g_audioMuted);
        RuntimeConfigFile::SetAudioMuted(g_audioMuted);
    }
    }
    if (basicOnly) return;
    ImGui::Separator();
    if (SettingsCheckbox("Mix audio on a worker thread", &g_audioMixWorker)) {
        // Applies immediately: SetMixWorkerEnabled joins any in-flight mix
        // before switching, so the change never lands mid-frame.
        AxDspHle::SetMixWorkerEnabled(g_audioMixWorker);
        RuntimeConfigFile::SetAudioMixWorker(g_audioMixWorker);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Runs the AX/DSP voice mix off the game thread. Turn this off if you "
            "suspect an audio problem; the mix then runs inline as it used to.");
    }
    ImGui::Separator();
    // Android detects other apps' playback passively (ExternalMediaDetector.kt, never audio
    // focus); desktop uses the platform media-session monitor.
    if (SettingsCheckbox("Mute game music while external media is playing",
                        &g_attenuateMusicWhenMediaPlays)) {
        MusicAttenuation::SetEnabled(g_attenuateMusicWhenMediaPlays);
        RuntimeConfigFile::SetAttenuateMusicWhenMediaPlays(g_attenuateMusicWhenMediaPlays);
    }
    if (g_attenuateMusicWhenMediaPlays) {
        if (MusicAttenuation::IsExternalMediaPlaying()) {
            ImGui::TextDisabled("External media is playing; game music is muted.");
        } else if (!MusicAttenuation::IsMediaControlInitializationComplete()) {
            ImGui::TextDisabled(kMediaMonitorWaitingText);
        } else if (!MusicAttenuation::IsMediaControlAvailable()) {
            ImGui::TextDisabled(kMediaMonitorUnavailableText);
        } else {
            ImGui::TextDisabled("No external media is currently playing.");
        }
    }
}

#if defined(__SWITCH__)
extern "C" void AuroraSwitchSetMotionSteering(int enabled, float sensitivity, int inverted);
extern "C" void AuroraSwitchRecenterMotionSteering();
extern "C" int AuroraSwitchMotionSteeringSeen();

// Tilt to steer (aurora-main/lib/switch/input_switch.cpp). Kept in its own small file beside
// Config.toml, like the music player's settings.
struct MotionSteeringSettings {
    bool enabled = false;
    bool inverted = false;
    int sensitivityPercent = 100;
    bool loaded = false;
};
MotionSteeringSettings g_motionSteering;

void ApplyMotionSteering(bool save) {
    AuroraSwitchSetMotionSteering(g_motionSteering.enabled ? 1 : 0,
                                  static_cast<float>(g_motionSteering.sensitivityPercent) / 100.0f,
                                  g_motionSteering.inverted ? 1 : 0);
    if (!save) {
        return;
    }
    const auto path = RuntimeConfigFile::ApplicationDataDirectory() / "motion_steering.cfg";
    if (FILE* file = std::fopen(path.string().c_str(), "wb")) {
        std::fprintf(file, "%d %d %d\n", g_motionSteering.enabled ? 1 : 0, g_motionSteering.inverted ? 1 : 0,
                     g_motionSteering.sensitivityPercent);
        std::fclose(file);
    }
}

void LoadMotionSteering() {
    if (g_motionSteering.loaded) {
        return;
    }
    g_motionSteering.loaded = true;
    const auto path = RuntimeConfigFile::ApplicationDataDirectory() / "motion_steering.cfg";
    if (FILE* file = std::fopen(path.string().c_str(), "rb")) {
        int enabled = 0, inverted = 0, sensitivity = 100;
        if (std::fscanf(file, "%d %d %d", &enabled, &inverted, &sensitivity) == 3) {
            g_motionSteering.enabled = enabled != 0;
            g_motionSteering.inverted = inverted != 0;
            g_motionSteering.sensitivityPercent = std::clamp(sensitivity, 50, 200);
        }
        std::fclose(file);
    }
    ApplyMotionSteering(false);
}

void DrawMotionSteeringSettings() {
    LoadMotionSteering();
    ImGui::SeparatorText("Tilt to steer (player 1)");
    bool changed = SettingsCheckbox("Steer by tilting the console or controller", &g_motionSteering.enabled);
    if (g_motionSteering.enabled) {
        ImGui::SetNextItemWidth(220.0f);
        changed |= ImGui::SliderInt("Sensitivity##Motion", &g_motionSteering.sensitivityPercent, 50, 200, "%d%%");
        changed |= SettingsCheckbox("Reverse direction", &g_motionSteering.inverted);
        if (ImGui::Button("Set straight ahead to how I am holding it now", ImVec2(-1.0f, 48.0f))) {
            AuroraSwitchRecenterMotionSteering();
        }
        if (AuroraSwitchMotionSteeringSeen() == 0) {
            ImGui::TextDisabled("Waiting for the tilt sensor...");
        }
    }
    if (changed) {
        ApplyMotionSteering(true);
    }
}
#endif

// The player's own MP3s over the game (music_player.cpp). Big buttons and tall rows: on Switch this
// page is used with a finger as much as with the controller.
void DrawMusicPlayer() {
    static std::vector<std::string> tracks;
    static bool listed = false;
    if (!listed) {
        tracks = MusicPlayer::Rescan();
        listed = true;
    }
    const MusicPlayer::Status status = MusicPlayer::GetStatus();
    const float width = ImGui::GetContentRegionAvail().x;

    if (tracks.empty()) {
        ImGui::TextWrapped("No music yet. Put MP3 files in %s on the SD card, then press Look again.",
                           MusicPlayer::FolderForDisplay().c_str());
        if (ImGui::Button("Look again", ImVec2(width, 52.0f))) {
            tracks = MusicPlayer::Rescan();
        }
        return;
    }

    const bool haveTrack = status.index >= 0 && status.index < static_cast<int>(tracks.size());
    ImGui::TextWrapped("%s", haveTrack ? tracks[static_cast<size_t>(status.index)].c_str() : "Nothing playing");
    const auto clock = [](double seconds) {
        const int total = static_cast<int>(seconds);
        char text[16];
        std::snprintf(text, sizeof(text), "%d:%02d", total / 60, total % 60);
        return std::string(text);
    };
    const float fraction =
        status.durationSeconds > 0.0 ? static_cast<float>(status.positionSeconds / status.durationSeconds) : 0.0f;
    const std::string times = clock(status.positionSeconds) + " / " + clock(status.durationSeconds);
    ImGui::ProgressBar(fraction, ImVec2(width, 22.0f), status.loaded ? times.c_str() : "");

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float third = (width - spacing * 2.0f) / 3.0f;
    if (ImGui::Button("Previous", ImVec2(third, 60.0f))) {
        MusicPlayer::Previous();
    }
    ImGui::SameLine();
    if (ImGui::Button(status.playing ? "Pause" : "Play", ImVec2(third, 60.0f))) {
        MusicPlayer::TogglePause();
    }
    ImGui::SameLine();
    if (ImGui::Button("Next", ImVec2(third, 60.0f))) {
        MusicPlayer::Next();
    }

    int volume = static_cast<int>(std::lround(MusicPlayer::Volume() * 100.0f));
    ImGui::SetNextItemWidth(width * 0.62f);
    if (ImGui::SliderInt("Volume##MusicPlayer", &volume, 0, 100, "%d%%")) {
        MusicPlayer::SetVolume(static_cast<float>(volume) / 100.0f);
    }
    bool shuffle = MusicPlayer::Shuffle();
    if (SettingsCheckbox("Shuffle", &shuffle)) {
        MusicPlayer::SetShuffle(shuffle);
    }
    bool muteGame = MusicPlayer::MuteGameMusic();
    if (SettingsCheckbox("Turn off the game's own music while this plays", &muteGame)) {
        MusicPlayer::SetMuteGameMusic(muteGame);
    }
    if (status.loaded) {
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            MusicPlayer::Stop();
        }
    }

    ImGui::Separator();
    for (size_t i = 0; i < tracks.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool current = static_cast<int>(i) == status.index;
        if (ImGui::Selectable(tracks[i].c_str(), current, 0, ImVec2(0.0f, 44.0f))) {
            MusicPlayer::Play(static_cast<int>(i));
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Look for new files", ImVec2(width, 48.0f))) {
        tracks = MusicPlayer::Rescan();
    }
}

// Render-resolution scale (0.5x-8x the Wii's native output), same list/logic as DrawTopBar's own
// resolution dropdown. That one lives inline in DrawTopBar (desktop's menu bar) and is left alone
// there; this is a separate, sidebar-appropriate (BeginCombo, not BeginMenu) copy for
// DrawAndroidSidebar, which never actually included this setting at all when it replaced the top
// bar on Android - confirmed directly as a real regression ("in Display settings add
// resolution(the scaling settings, as those were removed fully.)").
void DrawFrameInterpolationSettings() {
    constexpr std::array<const char*, 3> modes{
        "60 FPS (original)", "120 FPS (interpolated)", "180 FPS (interpolated)",
    };
    if (ImGui::BeginCombo("Frame rate", modes[static_cast<size_t>(g_frameInterpolationMode)])) {
        // The Thor upper panel exposes 60/120 Hz modes. Keep higher targets
        // available on other displays without offering an unviewable Thor mode.
        const int count = RuntimeConfigFile::IsThorDisplay() ? 2 : static_cast<int>(modes.size());
        for (int mode = 0; mode < count; ++mode) {
            const bool selected = g_frameInterpolationMode == mode;
            if (ImGui::Selectable(modes[static_cast<size_t>(mode)], selected)) {
                g_frameInterpolationMode = mode;
                const uint32_t target = kFrameInterpolationTargetFps[static_cast<size_t>(mode)];
                aurora_set_frame_interpolation_fps(target);
                RuntimeConfigFile::SetFrameInterpolationFps(target);
                LimitResolutionForFrameRate();
                if (aurora_get_display_mode() == AURORA_DISPLAY_MODE_EXCLUSIVE) {
                    aurora_set_display_mode(AURORA_DISPLAY_MODE_EXCLUSIVE);
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Interpolated frames; game logic stays at 60 Hz. Experimental.");
    ImGui::PopTextWrapPos();
}

void DrawResolutionSettings() {
    const auto resolutionIt = std::find_if(kResolutions.begin(), kResolutions.end(), [](const ResolutionItem& item) {
        return std::fabs(item.scale - g_resolutionScale) < 0.001f;
    });
    const char* resolutionLabel = resolutionIt != kResolutions.end() ? ResolutionLabel(*resolutionIt) : "Custom";
    if (ImGui::BeginCombo("Resolution", resolutionLabel)) {
        for (const auto& resolution : kResolutions) {
            const bool selected = std::fabs(resolution.scale - g_resolutionScale) < 0.001f;
            const bool disabled = IsHighFrameRateMode() && IsHighResolutionScale(resolution.scale);
            ImGui::BeginDisabled(disabled);
            const bool clicked = ImGui::Selectable(ResolutionLabel(resolution), selected);
            ImGui::EndDisabled();
            if (clicked) {
                SetResolutionScale(resolution.scale);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (RuntimeConfigFile::IsThorDisplay()) {
        ImGui::TextWrapped("1080p uses the upper display's full pixel size. Thermal scaling can lower rendering resolution.");
        DrawFrameInterpolationSettings();
    }
    ImGui::Separator();

    // Requested directly after a real device report (Retroid Pocket Nova, a 4:3-screened Android
    // handheld): the game was always told it's on a 16:9 TV (SCGetAspectRatio_HLE, hle/sc.cpp,
    // defaults to widescreen=true) regardless of the device's actual screen shape, so its OWN
    // HUD/menu layout math (not just output scaling) used 16:9-safe-area coordinates - on a
    // genuinely 4:3 screen those elements land toward the screen's center instead of the corners.
    // A picker here lets the player match this to their real screen instead of only auto-detecting
    // it (which would need to run before this menu can even show settings, and some 4:3-ish
    // devices - e.g. anything with on-screen nav bars eating into the aspect ratio - are
    // ambiguous enough that a manual override is worth having regardless).
    constexpr std::array<const char*, 2> kAspectRatioModes{"Widescreen (16:9)", "4:3 (original Wii)"};
    int aspectRatioIndex = g_widescreen ? 0 : 1;
    if (ImGui::Combo("Aspect Ratio", &aspectRatioIndex, kAspectRatioModes.data(),
                      static_cast<int>(kAspectRatioModes.size()))) {
        g_widescreen = aspectRatioIndex == 0;
        RuntimeConfigFile::SetWidescreen(g_widescreen);
        RequestMkwAspectMode(g_widescreen);
    }
    // The picture switches immediately, but the game lays out menus and the HUD when a screen
    // loads, so whatever is already on screen keeps its old layout until the next one.
    ImGui::PushTextWrapPos(RuntimeConfigFile::IsThorDisplay() ? 0.0f : ImGui::GetCursorPosX() + 380.0f);
    ImGui::TextDisabled("The HUD and menus update on the next screen or race.");
    ImGui::PopTextWrapPos();
    ImGui::Separator();
}

template <typename Choices>
void ThorChoiceRow(const char* label, const char* preview, int count, Choices choices, bool frameRatePopup = false) {
    const float scale = std::clamp(ImGui::GetIO().DisplaySize.y / 1080.0f, 0.65f, 1.5f);
    ImGui::PushID(label);
    if (ThorArtButton(label, preview, ImVec2(0, 88.0f * scale))) ImGui::OpenPopup("##Choices");
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (frameRatePopup)
        ImGui::SetNextWindowSize(ImVec2(1040.0f * scale, 450.0f * scale), ImGuiCond_Always);
    else
        ImGui::SetNextWindowSize(ImVec2(std::min(display.x - 180.0f * scale, 1280.0f * scale),
            std::min(display.y - 140.0f * scale, (count * 89.0f + 130.0f) * scale)), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##Choices", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground)) {
        SetThorFontPixels(40.0f * scale);
        const ImVec2 min = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        if (frameRatePopup) {
            // Presentation only: separate the original white paper from the
            // surrounding settings paper without adding replacement artwork.
            draw->PushClipRect(ImVec2(0, 0), display, false);
            draw->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, 44));
            draw->AddRectFilled(ImVec2(min.x + 6.0f * scale, min.y + 10.0f * scale),
                ImVec2(min.x + size.x + 6.0f * scale, min.y + size.y + 10.0f * scale),
                IM_COL32(0, 0, 0, 48), 24.0f * scale);
            draw->PopClipRect();
        }
        thor_settings_assets::DrawImage("panel", ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y),
                                       20.0f * scale);
        if (frameRatePopup) {
            if (const ImTextureID header = thor_settings_assets::Texture("header"))
                draw->AddImageRounded(header, min, ImVec2(min.x + size.x, min.y + 96.0f * scale),
                    ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(ImVec4(1, 1, 1, 1)),
                    20.0f * scale, ImDrawFlags_RoundCornersTop);
            draw->AddText(ImGui::GetFont(), 42.0f * scale, ImVec2(min.x + 42.0f * scale, min.y + 25.0f * scale),
                ImGui::GetColorU32(ImGuiCol_Text), label);
            ImGui::SetCursorPos(ImVec2(42.0f * scale, 116.0f * scale));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20.0f * scale, 24.0f * scale));
            choices(ImGui::IsWindowAppearing(), scale);
            ImGui::PopStyleVar();
            SetThorFontPixels(28.0f * scale);
            ImGui::SetCursorPos(ImVec2(42.0f * scale, 389.0f * scale));
            ImGui::TextUnformatted("A: Select     B: Back");
            constexpr const char* logic = "Game logic: 60 Hz";
            const float width = ImGui::CalcTextSize(logic).x;
            draw->AddText(ImVec2(min.x + size.x - 42.0f * scale - width, min.y + 389.0f * scale),
                ImGui::GetColorU32(ImGuiCol_Text), logic);
        } else {
            ImGui::TextUnformatted(label);
            ImGui::Spacing();
            choices(ImGui::IsWindowAppearing(), scale);
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void DrawThorControllerAssignment() {
    const uint32_t port = static_cast<uint32_t>(g_controllerPort);
    const char* name = PADGetName(port);
    const int selectedIndex = PADGetIndexForPort(port);
    ThorChoiceRow("Assigned controller", name ? name : "None", static_cast<int>(PADCount()),
        [port, selectedIndex](bool appearing, float scale) {
            const uint32_t count = PADCount();
            for (uint32_t index = 0; index < count; ++index) {
                const char* candidate = PADGetNameForControllerIndex(index);
                ImGui::PushID(static_cast<int>(index));
                if (ThorArtButton(candidate ? candidate : "Unknown controller", nullptr, ImVec2(0, 76.0f * scale),
                                  appearing && selectedIndex == static_cast<int>(index))) {
                    PADSetPortForIndex(index, port);
                    g_configuredControllerIndices.fill(std::numeric_limits<int32_t>::min());
                    ApplyConfiguredMappings();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
        });
}

bool ThorVolumeControl(const char* label, int* value) {
    // IDs match ThorChoiceRow's label scope plus ThorArtButton's label scope.
    ImGui::PushID(label);
    ImGui::PushID(label);
    const ImGuiID rowId = ImGui::GetID("##MkwChoice");
    ImGui::PopID();
    ImGui::PopID();
    char preview[24];
    std::snprintf(preview, sizeof(preview), "< %d%% >", *value);
    bool changed = false;
    ThorChoiceRow(label, preview, 2, [value, &changed](bool appearing, float) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        changed = ImGui::SliderInt("##PreciseVolume", value, 0, 100, "%d%%");
        FocusThorLastItem(appearing);
        ImGui::TextWrapped("Press A, then Left / Right for precise volume.");
    });
    const ImGuiContext& context = *ImGui::GetCurrentContext();
    if (context.NavWindow == ImGui::GetCurrentWindow() && context.NavId == rowId) {
        int direction = 0;
        for (ImGuiKey key : {ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft, ImGuiKey_LeftArrow}) {
            ImGui::SetKeyOwner(key, rowId);
            if (ImGui::IsKeyPressed(key, ImGuiInputFlags_Repeat, rowId)) --direction;
        }
        for (ImGuiKey key : {ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight, ImGuiKey_RightArrow}) {
            ImGui::SetKeyOwner(key, rowId);
            if (ImGui::IsKeyPressed(key, ImGuiInputFlags_Repeat, rowId)) ++direction;
        }
        if (direction != 0) {
            const int next = std::clamp(*value + (direction > 0 ? 5 : -5), 0, 100);
            changed |= next != *value;
            *value = next;
        }
    }
    return changed;
}

void DrawThorDisplaySettings() {
    const auto resolution = std::find_if(kResolutions.begin(), kResolutions.end(), [](const ResolutionItem& item) {
        return std::fabs(item.scale - g_resolutionScale) < 0.001f;
    });
    ThorChoiceRow("Resolution", resolution == kResolutions.end() ? "Custom" : ResolutionLabel(*resolution),
        static_cast<int>(kResolutions.size()), [](bool appearing, float scale) {
            for (const auto& item : kResolutions) {
                const bool selected = std::fabs(item.scale - g_resolutionScale) < 0.001f;
                const bool disabled = IsHighFrameRateMode() && IsHighResolutionScale(item.scale);
                ImGui::BeginDisabled(disabled);
                if (ThorArtButton(ResolutionLabel(item), disabled ? "Unavailable" : nullptr,
                                  ImVec2(0, 76.0f * scale), appearing && selected && !disabled)) {
                    SetResolutionScale(item.scale);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
            }
        });
    constexpr std::array<const char*, 2> modes{"60 FPS (original)", "120 FPS (interpolated)"};
    ThorChoiceRow("Frame rate", modes[static_cast<size_t>(g_frameInterpolationMode)], 2,
        [](bool appearing, float scale) {
            constexpr std::array<const char*, 2> labels{"60 FPS (original)", "120 FPS (interpolated)"};
            for (int mode = 0; mode < 2; ++mode) {
                if (ThorArtButton(labels[static_cast<size_t>(mode)], nullptr, ImVec2(0, 96.0f * scale),
                                  appearing && g_frameInterpolationMode == mode)) {
                    g_frameInterpolationMode = mode;
                    const uint32_t target = kFrameInterpolationTargetFps[static_cast<size_t>(mode)];
                    aurora_set_frame_interpolation_fps(target);
                    RuntimeConfigFile::SetFrameInterpolationFps(target);
                    LimitResolutionForFrameRate();
                    if (aurora_get_display_mode() == AURORA_DISPLAY_MODE_EXCLUSIVE)
                        aurora_set_display_mode(AURORA_DISPLAY_MODE_EXCLUSIVE);
                    ImGui::CloseCurrentPopup();
                }
            }
        }, true);
    ThorChoiceRow("Aspect ratio", g_widescreen ? "16:9" : "4:3", 2, [](bool appearing, float scale) {
        for (int mode = 0; mode < 2; ++mode) {
            const bool widescreen = mode == 0;
            if (ThorArtButton(widescreen ? "Widescreen (16:9)" : "Original Wii (4:3)", nullptr,
                              ImVec2(0, 76.0f * scale), appearing && g_widescreen == widescreen)) {
                g_widescreen = widescreen;
                RuntimeConfigFile::SetWidescreen(g_widescreen);
                RequestMkwAspectMode(g_widescreen);
                ImGui::CloseCurrentPopup();
            }
        }
    });
    ImGui::TextWrapped("Interpolated frames; game logic stays at 60 Hz. Experimental.");
    ImGui::TextWrapped("The HUD and menus update on the next screen or race.");
    ImGui::Spacing();
}

void DrawGraphicsSettings() {
    g_displayMode = static_cast<int>(aurora_get_display_mode());
    struct EffectFlag {
        const char* label;
        uint32_t flag;
    };
    static constexpr std::array<EffectFlag, 1> kEffectFlags = {{
        {"Disable bloom", 0x10u},
    }};

    for (const auto& effect : kEffectFlags) {
        bool disabled = (g_disabledPostProcessingPaths & effect.flag) != 0;
        if (SettingsCheckbox(effect.label, &disabled)) {
            if (disabled) {
                g_disabledPostProcessingPaths |= effect.flag;
            } else {
                g_disabledPostProcessingPaths &= ~effect.flag;
            }
            RuntimeGameGraphicsOptions::SetDisabledPostProcessingPaths(g_disabledPostProcessingPaths);
            RuntimeConfigFile::SetDisabledPostProcessingPaths(g_disabledPostProcessingPaths);
        }
    }
    ImGui::TextDisabled("Applied when the next scene renderer is created.");
    ImGui::Separator();
    static constexpr const char* kDisplayModes[] = {
        "Windowed",
        "Borderless fullscreen",
        "Exclusive fullscreen",
    };
    if (ImGui::Combo("Display mode", &g_displayMode, kDisplayModes, static_cast<int>(std::size(kDisplayModes)))) {
        const auto mode = static_cast<AuroraDisplayMode>(g_displayMode);
        aurora_set_display_mode(mode);
        const AuroraDisplayMode activeMode = aurora_get_display_mode();
        if (activeMode == mode) {
            RuntimeConfigFile::SetDisplayMode(std::string(kDisplayModeConfigNames[static_cast<size_t>(g_displayMode)]));
        } else {
            g_displayMode = static_cast<int>(activeMode);
        }
    }
    if (g_displayMode == AURORA_DISPLAY_MODE_EXCLUSIVE) {
        ImGui::TextDisabled(
            "Requests the closest native-resolution display mode to the output frame "
            "rate (60 Hz, or the frame interpolation target).");
    }
    if (!RuntimeConfigFile::IsThorDisplay()) DrawFrameInterpolationSettings();
    if (SettingsCheckbox("Disable copy filter", &g_disableCopyFilter)) {
        aurora_set_disable_copy_filter(g_disableCopyFilter);
        RuntimeConfigFile::SetDisableCopyFilter(g_disableCopyFilter);
    }
    // KartPad issue #193: on some Adreno phones (Galaxy S24 Ultra) characters render as only their
    // eyes while karts are fine. Off by default - it is an unconfirmed workaround.
#if defined(__ANDROID__)
    // A phone that gets hot drops its clocks whatever the game does, so give up pixels before frames.
    if (SettingsCheckbox("Lower resolution when the phone gets hot", &g_thermalAutoQuality)) {
        ThermalQuality::SetEnabled(g_thermalAutoQuality);
        RuntimeConfigFile::SetThermalAutoQuality(g_thermalAutoQuality);
    }
    if (g_thermalAutoQuality) {
        const float headroom = ThermalQuality::LastHeadroom();
        const float factor = ThermalQuality::CurrentFactor();
        if (headroom < 0.0f) {
            ImGui::TextDisabled("This device does not report thermal readings.");
        } else if (factor >= 1.0f) {
            ImGui::TextDisabled("Running at full resolution (heat %.0f%% of the limit).",
                                static_cast<double>(headroom) * 100.0);
        } else {
            ImGui::TextDisabled("Rendering at %.0f%% resolution (heat %.0f%% of the limit).",
                                static_cast<double>(factor) * 100.0,
                                static_cast<double>(headroom) * 100.0);
        }
    }
#endif
    // Decodes graphics commands on their own thread, freeing roughly a fifth of the game thread in
    // profiles. Experimental: off by default until it has been played on more devices.
    if (SettingsCheckbox("Multithreaded graphics (experimental)", &g_threadedGx)) {
        AuroraSetThreadedGx(g_threadedGx);
        RuntimeConfigFile::SetThreadedGx(g_threadedGx);
    }
#if !defined(__SWITCH__)
    if (SettingsCheckbox("Fix missing characters (some Adreno GPUs)", &g_constantMatrixIndexing)) {
        AuroraSetConstantMatrixIndexing(g_constantMatrixIndexing);
        RuntimeConfigFile::SetConstantMatrixIndexing(g_constantMatrixIndexing);
    }
    {
        static constexpr std::array<const char*, 4> labels{
            "Automatic", "Off", "Characters only", "Everything (slower)"};
        if (ImGui::Combo("Fix stretched graphics (Snapdragon)", &g_vertexRepackChoice, labels.data(),
                         static_cast<int>(labels.size()))) {
            RuntimeConfigFile::SetVertexRepack(kVertexRepackConfigValues[static_cast<size_t>(g_vertexRepackChoice)]);
        }
        ImGui::TextDisabled("Takes effect the next time the game starts.");
    }
#endif
    if (SettingsCheckbox("Simpler computer karts (faster races)", &g_lowDetailComputerKarts)) {
        RuntimeSetLowDetailComputerKarts(g_lowDetailComputerKarts);
        RuntimeConfigFile::SetLowDetailComputerKarts(g_lowDetailComputerKarts);
    }
    ImGui::TextDisabled("Applies from the next race.");
    if (SettingsCheckbox("30 FPS races (normal speed, half the frames)", &g_thirtyFpsRaces)) {
        RuntimeSetThirtyFpsRaces(g_thirtyFpsRaces);
        RuntimeConfigFile::SetThirtyFpsRaces(g_thirtyFpsRaces);
    }
    ImGui::TextDisabled("For when 60 can't be held. Applies from the next race.");
    if (SettingsCheckbox("Skip draws while shaders compile", &g_skipUnreadyPipelines)) {
        aurora_set_skip_unready_pipelines(g_skipUnreadyPipelines);
        RuntimeConfigFile::SetSkipUnreadyPipelines(g_skipUnreadyPipelines);
    }
    if (SettingsCheckbox("Show FPS", &g_showFps)) {
        RuntimeConfigFile::SetShowFps(g_showFps);
    }
    // Requested in GitHub issue #5, alongside Show FPS.
    if (SettingsCheckbox("Show \"shaders compiling\" message", &g_showShaderCompilation)) {
        RuntimeConfigFile::SetShowShaderCompilation(g_showShaderCompilation);
    }
#if defined(__ANDROID__)
    if (SettingsCheckbox("Hide the settings button", &g_hideSettingsButton)) {
        RuntimeConfigFile::SetHideSettingsButton(g_hideSettingsButton);
        AndroidSetSettingsButtonHidden(g_hideSettingsButton);
    }
    ImGui::PushTextWrapPos(RuntimeConfigFile::IsThorDisplay() ? 0.0f : ImGui::GetCursorPosX() + 380.0f);
    ImGui::TextDisabled("The phone's Back button always opens these settings.");
    ImGui::PopTextWrapPos();
#endif
    ImGui::Separator();
    ImGui::Text("Graphics API: %s", GraphicsApiDisplayName());
    // Names the driver in use: a custom driver from the launcher's picker reports itself here
    // (Turnip prefixes the chip name), the phone's own driver reports the bare chip.
    if (const char* adapter = aurora_get_adapter_name(); adapter != nullptr && adapter[0] != '\0') {
        ImGui::Text("GPU: %s", adapter);
    }
}

void DrawFpsOverlay() {
    AuroraPresentTiming presentTiming{};
    aurora_get_present_timing(&presentTiming);
    if (!g_showFps) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    constexpr float kMargin = 10.0f;
    const float top = g_topBarVisible ? ImGui::GetFrameHeight() + kMargin : kMargin;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - kMargin, top), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.55f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                         ImGuiWindowFlags_NoDecoration |
                                         ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoInputs |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoNav |
                                         ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("FPS Overlay", nullptr, kFlags)) {
        if (presentTiming.sampleCount == 0) {
            ImGui::TextUnformatted("FPS: --");
        } else {
            // Present timing includes the additional frames produced by
            // interpolation, so this remains the actual displayed FPS.
            ImGui::Text("FPS: %.1f", presentTiming.framesPerSecond);
            // Replay-unsafe frames hold the presented cadence with duplicated
            // slots, so the counter alone reads 180 while the motion on screen
            // is 60 Hz. Surface the divergence instead of hiding it.
            if (presentTiming.effectiveFramesPerSecond <
                presentTiming.framesPerSecond * 0.95) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Motion: %.1f",
                                   presentTiming.effectiveFramesPerSecond);
            }
        }
    }
    ImGui::End();
}

void DrawShaderCompilationStatus() {
    if (!g_showShaderCompilation) {
        return;
    }
    const uint32_t queuedPipelines = aurora_get_queued_pipeline_count();
    if (queuedPipelines == 0) {
        return;
    }

    constexpr float kMargin = 10.0f;
    // Left margin only for this overlay is much bigger than kMargin (used everywhere else for
    // top/right-edge spacing) - tucked right into the corner it was easy to miss entirely
    // (reported directly: "not very visible right now"), so it's pulled well clear of the edge.
    constexpr float kLeftMargin = 140.0f;
    const float top = g_topBarVisible ? ImGui::GetFrameHeight() + kMargin : kMargin;
    ImGui::SetNextWindowPos(ImVec2(kLeftMargin, top), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7.0f, 4.0f));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoDecoration |
                                        ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Shader Compilation Status", nullptr, kFlags)) {
        ImGui::SetWindowFontScale(0.85f);
        ImGui::Text("%u shader%s compiling", queuedPipelines, queuedPipelines == 1 ? "" : "s");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

std::string g_backendFallbackNotice;
Clock::time_point g_backendFallbackNoticeUntil{};
bool g_backendFallbackNoticeStarted = false;

void DrawBackendFallbackNotice() {
    if (g_backendFallbackNotice.empty() || StartupScreenVisible()) {
        return;
    }
    // The clock starts with the first frame the player can actually see.
    if (!g_backendFallbackNoticeStarted) {
        g_backendFallbackNoticeStarted = true;
        g_backendFallbackNoticeUntil = Clock::now() + std::chrono::seconds(12);
    }
    if (Clock::now() >= g_backendFallbackNoticeUntil) {
        g_backendFallbackNotice.clear();
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + 24.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.8f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoDecoration |
                                        ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Backend Fallback Notice", nullptr, kFlags)) {
        ImGui::TextUnformatted(g_backendFallbackNotice.c_str());
    }
    ImGui::End();
}

void DrawStartupScreen() {
    if (!StartupScreenVisible()) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (RuntimeConfigFile::IsThorDisplay()) {
        // Presentation only: retain the existing proven strap/scene dismissal boundary. Never
        // cover later guest fades or infer readiness from a timeout or the first Java surface.
        ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(24, 29, 37, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("Wiicompiled Startup", nullptr, flags)) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 top = viewport->Pos;
            const float width = viewport->Size.x, height = viewport->Size.y;
            const float scale = std::min(width / 1920.0f, height / 1080.0f);
            const bool originalArt = thor_settings_assets::ready && thor_settings_assets::font;
            if (originalArt) {
                thor_settings_assets::DrawImage("background", draw, top, ImVec2(top.x + width, top.y + height));
                thor_settings_assets::DrawImage("header", draw,
                    ImVec2(top.x + width * .10f, top.y + height * .16f),
                    ImVec2(top.x + width * .90f, top.y + height * .32f));
                thor_settings_assets::DrawImage("panel", draw,
                    ImVec2(top.x + width * .16f, top.y + height * .40f),
                    ImVec2(top.x + width * .84f, top.y + height * .69f), 20.0f * scale);
            }
            ImFont* font = originalArt ? thor_settings_assets::font : ImGui::GetFont();
            const auto centered = [&](const char* text, float pixels, float y) {
                const float span = font->CalcTextSizeA(pixels, std::numeric_limits<float>::max(), 0, text).x;
                draw->AddText(font, pixels, ImVec2(top.x + (width - span) * .5f, top.y + y),
                              IM_COL32_WHITE, text);
            };
            centered(originalArt ? (RuntimeProduct::IsRetroRewind() ? "Retro Rewind" : "Mario Kart Wii")
                                 : "DriftDroid", 64.0f * scale, height * .20f);
            centered("Starting...", 46.0f * scale, height * .51f);
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        return;
    }
    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration |
                                        ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("Wiicompiled Startup", nullptr, kFlags)) {
        ImGui::SetWindowFontScale(1.25f);
        // The shipped app branding is "DriftDroid" on both device ports (the Switch NRO carries
        // the DriftDroid name, icon and NACP too); desktop stays "WiiCompiled", the underlying
        // static-recompilation project's own name, unrelated to the port's rebrand.
#if defined(__ANDROID__) || defined(__SWITCH__)
        constexpr const char* kTitle = "DriftDroid";
#else
        constexpr const char* kTitle = "WiiCompiled";
#endif
        const ImVec2 titleSize = ImGui::CalcTextSize(kTitle);
        const float titleX = std::max(0.0f, (viewport->Size.x - titleSize.x) * 0.5f);
        const float startY = std::max(0.0f, (viewport->Size.y - titleSize.y) * 0.5f);
        ImGui::SetCursorPos(ImVec2(titleX, startY));
        ImGui::TextUnformatted(kTitle);
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

#if defined(__ANDROID__)
// Any controller landing on Android - the touch overlay's own virtual gamepad
// (runtime/src/android_touch_controls.cpp) or a real Bluetooth pad - presents itself as a
// brand-new SDL gamepad with no saved mapping, and would otherwise land on whatever raw/default
// bindings SDL happens to pick (confirmed on-device: B triggered drift, L and R did nothing
// useful for the touch device; a real Switch Pro Controller needed a manual visit to the settings
// menu's "Classic Controller Pro" button before it worked at all - confirmed directly: "if you
// have a controller right now you have to manually go to the settings menu"). Auto-apply the
// preset to EVERY newly-seen controller index, not just the touch device by name, the instant it
// shows up, so nothing on Android ever needs manual setup.
//
// Tracked once per controller INDEX (not a single global bool) so this can fire independently for
// the touch device and any number of real controllers connecting over a session, without
// repeatedly re-stomping a controller the player has since deliberately customized differently.
// Indices are small and session-scoped in practice (SDL assigns them in connection order), so a
// bitset sized generously above PAD_MAX_CONTROLLERS is a safe, simple tracking scheme - the same
// per-process "configure once" tradeoff the single-bool version already accepted for the touch
// device specifically.
std::bitset<64> g_androidControllersAutoConfigured{};

bool HasConfiguredControllerBindings() {
    for (size_t i = 0; i < kControllerButtons.size(); ++i) {
        if (!RuntimeConfigFile::ControllerButton(i)) {
            return false;
        }
    }
    return true;
}

bool UsesThorLabelButtons(uint32_t index) {
    const char* enabled = std::getenv("MKW_THOR_LABEL_BUTTONS");
    if (enabled == nullptr || std::string_view(enabled) != "1") {
        return false;
    }
    SDL_Gamepad* gamepad = PADGetSDLGamepadForIndex(index);
    if (gamepad == nullptr) {
        return false;
    }
    const char* name = SDL_GetGamepadName(gamepad);
    return name != nullptr && std::string_view(name) == "Odin Controller" &&
           SDL_GetGamepadVendor(gamepad) == 0x2020 && SDL_GetGamepadProduct(gamepad) == 0x0111;
}

void AutoConfigureNewAndroidControllersIfPresent() {
    const uint32_t controllerCount = PADCount();
    RT_LOG(RT_TAG_CONFIG) << "AutoConfigureNewAndroidControllersIfPresent: scanning " << controllerCount
                           << " controller(s)";
    for (uint32_t index = 0; index < controllerCount; ++index) {
        if (index < g_androidControllersAutoConfigured.size() && g_androidControllersAutoConfigured[index]) {
            continue;
        }
        const char* name = PADGetNameForControllerIndex(index);
        RT_LOG(RT_TAG_CONFIG) << "  [" << index << "] " << (name != nullptr ? name : "(null)");

        // First port that is not already a *different* real controller - confirmed on-device this
        // matters: hard-coding port 0 silently evicted an already-connected, already
        // correctly-mapped controller the instant a second device showed up. An empty port, or a
        // port already holding this exact device (e.g. a re-run after some prior state reset), are
        // both fair game; anything else is a different pad and must be left alone.
        uint32_t targetPort = 0;
        bool foundPort = false;
        for (uint32_t port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
            const char* portName = PADGetName(port);
            if (portName == nullptr || (name != nullptr && std::string_view(portName) == name)) {
                targetPort = port;
                foundPort = true;
                break;
            }
        }
        if (!foundPort) {
            RT_LOG(RT_TAG_CONFIG) << "AutoConfigureNewAndroidControllersIfPresent: all ports already "
                                      "hold a different controller, leaving index "
                                   << index << " unbound";
            continue;
        }
        PADSetPortForIndex(index, targetPort);
        // A controller with a saved mapping has been set up before - by this function on an
        // earlier launch, or by the player remapping it. Likewise, once Config.toml holds
        // bindings (the preset from a first run, or the player's own remap),
        // ApplyConfiguredMappings applies those to every newly connected controller, so writing
        // the preset again would only overwrite the player's choices. g_androidControllersAutoConfigured only
        // lasts for this process, so without this check every launch re-applied the preset and
        // overwrote the player's own bindings (reported on an Odin 2, whose built-in controls
        // are a real SDL gamepad: "controls aren't saved when saving settings").
        if (PADHasSavedMappingForIndex(index) || HasConfiguredControllerBindings()) {
            if (index < g_androidControllersAutoConfigured.size()) {
                g_androidControllersAutoConfigured.set(index);
            }
            RT_LOG(RT_TAG_CONFIG) << "AutoConfigureNewAndroidControllersIfPresent: index " << index
                                   << " has a saved mapping, keeping it";
            continue;
        }
        auto preset = kClassicProPreset;
        const bool thorLabelButtons = UsesThorLabelButtons(index);
        if (thorLabelButtons) {
            // In Thor's OEM Odin layout, Android preserves the printed A/B/X/Y labels;
            // SDL names those keycodes south/east/west/north. The activity enables this
            // only for that OEM mode. Keep the remaining Classic Pro bindings.
            preset[0] = "south";
            preset[1] = "east";
            preset[2] = "west";
            preset[3] = "north";
        }
        for (size_t i = 0; i < kControllerButtons.size(); ++i) {
            if (const NativeButtonItem* native = FindNativeButton(preset[i])) {
                PADSetButtonMapping(targetPort,
                                     PADButtonMapping{native->nativeButton, kControllerButtons[i].padButton});
                PADSetAltButtonMapping(
                    targetPort, PADButtonMapping{PAD_NATIVE_BUTTON_INVALID, kControllerButtons[i].padButton});
                // The Thor exception belongs to this device, not every subsequently
                // connected controller receiving the global Config.toml bindings.
                if (!thorLabelButtons) {
                    RuntimeConfigFile::SetControllerButton(i, preset[i]);
                }
            }
        }
        PADSerializeMappings();
        if (index < g_androidControllersAutoConfigured.size()) {
            g_androidControllersAutoConfigured.set(index);
        }
        RT_LOG(RT_TAG_CONFIG) << "AutoConfigureNewAndroidControllersIfPresent: configured index " << index
                               << " on port " << targetPort
                               << (thorLabelButtons ? " with Thor button labels" : " with Classic Pro preset");
    }
}
#endif  // __ANDROID__

#if defined(__ANDROID__) || defined(__SWITCH__)
// Touch-friendly replacement for DrawTopBar() below - a top menu bar with hover-opened dropdowns
// is a desktop/mouse pattern that doesn't translate well to touch (small hit targets, no hover).
// Reuses DrawGraphicsSettings()/DrawControllerSettings()/DrawAudioSettings() as-is for content -
// only the outer navigation changes, to avoid re-deriving already-working settings logic.
// Requested directly: "the settings that already appear when you press the settings button but in
// the sidebar, cleaner."
// One full-width, left-aligned row in the root list - label plus an optional trailing glyph
// (">" for a row that drills into a sub-page, nothing for a plain action). Matches KartPad's own
// Android sidebar layout (icon+label+chevron rows in a flat scrollable list), which is what was
// actually requested after seeing a screenshot of it, rather than the tabbed layout this replaces.
bool DrawSidebarRow(const char* label, const char* trailing = ">") {
    const float rowHeight = 56.0f;
    const ImVec2 rowSize(ImGui::GetContentRegionAvail().x, rowHeight);
    const bool clicked = ImGui::Button((std::string(label) + "##Row").c_str(), rowSize);
    if (trailing && trailing[0] != '\0') {
        const float trailingWidth = ImGui::CalcTextSize(trailing).x;
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rowMax.x - trailingWidth - 16.0f, (rowMin.y + rowMax.y) * 0.5f - ImGui::GetTextLineHeight() * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), trailing);
    }
    return clicked;
}

static bool g_sidebarWasVisible = false;

// Lets a finger drag the current (child) window's content up and down from anywhere on it, the
// way every phone list scrolls. ImGui on its own only scrolls from the thin scrollbar, since a
// touch arrives as a left mouse button. A drag that is mostly vertical takes over: whatever
// widget the finger landed on is released without activating, and the content follows the
// finger, then coasts to a stop after it lifts. Call just before EndChild().
void ScrollCurrentWindowByTouchDrag() {
    static bool dragging = false;
    static bool pressBeganHere = false;
    static float velocity = 0.0f;
    ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        pressBeganHere = hovered;
        velocity = 0.0f;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        dragging = false;
        pressBeganHere = false;
        // Coast after the finger lifts.
        if (std::fabs(velocity) > 0.5f) {
            ImGui::SetScrollY(ImGui::GetScrollY() - velocity);
            velocity *= 0.92f;
        } else {
            velocity = 0.0f;
        }
        return;
    }
    if (!pressBeganHere) {
        return;
    }
    if (!dragging) {
        const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
        const float threshold = 12.0f * std::max(1.0f, io.FontGlobalScale);
        if (std::fabs(delta.y) > threshold && std::fabs(delta.y) > std::fabs(delta.x) * 1.5f) {
            dragging = true;
            ImGui::ClearActiveID();
        }
    }
    if (dragging) {
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
        velocity = velocity * 0.5f + io.MouseDelta.y * 0.5f;
        // Keep widgets from reacting to the finger while it is scrolling.
        ImGui::ClearActiveID();
    }
}

void ApplyThorSettingsPalette(ImGuiStyle& style) {
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 1);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.99f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.13f, 0.13f, 1);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.32f, 0.27f, 0.13f, 1);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.4f, 0.31f, 0.1f, 1);
    style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.18f, 1);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.48f, 0.33f, 0.08f, 1);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.65f, 0.4f, 0.06f, 1);
    style.Colors[ImGuiCol_Header] = ImVec4(0.32f, 0.27f, 0.13f, 1);
    style.Colors[ImGuiCol_HeaderHovered] = style.Colors[ImGuiCol_ButtonHovered];
    style.Colors[ImGuiCol_HeaderActive] = style.Colors[ImGuiCol_ButtonActive];
    style.Colors[ImGuiCol_CheckMark] = ImVec4(1, 0.77f, 0.12f, 1);
    style.Colors[ImGuiCol_SliderGrab] = style.Colors[ImGuiCol_CheckMark];
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1, 0.9f, 0.25f, 1);
    style.Colors[ImGuiCol_NavCursor] = ImVec4(1, 0.82f, 0.2f, 1);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0.35f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.65f, 0.65f, 0.65f, 1);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = style.Colors[ImGuiCol_CheckMark];
    style.Colors[ImGuiCol_ScrollbarGrabActive] = style.Colors[ImGuiCol_CheckMark];
}

void DrawThorSettings() {
    enum class Page { Root, Display, Controller, Audio, AdvancedDisplay, ControllerBindings, AdvancedAudio };
    static Page page = Page::Root;
    static bool visibleLastFrame = false, focusNextFrame = false, popupLastFrame = false, activeLastFrame = false;
    static bool restoreFocusUntilCancelReleased = false;
    static int restoreFocusThroughFrame = -1;
    static Clock::time_point openedAt{};
    const auto parentPage = [](Page current) {
        if (current == Page::AdvancedDisplay) return Page::Display;
        if (current == Page::ControllerBindings) return Page::Controller;
        if (current == Page::AdvancedAudio) return Page::Audio;
        return Page::Root;
    };
    if (!g_topBarVisible) {
        visibleLastFrame = false;
        popupLastFrame = false;
        activeLastFrame = false;
        restoreFocusUntilCancelReleased = false;
        restoreFocusThroughFrame = -1;
        return;
    }
    const bool opening = !visibleLastFrame;
    if (opening) {
        page = Page::Root;
        openedAt = Clock::now();
        focusNextFrame = true;
        restoreFocusUntilCancelReleased = false;
        restoreFocusThroughFrame = -1;
        ImGui::SetNextWindowFocus();
    }
    visibleLastFrame = true;
    const bool popupNow = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    // ImGui handles B inside popups/active sliders. Never let that same press
    // also leave a settings page. A later B returns page -> root -> game.
    if (!opening && !popupLastFrame && !popupNow && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
        if (activeLastFrame || ImGui::IsAnyItemActive()) {
            ImGui::ClearActiveID();
        } else if (page != Page::Root) {
            page = parentPage(page);
            focusNextFrame = true;
            restoreFocusUntilCancelReleased = true;
        } else {
            SetTopBarVisible(false);
        }
    }
    if (!g_topBarVisible) return;

    // ImGui's cancel navigation can leave focus on the child container (or
    // clear it) while B returns to a different page in that same child.
    // Keep the destination's first item focused through release and one
    // subsequent ImGui frame, after cancel/restore processing has settled.
    // Popup cancellation never starts this page-entry restoration.
    const int frame = ImGui::GetFrameCount();
    if (restoreFocusUntilCancelReleased) {
        focusNextFrame = true;
        if (!ImGui::IsKeyDown(ImGuiKey_GamepadFaceRight)) {
            restoreFocusUntilCancelReleased = false;
            restoreFocusThroughFrame = frame + 1;
        }
    }
    if (frame <= restoreFocusThroughFrame) focusNextFrame = true;

    ImGuiIO& io = ImGui::GetIO();
    const float scale = std::clamp(io.DisplaySize.y / 1080.0f, 0.65f, 1.5f);
    const ImVec2 margin(70.0f * scale, 45.0f * scale);
    const ImVec2 size(io.DisplaySize.x - margin.x * 2, io.DisplaySize.y - margin.y * 2);
    ImGuiStyle previousStyle = ImGui::GetStyle();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(42.0f * scale, 28.0f * scale);
    style.FramePadding = ImVec2(18.0f * scale, 12.0f * scale);
    style.ItemSpacing = ImVec2(20.0f * scale, 13.0f * scale);
    style.ItemInnerSpacing = ImVec2(20.0f * scale, 10.0f * scale);
    style.ScrollbarSize = 22.0f * scale;
    style.WindowRounding = style.ChildRounding = 20.0f * scale;
    style.PopupRounding = 18.0f * scale;
    style.FrameRounding = style.GrabRounding = 10.0f * scale;
    style.ScrollbarRounding = 12.0f * scale;
    style.WindowBorderSize = 0;
    style.FrameBorderSize = 1;
    ApplyThorSettingsPalette(style);
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));
    const float enter = std::clamp(std::chrono::duration<float>(Clock::now() - openedAt).count() / 0.14f, 0.0f, 1.0f);
    const float slide = 18.0f * scale * std::pow(1.0f - enter, 3.0f);
    ImGui::SetNextWindowPos(ImVec2(margin.x, margin.y + slide), ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("Settings##Thor", nullptr, flags)) {
        ImGui::SetWindowFontScale(scale);
        ImGui::SetNavCursorVisible(true);
        const ImVec2 top = ImGui::GetWindowPos();
        const ImVec2 end(top.x + size.x, top.y + size.y);
        thor_settings_assets::DrawImage("panel", ImGui::GetWindowDrawList(), top, end, 20.0f * scale);
        thor_settings_assets::DrawImage("header", ImGui::GetWindowDrawList(), top,
                                       ImVec2(end.x, top.y + 110.0f * scale));
        const char* title = page == Page::Root ? "Settings" : page == Page::Display ? "Display" :
            page == Page::Controller ? "Controller" : page == Page::Audio ? "Audio" :
            page == Page::AdvancedDisplay ? "Advanced display" :
            page == Page::ControllerBindings ? "Button mappings" : "Advanced audio";
        const float titleSize = 52.0f * scale;
        const ImVec2 titlePosition = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), titleSize, titlePosition,
                                          ImGui::GetColorU32(ImGuiCol_Text), title);
        ImGui::Dummy(ImVec2(900.0f * scale, titleSize));
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 280.0f * scale);
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        if (ThorArtButton(page == Page::Root ? "Close" : "Back", nullptr, ImVec2(265.0f * scale, 62.0f * scale))) {
            if (page == Page::Root) SetTopBarVisible(false);
            else { page = parentPage(page); focusNextFrame = true; }
        }
        ImGui::PopItemFlag();
        ImGui::SetCursorPosY(125.0f * scale);
        const float footerHeight = 75.0f * scale;
        if (focusNextFrame) ImGui::SetNextWindowScroll(ImVec2(0, 0));
        ImGui::BeginChild("ThorSettingsContent", ImVec2(0, -footerHeight), false);
        ImDrawList* const contentDrawList = ImGui::GetWindowDrawList();
        const bool primary = page == Page::Root || page == Page::Display || page == Page::Controller || page == Page::Audio;
        SetThorFontPixels((page == Page::Root ? 42.0f : primary ? 40.0f : 33.15f) * scale);
        g_thorArtRowHeight = (primary ? 88.0f : 66.0f) * scale;
        ImGui::PushItemWidth(std::max(280.0f * scale, ImGui::GetContentRegionAvail().x * 0.60f));
        if (!thor_settings_assets::ready) {
            // Asset staging is a packaging prerequisite; never present substitute
            // artwork as an authentic theme when the private files are absent.
            ImGui::TextWrapped("Original Wii settings assets are missing. Restore ThorMap/thor_settings and restart.");
        } else {
            if (focusNextFrame) {
                g_thorFocusNextPrimary = true;
                focusNextFrame = false;
            }
            if (page == Page::Root) {
                const float width = std::min(ImGui::GetContentRegionAvail().x, 1300.0f * scale);
                const float x = ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - width) * 0.5f;
#if defined(__ANDROID__)
                constexpr float rootRows = 5.0f;
#else
                constexpr float rootRows = 4.0f;
#endif
                const float blockHeight = (rootRows * 100.0f + rootRows * 24.0f + 34.0f) * scale;
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0f, (ImGui::GetContentRegionAvail().y - blockHeight) * 0.5f));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20.0f * scale, 24.0f * scale));
                ImGui::SetCursorPosX(x);
                if (ThorArtButton("Display", ">", ImVec2(width, 100.0f * scale))) { page = Page::Display; focusNextFrame = true; }
                ImGui::SetCursorPosX(x);
                if (ThorArtButton("Controller", ">", ImVec2(width, 100.0f * scale))) { page = Page::Controller; focusNextFrame = true; }
                ImGui::SetCursorPosX(x);
                if (ThorArtButton("Audio", ">", ImVec2(width, 100.0f * scale))) { page = Page::Audio; focusNextFrame = true; }
#if defined(__ANDROID__)
                ImGui::SetCursorPosX(x);
                if (ThorArtButton("Retro Rewind updates", ">", ImVec2(width, 100.0f * scale))) {
                    // Keep settings/input capture active until Android pauses the guest. Closing
                    // it here could deliver this activation's held A to the game underneath.
                    AndroidOpenRetroRewindUpdates();
                }
#endif
                static Clock::time_point savedUntil{};
                ImGui::SetCursorPosX(x);
#if defined(__ANDROID__)
                // Save and Main Menu share the last row so the page still fits without scrolling.
                const float half = (width - 20.0f * scale) * 0.5f;
                if (ThorArtButton("Save Settings", nullptr, ImVec2(half, 100.0f * scale)))
                    savedUntil = Clock::now() + std::chrono::seconds(2);
                ImGui::SameLine();
                if (ThorArtButton("Main Menu", nullptr, ImVec2(half, 100.0f * scale))) {
                    // Like Retro Rewind updates: stay open until Android pauses the guest so the
                    // held A is not delivered to the race. The runtime is retained and paused.
                    AndroidReturnToMainMenu();
                }
#else
                if (ThorArtButton("Save Settings", nullptr, ImVec2(width, 100.0f * scale)))
                    savedUntil = Clock::now() + std::chrono::seconds(2);
#endif
                ImGui::PopStyleVar();
                SetThorFontPixels(33.15f * scale);
                const char* note = Clock::now() < savedUntil ? (RuntimeConfigFile::LastSettingWriteSucceeded()
                    ? "Settings saved." : "Could not save settings. See runtime log.")
                    : "Settings also save automatically when changed.";
                ImGui::SetCursorPosX(x + std::max(0.0f, (width - ImGui::CalcTextSize(note).x) * 0.5f));
                ImGui::TextUnformatted(note);
            } else if (page == Page::Display) {
                DrawThorDisplaySettings();
                if (SettingsCheckbox("Show FPS", &g_showFps)) RuntimeConfigFile::SetShowFps(g_showFps);
                if (ThorArtButton("Advanced display", ">")) { page = Page::AdvancedDisplay; focusNextFrame = true; }
            } else if (page == Page::Controller) {
                const char* name = PADGetName(0);
                ImGui::Text("Player1: %s", name ? name : "No controller connected");
                ImGui::Spacing();
                if (ThorArtButton("Button mappings and controller setup", ">")) {
                    page = Page::ControllerBindings;
                    focusNextFrame = true;
                }
                ImGui::TextWrapped("Use physical controls. A selects, B goes back. Press both sticks to open or close settings.");
            } else if (page == Page::ControllerBindings) {
                DrawControllerSettings();
            } else if (page == Page::AdvancedDisplay) {
                ImGui::TextWrapped("Renderer options and compatibility workarounds. Change these only when needed.");
                DrawGraphicsSettings();
                ImGui::TextWrapped("Manage texture packs from the launcher before starting the game.");
            } else if (page == Page::AdvancedAudio) {
                DrawAudioSettings(false, true);
            } else if (page == Page::Audio) {
                DrawAudioSettings(true);
                if (ThorArtButton("Advanced audio", ">")) { page = Page::AdvancedAudio; focusNextFrame = true; }
            }
        }
        g_thorFocusNextPrimary = false;
        ImGui::PopItemWidth();
        ImGui::EndChild();
        if (popupLastFrame || popupNow ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            // A centered choice popup otherwise exposes fragments of parent
            // labels at its sides. Keep submitting that page for stable IDs,
            // layout and navigation, but hide only its own child draw list.
            // Popup draw lists and the surrounding header/footer are separate.
            // Include the closing frame, whose popup geometry is still drawn.
            for (ImDrawVert& vertex : contentDrawList->VtxBuffer)
                vertex.col &= ~IM_COL32_A_MASK;
        }
        g_thorArtRowHeight = 66.0f;
        const ImVec2 footerMin(top.x, end.y - footerHeight);
        thor_settings_assets::DrawImage("footer", ImGui::GetWindowDrawList(), footerMin, end);
        ImGui::SetCursorScreenPos(ImVec2(top.x + 42.0f * scale,
            footerMin.y + (footerHeight - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextUnformatted(page == Page::Audio ? "Left / Right: 5%     A: Precise volume     B: Back"
                                                  : "A: Select     B: Back     D-pad / Stick: Move");
    }
    ImGui::End();
    popupLastFrame = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    activeLastFrame = ImGui::IsAnyItemActive();
    ImGui::GetStyle() = previousStyle;
}

void DrawAndroidSidebar() {
    if (RuntimeConfigFile::IsThorDisplay()) {
        DrawThorSettings();
        return;
    }
    if (!g_topBarVisible) {
        g_sidebarWasVisible = false;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    // ~42% of screen width, matching KartPad's own Android sidebar proportions (measured directly
    // from a screenshot of it, at the user's request to look at how it actually does this).
    // Deliberately reaches into where the touch R/X/Y/A/B buttons normally sit; those are hidden
    // for as long as the sidebar is open (TouchControlsOverlay.setSettingsOpen) rather than trying
    // to squeeze around them, since gameplay input is already blocked while it's up.
    const float sidebarWidth = io.DisplaySize.x * 0.42f;
    // Controller navigation only moves inside the focused window: take focus when the panel opens.
    if (!g_sidebarWasVisible) {
        ImGui::SetNextWindowFocus();
    }
    g_sidebarWasVisible = true;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x, 0.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(sidebarWidth, io.DisplaySize.y), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoResize;
    if (!ImGui::Begin("Settings##AndroidSidebar", nullptr, kFlags)) {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    enum class SidebarPage { Root, Display, Controller, Audio, Music };
    static SidebarPage page = SidebarPage::Root;

    ImGui::SetWindowFontScale(1.15f);
    ImGui::TextUnformatted(page == SidebarPage::Root ? "Settings"
                            : page == SidebarPage::Display ? "Display"
                            : page == SidebarPage::Controller ? "Controller"
                            : page == SidebarPage::Music ? "Music player"
                                                          : "Audio");
    const float closeWidth = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f + 16.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - closeWidth);
    if (ImGui::Button("Close", ImVec2(closeWidth, 0.0f))) {
        SetTopBarVisible(false);
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("SidebarPageContent", ImVec2(0.0f, 0.0f), false);
    if (page == SidebarPage::Root) {
        if (DrawSidebarRow("Display")) {
            page = SidebarPage::Display;
        }
        if (DrawSidebarRow("Controller")) {
            page = SidebarPage::Controller;
        }
        if (DrawSidebarRow("Audio")) {
            page = SidebarPage::Audio;
        }
#if defined(__SWITCH__)
        // Android keeps its files where the player cannot reach them, so there is no folder to
        // drop music into there yet.
        if (DrawSidebarRow("Music player")) {
            page = SidebarPage::Music;
        }
#endif
        ImGui::Separator();
        ImGui::Spacing();
        // Every slider/checkbox on every page already writes straight to Config.toml the instant
        // it changes (see e.g. DrawAudioSettings' RuntimeConfigFile::Set* calls) - there is
        // nothing this button actually needs to commit. It exists anyway, requested directly
        // while testing, as an explicit confirmation point rather than trusting that silently.
        static Clock::time_point savedConfirmationUntil{};
        if (ImGui::Button("Save Settings", ImVec2(-1.0f, 52.0f))) {
            savedConfirmationUntil = Clock::now() + std::chrono::seconds(2);
        }
        if (Clock::now() < savedConfirmationUntil) {
            if (RuntimeConfigFile::LastSettingWriteSucceeded()) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Settings saved.");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Could not save settings (see stderr.txt).");
            }
        }
#if defined(__ANDROID__)
        // Back to the game-case launcher. The runtime stays paused in memory, so choosing the
        // same game again resumes exactly where it left off (choosing the other game restarts).
        ImGui::Spacing();
        if (ImGui::Button("Main Menu", ImVec2(-1.0f, 52.0f))) {
            // Stay open until Android pauses the guest so the press is not delivered to the game.
            AndroidReturnToMainMenu();
        }
#endif
    } else {
        if (DrawSidebarRow("Back to Settings", "<")) {
            page = SidebarPage::Root;
        }
        ImGui::Separator();
        ImGui::Spacing();
        switch (page) {
        case SidebarPage::Display:
            DrawResolutionSettings();
            DrawGraphicsSettings();
            ImGui::Separator();
            ImGui::Spacing();
            // Texture pack import/management deliberately does NOT live here - it used to have a
            // button on this page that opened Kotlin's ModManager dialog, but that dialog
            // launches Android's system file picker as a separate Activity, which backgrounds this
            // live game session's GPU surface. Confirmed directly on-device: doing that mid-race
            // lost the WebGPU/Vulkan surface ("QueuePresent failed with VK_ERROR_SURFACE_LOST_KHR")
            // and crashed. No such surface exists yet on the launcher screen (ModePickerActivity),
            // so that's the only place it's safe - same reasoning as why Retro Rewind's own install
            // flow only ever runs from there too.
#if defined(__ANDROID__)
            ImGui::TextWrapped(
                "Manage texture packs from the launcher screen (before starting a race).");
#endif
            break;
        case SidebarPage::Controller:
            DrawControllerSettings();
            ImGui::Separator();
            ImGui::Spacing();
#if defined(__SWITCH__)
            DrawMotionSteeringSettings();
#endif
#if defined(__ANDROID__)
            if (ImGui::Button("Edit Touch Layout", ImVec2(-1.0f, 48.0f))) {
                // Editing happens in Kotlin's TouchControlsOverlay, not this ImGui panel - close
                // the sidebar first so it isn't sitting on top of (and eating touches meant for)
                // the layout editor that's about to appear.
                SetTopBarVisible(false);
                AndroidStartTouchLayoutEdit();
            }
            if (ImGui::Button("Motion Steering", ImVec2(-1.0f, 48.0f))) {
                // Same reasoning as Edit Touch Layout above - the dialog is a real Kotlin
                // AlertDialog, not an ImGui popup, so get the sidebar out of the way first.
                SetTopBarVisible(false);
                AndroidShowMotionSteeringDialog();
            }
#endif
            break;
        case SidebarPage::Audio:
            DrawAudioSettings();
            break;
        case SidebarPage::Music:
            DrawMusicPlayer();
            break;
        case SidebarPage::Root:
            break;
        }
    }
    ScrollCurrentWindowByTouchDrag();
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar();
}

#endif  // __ANDROID__

void DrawTopBar() {
    if (!g_topBarVisible || !ImGui::BeginMainMenuBar()) {
        return;
    }

    ImGui::TextUnformatted("WiiCompiled");
    ImGui::Separator();
    const auto resolutionIt = std::find_if(kResolutions.begin(), kResolutions.end(), [](const ResolutionItem& item) {
        return std::fabs(item.scale - g_resolutionScale) < 0.001f;
    });
    const char* resolutionLabel = resolutionIt != kResolutions.end() ? ResolutionLabel(*resolutionIt) : "Custom";
    const std::string resolutionMenuLabel = std::string("Resolution: ") + resolutionLabel;
    if (ImGui::BeginMenu(resolutionMenuLabel.c_str())) {
        for (const auto& resolution : kResolutions) {
            const bool selected = std::fabs(resolution.scale - g_resolutionScale) < 0.001f;
            const bool disabled = IsHighFrameRateMode() && IsHighResolutionScale(resolution.scale);
            ImGui::BeginDisabled(disabled);
            const bool clicked = ImGui::MenuItem(ResolutionLabel(resolution), nullptr, selected);
            ImGui::EndDisabled();
            if (clicked) {
                SetResolutionScale(resolution.scale);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Graphics")) {
        DrawGraphicsSettings();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Controller settings")) {
        DrawControllerSettings();
        ImGui::EndMenu();
    }

    const std::string audioLabel = g_audioMuted
        ? "Audio: Muted"
        : "Audio: " + std::to_string(g_audioVolumePercent) + "%";
    // Keep the popup ID stable while the Master slider changes the visible
    // label. Without the ### suffix, ImGui treats every new percentage as a
    // different menu and closes the popup on the first drag update.
    const std::string audioMenuLabel = audioLabel + "###AudioSettingsMenu";
    if (ImGui::BeginMenu(audioMenuLabel.c_str())) {
        DrawAudioSettings();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Music player")) {
        ImGui::Dummy(ImVec2(420.0f, 0.0f));
        DrawMusicPlayer();
        ImGui::EndMenu();
    }

    const float hideWidth = ImGui::CalcTextSize("Hide (F10)").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - hideWidth - 8.0f));
    if (ImGui::MenuItem("Hide (F10)")) {
        SetTopBarVisible(false);
    }
    ImGui::EndMainMenuBar();
}

#if defined(__SWITCH__)
void ForwardGamepadToImGui(const SDL_Event& event) {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        ImGuiKey key = ImGuiKey_None;
        switch (event.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: key = ImGuiKey_GamepadDpadUp; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: key = ImGuiKey_GamepadDpadDown; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: key = ImGuiKey_GamepadDpadLeft; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: key = ImGuiKey_GamepadDpadRight; break;
        case SDL_GAMEPAD_BUTTON_SOUTH: key = ImGuiKey_GamepadFaceDown; break;   // A: activate
        case SDL_GAMEPAD_BUTTON_EAST: key = ImGuiKey_GamepadFaceRight; break;   // B: back
        case SDL_GAMEPAD_BUTTON_WEST: key = ImGuiKey_GamepadFaceLeft; break;
        case SDL_GAMEPAD_BUTTON_NORTH: key = ImGuiKey_GamepadFaceUp; break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: key = ImGuiKey_GamepadL1; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: key = ImGuiKey_GamepadR1; break;
        default: break;
        }
        if (key != ImGuiKey_None) {
            io.AddKeyEvent(key, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        }
    } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
               (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)) {
        constexpr float kDeadZone = 8000.0f;
        const float v = static_cast<float>(event.gaxis.value);
        const float neg = v < -kDeadZone ? std::min(1.0f, (-v - kDeadZone) / (32767.0f - kDeadZone)) : 0.0f;
        const float pos = v > kDeadZone ? std::min(1.0f, (v - kDeadZone) / (32767.0f - kDeadZone)) : 0.0f;
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, neg > 0.0f, neg);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, pos > 0.0f, pos);
        } else {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, neg > 0.0f, neg);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, pos > 0.0f, pos);
        }
    }
}
#endif

bool IsToggleKey(const SDL_Event& event, SDL_Scancode code) {
    return event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == code;
}

bool IsMouseActivity(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return true;
    default:
        return false;
    }
}

// Runs on the thread that pumps SDL events (the same one that calls Draw), so
// the SDL cursor calls are safe here on platforms that have a real cursor to hide.
//
// Android has none - there is no mouse pointer on a touchscreen - and SDL_HideCursor()/
// SDL_ShowCursor() there route through Android_JNI_SetCustomCursor -> SDLActivity.setCustomCursor
// -> View.setPointerIcon -> a Binder call to IWindowSession.updatePointerIcon. Confirmed directly
// on-device via a real Android debuggerd tombstone (our own SIGABRT interception normally hides
// this - see main.cpp's AbortSignalHandler): that JNI chain aborted with "JNI DETECTED ERROR IN
// APPLICATION: JNI ERROR (app bug): jstring is an invalid JNI transition frame reference", called
// from settings_overlay::Draw() on the guest fiber thread - a call with no purpose on a device
// with no cursor, so skipping it here removes both the pointless work and the crash.
void UpdateCursorAutoHide() {
#if defined(__ANDROID__)
    return;
#else
    const bool shouldHide =
        !g_topBarVisible && Clock::now() - g_lastMouseActivity >= kCursorAutoHideDelay;
    if (shouldHide == g_cursorHidden) {
        return;
    }
    g_cursorHidden = shouldHide;
    if (shouldHide) {
        SDL_HideCursor();
    } else {
        SDL_ShowCursor();
    }
#endif
}

// Alt+Enter toggles the display mode inside aurora without going through the
// F10 combo, so the active mode is compared against the last persisted one
// every frame and written back on change.
void PersistDisplayModeIfChanged() {
    const int active = static_cast<int>(aurora_get_display_mode());
    if (active == g_displayMode) {
        return;
    }
    g_displayMode = active;
    RuntimeConfigFile::SetDisplayMode(std::string(kDisplayModeConfigNames[static_cast<size_t>(active)]));
}
} // namespace

void InitializeThorSettingsAssets(const AuroraWindowSize*) noexcept {
#if defined(__ANDROID__)
    if (!RuntimeConfigFile::IsThorDisplay()) return;
    try {
        const auto folder = RuntimeConfigFile::ApplicationDataDirectory() / "ThorMap" / "thor_settings";
        if (!thor_settings_assets::Load(folder)) {
            RT_LOG(RT_TAG_CONFIG) << "Original Wii settings assets missing or invalid: " << folder << std::endl;
            return;
        }
        // Aurora's generic Android path applies2.4x. The original51px atlas
        // already has ample resolution; scale font and geometry independently.
        ImGui::GetStyle() = ImGuiStyle();
        ImGui::GetStyle().ScaleAllSizes(2.0f);
        ApplyThorSettingsPalette(ImGui::GetStyle());
        ImGui::GetIO().ConfigNavCursorVisibleAlways = true;
        RT_LOG(RT_TAG_CONFIG) << "Loaded original Wii settings font and menu artwork from " << folder << std::endl;
    } catch (const std::exception& error) {
        RT_LOG(RT_TAG_CONFIG) << "Original Wii settings assets: " << error.what() << std::endl;
    }
#endif
}

void InitializeRuntimeSettings() noexcept {
#if defined(__SWITCH__)
    LoadMotionSteering();  // a saved "tilt to steer" applies without opening the menu
#endif
#if defined(__ANDROID__)
    // The g_* globals above were initialized at static-init time, which on Android runs
    // before Kotlin's JNI call to SetAndroidFilesDir() - so RuntimeConfigFile::ApplicationDataDirectory()
    // still resolved to nothing back then, and every read silently fell back to its hardcoded
    // default (this is why persisted settings like display mode "didn't save": they were never
    // actually being read back, on any launch). SetAndroidFilesDir() has definitely run by the
    // time SDL_main calls this function, so re-read everything from the real config now.
    g_resolutionScale = RuntimeConfigFile::ResolutionMultiplier(1.0f);
    if (g_resolutionScale > 0.0f && g_resolutionScale < 1.0f) {
        g_resolutionScale = 1.0f;
    }
    g_audioVolumePercent = static_cast<int>(std::lround(RuntimeConfigFile::AudioVolume(1.0f) * 100.0f));
    g_musicVolumePercent = static_cast<int>(std::lround(RuntimeConfigFile::MusicVolume(1.0f) * 100.0f));
    g_soundEffectsVolumePercent =
        static_cast<int>(std::lround(RuntimeConfigFile::SoundEffectsVolume(1.0f) * 100.0f));
    g_uiVolumePercent = static_cast<int>(std::lround(RuntimeConfigFile::UiVolume(1.0f) * 100.0f));
    g_voicesVolumePercent = static_cast<int>(std::lround(RuntimeConfigFile::VoicesVolume(1.0f) * 100.0f));
    g_audioMuted = RuntimeConfigFile::AudioMuted(false);
    g_audioMixWorker = RuntimeConfigFile::AudioMixWorkerEnabled(true);
    g_attenuateMusicWhenMediaPlays = RuntimeConfigFile::AttenuateMusicWhenMediaPlays(false);
    g_frameInterpolationMode = ConfiguredFrameInterpolationMode();
    g_displayMode = ParseDisplayModeConfig(RuntimeConfigFile::DisplayMode("borderless"));
    g_skipUnreadyPipelines = RuntimeConfigFile::SkipUnreadyPipelines(true);
    g_disableCopyFilter = RuntimeConfigFile::DisableCopyFilter(true);
    // With no saved choice, the GPUs known to need the workaround get it.
    g_constantMatrixIndexing = RuntimeConfigFile::ConstantMatrixIndexing(AuroraAdapterWantsConstantMatrixIndexing());
    g_vertexRepackChoice = ParseVertexRepackConfig(RuntimeConfigFile::VertexRepack("auto"));
#if defined(__SWITCH__)
    // An Adreno driver workaround with no purpose on the Switch's GPU, and a harmful one: with it
    // on, item boxes rendered as opaque black/grey cubes instead of translucent rainbow ones
    // (2026-09-26, confirmed by toggling it on hardware). A config written by an older build or
    // the settings checkbox must not bring it back.
    g_constantMatrixIndexing = false;
#endif
    if (aurora_get_backend() == BACKEND_OPENGLES && !g_constantMatrixIndexing) {
        // Not optional on this backend. Dynamically indexed matrices make Qualcomm's GLSL
        // compiler segfault outright rather than merely miscompile: confirmed by a debuggerd
        // tombstone landing inside libllvm-glnext.so's CreateQGLCShader, called from Dawn's
        // ShaderModule::CompileShader, which took the whole process down during boot every
        // time. On Vulkan the same setting is a fix for missing characters and stays the user's
        // choice; here it is the difference between running and not.
        g_constantMatrixIndexing = true;
        RT_LOG(RT_TAG_RUNTIME)
            << "forcing constant matrix indexing on: the OpenGL ES driver cannot compile the "
               "dynamically indexed form"
            << std::endl;
    }
    g_threadedGx = RuntimeConfigFile::ThreadedGx(false);
    g_thermalAutoQuality = RuntimeConfigFile::ThermalAutoQuality(false);
    g_showFps = RuntimeConfigFile::ShowFps(true);
    g_showShaderCompilation = RuntimeConfigFile::ShowShaderCompilation(!RuntimeConfigFile::IsThorDisplay());
    g_hideSettingsButton = RuntimeConfigFile::HideSettingsButton(false);
    g_disabledPostProcessingPaths = RuntimeConfigFile::DisabledPostProcessingPaths(0);
    // Same staleness class as everything else in this block, plus one more wrinkle: on a
    // brand-new install with no [video] widescreen key yet, android_jni_bridge.cpp's SDL_main
    // only just wrote the device's auto-detected default moments ago (right before the config
    // reload that made this whole re-read necessary) - without this line the very first launch's
    // settings sidebar would show "Widescreen (16:9)" regardless of the real screen shape, even
    // though the auto-detected value was already correctly in effect for actual gameplay.
    g_widescreen = RuntimeConfigFile::WidescreenEnabled(true);
#endif
    controller_mapping_wizard::LoadPersistedMappings();
    ApplyConfiguredMappings();
    AudioBackend::Instance().SetMasterVolume(static_cast<float>(g_audioVolumePercent) / 100.0f);
    AudioBackend::Instance().SetMuted(g_audioMuted);
    AxDspHle::SetMixWorkerEnabled(g_audioMixWorker);
    MusicAttenuation::SetMusicVolume(static_cast<float>(g_musicVolumePercent) / 100.0f);
    MusicAttenuation::SetSoundEffectsVolume(static_cast<float>(g_soundEffectsVolumePercent) / 100.0f);
    MusicAttenuation::SetUiVolume(static_cast<float>(g_uiVolumePercent) / 100.0f);
    MusicAttenuation::SetVoicesVolume(static_cast<float>(g_voicesVolumePercent) / 100.0f);
    MusicAttenuation::SetEnabled(g_attenuateMusicWhenMediaPlays);
    RuntimeGameGraphicsOptions::SetDisabledPostProcessingPaths(g_disabledPostProcessingPaths);
    const uint32_t targetFps = kFrameInterpolationTargetFps[static_cast<size_t>(g_frameInterpolationMode)];
    LimitResolutionForFrameRate();
    aurora_set_frame_interpolation_fps(targetFps);
    aurora_set_display_mode(static_cast<AuroraDisplayMode>(g_displayMode));
    g_displayMode = static_cast<int>(aurora_get_display_mode());
    aurora_set_disable_copy_filter(g_disableCopyFilter);
    AuroraSetConstantMatrixIndexing(g_constantMatrixIndexing);
    g_lowDetailComputerKarts = RuntimeConfigFile::LowDetailComputerKarts(kLowDetailComputerKartsDefault);
    RuntimeSetLowDetailComputerKarts(g_lowDetailComputerKarts);
    g_thirtyFpsRaces = RuntimeConfigFile::ThirtyFpsRaces(false);
    RuntimeSetThirtyFpsRaces(g_thirtyFpsRaces);
#if !defined(__SWITCH__)
    AuroraSetVertexRepackMode(VertexRepackModeForChoice(g_vertexRepackChoice));
#endif
    AuroraSetThreadedGx(g_threadedGx);
    aurora_set_overlap_frame_encode(RuntimeConfigFile::OverlapFrameEncode(true));
    ThermalQuality::SetEnabled(g_thermalAutoQuality);
#if defined(__ANDROID__)
    AndroidSetSettingsButtonHidden(g_hideSettingsButton);
#endif
    aurora_set_skip_unready_pipelines(g_skipUnreadyPipelines);
    g_strapInputAccepted.store(false, std::memory_order_relaxed);
    g_bootSceneExited.store(false, std::memory_order_relaxed);
    g_startupDismissFrame.store(UINT64_MAX, std::memory_order_relaxed);
    g_startupDismissAtMs.store(INT64_MAX, std::memory_order_relaxed);
    PADBlockInput(g_topBarVisible);
}

void HandleEvents(const AuroraEvent* events) noexcept {
    if (!events) {
        return;
    }
    for (const AuroraEvent* ev = events; ev->type != AURORA_NONE; ++ev) {
        if (ev->type == AURORA_CONTROLLER_ADDED || ev->type == AURORA_CONTROLLER_REMOVED) {
            g_configuredControllerIndices.fill(std::numeric_limits<int32_t>::min());
#if defined(__ANDROID__)
            if (ev->type == AURORA_CONTROLLER_ADDED) {
                AutoConfigureNewAndroidControllersIfPresent();
            }
#endif
        }
        if (ev->type != AURORA_SDL_EVENT) {
            continue;
        }
        controller_mapping_wizard::HandleSdlEvent(ev->sdl);
        if (IsToggleKey(ev->sdl, SDL_SCANCODE_F10)) {
            SetTopBarVisible(!g_topBarVisible);
        }
        // Controller shortcut (the Switch has no F10): click both sticks together.
        if (ev->sdl.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || ev->sdl.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
            static bool leftStickHeld = false;
            static bool rightStickHeld = false;
            const bool down = ev->sdl.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            const bool wasBoth = leftStickHeld && rightStickHeld;
            if (ev->sdl.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
                leftStickHeld = down;
            } else if (ev->sdl.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
                rightStickHeld = down;
            }
            if (!wasBoth && leftStickHeld && rightStickHeld) {
                SetTopBarVisible(!g_topBarVisible);
            }
        }
#if defined(__SWITCH__)
        // Drive the settings panel's controller navigation straight from the gamepad events: the
        // Switch's virtual SDL gamepad is not guaranteed to be opened by ImGui's SDL backend.
        ForwardGamepadToImGui(ev->sdl);
#endif
        if (IsMouseActivity(ev->sdl)) {
            g_lastMouseActivity = Clock::now();
        }
    }
}

void Draw() noexcept {
    // Wait for the frame worker's DONE phase: it has replayed the previous frame's ImGui draw lists
    // and started the next ImGui frame, so all overlay callers can now safely issue ImGui commands.
    aurora_wait_for_frame_worker();
    ApplyConfiguredMappings();
    PersistDisplayModeIfChanged();
    UpdateCursorAutoHide();
    if (!StartupScreenVisible()) {
        DrawShaderCompilationStatus();
    }
    DrawFpsOverlay();
    DrawBackendFallbackNotice();
#if defined(__ANDROID__) || defined(__SWITCH__)
    DrawAndroidSidebar();
#else
    DrawTopBar();
#endif
    controller_mapping_wizard::Draw();
    // The wizard captures raw presses; keep them out of the game even when the
    // top bar is hidden mid-setup.
    PADBlockInput(g_topBarVisible || controller_mapping_wizard::IsActive());
    DrawStartupScreen();
}

bool StartupScreenVisible() noexcept {
    return !g_strapInputAccepted.load(std::memory_order_acquire) ||
           (g_presentedFrame < g_startupDismissFrame.load(std::memory_order_relaxed) &&
            StartupClockMs() < g_startupDismissAtMs.load(std::memory_order_relaxed));
}

void ToggleTopBar() noexcept {
    SetTopBarVisible(!g_topBarVisible);
}

void NotifyStrapInputAccepted() noexcept {
    bool expected = false;
    if (g_strapInputAccepted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        g_startupDismissFrame.store(g_presentedFrame + kStrapTransitionCoverFrames,
                                    std::memory_order_release);
        g_startupDismissAtMs.store(StartupClockMs() + kStrapTransitionCoverMs, std::memory_order_release);
    }
}

void NotifyBootSceneExited() noexcept {
    if (!g_strapInputAccepted.load(std::memory_order_acquire)) {
        if (!RuntimeProduct::IsRetroRewind()) {
            return;
        }
        // RR's mod boot route does not guarantee a visit to our strap-input hook.
        // Its actual scene exit can release the presentation cover independently;
        // this updates only cover bookkeeping, never guest acceptance or controller state.
        NotifyStrapInputAccepted();
    }
    bool expected = false;
    if (g_bootSceneExited.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        g_startupDismissFrame.store(g_presentedFrame + kSceneExitCoverFrames, std::memory_order_release);
        g_startupDismissAtMs.store(StartupClockMs() + kSceneExitCoverMs, std::memory_order_release);
    }
}

void NotifyGuestFrameRendered() noexcept {
#if defined(__ANDROID__)
    if (!RuntimeProduct::IsRetroRewind() || g_bootSceneExited.load(std::memory_order_acquire)) {
        return;
    }
    // RR can enter the title/menu directly, without our strap-input callback.
    // Title/licence changes within that scene do not emit "Scene Exit"; waiting
    // for that report concealed usable screens until several blind A presses.
    // Observe the actual PAL scene only after GXCopyDisp completed a guest frame.
    // RKSystem::spInstance is 80385FC8; getSceneMgr (80009828) reads +54;
    // calcCurrentScene (8023B588) reads manager+0C. EGG::Scene's ID and owner
    // are +28/+2C, corroborated by createScene (8023B0E4). Bootstrap/strap is5.
    const auto read = [](uint32_t address, uint32_t& value) {
        const bool cachedRam = (address >= 0x80000000u && address < 0x82000000u) ||
                               (address >= 0x90000000u && address < 0x98000000u);
        return cachedRam && (address & 3u) == 0 && Memory::Contains(address, 4) &&
               Memory::TryRead32(address, value);
    };
    uint32_t system = 0, manager = 0, scene = 0, sceneId = 0, owner = 0, managerId = 0;
    if (!read(0x80385FC8u, system) || !read(system + 0x54u, manager) ||
        !read(manager + 0x0Cu, scene) || !read(scene + 0x28u, sceneId) ||
        !read(scene + 0x2Cu, owner) || !read(manager + 0x18u, managerId) ||
        owner != manager || sceneId != managerId) {
        return;
    }
    // Pinned RKScene.hpp: title0, menu1, race2, globe4, flag-open12.
    // No timeout or controller press is used to infer guest acceptance.
    if (sceneId != 0 && sceneId != 1 && sceneId != 2 && sceneId != 4 && sceneId != 12) {
        return;
    }
    RT_LOG(RT_TAG_RUNTIME) << "RR startup cover: completed guest frame in scene " << sceneId
                           << "; releasing presentation cover" << std::endl;
    NotifyBootSceneExited();
#endif
}

void AdvancePresentedFrame() noexcept { ++g_presentedFrame; }

void NotifyBackendFallback(const char* requested, const char* actual) noexcept {
    const auto pretty = [](const char* name) -> std::string {
        const std::string_view value = name != nullptr ? name : "";
        if (value == "vulkan") return "Vulkan";
        if (value == "opengles") return "OpenGL ES";
        return std::string(value);
    };
    try {
        g_backendFallbackNotice = pretty(requested) + " could not start on this device. Using " + pretty(actual) +
                                  " instead, which may be slower.";
    } catch (...) {
    }
}
} // namespace settings_overlay
