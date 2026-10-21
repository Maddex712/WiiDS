#pragma once

#include <Dolphin/types.h>
#include <JSystem/J3D/J3DPacket.hxx>

class J3DDrawBuffer {
public:
    J3DDrawBuffer(size_t);

    void draw() const;
    void drawHead() const;
    void drawTail() const;
    bool entryImm(J3DPacket *, u16);
    bool entryInvalidSort(J3DMatPacket *);
    bool entryMatAnmSort(J3DMatPacket *);
    bool entryMatSort(J3DMatPacket *);
    bool entryModelSort(J3DMatPacket *);
    bool entryNonSort(J3DMatPacket *);
    bool entryZSort(J3DMatPacket *);
    void frameInit();

    u32 *mFrames;
#if __SIZEOF_POINTER__ == 8
  //?
    u32 mSize;
#else
  //?
    size_t mSize;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _4
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C;
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
    u32 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_8[28];
    J3DPacket *mPacket;
#else
    J3DPacket *mPacket;
#endif

};