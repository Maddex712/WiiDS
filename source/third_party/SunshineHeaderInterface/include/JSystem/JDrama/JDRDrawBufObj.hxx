#pragma once

#include <Dolphin/types.h>
#include <JSystem/J3D/J3DDrawBuffer.hxx>
#include <JSystem/JDrama/JDRPlacement.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>

namespace JDrama {

    class TDrawBufObj : public TViewObj {
    public:
        TDrawBufObj();
        TDrawBufObj(u32, u32, const char *);
        virtual ~TDrawBufObj();

        virtual void load(JSUMemoryInputStream &stream);
        virtual void perform(u32, TGraphics *);

        J3DDrawBuffer *mDrawBuffer;  // 10
        u32 mBufferSize;             // 14
        u32 _18;                     // init'd to 7
    };

}  // namespace JDrama