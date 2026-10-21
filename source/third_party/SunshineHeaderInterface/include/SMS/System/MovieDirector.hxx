#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRDirector.hxx>
#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/Player/Mario.hxx>

class TMovieDirector : public JDrama::TDirector {
public:
    TMovieDirector();
    virtual ~TMovieDirector();

    s32 direct() override;

    s32 decideNextMode(s32 *);
    const char *getStreamMovieName(u32);
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
#else
    u32 _34;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _38;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[60];
#else
#endif
};