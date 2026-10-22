#pragma once

#include <android/input.h>
#include <stdint.h>
#include "port/input.h"

namespace mobile_input {

// AYN Thor: Galaxy's controller layout is a table (Settings::buttonMap): each physical button
// does one action, changeable in the Galaxy settings menu (Controls > Buttons). The D-pad, the
// sticks and Back keep their own jobs.
enum PhysicalButton { BTN_A, BTN_B, BTN_X, BTN_Y, BTN_L1, BTN_R1, BTN_L2, BTN_R2, BTN_L3, BTN_R3, BTN_START, BTN_SELECT,
    BTN_COUNT };
enum ButtonAction { ACT_NONE, ACT_JUMP, ACT_SHOOT, ACT_SPIN, ACT_CROUCH, ACT_CAMERA, ACT_PAUSE, ACT_CENTER_POINTER, ACT_COUNT };
// The standard layout (also Settings::buttonMap's default).
constexpr int kDefaultButtonMap[BTN_COUNT] = {ACT_JUMP, ACT_SHOOT, ACT_SPIN, ACT_SPIN, ACT_CAMERA, ACT_SPIN,
    ACT_CROUCH, ACT_SHOOT, ACT_CROUCH, ACT_CENTER_POINTER, ACT_PAUSE, ACT_PAUSE};
// Replaces the layout (BTN_COUNT actions); out-of-range values mean ACT_NONE.
void setButtonMap(const int* map);

class GamepadInput {
public:
    GamepadInput() = default;

    // Handles physical joystick motion events (analog sticks, triggers, D-pad hat)
    bool handleMotionEvent(AInputEvent* event, PortPadState& pad, int& spinFrames, bool touchscreenDown);

    // Handles physical gamepad / keyboard button events
    bool handleKeyEvent(AInputEvent* event, PortPadState& pad, int& spinFrames);

    // Resets held buttons on pause
    void reset();
    // Restore physical pointer state after the touch updater and integrate a
    // held stick once per frame, rather than once per Android motion event.
    void updatePointer(PortPadState& pad, float seconds, bool touchscreenDown);

    // Returns whether a physical controller has been detected
    bool isGamepadDetected() const { return mGamepadDetected; }

    // Physical analog stick values (zero while the left stick steers the pointer in a menu, and
    // after a menu closes until the stick is let go)
    float stickX() const { return (mMenuPointer || mStickLatched) ? 0.0f : mPhysicalStickX; }
    float stickY() const { return (mMenuPointer || mStickLatched) ? 0.0f : mPhysicalStickY; }

    // Combined physical button masks
    uint32_t buttons() const { return mPhysicalButtons | mPhysicalAxisButtons | mActionButtons; }

private:
    // A physical button went down or up: updates the held actions and fires the one-shot ones.
    void press(int button, bool down, PortPadState& pad, int& spinFrames);
    void centerPointer(PortPadState& pad);

    bool mGamepadDetected = false;
    bool mHeld[BTN_COUNT] = {};
    uint32_t mActionButtons = 0;  // Wii buttons held through the layout table
    uint32_t mPhysicalButtons = 0;
    uint32_t mPhysicalAxisButtons = 0;
    float mPhysicalStickX = 0.0f;
    float mPhysicalStickY = 0.0f;
    float mPointerStickX = 0.0f, mPointerStickY = 0.0f;
    float mMenuStickX = 0.0f, mMenuStickY = 0.0f;  // left stick, for the pointer in menus
    float mPointerX = 0.0f, mPointerY = 0.0f;
    bool mMenuPointer = false;  // the left stick is steering the pointer
    bool mStickLatched = false;  // a stick held when a menu closed: centred for the game until let go
};

}  // namespace mobile_input
