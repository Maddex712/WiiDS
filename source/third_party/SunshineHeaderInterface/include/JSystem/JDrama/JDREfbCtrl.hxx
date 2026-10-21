#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRRect.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>

namespace JDrama {
    class TEfbCtrl : public TViewObj {
    public:
        TEfbCtrl(const char *name) : TViewObj(name) {}
        ~TEfbCtrl() override;

        void perform(u32, JDrama::TGraphics *) override;

        void setSrcRect(const TRect &);

        TRect mSrcRect;
    };

    class TEfbCtrlTex final : public TEfbCtrl {
    public:
        TEfbCtrlTex(const char *name, TFlagT<u16>);
        ~TEfbCtrlTex() override;

        void perform(u32, JDrama::TGraphics *) override;

        void setTexAttb(const GXTexObj &);

        u16 mFlags;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[2];
        u32 mTexSizeS;
#else
        size_t mTexSizeS;
#endif

#if __SIZEOF_POINTER__ == 8
        u32 mTexSizeT;
#else
        size_t mTexSizeT;
#endif

        GXTexObj mTexObj;
    
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};

    class TEfbCtrlDisp final : public TEfbCtrl {
    public:
        TEfbCtrlDisp() : TEfbCtrl("<TEfbCtrlDisp>") {}
        ~TEfbCtrlDisp() override;

        void perform(u32, JDrama::TGraphics *) override;
    };
}  // namespace JDrama