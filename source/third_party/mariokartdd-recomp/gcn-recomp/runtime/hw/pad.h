#pragma once
#include <cstdint>

// PAD_BUTTON_* bit layout (as returned in PADStatus.button)
enum : uint16_t {
    PAD_LEFT = 0x0001, PAD_RIGHT = 0x0002, PAD_DOWN = 0x0004, PAD_UP = 0x0008,
    PAD_Z = 0x0010, PAD_R = 0x0020, PAD_L = 0x0040,
    PAD_A = 0x0100, PAD_B = 0x0200, PAD_X = 0x0400, PAD_Y = 0x0800, PAD_START = 0x1000,
};

struct PadState {
    bool connected = false;
    uint16_t buttons = 0;
    uint8_t stick_x = 0x80, stick_y = 0x80;
    uint8_t cstick_x = 0x80, cstick_y = 0x80;
    uint8_t trig_l = 0, trig_r = 0;
};

void pad_set_state(int chan, const PadState& s);
// Whether the game has the pad's rumble motor running: the low bits of the poll command
// in the channel's output buffer (PADControlMotor).
bool pad_motor(int chan);
