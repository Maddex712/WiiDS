#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JUtility/JUTColor.hxx>
#include <SMS/M3DUtil/MActor.hxx>
#include <SMS/MoveBG/EggYoshi.hxx>

class TMario;

class TYoshi {
public:
    enum Color { GREEN, ORANGE, PURPLE, PINK };
    enum State { EGG = 0, DROWNING = 3, DYING = 4, UNMOUNTED = 6, MOUNTED = 8 };

    bool appearFromEgg(const TVec3f &);
    void calcAnim();
    void changeAnimation(int id);
    bool disappear();
    void doEat(u32 fruitID);
    void doSearch();
    void entry();
    void getEmitPosDir(TVec3f *, TVec3f *) const;
    J3DFrameCtrl *getFrameCtrl() const;
    Mtx *getMtxPtrFootL() const;
    Mtx *getMtxPtrFootR() const;
    void getOff(bool knockedOff);
    void init(TMario *);
    void initInLoadAfter();
    void kill();
    void movement();
    int onYoshi();
    void ride();
    void setEggYoshiPtr(TEggYoshi *);
    void thinkAnimation();
    void thinkBtp(int);
    void thinkHoldOut();
    void thinkUpper();
    void viewCalc();

    s8 mState;                 // 0x0000
    u16 mSubState;
#if __SIZEOF_POINTER__ == 8
#else
             // 0x0002 ??
    u32 _01;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[4];
                   // 0x0004
    s32 mMaxJuice;
#else
                   // 0x0004
    s32 mMaxJuice;
#endif
             // 0x0008
    s32 mCurJuice;             // 0x000C
    TMario *mMario;
#if __SIZEOF_POINTER__ == 8
#else
            // 0x0010
    u32 _02[0xC / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_7[12];
          // 0x0014
    TVec3f mTranslation;
#else
          // 0x0014
    TVec3f mTranslation;
#endif

#if __SIZEOF_POINTER__ == 8
#else
       // 0x0020
    u32 _03[0x8 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_9[16];
          // 0x002C
    MActor *mActor;
#else
          // 0x002C
    MActor *mActor;
#endif

#if __SIZEOF_POINTER__ == 8
#else
            // 0x0034
    u32 _04[0x4C / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_11[108];
         // 0x0038
    f32 mRedComponent;
#else
         // 0x0038
    f32 mRedComponent;
#endif
         // 0x0084
    f32 mGreenComponent;       // 0x0088
    f32 mBlueComponent;
#if __SIZEOF_POINTER__ == 8
#else
        // 0x008C
    u32 _05[0x28 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_15[56];
         // 0x0090
    u8 mFlutterState;
#else
         // 0x0090
    u8 mFlutterState;
#endif

#if __SIZEOF_POINTER__ == 8
#else
          // 0x00B8
    u8 _06;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_17[1];
                    // 0x00B9
    u16 mFlutterTimer;
#else
                    // 0x00B9
    u16 mFlutterTimer;
#endif
         // 0x00BA
    u16 mMaxFlutterTimer;
#if __SIZEOF_POINTER__ == 8
#else
      // 0x00BC
    u16 _07;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_20[2];
                   // 0x00BE
    f32 mMaxVSpdStartFlutter;
#else
                   // 0x00BE
    f32 mMaxVSpdStartFlutter;
#endif
  // 0x00C0
    f32 mFlutterAcceleration;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x00C4
    u32 _08[0x8 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_23[8];
          // 0x00C8
    s8 mType;
#else
          // 0x00C8
    s8 mType;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                  // 0x00D0
    u8 _09;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                    // 0x00D1
    u16 _10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                   // 0x00D2
    u32 _11[0x1C / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_27[31];
         // 0x00D4
    TEggYoshi *mEgg;
#else
         // 0x00D4
    TEggYoshi *mEgg;
#endif

#if __SIZEOF_POINTER__ == 8
#else
           // 0x00F0
    u32 _F4[0x30 / 4];
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[64];
#else
#endif
};

extern JUtility::TColor bodyColor[4];