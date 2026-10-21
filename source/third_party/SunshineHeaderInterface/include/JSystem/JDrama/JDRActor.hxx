#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRPlacement.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JStage/JSGActor.hxx>

namespace JDrama {

    class TActor : public TPlacement, public JStage::TActor {
    public:
        TActor(const char *);
        virtual ~TActor();

        virtual u32 getType() const override;
        virtual void load(JSUMemoryInputStream &) override;
        virtual void perform(u32, TGraphics *) override;
        virtual void JSGGetTranslation(Vec *) const override;
        virtual void JSGSetTranslation(const Vec &) override;
        virtual void JSGGetScaling(Vec *) const override;
        virtual void JSGSetScaling(const Vec &) override;
        virtual void JSGGetRotation(Vec *) const override;
        virtual void JSGSetRotation(const Vec &) override;

        TVec3f mScale;     // 24
        TVec3f mRotation;
#if __SIZEOF_POINTER__ == 8
#else
  // 30
        u32 _3C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[8];
           // 3C
        TNameRef *mRootRef;
#else
           // 3C
        TNameRef *mRootRef;
#endif

    };

}  // namespace JDrama