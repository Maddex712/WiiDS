#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <SMS/MoveBG/Item.hxx>
#include <SMS/Strategic/HitActor.hxx>

class TCoin : public TItem {
public:
    TCoin(const char *);
    virtual ~TCoin();

    virtual void loadAfter() override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual void appear() override;
    virtual void makeObjAppeared() override;
    virtual void makeObjDead() override;
    virtual void initMapObj() override;
    virtual void taken(THitActor *) override;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _154;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};