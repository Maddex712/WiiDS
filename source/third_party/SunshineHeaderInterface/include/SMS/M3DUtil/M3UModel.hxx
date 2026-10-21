#pragma once

#include <Dolphin/types.h>
#include <JSystem/J3D/J3DModel.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>

class M3UMtxCalcSetInfo {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};

class M3UModel {
public:
    virtual void changeMtxCalcAnmTransform(int, u8);
    virtual void changeAnmTexPattern(int, u8);
    virtual void setMtxCalc(const M3UMtxCalcSetInfo &);
    virtual void updateIn();
    virtual void updateOut();
    virtual void entryIn();
    virtual void entryOut();

    void perform(u32, JDrama::TGraphics *);
    void updateInMotion();
    void updateInTexPatternAnm();
#if __SIZEOF_POINTER__ == 8
#else
    void *_04;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[8];
                 // 0x0000
    J3DModel *mModel;
#else
                 // 0x0000
    J3DModel *mModel;
#endif
          // 0x0008
    J3DFrameCtrl *mFrameCtrl;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x000C
    u32 _10[0x2C / 4];
#endif

    u32 mLightID;
#if __SIZEOF_POINTER__ == 8
#else
#endif

    u32 mLightType;  // _44

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[24];
#else
#endif
};

class M3UModelCommon {
public:
    virtual M3UMtxCalcSetInfo *getMtxCalc(const M3UMtxCalcSetInfo &) const;
};