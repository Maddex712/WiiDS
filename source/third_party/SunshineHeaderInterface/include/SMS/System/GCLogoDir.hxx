#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRDirector.hxx>
#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/Player/Mario.hxx>

class TNintendo2D : public JDrama::TViewObj {
    void perform(u32, JDrama::TGraphics *) override;

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
#endif
};

class TGCLogoDir : public JDrama::TDirector {
public:
    TGCLogoDir();
    virtual ~TGCLogoDir();

    s32 direct() override;
    s32 direct_dolby();
    s32 direct_nlogo();
    s32 setup(JDrama::TDisplay *, TMarioGamePad *);
#if __SIZEOF_POINTER__ == 8
#else
    u32 _18;
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
    u8 _pc64_4[20];
    f32 mVSyncTimesPerSec;
#else
    f32 mVSyncTimesPerSec;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _2C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _34;
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
    u16 _44;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[42];
#else
#endif
};
