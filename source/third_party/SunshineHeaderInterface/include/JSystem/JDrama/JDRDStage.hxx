#pragma once

#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JDrama/JDREfbCtrl.hxx>
#include <JSystem/JDrama/JDRFlagT.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>

namespace JDrama {
    class TViewConnecter : public TViewObj {
    public:
        TViewConnecter(TViewObj *, TViewObj *, TFlagT<u16> flags, const char *name);
        ~TViewConnecter() override;

        void perform(u32, JDrama::TGraphics *) override;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_0[4];
        TViewObj *mCamera;
#else
        TViewObj *mCamera;
#endif

        TViewObj *mViewObj;
        TFlagT<u16> mViewFlags;
    };

    class TDStageDisp : public TViewConnecter {
    public:
        TDStageDisp(const char *name, TFlagT<u16>);
        ~TDStageDisp() override;

        void perform(u32, JDrama::TGraphics *) override;

        TEfbCtrlDisp *getEfbCtrlDisp();
    };
}  // namespace JDrama