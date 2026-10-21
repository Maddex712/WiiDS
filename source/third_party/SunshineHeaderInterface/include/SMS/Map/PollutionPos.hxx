#pragma once

#include <Dolphin/types.h>

class TPollutionPos {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};