#pragma once

#include <SMS/Map/MapCollisionMove.hxx>
#include <SMS/MapObj/MapObjGeneral.hxx>

class TMapObjTree : public TMapObjGeneral {
public:
#if __SIZEOF_POINTER__ == 8
#else
    u32 _148;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _14C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_2[8];
    s32 mLeafCount;
#else
    s32 mLeafCount;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[4];
    TMapCollisionMove *mCollisionMove;
#else
    TMapCollisionMove *mCollisionMove;
#endif

};