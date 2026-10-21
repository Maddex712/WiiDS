#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRViewObj.hxx>

#include <JSystem/JStage/JSGAmbientLight.hxx>

namespace JDrama {

    class TAmbColor : public TViewObj, public JStage::TAmbientLight {
    public:
        TAmbColor();
        virtual ~TAmbColor();

        virtual void load(JSUMemoryInputStream &stream);
        void perform(u32, TGraphics *) override;

        u8 r;  // 14
        u8 g;  // 15
        u8 b;  // 16
        u8 a;  // 17
    };

}  // namespace JDrama