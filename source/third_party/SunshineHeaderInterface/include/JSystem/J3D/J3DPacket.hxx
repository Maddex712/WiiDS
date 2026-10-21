#pragma once

#include <Dolphin/types.h>

class J3DDisplayListObj {
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
    u8 _pc64_tail[24];
#else
#endif
};

class J3DPacket {
public:
    u32 *vTable;
    J3DPacket *mParentPacket;  // _4
    J3DPacket *mChildPacket;   // _8
};

class J3DDrawPacket : public J3DPacket {
public:
#if __SIZEOF_POINTER__ == 8
#else
    u8 _C[0x30 - 0xC];
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 *_30;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[64];
#else
#endif
};

class J3DCallbackPacket : public J3DPacket {};

class J3DShapePacket : public J3DCallbackPacket {};

class J3DMatPacket : public J3DDrawPacket {
public:
    J3DShapePacket *mShapePacket;
#if __SIZEOF_POINTER__ == 8
#else
  // _34
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
    u8 _pc64_tail[32];
#else
#endif
};