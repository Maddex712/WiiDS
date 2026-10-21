#pragma once

#include <Dolphin/types.h>
#include <SMS/MapObj/MapObjBall.hxx>

class TResetFruit : public TMapObjBall {
public:
    enum Fruit {
        COCONUT   = 0x40000390,
        PAPAYA    = 0x40000391,
        PINEAPPLE = 0x40000392,
        DURIAN    = 0x40000393,
        BANANA    = 0x40000394
    };

    TResetFruit(const char *);
    virtual ~TResetFruit();

    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *, u32) override;
    virtual void control() override;
    virtual void makeObjAppeared() override;
    virtual void initMapObj() override;
    virtual void touchActor(THitActor *) override;
    virtual void touchWater(THitActor *) override;
    virtual s32 getLivingTime() const override;
    virtual void appearing() override;
    virtual void breaking() override;
    virtual void waitingToAppear() override;
    virtual void hold(TTakeActor *) override;
    virtual void thrown() override;
    virtual void checkGroundCollision(TVec3f *) override;
    virtual void touchWaterSurface() override;
    virtual void touchPollution() override;
    virtual void kicked() override;

    void killByTimer(int);
    void makeObjLiving();
    void makeObjWaitingToAppear();
#if __SIZEOF_POINTER__ == 8
#else
    f32 _198;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _19C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _19E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _1A0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _1A2;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _1A4;
#endif


    static f32 mBreakingScaleSpeed;
    static s32 mFruitLivingTime;
    static s32 mFruitWaitTimeToAppear;
    static f32 mScaleUpSpeed;

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[13];
#else
    u8 _pc32_tail[3];
#endif
};