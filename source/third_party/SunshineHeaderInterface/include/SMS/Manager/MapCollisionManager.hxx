#pragma once

#include <Dolphin/types.h>
#include <SMS/Map/MapCollisionBase.hxx>

class TLiveActor;

class TMapCollisionManager {
public:
    enum CollisionKind { STATIC, MOVE, WARP };

    TMapCollisionManager(u16 size, const char *folder, const TLiveActor *actor);

    void changeCollision(u32 index);
    void createCollision(const char *, u8 collisionKind);
    void init(const char *model, u16, const char *folder);

    TMapCollisionBase **mMapCollisions;
    u16 mMapCollisionSize;
    u16 mMapCollisionNum;
    TMapCollisionBase *mCurrentMapCollision;
    const char *mName;
    TLiveActor *mActor;
    u16 mState;
};