#pragma once

#include <JSystem/J3D/J3DAnimation.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>

class TLensGlow : public JDrama::TViewObj {
public:
    TLensGlow(bool, const char *name);
    virtual ~TLensGlow();

    virtual void perform(u32, JDrama::TGraphics *) override;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _10[3];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[28];
    J3DFrameCtrl mGlowBtk;
#else
    J3DFrameCtrl mGlowBtk;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _30;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[8];
    J3DFrameCtrl mGlowBrk;
#else
    J3DFrameCtrl mGlowBrk;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _48[0x20 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[32];
    f32 mViewingSize;
#else
    f32 mViewingSize;
#endif
    // 0x0068
    f32 mPeekingSize;    // 0x006C
    f32 mSizeLerpSpeed;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0070
    u32 _70[3];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_9[12];
    f32 mOffsetFromPosX;
#else
    f32 mOffsetFromPosX;
#endif
  // 0x0080
    f32 mOffsetFromPosY;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0084
    u32 _88[2];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_12[8];
    f32 mOffsetReturnSpeed;
#else
    f32 mOffsetReturnSpeed;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x0090
    u32 _94;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
#endif
};