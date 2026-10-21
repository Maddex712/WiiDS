#pragma once

#include <Dolphin/types.h>

class TCameraMarioData {
public:
    void calcAndSetMarioData();
    bool isMarioClimb(u32) const;
    bool isMarioGoDown() const;
    bool isMarioIndoor() const;
    bool isMarioLeanMirror() const;
    bool isMarioRocketing() const;
    bool isMarioSlider() const;
#if __SIZEOF_POINTER__ == 8
#else
    f32 _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _14;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _1C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[32];
#else
#endif
};

extern TCameraMarioData *gpCameraMario;