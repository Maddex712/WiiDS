#pragma once

#include <Dolphin/types.h>

class J3DModel;
class J3DModelData;

enum J3DDeformAttachFlag { DEFORM_UNK_0, DEFORM_UNK_1 };

class J3DSkinDeform {
public:
    J3DSkinDeform();

    virtual void deform(J3DModel *);
    virtual ~J3DSkinDeform();

    void calcNrmMtx(J3DModel *);
    void initMtxIndexArray(J3DModelData *);
#if __SIZEOF_POINTER__ == 8
#else
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _10;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[28];
#else
#endif
};