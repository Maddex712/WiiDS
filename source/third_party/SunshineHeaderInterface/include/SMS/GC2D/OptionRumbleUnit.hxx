#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DScreen.hxx>

class TOptionRumbleUnit {
public:
    J2DScreen *mScreen;
#if __SIZEOF_POINTER__ == 8
#else
  // _0
    u32 *_4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
             // TExPane*
    u8 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _9;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _B;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u32 _C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 *_10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    // TPatternAnmControl*
    u32 *_14;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    // TPatternAnmControl*
    u32 *_18;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_10[48];
    // some class
    u32 mState;
#else
    // some class
    u32 mState;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _1C
    u32 *_20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    // copy of _18
    f32 _24;
#endif


    /*
    f32 mAnimSpeed;      //0x0088
    f32 mAnimFrame;      //0x008C
    s32 mXCoordA;          //0x0090
    s32 mYCoordA;          //0x0094
    s32 mXCoordB;          //0x0098
    s32 mYCoordB;          //0x009C
    f32 mAnimSizeFactor; //0x00A0
    */

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
#endif
};