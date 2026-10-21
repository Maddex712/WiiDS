#pragma once
#include <SDL.h>

namespace gx {
// SDL Android maps KEYCODE_BUTTON_1..12 to raw buttons 20..31 and converts
// HAT_X/Y to raw D-pad buttons 11..14. Its generated controller mapping only
// accepts named A/B/X/Y buttons, so this legacy adapter needs an explicit one.
// Face/shoulder/right-stick assignments follow SDL's existing Linux profile
// for 0810:0001 (PS1 Controller), translated to the Android raw-button ABI.
inline bool androidLegacyControllerMapping(SDL_JoystickGUID guid, Uint16 vendor, Uint16 product,
        int axes, int buttons, int hats, Uint16 buttonMask, Uint16 axisMask,
        bool alreadyMapped, char* mapping, size_t capacity) {
    if (alreadyMapped || vendor != 0x0810 || product != 0x0001 || axes != 4 ||
        buttons < 32 || hats != 0 || buttonMask != 0x7800 || axisMask != 0x000f)
        return false;
    char id[33]; SDL_JoystickGetGUIDString(guid, id, sizeof id);
    const int n = SDL_snprintf(mapping, capacity,
        "%s,Legacy USB Gamepad 0810:0001,"
        "a:b22,b:b21,x:b23,y:b20,back:b28,start:b29,"
        "leftshoulder:b26,rightshoulder:b27,lefttrigger:b24,righttrigger:b25,"
        "leftstick:b30,rightstick:b31,leftx:a0,lefty:a1,rightx:a3,righty:a2,"
        "dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,", id);
    return n > 0 && size_t(n) < capacity;
}
} // namespace gx
