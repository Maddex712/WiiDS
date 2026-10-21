#pragma once

#include <JDrama/JDRViewObj.hxx>

class TMapObjWave : public JDrama::TViewObj {
public:
    TMapObjWave(const char *name);
    ~TMapObjWave() override;

    void load(JSUMemoryInputStream &in) override;
    void perform(u32, JDrama::TGraphics *) override;

    void initDraw();
    void draw();

    f32 getHeight(f32 x, f32 y, f32 z) const;
    f32 getWaveHeight(f32 x, f32 z) const;

    void noWave();
    void updateHeightAndAlpha();
    void updateTime();

public:
#if __SIZEOF_POINTER__ == 8
#else
    f32 _10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _14;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _18;
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
    f32 _24;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _2C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _34;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _38;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _3C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _40;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _44;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _48;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _4C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _50;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _54;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _58;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _5C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _60;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _64;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _68;
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
#else
    f32 _74;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _78;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _7C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _7E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _80;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _82;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _84;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _86;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _88;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _8A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _8C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _8E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _90;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _92;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _94;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _98;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _9A;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[142];
#else
#endif
};

extern TMapObjWave *gpMapObjWave;