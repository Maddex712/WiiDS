#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DScreen.hxx>

class TOptionSubtitleUnit {
public:
    J2DScreen *mScreen;
#if __SIZEOF_POINTER__ == 8
#else
  // _0
    u32 *_4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _9;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u8 _B;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u32 *_C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _10;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[32];
#else
#endif
};