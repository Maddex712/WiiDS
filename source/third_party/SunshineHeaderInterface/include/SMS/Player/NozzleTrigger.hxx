#pragma once

#include <Dolphin/types.h>
#include <SMS/Player/NozzleBase.hxx>

class TNozzleTrigger : public TNozzleBase {

public:
    enum SPRAYSTATE { INACTIVE, ACTIVE, DEAD };
#if __SIZEOF_POINTER__ == 8
#else
    bool _00;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[1];
                     // 0x0384
    u8 mSprayState;
#else
                     // 0x0384
    u8 mSprayState;
#endif
               // 0x0385
    s16 mSprayQuarterFramesLeft;  // 0x0386
    f32 mTriggerFill;
#if __SIZEOF_POINTER__ == 8
#else
             // 0x0388
    u32 _04;
#endif
                      // 0x038C

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
#endif
};