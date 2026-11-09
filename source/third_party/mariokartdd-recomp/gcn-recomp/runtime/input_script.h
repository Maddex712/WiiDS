// Scripted controller input, for driving the game with no human at the controls.
//
// Shared by every frontend -- the SDL one, the headless EGL renderer and the
// benchmark -- since anything past the title screen needs button presses, and the
// headless targets have no other way to produce them.
#pragma once
#include "hw/pad.h"

// Parses "frame:BUTTON[+BUTTON]:duration,..." where frame is the presented-frame
// count at which to press, e.g. "900:START:10,1200:A:10".
// Buttons: A B X Y Z L R START UP DOWN LEFT RIGHT; stick: SL SR SU SD.
// GCN_INPUT_LOG=1 logs each press as it fires.
//
// `spec` null or empty falls back to the GCN_INPUT environment variable, which an
// Android app has no way to set -- it passes the script in directly instead.
void input_script_init(const char* spec = nullptr);

// Overlays any press that is active at the current presented-frame count.
void input_script_apply(PadState& p);
