#pragma once

#include <Dolphin/types.h>
#include <JSystem/JAudio/JAISound.hxx>

class JAIAnimeSound {
public:
    JAIAnimeSound();

    void startAnimSound(void *, u32, JAISound **, u32 *, u8);  // JAIActor *
    void setSpeedModifySound(JAISound *, u32 *, f32);
#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_0[4];
          // JAIAnimeFrameSoundData *

    u8 _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _1;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _2;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u8 _3;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _D;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u8 _F;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u32 _10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _14;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _19;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _1A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u8 _1B;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u32 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _24;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _25;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _26;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u8 _27;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u32 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _2C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _31;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _32;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u8 _33;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u32 _34;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _38[0x94 - 0x38];
#endif

};