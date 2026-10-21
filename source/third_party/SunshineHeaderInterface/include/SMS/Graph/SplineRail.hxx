#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>

#include <JSystem/JGeometry/JGMVec.hxx>

class TGraphWeb;

class TSplineRail {
public:
    TSplineRail(TGraphWeb *);

    void getPosAndRot(f32, TVec3f *posOut, TVec3f *rotOut);

#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_tail[1];
#endif
};

class TSplinePath {
public:
    TSplinePath(int);

    f32 calcSpline(f32, f32 *, f32 *, f32 *);
    void calcTable();
    Vec getPoint(f32);
    void makeTable(f32 *, f32 *, f32 *);

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};