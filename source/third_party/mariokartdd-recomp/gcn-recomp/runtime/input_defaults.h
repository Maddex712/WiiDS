// A game's own defaults for controls.txt's settings (input_map.h), set from the game
// project's static initialisers: what its code does with the pad, which the file can
// still override. Kept apart from input_map.cpp, which needs SDL, so that a game's hooks
// link into the headless benchmark too.
#pragma once

// `key` as controls.txt spells it, e.g. "stick_x_min".
void input_set_default(const char* key, float value);
float input_default(const char* key, float fallback);
