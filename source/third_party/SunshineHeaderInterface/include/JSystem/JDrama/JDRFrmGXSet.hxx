#pragma once

#include <JSystem/J3D/J3DAnimation.hxx>
#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGadget/List.hxx>

namespace JDrama {
    class TFrmGXSet final : public TViewObj {
    public:
        TFrmGXSet(TDisplay *display) : TViewObj("<FrmGXSet>"), mDisplay(display){};
        ~TFrmGXSet() override;

        void perform(u32, TGraphics *) override;

        TDisplay *mDisplay;
    };
}  // namespace JDrama