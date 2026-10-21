#pragma once

#include <Dolphin/OS.h>
#include <Dolphin/PAD.h>
#include <Dolphin/types.h>
#include <JSystem/JKernel/JKRDisposer.hxx>

enum EPadPort { Port1, Port2, Port3, Port4, PortRecorder };

class JUTGamePad : public JKRDisposer {
public:
    JUTGamePad(EPadPort port);
    virtual ~JUTGamePad();

    static bool recalibrate(u32);

    void assign();
    void checkResetSwitch();
    void clearForReset();
    void init();
    void initList();
    void read();
    void setButtonRepeat(u32, u32, u32);
    void update();

    enum EButtons {
        MAINSTICK_UP    = 0x8000000,
        MAINSTICK_DOWN  = 0x4000000,
        MAINSTICK_RIGHT = 0x2000000,
        MAINSTICK_LEFT  = 0x1000000,
        CSTICK_UP       = 0x80000,
        CSTICK_DOWN     = 0x40000,
        CSTICK_RIGHT    = 0x20000,
        CSTICK_LEFT     = 0x10000,
        START           = 0x1000,
        Y               = 0x800,
        X               = 0x400,
        B               = 0x200,
        A               = 0x100,
        L               = 0x40,
        R               = 0x20,
        Z               = 0x10,
        DPAD_UP         = 0x8,
        DPAD_DOWN       = 0x4,
        DPAD_RIGHT      = 0x2,
        DPAD_LEFT       = 0x1
    };

    enum EStickMode { NonClamped, Clamped };

    enum EWhichStick { WhichStick_ControlStick, WhichStick_CStick };

    class CButton {
    public:
        CButton();

        void clear();
        void update(const PADStatus *, u32);
        void setRepeat(u32, u32, u32);

        u32 mInput;  // _0
        u32 mFrameInput;
#if __SIZEOF_POINTER__ == 8
#else
        u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[4];
        u8 mAnalogA;
#else
        u8 mAnalogA;
#endif
   // _C
        u8 mAnalogB;   // _D
        u8 mTriggerL;  // _E
        u8 mTriggerR;  // _F
        f32 mAnalogL;  // _10
        f32 mAnalogR;  // _14
        u32 mRapidInput;
        u32 mFramesHeld;
#if __SIZEOF_POINTER__ == 8
#else
        u32 _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u32 _24;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u32 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u32 _2C;
#endif

    
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[16];
#else
#endif
};

    class CStick {
    public:
        CStick();

        void clear();
        u32 update(s8 x, s8 y, JUTGamePad::EStickMode, JUTGamePad::EWhichStick);
        u32 getButton();

        f32 mStickX;
        f32 mStickY;
        f32 mLengthFromNeutral;
        s16 mAngle;
    
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[2];
#else
    u8 _pc32_tail[2];
#endif
};

    class CRumble {
    public:
        void clear(JUTGamePad *);
        static void stopMotor(s32);
        static void stopMotorHard(s32);
        void update(u16);
        void setEnable(u32);
#if __SIZEOF_POINTER__ == 8
#else
        u32 _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u32 _C;
#endif

    
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[20];
#else
#endif
};

    class C3ButtonReset {
    public:
        static s32 sResetPattern;
    
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};

    CButton mButtons;      // _18
    CStick mControlStick;  // _48
    CStick mCStick;        // _58
    CRumble mRumble;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[4];
       // _68
    u16 mPort;
#else
       // _68
    u16 mPort;
#endif
             // _78
    u8 mPortConnected;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_6[5];
     // _7A
    JSUPtrLink mPtrLink;
#else
     // _7A
    JSUPtrLink mPtrLink;
#endif

#if __SIZEOF_POINTER__ == 8
#else
   // _7C
    u32 _8C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _90;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _94;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _98;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_11[23];
    u8 mResetFlagJUT;
#else
    u8 mResetFlagJUT;
#endif
   // _9C
    OSTick mResetTime;  // _A0
};