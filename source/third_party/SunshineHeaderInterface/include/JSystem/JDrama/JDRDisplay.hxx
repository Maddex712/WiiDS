#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRVideo.hxx>

namespace JDrama {

    class TDisplay {
    public:
        TDisplay(u16 retraceCount, void *, void *, const GXRenderModeObj &);

        void startRendering();
        void endRendering();
#if __SIZEOF_POINTER__ == 8
#else
        bool _00;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[8];
        GXRenderModeObj mRenderObj;
#else
        GXRenderModeObj mRenderObj;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u8 _40[0xC];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[20];
        u16 mRetraceCount;
#else
        u16 mRetraceCount;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u16 _4E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u32 _50[0x10 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_6[22];
        JDrama::TVideo *mVideo;
#else
        JDrama::TVideo *mVideo;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x0060
        u16 _64;
#endif

    
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[2];
#else
#endif
};

}  // namespace JDrama