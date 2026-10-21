#pragma once

#include <JDrama/JDRRect.hxx>
#include <JDrama/JDRViewObj.hxx>
#include <JUtility/JUTColor.hxx>
#include <SMS/Player/MarioGamePad.hxx>

class TSunGlass : public JDrama::TViewObj {
public:
    TSunGlass(const char *);
    ~TSunGlass() override;

    void load(JSUMemoryInputStream &) override;
    void loadAfter() override;
    void perform(u32, JDrama::TGraphics *) override;

    virtual void draw(const JDrama::TRect &, JUtility::TColor color);
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_0[4];
    TMarioGamePad *mGamepad;
#else
    TMarioGamePad *mGamepad;
#endif
  // _10
    JUtility::TColor mColor;
#if __SIZEOF_POINTER__ == 8
#else
  // _14
    u16 _18;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[2];
    u16 mBlendFlags;
#else
    u16 mBlendFlags;
#endif
    // _1A
    u8 mToAlpha;        // _1C
    u8 mFromAlpha;      // _1D
    u8 mSelectorA;      // _1E
    u8 mSelectorB;
#if __SIZEOF_POINTER__ == 8
#else
      // _1F
    u16 _20;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_9[2];
            // _20
    u16 mLerpTimerMax;
#else
            // _20
    u16 mLerpTimerMax;
#endif
  // _22
    u16 mLerpTimer;     // _24
    bool mIsUpdating;   // _26

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};