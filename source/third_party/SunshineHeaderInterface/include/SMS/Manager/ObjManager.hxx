#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/M3DUtil/MActor.hxx>
#include <SMS/Strategic/HitActor.hxx>
#include <SMS/Strategic/ObjChara.hxx>

struct TModelDataLoadEntry {
    const char *mModelPath;
    u32 mModelFlags;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _0C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
#endif
};

/* Size -- 0x34 */
class TObjManager : public JDrama::TViewObj {
public:
    TObjManager(const char *);
    virtual ~TObjManager();

    virtual void load(JSUMemoryInputStream &) override;
    virtual TObjManager *searchF(u16, const char *) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual void createModelData();
    virtual void createAnmData();
    virtual void createModelDataArray(const TModelDataLoadEntry *);

    void createModelDataArrayBase(const TModelDataLoadEntry *, const char *);
    MActorAnmData *getMActorAnmData();
    void *getModelDataKeeper();
    void manageObj(THitActor *obj);
#if __SIZEOF_POINTER__ == 8
    u32 mMaxObjs;
#else
    size_t mMaxObjs;
#endif
  // _10
    size_t mObjCount; // _14
    JDrama::TViewObj **mObjAry;
    TObjChara *mObjChara;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _24;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _2C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _30;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[28];
#else
#endif
};