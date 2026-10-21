#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DPane.hxx>
#include <JSystem/JUtility/JUTRect.hxx>

class J2DOrthoGraph : public J2DGrafContext {
public:
    J2DOrthoGraph(int x, int y, int w, int h);
    J2DOrthoGraph(const JUTRect &);
    ~J2DOrthoGraph();

    void setPort();
    void setLookat();
    void scissorBounds(JUTRect *, JUTRect *);
#if __SIZEOF_POINTER__ == 8
#else
    JUTRect _D8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _DC;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _E0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _E4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
#endif

#if __SIZEOF_POINTER__ == 8
#else
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[24];
#else
#endif
};