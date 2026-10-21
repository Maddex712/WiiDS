#pragma once

#include <JSystem/JParticle/JPABaseParticle.hxx>
#include <JSystem/JSupport/JSUList.hxx>

class JPABaseEmitter : public JSUPtrLink {
public:
    JPABaseEmitter();

    void calc();

    JPABaseParticle *calcCreateParticle();
    void calcCurrentRateTimerStep();
    void calcEmitterGlobalParams();
    void calcKeyFrameAnime();
    bool checkMaxFrame();
    bool checkStartFrame();
    JPABaseParticle *createChildParticle(JPABaseParticle *particle);
    JPABaseParticle *createParticle();
    void deleteAllParticle();
    void doChildParticle();
    void doParticle();
    void drawEmitterCallback();
    f32 getAspect();
    void getEmitterGlobalTranslation(TVec3f &);
    f32 getFovy();
    void loadBaseEmitterBlock(JPABaseParticle * /*JPADataBlock **/);
    void setGlobalRTMatrix(Mtx);
    void setGlobalSRTMatrix(Mtx);
#if __SIZEOF_POINTER__ == 8
#else
    u32 _10[0xE4 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[416];
    JSUPtrList mParticleList;
#else
    JSUPtrList mParticleList;
#endif
       // _F4
    JSUPtrList mChildParticleList;
#if __SIZEOF_POINTER__ == 8
#else
  // _100
    u32 _10C[0x48 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[96];
    TVec3f mSize1;
#else
    TVec3f mSize1;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _154
    u32 _160[0x14 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_6[20];
    TVec3f mSize3;
#else
    TVec3f mSize3;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _174
    u32 _180[0xB0 / 4];
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[160];
#else
#endif
};