#pragma once

#include <Dolphin/types.h>

class TMarioSoundValues {
public:
    TMarioSoundValues();
#if __SIZEOF_POINTER__ == 8
#else
    u32 _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _14;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _22;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _24;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _26;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _2A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _2B;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _2C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[45];
#else
#endif
};