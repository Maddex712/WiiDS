#pragma once

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/Graph/GraphTracer.hxx>
#include <SMS/Graph/GraphWeb.hxx>
#include <SMS/MapObj/MapObjGeneral.hxx>

class TRailMapObj : public TMapObjBase {
public:
    enum RailStatus { ACTIVE, UNK_1, RESETTING };

    TRailMapObj(const char *);
    virtual ~TRailMapObj();

    void load(JSUMemoryInputStream &) override;
    void perform(u32, JDrama::TGraphics *) override;
    void control() override;
    u32 getShadowType() override;
    void initMapObj() override;
    void setGroundCollision() override;

    virtual int calcRecycle();
    virtual void resetPosition();
    virtual void readRailFlag();

    bool checkMarioRiding();
    void initGraphTracer(TGraphWeb *);
    int moveToNextNode(f32);
    void resetStep(f32);

    TGraphTracer *mGraphTracer;
    s32 mDistanceToNext;
    u32 mControlState;  // 1 = mario contact; 2 = idle
    f32 mTravelSpeed;
    u8 mRailStatus;
    s16 mContextTimer;   // default = 180
    u8 mLastRailStatus;  // ?

#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_tail[3];
#endif
};