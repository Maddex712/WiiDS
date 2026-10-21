#pragma once

#include <Dolphin/types.h>
#include <SMS/MapObj/MapObjGeneral.hxx>

class TMapObjBall : public TMapObjGeneral {
public:
    TMapObjBall(const char *);
    virtual ~TMapObjBall();

    virtual int receiveMessage(THitActor *, u32) override;
    virtual void control() override;
    virtual void makeObjAppeared() override;
    virtual void initMapObj() override;
    virtual void touchActor(THitActor *) override;
    virtual void touchWater(THitActor *) override;
    virtual void makeObjDefault() override;
    virtual f32 getDepthAtFloating() override;
    virtual s32 getLivingTime() const override;
    virtual void hold(TTakeActor *) override;
    virtual void put() override;
    virtual void touchGround(TVec3f *) override;
    virtual void checkWallCollision(TVec3f *) override;
    virtual void touchWall(TVec3f *, TBGWallCheckRecord *) override;
    virtual void touchRoof(TVec3f *) override;
    virtual void rebound(TVec3f *);
    virtual void touchWaterSurface();
    virtual void touchPollution();
    virtual void kicked();
    virtual void calcCurrentMtx();

    void boundByActor(THitActor *);
#if __SIZEOF_POINTER__ == 8
#else
    f32 _148;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _14C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _150;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _154;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _158;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _15C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _160;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _164;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _168;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _16C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _170;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _174;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_12[48];
    f32 mKickedVSpeed;
#else
    f32 mKickedVSpeed;
#endif

    f32 mSprayPushSpeed;
#if __SIZEOF_POINTER__ == 8
#else
    f32 _180;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_15[4];
    f32 _184;
#else
    f32 _184;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _188;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _18C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_18[8];
    f32 mAdditionalHeldHeight;
#else
    f32 mAdditionalHeldHeight;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _194;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
#endif
};