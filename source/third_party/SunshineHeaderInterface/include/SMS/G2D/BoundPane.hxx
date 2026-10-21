#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DPane.hxx>
#include <JSystem/JUtility/JUTPoint.hxx>
#include <JSystem/JUtility/JUTRect.hxx>

class TBoundPane {
public:
    inline TBoundPane(J2DPane *animPane, JUTRect rect)
        : mPane(animPane), mRect(rect), mActive(false), mCurrentInterpolate(0.0f),
          mIncInterpolate(1.0f) {}

    void startAnimation() {
        mActive             = true;
        mCurrentInterpolate = 0.0f;
    }

    void setPanePosition(s32, const JUTPoint &begin, const JUTPoint &mid, const JUTPoint &end);
    void setPaneSize(s32, const JUTPoint &begin, const JUTPoint &mid, const JUTPoint &end);
    void update();

    J2DPane *mPane;
    JUTRect mRect;
#if __SIZEOF_POINTER__ == 8
#else
    JUTRect _14;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[16];
    bool mActive;
#else
    bool mActive;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[3];
             // 0x24
    f32 mCurrentInterpolate;
#else
             // 0x24
    f32 mCurrentInterpolate;
#endif
  // 0x28
    f32 mIncInterpolate;
#if __SIZEOF_POINTER__ == 8
#else
      // 0x2C
    f32 _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _34;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_8[8];
    JUTPoint mStartPos;
#else
    JUTPoint mStartPos;
#endif
  // 0x38
    JUTPoint mMidPos;    // 0x40
    JUTPoint mEndPos;
#if __SIZEOF_POINTER__ == 8
#else
    // 0x48
    u32 _50[0x18 / 4];
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[24];
#else
#endif
};