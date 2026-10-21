#pragma once

#include <JSystem/J2D/J2DPicture.hxx>
#include <JSystem/J2D/J2DSetScreen.hxx>
#include <JSystem/J2D/J2DTextBox.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JUtility/JUTTexture.hxx>
#include <SMS/G2D/BoundPane.hxx>
#include <SMS/G2D/ExPane.hxx>
#include <SMS/GC2D/SelectGrad.hxx>

class TSelectDir;
class TSelectShineManager;

class TSelectMenu : public JDrama::TViewObj {
public:
    TSelectMenu(const char *);
    virtual ~TSelectMenu();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void loadAfter() override;
    virtual void perform(u32, JDrama::TGraphics *) override;

    u32 mState;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _14[0xC / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_2[16];
    J2DSetScreen *mScreen;
#else
    J2DSetScreen *mScreen;
#endif
           // 0x0020
    TExPane *mMask1;                 // 0x0024
    TExPane *mMask2;                 // 0x0028
    J2DPane *mMap;                   // 0x002C
    J2DPane *mPaneDynName;           // 0x0030
    TBoundPane *mBoundPaneDynName1;  // 0x0034
    TBoundPane *mBoundPaneDynName2;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0038
    u32 _3C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_10[8];
    TExPane *mS_0;
#else
    TExPane *mS_0;
#endif
             // 0x0040
    J2DTextBox *mSttxTextBox;  // 0x0044
    J2DPicture *mPicture0;     // 0x0048
    J2DPicture *mPicture1;
#if __SIZEOF_POINTER__ == 8
#else
     // 0x004C
    u32 _50;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    bool _54;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_16[3];
    JUTRect _58;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_17[32];
    TExPane *m0_0;
#else
    TExPane *m0_0;
#endif
             // 0x0068
    J2DTextBox *m0ttxTextBox;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x006C
    u32 _70[0x10 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_20[32];
    JUTTexture *mShineCountNumberTex[8];
#else
    JUTTexture *mShineCountNumberTex[8];
#endif
  // 0x0080
    J2DPane *mI_0;                        // 0x00A0
    J2DPane *mSc_0;                       // 0x00A4
    JUTTexture *mCoinCountNumberTex[10];  // 0x00A8
    JUTTexture *mScMark1;
    JUTTexture *mScMark0;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _D8[0x2C / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_27[88];
    J2DPane *mA_l;
#else
    J2DPane *mA_l;
#endif
  // 0x0104
    J2DPane *mA_r;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0108
    bool _10C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _10D;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_31[4];
    JUTRect mLeftArrowPos;
#else
    u8 _pc32_31[2];
    JUTRect mLeftArrowPos;
#endif
          // 0x0110
    JUTRect mRightArrowPos;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_33[4];
         // 0x0120
    TSelectShineManager *mManager;
#else
         // 0x0120
    TSelectShineManager *mManager;
#endif
  // 0x0130
    TSelectDir *mDir;
#if __SIZEOF_POINTER__ == 8
#else
               // 0x0134
    u8 _138;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_36[1];
    u8 mCloseTime;
#else
    u8 mCloseTime;
#endif

    u8 mAreaID;                         // 0x013A
    s8 mEpisodeID;                      // 0x013B
    s8 mEpisodeCount;
#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_40[1];
                   // 0x013C
    u16 _13E;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_41[3];
    JUtility::TColor mSelectedColor;
#else
    JUtility::TColor mSelectedColor;
#endif
    // 0x0140
    JUtility::TColor mUnselectedColor;  // 0x0144
    u8 mSelectedFlickerTime;            // 0x0148
    u8 mUnselectedAlphaMix;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_45[2];
             // 0x0149
    f32 mAnmDeltaTime;
#else
    u8 _pc32_45[2];
             // 0x0149
    f32 mAnmDeltaTime;
#endif
                  // 0x014C
    u8 mSelectionState[8];              // 0x0150
    void *mStageBMGData;                // 0x0158
    void *mScenarioBMGData;
#if __SIZEOF_POINTER__ == 8
#else
             // 0x015C
    JUtility::TColor _160;
#endif

#if __SIZEOF_POINTER__ == 8
#else
              // 0x0160
    JUtility::TColor _164;
#endif

#if __SIZEOF_POINTER__ == 8
#else
              // 0x0164
    s16 _168;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    s16 _16A;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_53[12];
    s16 mCloseDuration;
#else
    s16 mCloseDuration;
#endif

};