#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>

#include <SMS/Map/BGCheck.hxx>

class TLiveActor;

enum CollisionType { COLLISION_STATIC, COLLISION_MOVE, COLLISION_WARP };

class TMapCollisionBase {
public:
    TMapCollisionBase() {}

    virtual void init(const char *, u16, const TLiveActor *);
    virtual void moveSRT(const TVec3f &, const TVec3f &, const TVec3f &);
    virtual void moveTrans(const TVec3f &);
    virtual void moveMtx(float (*)[4]);
    virtual void setUp();
    virtual void setUpTrans(const TVec3f &);
    virtual void remove();

    void initAllCheckData(s16, const f32 *, u16, const TLiveActor *);
    void initCheckData(s16, u16, const TLiveActor *);
    bool isSetUp() const;
    void setAllActor(const TLiveActor *);
    void setAllBGType(u16);
    void setAllData(s16);
    void setCheckData(const f32 *, const s16 *, TBGCheckData *, int);
    void setMtx(float (*)[4]);
    void setVertexData(u32, const TVec3f &, const TVec3f &, const TVec3f &);
    void update();
    void updateTrans(const TVec3f &);

    TBGCheckData *mCheckDatas;
#if __SIZEOF_POINTER__ == 8
#else
    size_t _8;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_2[4];
    u32 mNumFaces;
#else
    size_t mNumFaces;
#endif

    u32 mNumVerts;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[4];
    // _10
    u32 mVertOffset;
#else
    // _10
    u32 mVertOffset;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[4];
  // _14
    u32 mGroupCount;
#else
  // _14
    u32 mGroupCount;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_6[4];
  // _18
    u32 *mGroups;
#else
  // _18
    u32 *mGroups;
#endif
     // _1C
    Mtx mGlobalTRS;
#if __SIZEOF_POINTER__ == 8
#else
    TVec3f _50;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_9[12];
    u16 _5C;
#else
    u16 _5C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _5E;
#endif
  // padding
};