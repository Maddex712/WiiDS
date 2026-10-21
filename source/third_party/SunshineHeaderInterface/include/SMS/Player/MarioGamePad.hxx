#pragma once

#include <Dolphin/types.h>
#include <JUtility/JUTGamePad.hxx>

struct TMarioControllerWork {
    enum Buttons {
        R = 0x20,
        A = 0x100,
        B = 0x200,
        L = 0x4000,
    };

    s16 mStickHS16;
    s16 mStickVS16;
    Buttons mInput;
    Buttons mFrameInput;
    u8 mAnalogRU8;
    u8 mAnalogLU8;
    f32 mStickH;
    f32 mStickV;
    f32 mStickDist;
    f32 mAnalogL;
    f32 mAnalogR;
};

class TMarioGamePad : public JUTGamePad {
public:
    TMarioGamePad(EPadPort port) : JUTGamePad(port) {}
    virtual ~TMarioGamePad();

    void onNeutralMarioKey();
    void read();
    void reset();
    void updateMeaning();
#if __SIZEOF_POINTER__ == 8
#else
    u32 _A4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _A8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _AC;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[12];
    f32 _B0;
#else
    f32 _B0;
#endif

    f32 _B4;
#if __SIZEOF_POINTER__ == 8
#else
    f32 _B8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _BC;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_7[8];
    f32 mStickX;
#else
    f32 mStickX;
#endif
  // _C0
    f32 mStickY;
#if __SIZEOF_POINTER__ == 8
#else
  // _C4
    f32 _C8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _CC;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_11[8];
    u32 mMeaning;
#else
    u32 mMeaning;
#endif

    u32 mFrameMeaning;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _D8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _DC;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _DE;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _E0;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_17[10];
    struct {
        u16 _09          : 1;
        u16 mReadInput : 1;
        u16 _08        : 1;
        u16 _07        : 1;
        u16 _06        : 1;
        u16 _05        : 1;
        u16 mIsTalking : 1;
        u16 _03        : 1;
        u16 _02        : 1;
        u16 _01        : 1;
        u16 mDisable   : 1;
        u16 _00          : 5;
    } mState;
#else
    struct {
        u16 _09          : 1;
        u16 mReadInput : 1;
        u16 _08        : 1;
        u16 _07        : 1;
        u16 _06        : 1;
        u16 _05        : 1;
        u16 mIsTalking : 1;
        u16 _03        : 1;
        u16 _02        : 1;
        u16 _01        : 1;
        u16 mDisable   : 1;
        u16 _00          : 5;
    } mState;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x00E2

    u16 _E4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _E6;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u32 _E8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _EC;
#endif
  // padding?

    static u16 mResetFlag;

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[12];
#else
#endif
};