#pragma once

#include <JSystem/J3D/J3DAnimation.hxx>
#include <JSystem/JDrama/JDRActor.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>

class TSunModel : public JDrama::TActor {
public:
    TSunModel(bool, const char *name);
    virtual ~TSunModel();

    virtual void load(JSUMemoryInputStream &stream) override;
    virtual void perform(u32, JDrama::TGraphics *) override;

    void calcDispRatioAndScreenPos();
    void calcOtherFPosFromCenterAndRadius(JGeometry::TVec2<f32> *, const JGeometry::TVec2<f32> &,
                                          f32);
    void getZBufValue();
#if __SIZEOF_POINTER__ == 8
#else
    u32 _44[3];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[24];
    J3DFrameCtrl mFrameCtrl;
#else
    J3DFrameCtrl mFrameCtrl;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x0050
    u32 _64[0x1C / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[32];
    u8 mBlindingStrength;
#else
    u8 mBlindingStrength;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x0080
    u32 _84[0x20 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JGeometry::TVec2<s16> _B4[11];
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x00B4
    JGeometry::TVec2<f32> _F8[11];
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x00F8
    u32 _150[0x5C / 4];
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _1AC;
#endif
  // END

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[263];
#else
    u8 _pc32_tail[3];
#endif
};