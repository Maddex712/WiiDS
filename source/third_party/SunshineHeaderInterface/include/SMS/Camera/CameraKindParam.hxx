#pragma once

#include <Dolphin/types.h>
#include <SMS/System/Params.hxx>

#include <SMS/Camera/CamSaveKindParam.hxx>

struct TCameraKindParam {
    TCameraKindParam(const TCamSaveKindParam &params) { copySaveParam(params); }
    ~TCameraKindParam() {}

    void inbetweenData(const TCamSaveKindParam &params, f32 lerp);
    void copySaveParam(const TCamSaveKindParam &params);

    f32 mSLFovy;
    f32 mSLNearClip;
    f32 mSLDistMin;
    f32 mSLDistMax;
    f32 mSLCushionMin;
    f32 mSLCushionMax;
    s16 mSLXAngleMin;
    s16 mSLXAngleMax;
    f32 mSLXRotRatioManualSpeed;
    s16 mSLYAngleManualSpeedXMin;
    s16 mSLYAngleManualSpeedXMax;
    f32 mSLAtOffsetY;
    f32 mSLXRotRatioAtOffsetY;
    f32 mSLTargetAtJumpOffsetY;
    f32 mSLAtJumpOffsetSpeed;
    f32 mSLHeightPanChaseRateY;
    f32 mSLSecureViewChase;
    f32 mSLSecureViewDistXMin;
    f32 mSLSecureViewDistZMin;
    f32 mSLSecureViewDistXMax;
    f32 mSLSecureViewDistZMax;
    f32 mSLHoldAddDistXZMin;
    f32 mSLHoldAddDistXZMax;
    s16 mSLHoldOffsetAngleXMin;
    s16 mSLHoldOffsetAngleXMax;
    s16 mSLOffsetAngleX;
    s16 mSLOffsetAngleY;
    f32 mSLOffsetLookatXZ;
    s16 mSLMaxAddAngleY;
    u32 mSLAutoChaseStartFrame;
    u32 mSLAutoChaseCompleteFrame;
    f32 mSLFollowSpeedXmin;
    f32 mSLFollowSpeedXmax;
    f32 mSLJumpFollowSpeedXmin;
    f32 mSLJumpFollowSpeedXmax;
    f32 mSLObstructMaginfXmin;
    f32 mSLObstructMaginfXmax;
    f32 mSLInHouseMaginfXmin;
    f32 mSLInHouseMaginfXmax;
    f32 mSLFollowMaginfXmin;
    f32 mSLFollowMaginfXmax;
    f32 mSLPosChaseRateXZ;
    f32 mSLPosChaseRateXZ_C;
    f32 mSLPosChaseRateY;
    f32 mSLAtChaseRateXZ;
    f32 mSLAtChaseRateY;
};