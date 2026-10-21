// Backs the on-screen touch controls (Kotlin: TouchControlsOverlay and its per-control Views)
// with a real SDL3 virtual gamepad, so touch input flows through the exact same input:: /
// controller-mapping-wizard pipeline a physical controller already uses - no special-casing
// needed anywhere else in the engine, and the player can map touch buttons the same way they'd
// map a real pad. Attached lazily on first touch input rather than from SDL_main's own startup:
// this file only needs the joystick subsystem SDL_main already initializes for real controllers,
// and staying lazy means the Kotlin side needs no separate "native ready" signal.
#if defined(__ANDROID__)

#include <jni.h>
#include <algorithm>
#include <mutex>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>

namespace {

std::mutex g_mutex;
SDL_JoystickID g_instanceId = 0;
SDL_Joystick* g_joystick = nullptr;

SDL_Joystick* EnsureVirtualJoystick() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_joystick != nullptr) {
        return g_joystick;
    }

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    // Only LEFTX/LEFTY are used (the on-screen stick); every button is digital, including L/R
    // (SDL_GAMEPAD_BUTTON_LEFT/RIGHT_SHOULDER), matching the Classic Controller Pro preset that
    // AutoConfigureNewAndroidControllersIfPresent (settings_overlay.cpp) applies to this device.
    desc.naxes = SDL_GAMEPAD_AXIS_LEFTY + 1;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY);
    // SDL's virtual joystick backend does NOT expose SDL_SetJoystickVirtualButton's "button"
    // index as the SDL_GamepadButton enum value - it auto-derives a gamepad mapping by walking
    // the enum in order and assigning each *present* bit in button_mask the next free target
    // slot, packing around any gaps (see SDL_virtualjoystick.c's UpdateVirtualGamepadMapping).
    // The previous button_mask here omitted GUIDE/LEFT_STICK/RIGHT_STICK, which silently shifted
    // every later button (START and past it) down by however many gaps preceded it - confirmed
    // on-device: Start fired the item button (really landing on LEFT_SHOULDER's slot), and the
    // D-Pad was scrambled by one slot per axis. Setting every bit from SOUTH..DPAD_RIGHT makes
    // the packed slot equal the enum value for every button we use, with no gaps to shift around.
    desc.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
    desc.button_mask = (1u << desc.nbuttons) - 1u;
    desc.name = "WiiCompiled Touch Controls";

    g_instanceId = SDL_AttachVirtualJoystick(&desc);
    if (g_instanceId == 0) {
        return nullptr;
    }
    g_joystick = SDL_OpenJoystick(g_instanceId);
    return g_joystick;
}

}  // namespace

// button is an SDL_GamepadButton value (SDL_GAMEPAD_BUTTON_SOUTH, ..._DPAD_RIGHT, etc.) - shared
// directly with Kotlin via TouchButton.kt's constants so there is exactly one place either side
// could disagree.
extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_touch_TouchInputBridge_nativeSetTouchButton(JNIEnv*, jclass, jint button,
                                                                          jboolean pressed) {
    if (SDL_Joystick* joystick = EnsureVirtualJoystick()) {
        SDL_SetJoystickVirtualButton(joystick, button, pressed == JNI_TRUE);
    }
}

// axis is an SDL_GamepadAxis value; value is normalized [-1, 1].
extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_touch_TouchInputBridge_nativeSetTouchAxis(JNIEnv*, jclass, jint axis,
                                                                        jfloat value) {
    if (SDL_Joystick* joystick = EnsureVirtualJoystick()) {
        const float clamped = std::clamp(value, -1.0f, 1.0f);
        const auto scaled = static_cast<Sint16>(clamped * 32767.0f);
        SDL_SetJoystickVirtualAxis(joystick, axis, scaled);
    }
}

#endif  // __ANDROID__
