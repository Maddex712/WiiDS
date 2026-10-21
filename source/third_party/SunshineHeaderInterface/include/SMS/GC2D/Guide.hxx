#pragma once

#include <JSystem/J2D/J2DPicture.hxx>
#include <JSystem/J2D/J2DSetScreen.hxx>
#include <JSystem/J2D/J2DTextBox.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/G2D/BoundPane.hxx>
#include <SMS/G2D/ExPane.hxx>

class TGuide : public JDrama::TViewObj {
public:
    TGuide(const char *);
    virtual ~TGuide();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void loadAfter() override;
    virtual void perform(u32, JDrama::TGraphics *) override;

    s32 mState;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0010
    u32 _14[0xA8 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_2[168];
    J2DSetScreen *mScreen;
#else
    J2DSetScreen *mScreen;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x00BC
    u32 _C0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _C4;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[9];
    bool mIsSetUp;
#else
    bool mIsSetUp;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_6[6];
                  // 0x00C5
    u32 *mTextures[10];
#else
    u8 _pc32_6[2];
                  // 0x00C5
    u32 *mTextures[10];
#endif
             // 0x00C8 - JUTTexture *
    J2DPane *mPane_ss_i;            // 0x00F4
    J2DPane *mPane_ss[2];           // 0x00F8
    J2DPane *mPane_sq_i;            // 0x0100
    J2DPane *mPane_sq[2];           // 0x0104
    J2DPane *mPane_sc[3];           // 0x010C
    J2DPane *mPane_sc_s;            // 0x0118
    J2DPane *mPane_sb[2];           // 0x011C
    J2DTextBox *mTextBox1;          // 0x0124
    TExPane *mExPanes[2];           // 0x0128
    J2DPicture *mPictureSun2;       // 0x0134
    J2DPicture *mPictureShip2;      // 0x0138
    J2DPicture *mPicturePalmTree2;  // 0x013C
    J2DPicture *mPicturePalmTree1;  // 0x0140
    J2DPicture *mPictureFish2;      // 0x0144
    J2DPane *mPane_11;              // 0x0148
    J2DPane *mPane_14;              // 0x014C
    J2DPane *mPane_15;              // 0x0150
    J2DPane *mPane_12;              // 0x0154
    J2DPane *mPane_19;
#if __SIZEOF_POINTER__ == 8
#else
              // 0x0158
    u32 _15C[3];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_27[20];
    J2DPane *mPanes[13];
#else
    J2DPane *mPanes[13];
#endif
  // 0x0168
    J2DPane *mPane0;
#if __SIZEOF_POINTER__ == 8
#else
      // 0x019C
    u32 _1A0[8];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_30[60];
    TExPane *mExPanes0[13];
#else
    TExPane *mExPanes0[13];
#endif
      // 0x01C0
    TExPane *mLeftWindowExPane;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x01F4
    u32 _1F8[8];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_33[64];
    JUTRect mRects[13];
#else
    JUTRect mRects[13];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_34[8];
  // 0x0218
    JUTRect mBorder0;
#else
  // 0x0218
    JUTRect mBorder0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    // ? 0x02E8
    u32 _2F8[0x80 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_36[128];
    TExPane *mExPanes1[13];
#else
    TExPane *mExPanes1[13];
#endif
  // 0x0378
    TExPane *mExPane1;
#if __SIZEOF_POINTER__ == 8
#else
       // 0x03AC
    u32 _3B0[0x64 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_39[172];
    TExPane *mPane_unk0;
#else
    TExPane *mPane_unk0;
#endif
        // 0x0424
    TExPane *mPane_unk1;        // 0x0428
    s16 mSelectedRectIndex;
#if __SIZEOF_POINTER__ == 8
     // 0x042C
    J2DPane *mPaneMarioIcon;
#else
    u8 _pc32_42[2];
     // 0x042C
    J2DPane *mPaneMarioIcon;
#endif
    // 0x0430
    JUTRect mBorder1;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_44[2];
           // ? 0x0434
    TBoundPane *mBoundPane_20;
#else
           // ? 0x0434
    TBoundPane *mBoundPane_20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x0444
    u32 _448;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_46[8];
    J2DPane *mPane_pn[10];
#else
    J2DPane *mPane_pn[10];
#endif
  // 0x044C
    void *mStageNameBMG;    // 0x0474
    TExPane *mMarker;
#if __SIZEOF_POINTER__ == 8
#else
       // 0x0478
    u32 _47C[0x27C / 4];
#endif
    // 0x047C

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[644];
#else
#endif
};