#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>

class TRailNode {

public:
    TVec3s mPosition;           // 0x0000
    s16 mNeighborCount;         // 0x0006
    u32 mFlags;                 // 0x0008
    u16 mValues[4];             // 0x000C
    s16 mNeighborIDs[8];        // 0x0014
    f32 mNeighborDistances[8];  // 0x0024
};