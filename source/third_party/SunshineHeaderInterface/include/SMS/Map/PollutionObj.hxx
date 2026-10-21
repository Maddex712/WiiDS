#pragma once

#include <Dolphin/types.h>
#include <SMS/Map/PollutionPos.hxx>

#include <SMS/Map/JointObj.hxx>

class TPollutionObj : public TJointObj {

public:
#if __SIZEOF_POINTER__ == 8
#else
    u32 _00[0x10 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[16];
   // 0x0020
    u32 mCleanedDegree;
#else
   // 0x0020
    u32 mCleanedDegree;
#endif
  // 0x0030

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
#endif
};