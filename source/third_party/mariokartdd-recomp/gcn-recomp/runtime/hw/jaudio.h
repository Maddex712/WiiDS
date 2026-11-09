#pragma once
#include <cstdint>

// The JAudio synthesizer ucode: the DSP program that Nintendo's JSystem audio library
// boots instead of AX. See jaudio.cpp.
bool jaudio_is_ucode(uint32_t hash);
void jaudio_boot(uint32_t hash, bool resume);

enum class JAudioNext {
    Stay,      // the ucode keeps the DSP
    NewUcode,  // the mails that follow describe another ucode for the boot ROM to load
    Reset,     // back to the boot ROM
};
JAudioNext jaudio_mail(uint32_t mail);
