#pragma once

#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGadget/List.hxx>
#include <SMS/Strategic/LiveActor.hxx>

struct TAlphaShadowQuad {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};

struct TCircleShadowRequest {
    TVec3f mTranslation;
    f32 mOffsetY;   // ?
    f32 mOffsetY2;  // ?
    f32 mRotationY;
#if __SIZEOF_POINTER__ == 8
#else
    f32 _18;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[4];
    u8 mShadowType;
#else
    u8 mShadowType;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[3];
#else
    u8 _pc32_tail[3];
#endif
};

class TMBindShadowBody {
#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_tail[1];
#endif
};

class TMBindShadowManager : public JDrama::TViewObj {
public:
    TMBindShadowManager(char *);
    ~TMBindShadowManager() override;

    void load(JSUMemoryInputStream &in) override;
    void perform(u32 flags, JDrama::TGraphics *graphics) override;

    void calcVtx();
    void drawShadow(u32 flags, JDrama::TGraphics *graphics);
    void drawShadowGD(u32 flags, JDrama::TGraphics *graphics);
    void drawShadowVolume(bool, TAlphaShadowQuad *);
    void forceRequest(const TCircleShadowRequest &, u32);
    void request(const TCircleShadowRequest &, u32);
#if __SIZEOF_POINTER__ == 8
#else
    u32 _10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _14;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _1C;
#endif

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
#else
    u32 _34;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _38;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _3C;
#endif

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
    u8 _48;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _49;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_16[100];
    JGadget::TList<TMBindShadowBody *> mBodies;
#else
    u8 _pc32_16[2];
    JGadget::TList<TMBindShadowBody *> mBodies;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _5C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _5D;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _5E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _5F;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _60;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _64;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _65;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_24[2];
    f32 _68;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _6C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _70;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[32];
#else
#endif
};

extern TMBindShadowManager *gpBindShadowManager;