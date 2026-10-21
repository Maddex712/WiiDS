#pragma once

#include <JSystem/J3D/J3DAnimation.hxx>
#include <JSystem/JDrama/JDRFrmGXSet.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObjPtrListT.hxx>
#include <JSystem/JGadget/List.hxx>

namespace JDrama {
    class TDStageGroup final : public TViewObjPtrListT<TViewObj> {
    public:
        TDStageGroup(TDisplay *display) : TViewObjPtrListT("<TDStageGroup>"), mFrmGXSet(display){};
        ~TDStageGroup() override;

        void perform(u32, TGraphics *) override;

        TFrmGXSet mFrmGXSet;
    };
}  // namespace JDrama