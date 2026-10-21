#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRGraphics.hxx>

class TMario;

class TMarioCap {
public:
    TMarioCap(TMario *);

    virtual void perform(u32, JDrama::TGraphics *);

    void createMirrorModel();
    void mtxEffectHide();
    void mtxEffectShow();
#if __SIZEOF_POINTER__ == 8
#else
    u32 _00;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _04;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _08;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[24];
    J3DModel *mCap1;
#else
    J3DModel *mCap1;
#endif

    J3DModel *mCap3;
    J3DModel *mDiverHelm;
    J3DModel *maGlass1;
};

extern const char *cDirtyFileName;
extern const char *cDirtyTexName;