#pragma once

#include <JSystem/J3D/J3DAnimation.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JDrama/JDRViewObjPtrListT.hxx>
#include <JSystem/JGadget/List.hxx>
#include <SMS/Strategic/HitActor.hxx>

class TIdxGroupObj : public JDrama::TViewObjPtrListT<THitActor> {
public:
    TIdxGroupObj(const char *name) : TViewObjPtrListT(name){};
    virtual ~TIdxGroupObj();

    virtual void loadSuper(JSUMemoryInputStream &stream) override;
};

struct TObjCheckList {
    TObjCheckList *mNext;
    void *mData;
};

class TObjHitCheck {
public:
    TObjHitCheck();

    void checkActorsHit();
    void checkAndEntryGroup(TIdxGroupObj *group);
    void checkWater();
    void clearHitNum();
    void entryGroup(TIdxGroupObj *group);

    bool suffererIsInAttackArea(THitActor *attacker, THitActor *sufferer);

    TObjCheckList mStaticCheckLists[256];
    TObjCheckList *mMoveCheckLists;
    size_t mCount;
};

extern TObjHitCheck *gpObjHitCheck;