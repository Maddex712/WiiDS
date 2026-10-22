// AYN Thor lower screen: the live state of the stage's placed world pieces (port/thor_world.h).
//
// Every object the placement files create (PlacementInfoOrdered::initPlacement) is noted with its
// zone and l_id when it is a LiveActor; once a frame, after the scene's update and calcAnim
// (GameSystem::frameLoop), each noted actor's state is copied out: whether it is alive, whether
// its model is hidden, and its model's base matrix and scale. The lower screen draws its baked
// copy of each piece from that, so pieces appear, move and vanish as the game's do.
//
// Read-only: only the actors' own fields and getBaseMtx are read; nothing is changed. The scene is
// destroyed on another thread (GameSystemSceneController::destroyScene, run asynchronously), so
// the table is cleared under a lock before the scene is deleted and every read holds that lock.
// No heap allocation: static buffers only.
#ifdef TARGET_MOBILE

#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/MapObj/PlantGroup.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "port/thor_world.h"

#include <string.h>

extern "C" __attribute__((weak)) void port_thor_world_commit(const ThorWorldHeader* pHeader, const ThorWorldPiece* pPieces,
                                                             uint32_t count);

namespace {
    struct Entry {
        LiveActor* mActor;
        s16 mZone;
        s16 mChild;
        s32 mLinkId;
    };

    Entry sEntries[THOR_WORLD_PIECES];
    s32 sCount;
    s32 sZoneIds[THOR_WORLD_ZONES];
    ThorWorldHeader sHeader;
    ThorWorldPiece sPieces[THOR_WORLD_PIECES];
    u32 sGeneration;
    bool sChanged;
    int sLock;

    // Held for microseconds by the game thread and the scene loader / destroyer threads.
    struct Guard {
        Guard() {
            while (__atomic_exchange_n(&sLock, 1, __ATOMIC_ACQUIRE) != 0) {
            }
        }
        ~Guard() { __atomic_store_n(&sLock, 0, __ATOMIC_RELEASE); }
    };

    // Whether [pObj] is a LiveActor: every LiveActor joins the scene's all-actor group as it is
    // built, so a just-created object sits near the group's end (after its own sub-actors).
    LiveActor* asLiveActor(NameObj* pObj) {
        AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
        if (pGroup == nullptr) {
            return nullptr;
        }
        const s32 n = pGroup->getObjNum();
        for (s32 i = n - 1; i >= 0 && i >= n - 1024; i--) {
            if (pGroup->getObj(i) == pObj) {
                return static_cast< LiveActor* >(pObj);
            }
        }
        return nullptr;
    }

    s32 zoneSlot(s32 zoneId) {
        for (u32 i = 0; i < sHeader.zoneCount; i++) {
            if (sZoneIds[i] == zoneId) {
                return static_cast< s32 >(i);
            }
        }
        if (sHeader.zoneCount >= THOR_WORLD_ZONES) {
            return -1;
        }
        const s32 slot = static_cast< s32 >(sHeader.zoneCount++);
        sZoneIds[slot] = zoneId;
        const char* pName = MR::getZoneNameFromZoneId(zoneId);
        memset(sHeader.zones[slot], 0, THOR_WORLD_ZONE_NAME);
        if (pName != nullptr) {
            strncpy(sHeader.zones[slot], pName, THOR_WORLD_ZONE_NAME - 1);
        }
        return slot;
    }
}  // namespace

// PlacementInfoOrdered::initPlacement, after each placed object's init.
void thorWorldRegister(NameObj* pObj, const JMapInfoIter& rIter) {
    if (port_thor_world_commit == nullptr || pObj == nullptr) {
        return;
    }
    LiveActor* pActor = asLiveActor(pObj);
    s32 linkId = -1;
    if (pActor == nullptr || !MR::getJMapInfoLinkID(rIter, &linkId)) {
        return;
    }
    const s32 zoneId = MR::getPlacedZoneId(rIter);
    Guard guard;
    if (sCount >= THOR_WORLD_PIECES) {
        return;
    }
    const s32 slot = zoneSlot(zoneId);
    if (slot < 0) {
        return;
    }
    Entry& rEntry = sEntries[sCount++];
    rEntry.mActor = pActor;
    rEntry.mZone = static_cast< s16 >(slot);
    rEntry.mChild = 0;
    rEntry.mLinkId = linkId;
    // A plant group lays its flowers or bushes out itself (on the ground around it, at init): its
    // members are drawn from their own live matrices, as the group's children.
    const char* pName = nullptr;
    MR::getObjectName(&pName, rIter);
    if (pName != nullptr && (MR::isEqualString(pName, "FlowerGroup") || MR::isEqualString(pName, "FlowerBlueGroup") ||
                             MR::isEqualString(pName, "CutBushGroup"))) {
        PlantGroup* pGroup = static_cast< PlantGroup* >(pActor);
        for (s32 i = 0; i < pGroup->mMemberCount && sCount < THOR_WORLD_PIECES; i++) {
            if (pGroup->mMembers == nullptr || pGroup->mMembers[i] == nullptr) {
                continue;
            }
            Entry& rMember = sEntries[sCount++];
            rMember.mActor = pGroup->mMembers[i];
            rMember.mZone = static_cast< s16 >(slot);
            rMember.mChild = static_cast< s16 >(i + 1);
            rMember.mLinkId = linkId;
        }
    }
    sChanged = true;
}

// GameSystemSceneController::destroyScene, before the scene is deleted.
void thorWorldClear() {
    Guard guard;
    sCount = 0;
    sHeader.zoneCount = 0;
    sChanged = true;
}

// GameSystem::frameLoop, after thorLowerPublish.
void thorWorldPublish() {
    if (port_thor_world_commit == nullptr) {
        return;
    }
    Guard guard;
    if (sChanged) {
        sGeneration++;
        sChanged = false;
    }
    for (s32 i = 0; i < sCount; i++) {
        const LiveActor* pActor = sEntries[i].mActor;
        ThorWorldPiece& rPiece = sPieces[i];
        rPiece.zone = sEntries[i].mZone;
        rPiece.linkId = sEntries[i].mLinkId;
        rPiece.child = sEntries[i].mChild;
        rPiece.reserved = 0;
        u16 flags = 0;
        if (!pActor->mFlag.mIsDead) flags |= THOR_PIECE_ALIVE;
        if (pActor->mFlag.mIsHiddenModel) flags |= THOR_PIECE_HIDDEN;
        if (pActor->mFlag.mIsClipped) flags |= THOR_PIECE_CLIPPED;
        MtxPtr pMtx = pActor->mModelManager != nullptr ? pActor->getBaseMtx() : nullptr;
        if (pMtx != nullptr) {
            flags |= THOR_PIECE_MATRIX;
            memcpy(rPiece.mtx, pMtx, sizeof rPiece.mtx);
        } else {
            // No model matrix: position and rotation (degrees), as the placement gives them.
            memset(rPiece.mtx, 0, sizeof rPiece.mtx);
            rPiece.mtx[0] = pActor->mRotation.x; rPiece.mtx[1] = pActor->mRotation.y; rPiece.mtx[2] = pActor->mRotation.z;
            rPiece.mtx[3] = pActor->mPosition.x; rPiece.mtx[7] = pActor->mPosition.y; rPiece.mtx[11] = pActor->mPosition.z;
        }
        rPiece.scale[0] = pActor->mScale.x; rPiece.scale[1] = pActor->mScale.y; rPiece.scale[2] = pActor->mScale.z;
        rPiece.flags = flags;
    }
    sHeader.version = THOR_WORLD_VERSION;
    sHeader.generation = sGeneration;
    sHeader.count = static_cast< u32 >(sCount);
    port_thor_world_commit(&sHeader, sPieces, static_cast< u32 >(sCount));
}

#endif  // TARGET_MOBILE
