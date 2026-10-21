#pragma once

#include <SMS/Strategic/HitActor.hxx>

class TEffectObjBase : public THitActor {
public:
    TEffectObjBase(const char *);
    virtual ~TEffectObjBase();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *, u32) override;
    virtual void init();
    virtual void reset();
    virtual void behaveToWater();
    virtual void moveObject();
#if __SIZEOF_POINTER__ == 8
#else
    u32 _68;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _6C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _70;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[12];
#else
#endif
};