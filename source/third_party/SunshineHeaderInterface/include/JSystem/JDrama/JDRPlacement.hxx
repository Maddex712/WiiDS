#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRFlagT.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>

namespace JDrama {

    class TPlacement : public TViewObj {
    public:
        TPlacement(const char *name)
            : TViewObj(name), mTranslation(0.0f, 0.0f, 0.0f), mPlacementFlags(0) {}
        virtual ~TPlacement();

        void load(JSUMemoryInputStream &) override;

        TVec3f mTranslation;
        TFlagT<u16> mPlacementFlags;
    };

}  // namespace JDrama