#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>

class RumbleMgr {
public:
    RumbleMgr(bool p1, bool p2, bool p3, bool p4);

    void finishPause();
    void init();
    void reset();
    void setActive(bool active);
    void start(int, int, Vec *);
    void start(int, int, f32 *);
    void start(int, Vec *);
    void start(int, f32 *);
    void startPause();
    void stop(int);
    void stop();
    void update();
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
    u8 _B;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _C[0x1C - 0xC];
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _24;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 *_2C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[88];
#else
#endif
};

extern RumbleMgr *SMSRumbleMgr;