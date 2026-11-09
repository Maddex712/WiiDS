// Controls from a text file: any joystick SDL sees -- a wheel, pedals, a button box, a
// gamepad without a mapping -- and any key, bound to the GameCube pad.
//
// The file (controls.txt in the current directory, or --controls=<path>) has one binding
// per line, `<pad control> = <source> [options]`, # to the end of a line a comment:
//
//   stick_x = "Simucube 2 Pro" axis 0 gain 2.5
//   A       = "Heusinkveld Sprint Pedals" axis 1
//   B       = "Heusinkveld Sprint Pedals" axis 2
//   R       = "Simucube 2 Pro" button 5
//   start   = key Return
//
// Pad controls:
//   A B X Y Z start dpad_up dpad_down dpad_left dpad_right   digital
//   L R                     the analog trigger, and its digital click past the threshold
//   stick_x stick_y cstick_x cstick_y                         a whole axis, -1 to +1
//   stick_left stick_right stick_up stick_down, cstick_...    one direction, 0 to 1
//
// Sources:
//   key <name>                    an SDL scancode name: X, Return, Space, "Left Shift"
//   "<device>" button <n>
//   "<device>" hat <n> up|down|left|right
//   "<device>" axis <n>           the whole axis. Onto a one-way control (a button, a
//                                 trigger, stick_left...) its whole travel is 0 to 1,
//                                 which is what a pedal resting at one end wants.
//   "<device>" axis <n>+ / <n>-   one half of an axis, from its centre outwards
// A device is named as SDL names it; "<name> #2" is the second of two with that name.
//
// Options, after the source:
//   invert          turn the axis round (a pedal that rests at the top of its range)
//   deadzone <f>    the fraction of the travel that reads as nothing (default 0)
//   gain <f>        multiplies the value before it is clamped: 2 is full lock at half
//                   the wheel's rotation (default 1)
//   threshold <f>   where a digital control is pressed (default 0.5; L and R 0.85)
//
// Several bindings may share a pad control: buttons are pressed if any is, one-way values
// take the largest, and a whole stick axis is the sum of its bindings, so stick_left and
// stick_right on two buttons cancel. A stick or trigger the file leaves at rest is left to
// the default keyboard and gamepad mapping, which stays on except for a device the file
// names.
//
// --list-controls runs input_map_probe() instead of the game: it prints the devices and,
// for each control pressed or moved, the source to paste into the file.
//
// Force feedback (ffb.h) goes to the device stick_x is bound to, unless the file says
// otherwise:
//   ffb = off                     none
//   ffb_device = "<device>"       another device
//   ffb_<setting> = <number>      FfbSettings without its prefix: ffb_gain = 0.5
//
// Stick ranges: `stick_x_min = 19`, `stick_x_max = 75` and the same for stick_y, cstick_x
// and cstick_y, in the pad's units from the centre. A bound stick axis is spread from min,
// the first offset the game responds to, to max, where it is at full, so that the moment
// the binding leaves its own dead zone the game reacts. A whole axis on a stick axis has a
// dead zone of 0.005 unless the line gives one. A game supplies its own ranges
// (input_defaults.h); by default they are 0 and 100.
#pragma once
#include "hw/pad.h"

union SDL_Event;

// Reads the file; true if there is none. Problems are reported on stderr, line by line,
// and the lines skipped.
bool input_map_load(const char* path);
// Devices coming and going. Every SDL event may be passed.
void input_map_event(const SDL_Event& e);
// Whether the file names the joystick at this SDL device index (the default gamepad
// mapping leaves such a device alone).
bool input_map_binds_index(int device_index);
// The file's bindings, merged into `p`.
void input_map_apply(PadState& p);
// Stops the wheel's force feedback (ffb.h), before the program exits.
void input_map_shutdown();
// The --list-controls loop. SDL must not be initialised yet. Returns the exit code.
int input_map_probe();
// --test-ffb: the file's wheel with its force feedback and no game, showing where the
// steering is. SDL must not be initialised yet. Returns the exit code.
int input_map_test_ffb(const char* path);
