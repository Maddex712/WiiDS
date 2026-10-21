#pragma once

#include <Dolphin/types.h>
#include <SMS/Graph/GraphWeb.hxx>

class TGraphTracer {
public:
    TGraphTracer();

    f32 calcSplineSpeed(f32);
    int getCurGraphIndex() const;
    TGraphWeb *getGraph() const;
    void moveTo(int);
    void setTo(int);
    int traceSpline(f32);

    TGraphWeb *mGraph;  // 0x0000
    s32 mCurrentNode;   // 0x0004
    s32 mPreviousNode;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0008
    f32 _00;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _01;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[8];
    f32 _02;
#else
    f32 _02;
#endif

};