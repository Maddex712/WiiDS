#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>

class TLiveActor;

class TBGCheckData {
public:
    TBGCheckData();

    f32 getActiveJumpPower() const;
    TVec3f *getNormal() const;
    s32 getPlaneType();
    bool isIllegalData() const;
    bool isMarioThrough() const;
    bool isWaterSlip() const;
    bool isWaterSurface() const;
    void setVertex(const TVec3f &a, const TVec3f &b, const TVec3f &c);

    u16 mType;              // 0x0000
    s16 mValue;             // 0x0002
    u16 mFlags;             // 0x0004 | 0x10 = illegal
    u8 mSoundID;            // 0x0006 | Sound ID
    f32 mMinHeight;         // 0x0008
    f32 mMaxHeight;         // 0x000C
    TVec3f mVertices[3];    // 0x0010
    TVec3f mNormal;         // 0x0034
    f32 mProjectionFactor;  // 0x0040
    TLiveActor *mOwner;     // 0x0044
};

class TBGCheckList {
public:
    TBGCheckList();
    virtual void setPreNode(TBGCheckList *);

    TBGCheckList *mNextTriangle;  // 0x0004
    TBGCheckData *mColTriangle;   // 0x0008
};

class TBGCheckListRoot {
public:
    enum EPlaneType { GROUND, ROOF, WALL };

    TBGCheckListRoot();

    TBGCheckList mCheckList[3];  // 0x0000
};

class TBGCheckListWarp : public TBGCheckList {
public:
    TBGCheckListWarp();
    virtual void setPreNode(TBGCheckList *);

    TBGCheckList *mPreNode;  // _C
    s16 mCellX;
    s16 mCellZ;
};