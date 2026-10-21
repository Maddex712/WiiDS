#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>

#include <SMS/Map/MapCollisionBase.hxx>
#include <SMS/Strategic/LiveActor.hxx>

class TMapCollisionMove : TMapCollisionBase {
public:
    TMapCollisionMove();

    virtual void init(const char *, u16, const TLiveActor *) override;
    virtual void moveMtx(float (*)[4]) override;
    virtual void moveSRT(const TVec3f &, const TVec3f &, const TVec3f &) override;
    virtual void moveTrans(const TVec3f &) override;

    void init(u32, u16, s16, const TLiveActor *);
    void move();
    void setList();
};