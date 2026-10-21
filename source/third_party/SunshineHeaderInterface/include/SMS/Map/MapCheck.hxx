#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <SMS/Map/BGCheck.hxx>

class TBGWallCheckRecord {
public:
    TVec3f mPosition;
    f32 mRadius;
#if __SIZEOF_POINTER__ == 8
    u32 mCollideMax;
#else
    size_t mCollideMax;
#endif

#if __SIZEOF_POINTER__ == 8
    u32 mNumWalls;
#else
    size_t mNumWalls;
#endif

    u32 mIgnoreFlags;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[4];
    TBGCheckData *mWalls[4];
#else
    TBGCheckData *mWalls[4];
#endif

};