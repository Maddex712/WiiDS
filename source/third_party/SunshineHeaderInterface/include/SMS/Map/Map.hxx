#pragma once

#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/Map/BGCheck.hxx>
#include <SMS/Map/MapCollisionData.hxx>

class TMapWarp {
public:
    class TMapWarpInfo {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};

    u32 mCurrentID;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_2[12];
    u32 mPrevID;
#else
    u32 mPrevID;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _C;
#endif
  // init'd to 3.0

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
#endif
};

class TMapXlu {
public:
    size_t mJointNum;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _4;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};

class TMap : public JDrama::TViewObj {
public:
    TMap(const char *);
    virtual ~TMap();

    virtual void load(JSUMemoryInputStream &stream) override;
    virtual void loadAfter() override;
    virtual void perform(u32, JDrama::TGraphics *) override;

    void changeModel(s16 index) const;
    f32 checkGround(f32 x, f32 y, f32 z, const TBGCheckData **out) const;
    f32 checkGround(const TVec3f &pos, const TBGCheckData **out) const;
    f32 checkGroundExactY(f32 x, f32 y, f32 z, const TBGCheckData **out) const;
    f32 checkGroundIgnoreWaterSurface(f32 x, f32 y, f32 z, const TBGCheckData **out) const;
    f32 checkGroundIgnoreWaterSurface(const TVec3f &pos, const TBGCheckData **out) const;
    f32 checkGroundIgnoreWaterThrough(f32 x, f32 y, f32 z, const TBGCheckData **out) const;
    f32 checkRoof(f32 x, f32 y, f32 z, const TBGCheckData **out) const;
    f32 checkRoof(const TVec3f &pos, const TBGCheckData **out) const;
    f32 checkRoofIgnoreWaterThrough(f32 x, f32 y, f32 z, const TBGCheckData **out) const;
    void draw(u32, JDrama::TGraphics *) const;
    const TBGCheckData *getIllegalCheckData();
    const TBGCheckData *intersectLine(const TVec3f &a, const TVec3f &b, bool, TVec3f *);
    bool isInArea(f32 x, f32 z) const;
    bool isTouchedOneWall(f32, f32, f32, f32) const;
    bool isTouchedOneWallAndMoveXZ(f32 *, f32, f32 *, f32) const;
    bool isTouchedWallsAndMoveXZ(TBGWallCheckRecord *record) const;
    void update();
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_0[4];
    TMapCollisionData *mCollisionData;
#else
    TMapCollisionData *mCollisionData;
#endif
  // 0x0010
    TMapWarp *mMapWarp;                 // 0x0014
    TMapXlu *mMapXlu;                   // 0x0018
};

extern TMap *gpMap;