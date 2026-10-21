#pragma once

#include <Dolphin/types.h>

enum JSUStreamSeekFrom { BEGIN, CURRENT, END };

class JSUIosBase {
public:
    JSUIosBase();
    virtual ~JSUIosBase();
#if __SIZEOF_POINTER__ == 8
#else
    u8 _4;
#endif
  // maybe state?

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
#endif
};