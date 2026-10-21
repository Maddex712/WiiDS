#pragma once

#include <Dolphin/types.h>
#include <SMS/MapObj/MapObjGeneral.hxx>

class TItem : public TMapObjGeneral {
public:
    TItem(const char *);
    virtual ~TItem();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *, u32) override;
    virtual void calcRootMatrix() override;
    virtual void appear() override;
    virtual void initMapObj() override;
    virtual void calc() override;
    virtual void touchPlayer(THitActor *) override;
    virtual void appearing() override;
    virtual void appeared() override;
    virtual void killByTimer(int);
    virtual void taken(THitActor *);
#if __SIZEOF_POINTER__ == 8
#else
    u32 *_148;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[8];
           // THideObjBase *
    s32 mKillTimer;
#else
           // THideObjBase *
    s32 mKillTimer;
#endif
      // _14C
    u32 mStateTimerSet;  // ?

    static f32 mAppearedScaleSpeed;
};