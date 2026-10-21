#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>

class J3DShape {
public:
    u32 countBumpMtxNum() const;
    void draw() const;
    void initialize();
    void loadVtxArray() const;
    void makeVcdVatCmd();
    void makeVtxArrayCmd();
#if __SIZEOF_POINTER__ == 8
#else
    u32 _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _6;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _C;
#endif

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
    f32 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _24;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _2C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    bool _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _31;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _32;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u8 _33;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u32 _34;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _38;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // might be a class instance
    u32 _3C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u32 _40;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u32 *_44;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _48;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _50;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _54;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _58;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[136];
#else
#endif
};

class J3DShapeMtx {
public:
    virtual ~J3DShapeMtx();

    virtual u32 getType() const;
    virtual u16 getUseMtxNum() const;
    virtual s16 getUseMtxIndex(u16) const;
    virtual void load() const;
    virtual void calcNBTScale(const Vec &, f32[3][3][3], f32[3][3][3]);

    void loadMtxIndx_NCPU(int, u16) const;
    void loadMtxIndx_PCPU(int, u16) const;
    void loadMtxIndx_PNCPU(int, u16) const;
    void loadMtxIndx_PNGP(int, u16) const;

    s16 mMtxIndex;

#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_tail[2];
#endif
};