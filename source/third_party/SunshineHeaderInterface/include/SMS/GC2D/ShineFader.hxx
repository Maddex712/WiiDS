#pragma once

#include <JSystem/JSupport/JSUInputStream.hxx>
#include <SMS/GC2D/SmplFader.hxx>

class TShineFader : public TSmplFader {
public:
    virtual ~TShineFader();
    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual void update() override;

    void registFadeout(u16, u16);
#if __SIZEOF_POINTER__ == 8
#else
    u16 _38;
#endif


#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_tail[2];
#endif
};