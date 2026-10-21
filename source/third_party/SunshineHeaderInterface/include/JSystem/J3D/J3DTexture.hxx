#pragma once

#include <Dolphin/types.h>

class J3DTexCoord {
public:
#if __SIZEOF_POINTER__ == 8
#else
    u8 _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _1;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _2;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _3;
#endif
  // padding?

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[3];
#else
#endif
};

class J3DTexture {
public:
    u16 mImageNum;
    void *mTexHeader;

    J3DTexture(u16 imagenum, void *texheader);
    virtual ~J3DTexture();
};