#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRDirector.hxx>
#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/Player/Mario.hxx>

class TMenuDirector : public JDrama::TDirector {
public:
    TMenuDirector();
    virtual ~TMenuDirector();

    s32 direct() override;

    void setFixedStageValue();
    void rsetup();
    s32 setup(JDrama::TDisplay *, TMarioGamePad *);
    void setupThreadFunc(void *);
#if __SIZEOF_POINTER__ == 8
#else
    bool _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _1C;
#endif

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
#else
    u16 _30;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_7[52];
    f32 mVSyncTimesPerSec;
#else
    f32 mVSyncTimesPerSec;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _38;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _3C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _40;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _44;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _48;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _50;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _54;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[48];
#else
#endif
};