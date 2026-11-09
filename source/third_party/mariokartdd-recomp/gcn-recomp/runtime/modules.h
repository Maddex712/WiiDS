// Code the game loads from its data files at run time. See modules.cpp.
#pragma once
#include <cstdint>

void modules_init();
// `addr` is in no recompiled function: find which module the game loaded there, by its
// bytes, and remember where. True when one was found.
bool modules_bind(uint32_t addr);
