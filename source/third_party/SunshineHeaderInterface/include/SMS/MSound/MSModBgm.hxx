#pragma once

#include <Dolphin/types.h>
#include <JSystem/JAudio/JAISound.hxx>

class MSModBgm {
public:
    void changeTempo(u8, u8);
    void loop();
    JAISound *modBgm(u8, u8);

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};