#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DPane.hxx>
#include <JSystem/JUtility/JUTPoint.hxx>
#include <JSystem/JUtility/JUTRect.hxx>
#include <SMS/GC2D/Coord2D.hxx>

class TExPane {
public:
    void setPanePosition(s32, const JUTPoint &begin, const JUTPoint &mid, const JUTPoint &end);
    void setPaneSize(s32, const JUTPoint &begin, const JUTPoint &mid, const JUTPoint &end);
    void update();

    J2DPane *mPane;
    JUTRect mRect;
    TCoord2D mOffset;
    TCoord2D mSize;
    f32 mCurrentPoint;
    f32 mIncPoint;
#if __SIZEOF_POINTER__ == 8
#else
    s16 _4C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_7[2];
    bool mIsOffsetUpdating;
#else
    bool mIsOffsetUpdating;
#endif

    bool mIsSizeUpdating;
    bool mIsAlphaUpdating;
};