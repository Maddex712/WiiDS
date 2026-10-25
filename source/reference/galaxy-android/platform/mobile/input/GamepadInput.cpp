#include "GamepadInput.h"

#include <android/keycodes.h>
#include <android/log.h>
#include <algorithm>
#include <math.h>

#include "../ui/UIOverlay.h"
#include "port/port.h"

namespace mobile_input {

bool GamepadInput::handleMotionEvent(AInputEvent* event, PortPadState& pad, int& spinFrames, bool touchscreenDown) {
    int32_t source = AInputEvent_getSource(event);
    bool isJoystick = (source & AINPUT_SOURCE_CLASS_JOYSTICK) != 0 ||
                      ((source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) ||
                      ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK);
    if (!isJoystick) return false;

    if (!mGamepadDetected) {
        mGamepadDetected = true;
        mobile_ui::UIOverlay::get().showToast("Gamepad Connected");
    }

    // Left Analog Stick
    float rawX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
    float rawY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
    float stickMag = sqrtf(rawX * rawX + rawY * rawY);
    const float kDeadzone = 0.18f;
    if (stickMag <= kDeadzone) {
        mPhysicalStickX = 0.0f;
        mPhysicalStickY = 0.0f;
        mMenuStickX = mMenuStickY = 0.0f;
        mStickLatched = false;  // let go: the stick reaches the game again
    } else {
        float norm = (stickMag - kDeadzone) / (1.0f - kDeadzone);
        float sens = mobile_ui::UIOverlay::get().settings().stickSensitivity;
        mPhysicalStickX = (rawX / stickMag) * std::min(1.0f, norm * sens);
        mPhysicalStickY = -(rawY / stickMag) * std::min(1.0f, norm * sens); // Invert Y for Wii convention
        mMenuStickX = (rawX / stickMag) * std::min(1.0f, norm);
        mMenuStickY = -(rawY / stickMag) * std::min(1.0f, norm);
    }

    // D-Pad Hat axes
    float hatX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
    float hatY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
    if (hatX < -0.5f) { mPhysicalAxisButtons |= 0x0001; mPhysicalAxisButtons &= ~0x0002; }
    else if (hatX > 0.5f) { mPhysicalAxisButtons |= 0x0002; mPhysicalAxisButtons &= ~0x0001; }
    else { mPhysicalAxisButtons &= ~0x0003; }

    if (hatY < -0.5f) { mPhysicalAxisButtons |= 0x0008; mPhysicalAxisButtons &= ~0x0004; }
    else if (hatY > 0.5f) { mPhysicalAxisButtons |= 0x0004; mPhysicalAxisButtons &= ~0x0008; }
    else { mPhysicalAxisButtons &= ~0x000C; }

    // Analog Triggers (L2 / R2)
    float rawL = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0));
    float rawBrake = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_BRAKE, 0));
    float lTrig = std::max(rawL, rawBrake);

    float rawR = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0));
    float rawGas = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_GAS, 0));
    float rTrig = std::max(rawR, rawGas);

    // The triggers are buttons L2 and R2 of the layout table.
    if ((lTrig > 0.4f) != mHeld[BTN_L2]) press(BTN_L2, lTrig > 0.4f, pad, spinFrames);

    if ((rTrig > 0.4f) != mHeld[BTN_R2]) press(BTN_R2, rTrig > 0.4f, pad, spinFrames);

    // Right stick (native camera turns or Star Pointer).
    float rightX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
    float rightY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
    if (fabsf(rightX) <= 0.05f && fabsf(rightY) <= 0.05f) {
        rightX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RX, 0);
        rightY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RY, 0);
    }
    float deadzone = 0.08f;
    float mag = sqrtf(rightX * rightX + rightY * rightY);
    float rStickX = 0.0f, rStickY = 0.0f;
    if (mag > deadzone) {
        float scale = std::min(1.0f, (mag - deadzone) / (1.0f - deadzone));
        rStickX = (rightX / mag) * scale;
        rStickY = (-rightY / mag) * scale;
    }
    if (mobile_ui::UIOverlay::get().settings().rightStickMode == 0) {
        // Keep the physical axes until the next event; virtual-touch decay must not re-arm
        // a held camera nudge between Android motion events. Sensitivity remains for pointing.
        mCameraStickX = -rStickX;
        mCameraStickY = rStickY;
        mPointerStickX = mPointerStickY = 0.0f;
        pad.rightStickX = mCameraStickX;
        pad.rightStickY = mCameraStickY;
    } else {
        mCameraStickX = mCameraStickY = 0.0f;
        pad.rightStickX = 0.0f;
        pad.rightStickY = 0.0f;
        mPointerStickX = rStickX;
        mPointerStickY = rStickY;
    }

    pad.connected = 1;
    return true;
}

bool GamepadInput::handleKeyEvent(AInputEvent* event, PortPadState& pad, int& spinFrames) {
    int32_t keyCode = AKeyEvent_getKeyCode(event);
    if (keyCode == AKEYCODE_VOLUME_UP || keyCode == AKEYCODE_VOLUME_DOWN || keyCode == AKEYCODE_VOLUME_MUTE) {
        return false;
    }

    if (!mGamepadDetected && (keyCode >= AKEYCODE_BUTTON_A && keyCode <= AKEYCODE_BUTTON_16)) {
        mGamepadDetected = true;
        mobile_ui::UIOverlay::get().showToast("Gamepad Connected");
    }

    int32_t keyAction = AKeyEvent_getAction(event);
    bool down = (keyAction == AKEY_EVENT_ACTION_DOWN);
    if (keyCode == AKEYCODE_BUTTON_A || keyCode == AKEYCODE_BUTTON_THUMBR)
        __android_log_print(ANDROID_LOG_INFO, "GalaxyInput", "key=%d action=%d pointer=(%.3f,%.3f) velocity=(%.3f,%.3f)",
            keyCode, keyAction, mPointerX, mPointerY, mPointerStickX, mPointerStickY);

    int physical = -1;
    switch (keyCode) {
    case AKEYCODE_BUTTON_A: case AKEYCODE_BUTTON_1: case AKEYCODE_DPAD_CENTER: case AKEYCODE_ENTER:
    case AKEYCODE_NUMPAD_ENTER: physical = BTN_A; break;
    case AKEYCODE_BUTTON_B: case AKEYCODE_BUTTON_2: case AKEYCODE_BUTTON_C: physical = BTN_B; break;
    case AKEYCODE_BUTTON_X: case AKEYCODE_BUTTON_3: physical = BTN_X; break;
    case AKEYCODE_BUTTON_Y: case AKEYCODE_BUTTON_4: physical = BTN_Y; break;
    case AKEYCODE_BUTTON_L1: case AKEYCODE_BUTTON_5: physical = BTN_L1; break;
    case AKEYCODE_BUTTON_R1: case AKEYCODE_BUTTON_6: physical = BTN_R1; break;
    case AKEYCODE_BUTTON_L2: case AKEYCODE_BUTTON_7: case AKEYCODE_BUTTON_Z: physical = BTN_L2; break;
    case AKEYCODE_BUTTON_R2: case AKEYCODE_BUTTON_8: physical = BTN_R2; break;
    case AKEYCODE_BUTTON_THUMBL: case AKEYCODE_BUTTON_11: physical = BTN_L3; break;
    case AKEYCODE_BUTTON_THUMBR: case AKEYCODE_BUTTON_12: physical = BTN_R3; break;
    case AKEYCODE_BUTTON_START: case AKEYCODE_BUTTON_10: case AKEYCODE_BUTTON_MODE: case AKEYCODE_MENU:
        physical = BTN_START; break;
    case AKEYCODE_BUTTON_SELECT: case AKEYCODE_BUTTON_9: physical = BTN_SELECT; break;
    default: break;
    }
    if (physical >= 0) {
        pad.connected = 1;
        // Key repeats are not new presses.
        if (!down || AKeyEvent_getRepeatCount(event) == 0) press(physical, down, pad, spinFrames);
        return true;
    }

    switch (keyCode) {
    case AKEYCODE_DPAD_UP:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x0008;
        else mPhysicalButtons &= ~0x0008;
        break;
    case AKEYCODE_DPAD_DOWN:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x0004;
        else mPhysicalButtons &= ~0x0004;
        break;
    case AKEYCODE_DPAD_LEFT:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x0001;
        else mPhysicalButtons &= ~0x0001;
        break;
    case AKEYCODE_DPAD_RIGHT:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x0002;
        else mPhysicalButtons &= ~0x0002;
        break;
    case AKEYCODE_BACK:
    case AKEYCODE_ESCAPE:
        if (down) {
            if (mobile_ui::UIOverlay::get().isSettingsOpen()) {
                mobile_ui::UIOverlay::get().closeSettings();
            } else {
                mPhysicalButtons |= 0x0010;
            }
        } else {
            mPhysicalButtons &= ~0x0010;
        }
        return true;
    default:
        return false;
    }

    return true;
}

namespace {
    int sButtonMap[BTN_COUNT] = {ACT_JUMP, ACT_SHOOT, ACT_SPIN, ACT_SPIN, ACT_CAMERA, ACT_SPIN,
        ACT_CROUCH, ACT_SHOOT, ACT_CROUCH, ACT_CENTER_POINTER, ACT_PAUSE, ACT_PAUSE};
    // The Wii button an action holds (0: a one-shot action, or none).
    uint32_t heldBits(int action) {
        switch (action) {
        case ACT_JUMP: return 0x0800;    // A: jump / confirm
        case ACT_SHOOT: return 0x0400;   // B: shoot Star Bits / cancel
        case ACT_CROUCH: return 0x2000;  // Nunchuk Z: crouch / ground pound
        case ACT_CAMERA: return 0x4000;  // Nunchuk C: camera behind Mario
        case ACT_PAUSE: return 0x0010;   // Plus: pause
        default: return 0;
        }
    }
}  // namespace

void setButtonMap(const int* map) {
    for (int i = 0; i < BTN_COUNT; i++) {
        const int action = map != nullptr ? map[i] : kDefaultButtonMap[i];
        sButtonMap[i] = action >= 0 && action < ACT_COUNT ? action : ACT_NONE;
    }
}

void GamepadInput::press(int button, bool down, PortPadState& pad, int& spinFrames) {
    const bool was = mHeld[button];
    mHeld[button] = down;
    if (down && !was) {
        const int action = sButtonMap[button];
        if (action == ACT_SPIN) spinFrames = 6;  // a shake of the Wii Remote
        else if (action == ACT_CENTER_POINTER) centerPointer(pad);
    }
    uint32_t bits = 0;
    for (int i = 0; i < BTN_COUNT; i++) if (mHeld[i]) bits |= heldBits(sButtonMap[i]);
    mActionButtons = bits;
}

void GamepadInput::centerPointer(PortPadState& pad) {
    mPointerX = mPointerY = 0.0f;
    pad.pointerX = 0.0f;
    pad.pointerY = 0.0f;
    pad.pointerValid = 1;
    mobile_ui::UIOverlay::get().setPointerPos(0.0f, 0.0f);
}

void GamepadInput::reset() {
    for (bool& held : mHeld) held = false;
    mActionButtons = 0;
    mPhysicalButtons = 0;
    mPhysicalAxisButtons = 0;
    mPhysicalStickX = mPhysicalStickY = 0.0f;
    mPointerStickX = mPointerStickY = 0.0f;
    mCameraStickX = mCameraStickY = 0.0f;
    mMenuStickX = mMenuStickY = 0.0f;
    mStickLatched = false;
}

void GamepadInput::updatePointer(PortPadState& pad, float seconds, bool touchscreenDown) {
    auto& overlay = mobile_ui::UIOverlay::get();
    const auto& settings = overlay.settings();
    // In the game's pointer menus (MR::isStarPointerModeMenuForPort) the left stick steers the
    // pointer too, and the game reads the Nunchuk stick as centred (stickX/stickY), so a menu that
    // pages by stick does not also turn while the cursor moves.
    const bool menu = mGamepadDetected && port_input_menu_pointer() != 0;
    const bool rightPointer = settings.rightStickMode == 1;
    if (mGamepadDetected) {
        pad.rightStickX = !rightPointer && !menu ? mCameraStickX : 0.0f;
        pad.rightStickY = !rightPointer && !menu ? mCameraStickY : 0.0f;
    }
    if (menu != mMenuPointer) {
        __android_log_print(ANDROID_LOG_INFO, "GalaxyInput", "menu pointer %s", menu ? "on" : "off");
        if (menu) {
            // Nothing showed the pointer (right stick on the camera, no touch): start mid-screen.
            if (!pad.pointerValid) overlay.setPointerPos(0.0f, 0.0f);
        } else {
            // Back in play: a stick still held from steering reaches Mario once it is let go.
            mStickLatched = mMenuStickX != 0.0f || mMenuStickY != 0.0f;
            // With the right stick on the camera nothing steers the pointer in play: hide it now,
            // not when the menu's last setPointerPos runs out 120 frames later.
            if (!rightPointer && !touchscreenDown) {
                overlay.releasePointer();
                pad.pointerValid = 0;
            }
        }
        mMenuPointer = menu;
    }
    if (!mGamepadDetected || (!rightPointer && !menu) || touchscreenDown) return;
    if (menu || rightPointer) {
        // Carry on from the shared pointer, so where a touch left it is kept.
        mPointerX = overlay.getPointerX();
        mPointerY = overlay.getPointerY();
    }
    const float dt = std::max(0.0f, std::min(0.05f, seconds));
    const float speed = 2.1f * settings.starStickSensitivity;
    float vx = rightPointer ? mPointerStickX : 0.0f;
    float vy = rightPointer ? mPointerStickY : 0.0f;
    if (menu) {
        vx = std::max(-1.0f, std::min(1.0f, vx + mMenuStickX));
        vy = std::max(-1.0f, std::min(1.0f, vy + mMenuStickY));
    }
    mPointerX = std::max(-1.0f, std::min(1.0f, mPointerX + vx * speed * dt));
    mPointerY = std::max(-1.0f, std::min(1.0f, mPointerY - vy * speed * dt));
    pad.pointerX = mPointerX;
    pad.pointerY = mPointerY;
    pad.pointerValid = 1;
    pad.pointerDist = 2.0f;
    overlay.setPointerPos(mPointerX, mPointerY);
}

}  // namespace mobile_input
