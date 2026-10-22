#include "GamepadInput.h"
#include <android/keycodes.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <utility>

struct AInputEvent { int source = AINPUT_SOURCE_JOYSTICK; int key = 0; int action = 0; float axes[64]{}; };
extern "C" int32_t AInputEvent_getSource(const AInputEvent* e) { return e->source; }
extern "C" int32_t AKeyEvent_getKeyCode(const AInputEvent* e) { return e->key; }
extern "C" int32_t AKeyEvent_getAction(const AInputEvent* e) { return e->action; }
extern "C" float AMotionEvent_getAxisValue(const AInputEvent* e, int32_t axis, size_t) { return e->axes[axis]; }

int main() {
    mobile_input::GamepadInput input;
    PortPadState pad{};
    int spin = 0, checks = 0;
    auto check = [&](bool value) { assert(value); ++checks; };
    auto key = [&](int code, bool down) {
        AInputEvent e; e.key = code; e.action = down ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;
        return input.handleKeyEvent(&e, pad, spin);
    };
    for (auto pair : {std::pair<int, unsigned>{AKEYCODE_BUTTON_A, 0x0800},
          {AKEYCODE_BUTTON_B, 0x0400}, {AKEYCODE_BUTTON_L1, 0x4000},
          {AKEYCODE_BUTTON_L2, 0x2000}, {AKEYCODE_BUTTON_R2, 0x0400},
          {AKEYCODE_BUTTON_START, 0x0010}}) {
        check(key(pair.first, true)); check(input.buttons() == pair.second);
        check(key(pair.first, false)); check(input.buttons() == 0);
    }
    for (int code : {AKEYCODE_BUTTON_X, AKEYCODE_BUTTON_Y, AKEYCODE_BUTTON_R1}) {
        spin = 0; key(code, true); check(spin == 6); check(input.buttons() == 0); key(code, false);
    }
    pad.pointerX = .6f; pad.pointerY = -.5f;
    key(AKEYCODE_BUTTON_THUMBR, true);
    check(pad.pointerX == 0 && pad.pointerY == 0 && pad.pointerValid == 1);
    AInputEvent motion;
    motion.axes[AMOTION_EVENT_AXIS_X] = .8f;
    motion.axes[AMOTION_EVENT_AXIS_Y] = -.8f;
    motion.axes[AMOTION_EVENT_AXIS_RTRIGGER] = 1;
    motion.axes[AMOTION_EVENT_AXIS_LTRIGGER] = 1;
    spin = 0;
    check(input.handleMotionEvent(&motion, pad, spin, false));
    check(input.buttons() == 0x2400); check(spin == 0);
    check(input.stickX() > 0 && input.stickY() > 0);
    motion.axes[AMOTION_EVENT_AXIS_RTRIGGER] = 0;
    input.handleMotionEvent(&motion, pad, spin, false);
    check(input.buttons() == 0x2000);
    key(AKEYCODE_BUTTON_B, true);
    motion.axes[AMOTION_EVENT_AXIS_LTRIGGER] = 0;
    input.handleMotionEvent(&motion, pad, spin, false);
    check(input.buttons() == 0x0400);
    key(AKEYCODE_BUTTON_B, false); check(input.buttons() == 0);
    input.reset(); check(input.buttons() == 0 && input.stickX() == 0 && input.stickY() == 0);
    motion.axes[AMOTION_EVENT_AXIS_Z] = 1;
    input.handleMotionEvent(&motion, pad, spin, false);
    // A held analog stick produces no new events. The physical pointer must
    // remain valid even when the preceding touch update clears it.
    for (int i = 0; i < 100; ++i) {
        pad.pointerValid = 0; pad.pointerX = pad.pointerY = 0;
        input.updatePointer(pad, 1.0f / 60.0f, false);
    }
    check(pad.pointerX == 1 && pad.pointerValid == 1);
    motion.axes[AMOTION_EVENT_AXIS_Z] = 0;
    input.handleMotionEvent(&motion, pad, spin, false);
    input.updatePointer(pad, 1, false); check(pad.pointerX == 1);
    key(AKEYCODE_BUTTON_THUMBR, true);
    input.updatePointer(pad, 1.0f / 60, false); check(pad.pointerX == 0 && pad.pointerY == 0);
    motion.axes[AMOTION_EVENT_AXIS_Z] = -1;
    input.handleMotionEvent(&motion, pad, spin, false);
    input.updatePointer(pad, 1.0f / 60, true); check(pad.pointerX == 0);
    input.reset(); input.updatePointer(pad, 1.0f / 60, false); check(pad.pointerX == 0);
    check(!key(AKEYCODE_VOLUME_UP, true));
    motion.source = AINPUT_SOURCE_TOUCHSCREEN;
    check(!input.handleMotionEvent(&motion, pad, spin, false));
    printf("PASS: %d actual GamepadInput source assertions\n", checks);
}
