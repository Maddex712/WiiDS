#pragma once

#include <Dolphin/types.h>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/M3DUtil/MActor.hxx>
#include <SMS/MapObj/MapObjGeneral.hxx>
#include <SMS/Strategic/HitActor.hxx>

class TMario;

class TEggYoshi : public TMapObjGeneral {
public:
    TEggYoshi(const char *);
    virtual ~TEggYoshi();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void control() override;
    virtual u16 getHitObjNumMax() override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *, u32) override;
    virtual void touchActor(THitActor *) override;

    u32 decideRandomLoveFruit();
    void startFruit();
    void touchFruit();

    MActor *mActor;    // 0x0148
    u32 mWantedFruit;  // 0x014C
    TMario *mMario;    // 0x0150
};