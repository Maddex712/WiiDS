#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRRect.hxx>

namespace JDrama {

    class TGraphics {
    public:
        void setViewport(const TRect &, f32, f32);

        u8 _00[0x20];
#if __SIZEOF_POINTER__ == 8
#else
        bool _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u8 _21[0x54 - 0x21];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[60];
        TRect mViewPortSpace;
#else
        TRect mViewPortSpace;
#endif

    };

}  // namespace JDrama