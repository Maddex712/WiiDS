#pragma once

#include <Dolphin/types.h>

constexpr size_t ObjDataTableSize = 360;
constexpr const char *gLiveManagerName =
    "\x96\xD8\x83\x7D\x83\x6C\x81\x5B\x83\x57\x83\x83\x81\x5B\x00";
constexpr const char *gUnkManagerName = "\x93\x47\x83\x4F\x83\x8B\x81\x5B\x83\x76\x00";

enum MapColType { MAP_STATIC, MAP_MOVE, MAP_WARP };

struct obj_info {
#if __SIZEOF_POINTER__ == 8
#else
    u16 _00;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _02;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    void *_04;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[16];
#else
#endif
};

struct sink_data {
#if __SIZEOF_POINTER__ == 8
#else
    f32 _00;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _04;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};

struct sound_data {
    u32 _00;
    u32 _04;
    u32 _08;
    u32 mUnkSoundID;
    u32 mReboundSoundID;
    u32 _14;
    u32 _18;
    u32 _1C;
    u32 _20;
    u32 _24;
};

struct sound_info {
    size_t mLength;
    sound_data *mSoundData;
};

struct anim_data {
    char *mMdlName;
    char *mName;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _08;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _0C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _10;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[24];
#else
#endif
};

struct hit_data {
    f32 mAttackRadius;
    f32 mAttackHeight;
    f32 mReceiveRadius;
    f32 mReceiveHeight;
};

struct obj_hit_info {
    u32 _00;
    u32 mType;
    f32 mVisualOfsY;
    hit_data *mHitData;
};

struct map_col_data {
    const char *mColName;
    MapColType mColType;
};

struct map_col_info {
    size_t mColNum;
    map_col_data *mColData;
};

struct ObjPhysicalData {
    f32 mGravity;
    f32 mFloorBounceSpeed;
    f32 mWallBounceSpeed;
    f32 mRotationStopFactor;  //?
    f32 mFloorBrakeFactor;
    f32 mRollBrakeFactor;  //?
    f32 mAirBrakeFactor;
    f32 mRollBrakeFactor2;
    f32 _20[0xC / 4];
    f32 mThrowDistance;
    f32 mThrowHeight;
};

struct ObjPhysicalInfo {
    u32 _00;  // Always 0xD?
    ObjPhysicalData *mPhysicalData;
    u32 _0C;
};

struct ObjData {
    const char *mMdlName;
    u32 mObjectID;
    const char *mLiveManagerName;
    const char *mObjKey;
    void *mAnimInfo;
    obj_hit_info *mObjCollisionData;
    obj_info *mMapCollisionInfo;
    sound_info *mSoundInfo;
    ObjPhysicalInfo *mPhysicalInfo;
    sink_data *mSinkData;
    void *_28;
    void *mBckMoveData;
    f32 _30;
    u32 mUnkFlags;
    u32 mKeyCode;  //?
};

extern ObjData *sObjDataTable[ObjDataTableSize];