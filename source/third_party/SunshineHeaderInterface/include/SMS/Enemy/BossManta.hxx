#pragma once

#include <Dolphin/types.h>
#include <SMS/Enemy/SpineEnemy.hxx>

class TBossManta : public TSpineEnemy {

public:
#if __SIZEOF_POINTER__ == 8
#else
    u32 _00[0x3C / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[60];
        // 0x0150
    u32 mGeneration;
#else
        // 0x0150
    u32 mGeneration;
#endif
          // 0x018C
    f32 mSpeed;               // 0x0190
    f32 mAngularVelocity;
#if __SIZEOF_POINTER__ == 8
#else
     // 0x0194
    u32 _01;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[4];
                  // 0x0198
    u32 mDamageCounter;
#else
                  // 0x0198
    u32 mDamageCounter;
#endif
       // 0x019C
    u32 mInvincibilityTimer;  // 0x01A0
};