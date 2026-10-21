#pragma once

#include <JSystem/J2D/J2DPane.hxx>
#include <JSystem/JUtility/JUTRect.hxx>

class J2DWindow : public J2DPane {
public:
    JUTRect mFillRect;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _FC[0x40 / 4];
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[88];
#else
#endif
};