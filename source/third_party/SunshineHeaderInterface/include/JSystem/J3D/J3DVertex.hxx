#pragma once

#include <Dolphin/types.h>

class J3DVertexData {
public:
#if __SIZEOF_POINTER__ == 8
#else
    u32 _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C;
#endif

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
    u8 _pc64_tail[128];
#else
#endif
};

class J3DVertexBuffer {
public:
    J3DVertexBuffer(J3DVertexData *);

    void copyTransformedVtxArray();

    J3DVertexData *mVtxData;
#if __SIZEOF_POINTER__ == 8
#else
  // _0
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C;
#endif

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


    virtual ~J3DVertexBuffer();  // _38

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[104];
#else
#endif
};

class J3DDrawMtxData {
public:
    J3DDrawMtxData();
    ~J3DDrawMtxData();
#if __SIZEOF_POINTER__ == 8
#else
    u16 _0;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[2];
    u16 mJointCount;
#else
    u16 mJointCount;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[12];
#else
#endif
};