#pragma once

#include <Dolphin/types.h>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/MapObj/MapObjNormalLift.hxx>

union U32SplitU16 {
    struct {
        u16 upper;
        u16 lower;
    } u16;
    u32 u32;
};

class TWoodBlock : public TNormalLift {
public:
    TWoodBlock(const char *);
    virtual ~TWoodBlock();

    virtual void load(JSUMemoryInputStream &);
    virtual int calcRecycle() override;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _15C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _160;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    U32SplitU16 _164;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    U32SplitU16 _168;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[17];
#else
#endif
};