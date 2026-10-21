#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>

class TCameraInbetween {

public:
#if __SIZEOF_POINTER__ == 8
#else
    u32 _00[0x30 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[48];
   // 0x0000
    TVec3f mSubjectPos;
#else
   // 0x0000
    TVec3f mSubjectPos;
#endif
  // 0x0030
};