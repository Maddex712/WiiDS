#pragma once

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/Strategic/ObjHitCheck.hxx>

class TStrategy : public JDrama::TViewObj {
public:
    TStrategy(const char *name);
    virtual ~TStrategy();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual void loadAfter() override;
    virtual TNameRef *searchF(u16, const char *) override;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_0[4];
    TIdxGroupObj *mMapGroup;
#else
    TIdxGroupObj *mMapGroup;
#endif

    TIdxGroupObj *mEmptyGroup;
    TIdxGroupObj *mManagerGroup;
    TIdxGroupObj *mObjectGroup;
    TIdxGroupObj *mGraffitiGroup;
    TIdxGroupObj *mItemGroup;
    TIdxGroupObj *mPlayerGroup;
    TIdxGroupObj *mEnemyGroup;
    TIdxGroupObj *mBossGroup;
    TIdxGroupObj *mNPCGroup;
    TIdxGroupObj *mWaterPartyGroup;
    TIdxGroupObj *mInitializationGroup;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _40;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _44;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _48;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_16[32];
    u16 mHitObjFlags;
#else
    u16 mHitObjFlags;
#endif

};

extern TStrategy *gpStrategy;