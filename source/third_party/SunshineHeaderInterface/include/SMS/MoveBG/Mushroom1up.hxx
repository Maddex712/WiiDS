#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/MapObj/MapObjBase.hxx>
#include <SMS/Strategic/HitActor.hxx>

class TMushroom1up : public TMapObjBase {
public:
    enum MushroomType { ATTRACT, REPEL, FIXED };

    TMushroom1up(const char *name);
    virtual ~TMushroom1up();

    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual void control() override;
    virtual void load(JSUMemoryInputStream &) override;
    virtual void initMapObj() override;
    virtual void makeObjAppeared() override;
    virtual void touchPlayer(THitActor *) override;

    bool mIsRevealed;  // 0x0138 - Applies to 1-Ups hidden behind WaterHitHideObj, MapObjSmoke, etc...
    u8 mMushroomType;  // 0x0139
    bool mIsTaken;
#if __SIZEOF_POINTER__ == 8
#else
     // 0x013A
    u8 _13B;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[1];
           // Padding?
    u32 mFramesAlive;
#else
           // Padding?
    u32 mFramesAlive;
#endif
  // 0x013C
};