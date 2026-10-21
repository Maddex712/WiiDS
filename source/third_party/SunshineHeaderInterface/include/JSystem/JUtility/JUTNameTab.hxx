#pragma once

#include <Dolphin/types.h>

class ResNTab {};

class JUTNameTab {
public:
    JUTNameTab(const ResNTab *);  // ResNTab*

    s32 getIndex(const char *name) const;
    u16 calcKeyCode(const char *name) const;
    char *getName(const u16);

    ResNTab *mResTab;
#if __SIZEOF_POINTER__ == 8
#else
  // ResNTab*
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _8;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[10];
#else
#endif
};