#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>

enum EnumCamShakeMode {
    UnknownShake3  = 0x3,
    UnknownShake5  = 0x5,
    UnknownShake8  = 0x8,
    UnknownShake9  = 0x9,
    UnknownShakeE  = 0xE,
    UnknownShakeF  = 0xF,
    UnknownShake10 = 0x10,
    UnknownShake11 = 0x11,
    UnknownShake18 = 0x18,
    UnknownShake27 = 0x27
};

class TCameraShake {
public:
    TCameraShake();

    void execShake(const TVec3f &, TVec3f *, TVec3f *);
    void *getUseShakeData();
    void keepShake(EnumCamShakeMode mode, f32 strength);
    void startShake(EnumCamShakeMode mode, f32 strength);
#if __SIZEOF_POINTER__ == 8
#else
    u16 _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _2;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4;
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
    u16 _A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    f32 _10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _14;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _1A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    f32 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _24;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _26;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    f32 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _2C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _32;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _34[0x6A8 - 0x34];
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1872];
#else
#endif
};

extern TCameraShake *gpCameraShake;