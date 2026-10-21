#pragma once

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JUtility/JUTColor.hxx>

class TSelectGrad : public JDrama::TViewObj {
public:
    TSelectGrad(const char *name);
    virtual ~TSelectGrad();

    virtual void perform(u32, JDrama::TGraphics *) override;

    void setStageColor(u8 stageID);
#if __SIZEOF_POINTER__ == 8
#else
    u32 _10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _14;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _18;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[12];
    JUtility::TColor mColorX1;
#else
    JUtility::TColor mColorX1;
#endif

    JUtility::TColor mColorX2;
};