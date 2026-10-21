#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DSetScreen.hxx>
#include <SMS/GC2D/OptionRumbleUnit.hxx>
#include <SMS/GC2D/OptionSoundUnit.hxx>
#include <SMS/GC2D/OptionSubtitleUnit.hxx>

enum SelectType {
    RUMBLE    = 0,
    SOUND     = 1,
    SUBTITLES = 2,
    UNKNOWN   = 3,
};

class TOptionControl {
public:
    J2DSetScreen *mSetScreen;
#if __SIZEOF_POINTER__ == 8
#else
            // _0
    u32 *_4;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_2[8];
                             // TArrowControl*
    TOptionRumbleUnit *mRumbleUnit;
#else
                             // TArrowControl*
    TOptionRumbleUnit *mRumbleUnit;
#endif
      // _8
    TOptionSoundUnit *mSoundUnit;        // _C
    TOptionSubtitleUnit *mSubtitleUnit;  // _10
    u32 mSelectType;                     // _14
    bool mIsOptionChanged;               // 0x0018
    bool mIsOptionSelected;
#if __SIZEOF_POINTER__ == 8
#else
              // 0x0019
    u8 _1A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                              // padding?
    u8 _1B;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                              // ^^
    u32 _1C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[6];
#else
#endif
};