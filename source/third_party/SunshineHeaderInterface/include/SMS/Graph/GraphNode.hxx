#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>
#include <SMS/Graph/RailNode.hxx>

#include <JSystem/JGeometry/JGMVec.hxx>

class TGraphNode {

public:
    TGraphNode();
    void getPoint(Vec *out) const;

    TRailNode *mRailNode;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _04;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _08;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _0C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[10];
#else
#endif
};