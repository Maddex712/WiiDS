#pragma once

#include <JSystem/JGeometry/JGMVec.hxx>

class JPABaseEmitter;
class JPABaseParticle;

template <typename EmitterT = JPABaseEmitter *> class JPACallBackBase {
public:
    virtual void execute(EmitterT) {}
    virtual void executeAfter(EmitterT) {}
    virtual void draw(EmitterT) {}
};

template <typename EmitterT = JPABaseEmitter *, typename ParticleT = JPABaseParticle *>
class JPACallBackBase2 {
public:
    virtual void execute(EmitterT, ParticleT) {}
    virtual void draw(EmitterT, ParticleT) {}
};

class JPABaseParticle {
public:
    virtual void executeParticleCallBack(JPABaseEmitter *);
    virtual void drawParticleCallBack();

    void calcGlobalPosition();
    void getCurrentPosition(TVec3f &);
    void incTimer();
    void initGlobalPosition();
#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_0[4];
    u8 _00[0x10];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[36];
    u16 mStateFlags;
#else
    u16 mStateFlags;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _12[0x34];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[54];
    f32 mTimerCurrent;
#else
    u8 _pc32_3[2];
    f32 mTimerCurrent;
#endif

    f32 mTimerLerp;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[4];
    f32 mTimerMax;
#else
    f32 mTimerMax;
#endif

};