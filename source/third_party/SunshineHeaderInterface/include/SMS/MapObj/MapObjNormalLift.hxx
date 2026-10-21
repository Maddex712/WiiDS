#pragma once

#include <Dolphin/types.h>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/MapObj/MapObjRail.hxx>

class TNormalLift : public TRailMapObj {
public:
    TNormalLift(const char *);
    virtual ~TNormalLift();

    virtual void control() override;
    virtual void initMapObj() override;
    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual void readRailFlag() override;
    virtual void resetPosition() override;
    virtual void setGroundCollision() override;
#if __SIZEOF_POINTER__ == 8
#else
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _152;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_2[2];
    f32 _154;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _158;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[12];
#else
    u8 _pc32_tail[3];
#endif
};