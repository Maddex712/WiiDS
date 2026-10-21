#pragma once

#include <Dolphin/types.h>
#include <JSystem/JSupport/JSUList.hxx>

class JKRDisposer {
public:
    JKRDisposer();
    virtual ~JKRDisposer();
#if __SIZEOF_POINTER__ == 8
#else
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[8];
    JSUPtrLink mPtrLink;
#else
    JSUPtrLink mPtrLink;
#endif
  // _8
};