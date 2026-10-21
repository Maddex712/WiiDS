#pragma once

#include <JSystem/JDrama/JDRViewObj.hxx>

#include <SMS/M3DUtil/MActor.hxx>

class TTargetArrow : public JDrama::TViewObj {
public:
    TTargetArrow(const char *name) : JDrama::TViewObj(name) {
        mActor     = nullptr;
        mIsVisible = false;
    }

    void loadAfter() override;
    void perform(u32, JDrama::TGraphics *) override;

    void setPos(const TVec3f &pos);
#if __SIZEOF_POINTER__ == 8
#else
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[4];
    MActor *mActor;
#else
    MActor *mActor;
#endif

    bool mIsVisible;
};

extern TTargetArrow *gpTargetArrow;