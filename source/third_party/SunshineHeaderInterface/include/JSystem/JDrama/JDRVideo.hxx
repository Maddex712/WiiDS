#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

namespace JDrama {

    class TVideo {
    public:
        TVideo();

        void setNextXFB(const void *);
        void waitForRetrace(u16);
#if __SIZEOF_POINTER__ == 8
#else
        GXRenderModeObj _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        GXRenderModeObj _3C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u32 _78;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[128];
        void *mNextFB;
#else
        void *mNextFB;
#endif
      // 7C
        s32 mTicks;         // 80
        s32 mRetraceCount;  // 84
    };

}  // namespace JDrama