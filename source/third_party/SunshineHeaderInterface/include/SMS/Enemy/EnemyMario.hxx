#pragma once

#include <SMS/M3DUtil/M3UModel.hxx>
#include <SMS/Player/Mario.hxx>

class M3UModelCommonMario : public M3UModelCommon {
public:
    virtual M3UMtxCalcSetInfo *getMtxCalc(const M3UMtxCalcSetInfo &) const;
};

class TEnemyMario : public TMario {
public:
    enum CONTEXTS {
        UNK0,
        CAUTIONWAIT,
        REPEATJUMP,
        DELAYJUMP,
        UNK5,
        IDLE,
        IDLENOCOL,
        WARPAWAY,
        RUNTONODE,
        RUNWAIT,
        STARTRUN,
        FALLING,
        LAYWAITING,
        RUNAWAY,
        MOVETONEWGRAPH,  //?
        LAUGHATTARGET  = 16,
        LEAVEMAP       = 17,
        LAUGHATTARGET2 = 18,
    };
#if __SIZEOF_POINTER__ == 8
#else
    s16 _00;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    s16 _02;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_2[4];
    s16 mEnemyHealth;
#else
    s16 mEnemyHealth;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    s16 _06;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _08[0x38 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[62];
    f32 mWarpSpeed;
#else
    f32 mWarpSpeed;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _44[0x38 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_7[96];
    void *mEnemyManager;
#else
    void *mEnemyManager;
#endif
  //?
};