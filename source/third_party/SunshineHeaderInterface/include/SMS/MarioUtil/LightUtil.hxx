#pragma once

#include <JDrama/JDRViewObj.hxx>
#include <JGeometry/JGMVec.hxx>
#include <JUtility/JUTColor.hxx>

class TLightCommon : public JDrama::TViewObj {
public:
    TLightCommon(const char *name);
    ~TLightCommon() override;

    void loadAfter() override;
    void perform(u32, JDrama::TGraphics *) override;

    virtual JUtility::TColor getAmbColor(int index) const;
    virtual JUtility::TColor getLightColor(int index) const;
    virtual TVec3f getLightPosition(int index);

    // Applies the light to the graphics object
    virtual void setLight(const JDrama::TGraphics *graphics, int index);

    f32 mSoftness;
#if __SIZEOF_POINTER__ == 8
#else
  // _10
    TVec3f _14;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_2[12];
     // _14
    int mGlobalAmbColorIndex;
#else
     // _14
    int mGlobalAmbColorIndex;
#endif

    int mGlobalLightColorIndex;
    bool mUseLocalLights;                // _28
    JUtility::TColor mAmbColorAry[2];    // _29
    JUtility::TColor mLightColorAry[4];
#if __SIZEOF_POINTER__ == 8
#else
  // _31
    bool _41;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_8[3];
    TVec3f mLightPosAry[4];
#else
    u8 _pc32_8[2];
    TVec3f mLightPosAry[4];
#endif
  // _44

    static void *mAmbAry;
    static void *mLightAry;
    static void *mLightPos;
} /*__attribute__((packed))*/;