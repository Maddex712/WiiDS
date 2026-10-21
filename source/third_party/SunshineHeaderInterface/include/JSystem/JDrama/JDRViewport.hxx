#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>

namespace JDrama {

    class TViewport : public TViewObj {
    public:
        TViewport(const TRect &, const char *);
        virtual ~TViewport();

        virtual void load(JSUMemoryInputStream &stream);
        virtual void perform(u32, TGraphics *);

        TRect mViewportRect;
#if __SIZEOF_POINTER__ == 8
#else
  // 10
        u16 _20;
#endif

    
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[2];
#else
#endif
};

}  // namespace JDrama