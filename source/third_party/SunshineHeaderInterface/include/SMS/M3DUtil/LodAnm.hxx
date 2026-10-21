#pragma once

#include <Dolphin/types.h>

class TLiveActor;

class TLodAnmIndex {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};

class TLodAnm {
public:
    TLodAnm(TLiveActor *, const TLodAnmIndex *, int, f32);
    bool setBckAnm(int);
    bool setBtpAnm(int);

    TLiveActor *mLiveActor;
    TLodAnmIndex *mLodAnmIndex;
    int mIndex;
#if __SIZEOF_POINTER__ == 8
#else
    f32 _0C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[4];
    u32 mLodNum;
#else
    size_t mLodNum;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    s32 _14;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // maybe type?
    s32 _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    s32 _1C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[20];
#else
#endif
};