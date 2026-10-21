#pragma once

#include <JSystem/J3D/J3DAnimation.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>

class TLensFlare : public JDrama::TViewObj {
public:
    TLensFlare(const char *name);
    virtual ~TLensFlare();

    virtual void perform(u32, JDrama::TGraphics *) override;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _10[0x3C / 4];
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[72];
#else
#endif
};