#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DPicture.hxx>
#include <JSystem/J2D/J2DWindow.hxx>
#include <JSystem/JUtility/JUTColor.hxx>
#include <SMS/GC2D/ConsoleStr.hxx>

class TGCConsole2 {

public:
    struct HealthPoint {
        J2DPicture *mActivePicture;
        J2DPicture *mInactivePicture;
    };
#if __SIZEOF_POINTER__ == 8
#else
    u32 _00[0x24 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[40];
    // 0x0000
    s32 mShineCount;
#else
    // 0x0000
    s32 mShineCount;
#endif
      // 0x0024
    s32 mCurWater;
#if __SIZEOF_POINTER__ == 8
#else
        // 0x0028
    u32 _2C[0x10 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[16];
    // 0x002C
    bool mIsRedCoinCard;
#else
    // 0x002C
    bool mIsRedCoinCard;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x003C
    bool _3D;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_6[1];
    bool mIsTimerCard;
#else
    bool mIsTimerCard;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x003E
    bool _3F;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    bool _40;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    bool _41;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    bool _42;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_11[4];
    bool mIsTimerCardActive;
#else
    bool mIsTimerCardActive;
#endif

    bool mIsDEBSAlert;       // 0x0044
    bool mWaterCardRising;   // 0x0045
    bool mIsWaterCard;
#if __SIZEOF_POINTER__ == 8
#else
       // 0x0046
    u8 _47;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                  // 0x0047
    u16 _48;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_17[3];
                 // 0x0048
    bool mIsResetTimer;
#else
                 // 0x0048
    bool mIsResetTimer;
#endif
      // 0x004A
    bool mWaterCardFalling;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x004B
    u32 _4C[0x28 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_20[40];
       // 0x004C
    s16 mRedCoinCardTimer;
#else
       // 0x004C
    s16 mRedCoinCardTimer;
#endif
   // 0x0074
    s16 mTimerCardTimer;
#if __SIZEOF_POINTER__ == 8
#else
     // 0x0076
    u32 _78[0x1C / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_23[36];
    TConsoleStr *mConsoleStr;
#else
    TConsoleStr *mConsoleStr;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                // 0x0094
    u16 _98;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_25[2];
                                 // 0x0098
    JUtility::TColor mWaterLeftPanelColor;
#else
                                 // 0x0098
    JUtility::TColor mWaterLeftPanelColor;
#endif
   // 0x009A
    JUtility::TColor mWaterRightPanelColor;  // 0x009E
    JUtility::TColor mJuiceCardOrangeColor;  // 0x00A2
    JUtility::TColor mJuiceCardPurpleColor;  // 0x00A6
    JUtility::TColor mJuiceCardPinkColor;
#if __SIZEOF_POINTER__ == 8
#else
    // 0x00AA
    u16 _AE;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_31[2];
                                 // 0x00AE
    J2DScreen *mMainScreen;
#else
                                 // 0x00AE
    J2DScreen *mMainScreen;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                  // 0x00B0
    u32 _B4[0xC8 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_33[360];
                       // 0x00B4
    HealthPoint mHealthPoints[9];
#else
                       // 0x00B4
    HealthPoint mHealthPoints[9];
#endif

#if __SIZEOF_POINTER__ == 8
#else
            // 0x017C
    u32 _1C4[2];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_35[12];
    s8 mHealthCount;
#else
    s8 mHealthCount;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_36[3];
               // 0x01CC
    JUTRect mHealthPointRects[9];
#else
               // 0x01CC
    JUTRect mHealthPointRects[9];
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x01D0
    u32 _260[0x40 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_38[120];
    J2DPicture *mWaterTopPanel;
#else
    J2DPicture *mWaterTopPanel;
#endif

#if __SIZEOF_POINTER__ == 8
#else
      // 0x02A0
    u32 _2A4[0x14 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_40[40];
               // 0x02A4
    u32 mWaterCardTopHeight;
#else
               // 0x02A4
    u32 mWaterCardTopHeight;
#endif

#if __SIZEOF_POINTER__ == 8
#else
         // 0x02B8
    u32 _2BC[0x14C / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_42[532];
             // 0x02BC
    TBoundPane *mJetBalloonCurrent;
#else
             // 0x02BC
    TBoundPane *mJetBalloonCurrent;
#endif
  // 0x0408
    TBoundPane *mJetBalloonCoaster;  // 0x040C
    TBoundPane *mJetBalloonPianta;
#if __SIZEOF_POINTER__ == 8
#else
   // 0x0410
    u32 _414[0x18 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_46[48];
    TExPane *mRedCoinPanelBack;
#else
    TExPane *mRedCoinPanelBack;
#endif
      // 0x042C
    TExPane *mRedCoinPanel;
#if __SIZEOF_POINTER__ == 8
#else
          // 0x0430
    TExPane *_434;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                   // 0x0434
    TExPane *_438;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                   // 0x0438
    TExPane *_43C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                   // 0x043C
    TExPane *_440;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                   // 0x0440
    TExPane *_444;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                   // 0x0444
    u32 _448[0xBC / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_54[296];
              // 0x0448
    TExPane *mTimerPanelNormal;
#else
              // 0x0448
    TExPane *mTimerPanelNormal;
#endif
      // 0x0504
    TExPane *mTimerPanelRush;
#if __SIZEOF_POINTER__ == 8
#else
        // 0x0508
    u32 _50C[0x8 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_57[8];
               // 0x050C
    bool mIsTimerPanelRush;
#else
               // 0x050C
    bool mIsTimerPanelRush;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_58[3];
          // 0x0514
    s32 mTimerSecondsLeft;
#else
          // 0x0514
    s32 mTimerSecondsLeft;
#endif

#if __SIZEOF_POINTER__ == 8
#else
           // 0x0518
    u32 _51C[0x8 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_60[8];
               // 0x051C
    TExPane *mTelopPanel;
#else
               // 0x051C
    TExPane *mTelopPanel;
#endif
            // 0x0524
    TExPane *mTelopWindow;           // 0x0528
    J2DTextBox *mTelopTextFront;     // 0x052C
    J2DTextBox *mTelopTextBack;
#if __SIZEOF_POINTER__ == 8
#else
      // 0x0530
    u32 _534;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_65[8];
    JUTRect mTelopPosition;
#else
    JUTRect mTelopPosition;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x0538
    u32 _548[0x10 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_67[16];
    u32 mTelopPixelWidth;
#else
    size_t mTelopPixelWidth;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x0558
    u32 _55C[0xC / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_69[12];
    f32 mDEBSScrollSpeed;
#else
    f32 mDEBSScrollSpeed;
#endif
  // 0x0568
    f32 mDEBSPositionX;
#if __SIZEOF_POINTER__ == 8
#else
    // 0x056C
    u32 _570[2];
#endif


    // 0x1C4 = TExPane *LifeMeter
    // 0x2F8 = TExPane *WaterBackPanel

    // 0x314 = J2DPicture *FruitText

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[16];
#else
#endif
};