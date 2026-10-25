#pragma once

#include <string>

namespace mobile_ui {

struct Settings {
    bool showControls = false;
    bool fullscreen = true;
    float opacity = 0.65f;
    float scale = 1.0f;
    bool dynamicStick = true;
    float stickOffsetX = 0.0f;
    float stickOffsetY = 0.0f;
    float buttonOffsetX = 0.0f;
    float buttonOffsetY = 0.0f;
    float btnAOffsetX = 0.0f;
    float btnAOffsetY = 0.0f;
    float btnBOffsetX = 0.0f;
    float btnBOffsetY = 0.0f;
    float btnZOffsetX = 0.0f;
    float btnZOffsetY = 0.0f;
    float btnSpinOffsetX = 0.0f;
    float btnSpinOffsetY = 0.0f;
    float starStickOffsetX = 0.0f;
    float starStickOffsetY = 0.0f;
    bool showDpad = false;
    bool dpadOnRight = false;
    float dpadOffsetX = 0.0f;
    float dpadOffsetY = 0.0f;

    // NVIDIA Shield and Remaster Mods
    bool modShieldTextures = false;
    bool modShieldCutscenes = false;
    bool modShieldControls = false;
    bool modClassicGamepadUI = false;
    bool modRiivolution = true;

    // Advanced Gameplay Mods
    bool modFly = false;
    bool modSkipCutscenes = false;
    bool modInfiniteHealth = false;
    bool modInfiniteLives = false;
    bool modInfiniteStarBits = false;
    bool speedrunHud = false;

    // System Settings
    int gameSpeed = 1;
    int languageChoice = 0;
    int renderScale = 0;
    bool haptics = false;
    int targetFps = 60;
    int preferredRefreshRate = 60;
    bool adaptiveFps = false;
    bool batterySaver = false;
    bool gyroAiming = false;
    bool gyroInvertX = false;
    bool gyroInvertY = false;

    bool starStick = false;
    float stickSensitivity = 1.0f;
    float starStickSensitivity = 1.0f;
    int rightStickMode = 1;
    bool touchCamera = true;

    bool fullTouchMode = false;
    int perfHudMode = 0;
    int graphicsBackend = 1;
    bool relativePointer = false;
    int screenOrientationChoice = 0;

    // AYN Thor settings menu (platform/mobile/thor/ThorSettingsBridge.cpp).
    int postProcess = -1;       // -1 engine default; 0 original, 1 neutral anti-aliasing, 2 added glow/color
    bool cameraInverted = false;
    // AYN Thor: what each controller button does in Galaxy (mobile_input::ButtonAction), in
    // mobile_input::PhysicalButton order: A B X Y L1 R1 L2 R2 L3 R3 Start Select. The standard
    // layout is the one the port shipped with.
    int buttonMap[12] = {1, 2, 3, 3, 5, 3, 4, 2, 4, 7, 6, 6};

    void load(const std::string& path);
    void save(const std::string& path) const;
};

}  // namespace mobile_ui
