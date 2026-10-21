#pragma once

#include <Dolphin/types.h>

#include <SMS/Map/MapCollisionBase.hxx>
#include <SMS/Strategic/LiveActor.hxx>

class TMapCollisionStatic : public TMapCollisionBase {
public:
    TMapCollisionStatic();

    virtual void init(const char *, u16, const TLiveActor *) override;
    virtual void remove() override;
    virtual void setUp() override;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_0[2];
    TLiveActor *mActor;
#else
    TLiveActor *mActor;
#endif
  // _60
};