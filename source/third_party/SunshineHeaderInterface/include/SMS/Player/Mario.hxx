#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>
#include <JSystem/J3D/J3DDrawBuffer.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <SMS/Map/BGCheck.hxx>
#include <SMS/Player/MarioBlend.hxx>
#include <SMS/Player/MarioCap.hxx>
#include <SMS/Player/MarioDraw.hxx>
#include <SMS/Player/MarioEffect.hxx>
#include <SMS/Player/MarioGamePad.hxx>
#include <SMS/Player/MarioSound.hxx>
#include <SMS/Player/Watergun.hxx>
#include <SMS/Player/Yoshi.hxx>
#include <SMS/Strategic/TakeActor.hxx>
#include <SMS/System/BaseParam.hxx>
#include <SMS/System/Params.hxx>

#define MESSAGE_MARIO_TOSS 7
#define MESSAGE_MARIO_BURN 10

#define OBJECT_ID_MARIO       0x80000001
#define OBJECT_ID_WATER_SPRAY 0x1000001
#define OBJECT_ID_POINK       0x100000D

class TMario : public TTakeActor, public TDrawSyncCallback {
public:
    enum E_SIDEWALK_TYPE { NONE, LEFT, RIGHT };

    struct JumpSlipRecord {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};

    struct TEParams : TParams {};

    struct TDeParams : public TParams {
        TDeParams();

        TParamT<s16> mHPMax;            // Max health
        TParamT<f32> mRunningMax;       // Max running speed
        TParamT<f32> mDashMax;          // Turbo Nozzle max speed
        TParamT<f32> mDashAcc;          // Turbo Nozzle acceleration
        TParamT<f32> mDashBrake;        // Turbo Nozzle stop speed?
        TParamT<f32> mDashStartTime;    // Turbo Nozzle start
        TParamT<f32> mWaitingRotSp;     // Waiting rotation speed
        TParamT<f32> mRunningRotSpMin;  // Running rotation speed min
        TParamT<f32> mRunningRotSpMax;  // Running rotation speed max
        TParamT<f32> mRocketRotSp;      // Rocket Nozzle rotation speed
        TParamT<f32> mPumpingRotSpMin;  // Pumping rotation speed min
        TParamT<f32> mPumpingRotSpMax;  // Pumping rotation speed max
        TParamT<f32> mInvincibleTime;
        TParamT<s16> mFootPrintTimerMax;
        TParamT<f32> mWaterTriggerRate;
        TParamT<f32> mGraffitoNoDmgTime;
        TParamT<f32> mRestMax;
        TParamT<f32> mShadowSize;
        TParamT<f32> mShadowErase;
        TParamT<f32> mHoldRadius;
        TParamT<f32> mDamageRadius;
        TParamT<f32> mDamageHeight;
        TParamT<f32> mAttackHeight;
        TParamT<f32> mTrampleRadius;
        TParamT<f32> mPushupRadius;
        TParamT<f32> mPushupHeight;
        TParamT<f32> mHipdropRadius;
        TParamT<f32> mQuakeRadius;
        TParamT<f32> mQuakeHeight;
        TParamT<f32> mTramplePowStep1;
        TParamT<f32> mTramplePowStep2;
        TParamT<f32> mTramplePowStep3;
        TParamT<f32> mJumpWallHeight;
        TParamT<f32> mThrowPower;
        TParamT<f32> mSlipStart;
        TParamT<f32> mWasOnWaterSlip;
        TParamT<f32> mInWaterSlip;
        TParamT<f32> mToroccoRotSp;
        TParamT<s16> mRecoverTimer;
        TParamT<s16> mHotTimer;
        TParamT<f32> mFeelDeep;
        TParamT<f32> mDamageFallHeight;
        TParamT<f32> mForceSlipAngle;
        TParamT<f32> mClashSpeed;  // Wall bonk speed?
        TParamT<f32> mHangWallMovableAngle;
        TParamT<f32> mColMvMax;
        TParamT<s16> mNoFreezeTime;    // Wall hang no freeze timer
        TParamT<s16> mKickFreezeTime;  // Wall hang freeze timer
        TParamT<s16> mSurfStartFreezeTime;
        TParamT<f32> mSleepingCheckDist;
        TParamT<f32> mSleepingCheckHeight;
        TParamT<s16> mIllegalPlaneCtInc;  // Death plane timer increment
        TParamT<s16> mIllegalPlaneTime;   // Death plane timer max
    };

    struct TBodyAngleParams : public TParams {
        TBodyAngleParams(const char *prm)
            : SMS_TPARAM_INIT(mHeadRot, 0.0f), SMS_TPARAM_INIT(mWaistRoll, 0.0f),
              SMS_TPARAM_INIT(mWaistPitch, 80.0f), SMS_TPARAM_INIT(mWaistRollMax, 0),
              SMS_TPARAM_INIT(mWaistPitchMax, 1000), SMS_TPARAM_INIT(mWaistAngleChangeRate, 0.07f) {
            load(prm);
        }

        TParamT<f32> mHeadRot;
        TParamT<f32> mWaistRoll;
        TParamT<f32> mWaistPitch;
        TParamT<s16> mWaistRollMax;
        TParamT<s16> mWaistPitchMax;
        TParamT<f32> mWaistAngleChangeRate;
    };

    struct TAttackParams : public TParams {
        TAttackParams(const char *prm)
            : SMS_TPARAM_INIT(mRadius, 100.0f), SMS_TPARAM_INIT(mHeight, 50.0f) {
            load(prm);
        }

        TParamT<f32> mRadius;
        TParamT<f32> mHeight;
    };

    struct TJumpParams : public TParams {
        TJumpParams();

        TParamT<f32> mGravity;
        TParamT<f32> mSpinJumpGravity;
        TParamT<f32> mJumpingMax;
        TParamT<f32> mJumpSpeedBrake;
        TParamT<f32> mJumpSpeedAccelControl;
        TParamT<f32> mJumpSlideControl;
        TParamT<f32> mTurnJumpForce;
        TParamT<f32> mFenceSpeed;
        TParamT<f32> mFireDownForce;
        TParamT<f32> mFireDownControl;
        TParamT<f32> mFireBackVelocity;
        TParamT<f32> mBroadJumpForce;
        TParamT<f32> mBroadJumpForceY;
        TParamT<f32> mRotateJumpForceY;
        TParamT<f32> mPopUpSpeedY;
        TParamT<f32> mPopUpForceYMult;
        TParamT<f32> mBackJumpForce;
        TParamT<f32> mBackJumpForceY;
        TParamT<f32> mHipAttackSpeedY;
        TParamT<f32> mSuperHipAttackSpeedY;
        TParamT<f32> mJumpCatchRotXSp;
        TParamT<f32> mJumpCatchRotXMax;
        TParamT<f32> mRotBroadEnableV;
        TParamT<f32> mRotBroadJumpForce;
        TParamT<f32> mRotBroadJumpForceY;
        TParamT<f32> mTrampolineDec;
        TParamT<f32> mSecJumpEnableSp;
        TParamT<f32> mSecJumpForce;
        TParamT<f32> mSecJumpSpeedMult;
        TParamT<f32> mSecJumpXZMult;
        TParamT<f32> mTriJumpEnableSp;
        TParamT<f32> mUltraJumpForce;
        TParamT<f32> mUltraJumpSpeedMult;
        TParamT<f32> mUltraJumpXZMult;
        TParamT<f32> mValleyDepth;
        TParamT<f32> mThrownAccel;
        TParamT<f32> mThrownSlide;
        TParamT<f32> mThrownBrake;
        TParamT<f32> mTremblePower;
        TParamT<f32> mTrembleAccele;
        TParamT<f32> mTrembleBrake;
        TParamT<s16> mTrembleTime;
        TParamT<s16> mClashAngle;
        TParamT<f32> mJumpJumpCatchSp;
        TParamT<f32> mGetOffYoshiY;
        TParamT<s16> mSuperHipAttackCt;  // Time until super hip attack?
    };

    struct TRunParams : public TParams {
        TRunParams();

        TParamT<f32> mMaxSpeed;
        TParamT<f32> mVelMinusBrake;
        TParamT<f32> mAddBase;
        TParamT<f32> mAddVelDiv;
        TParamT<f32> mDecStartNrmY;
        TParamT<f32> mDecBrake;
        TParamT<f32> mSoft2Walk;
        TParamT<f32> mWalk2Soft;
        TParamT<f32> mSoftStepAnmMult;
        TParamT<f32> mRunAnmSpeedBase;
        TParamT<f32> mRunAnmSpeedMult;
        TParamT<f32> mMotBlendWalkSp;
        TParamT<f32> mMotBlendRunSp;
        TParamT<f32> mSwimDepth;
        TParamT<f32> mInWaterBrake;
        TParamT<f32> mInWaterAnmBrake;
        TParamT<f32> mPumpingSlideSp;
        TParamT<f32> mPumpingSlideAnmSp;
        TParamT<f32> mDoJumpCatchSp;
        TParamT<f32> mTurnNeedSp;
        TParamT<s16> mDashRotSp;
    };

    struct TSwimParams : public TParams {
        TSwimParams();

        TParamT<f32> mStartSp;
        TParamT<f32> mMoveSp;
        TParamT<f32> mMoveBrake;
        TParamT<s16> mSwimmingRotSpMin;
        TParamT<s16> mSwimmingRotSpMax;
        TParamT<s16> mPumpingRotSpMin;
        TParamT<s16> mPumpingRotSpMax;
        TParamT<f32> mGravity;
        TParamT<f32> mWaitBouyancy;
        TParamT<f32> mMoveBouyancy;
        TParamT<f32> mUpDownBrake;
        TParamT<f32> mCanJumpDepth;
        TParamT<f32> mEndDepth;
        TParamT<f32> mFloatHeight;
        TParamT<f32> mStartVMult;
        TParamT<f32> mStartVYMult;
        TParamT<f32> mRush;
        TParamT<f32> mAnmBrake;
        TParamT<f32> mPaddleSpeedUp;
        TParamT<f32> mPaddleJumpUp;
        TParamT<f32> mFloatUp;
        TParamT<f32> mWaterLevelCheckHeight;
        TParamT<f32> mPaddleDown;
        TParamT<s16> mWaitSinkTime;
        TParamT<f32> mCanBreathDepth;
        TParamT<f32> mWaitSinkSpeed;
        TParamT<f32> mAirDec;
        TParamT<f32> mAirDecDive;
        TParamT<f32> mAirInc;
    };

    struct THangFenceParams : public TParams {
        THangFenceParams()
            : SMS_TPARAM_INIT(mMoveSp, 0.1f), SMS_TPARAM_INIT(mAnmRate, 0.5f),
              SMS_TPARAM_INIT(mRapidTime, 2000), SMS_TPARAM_INIT(mLimitTime, 2400),
              SMS_TPARAM_INIT(mAnmRapid, 8.0f), SMS_TPARAM_INIT(mDescentSp, 10.0f) {
            load("/Mario/Hanging.prm");
        }

        TParamT<f32> mMoveSp;
        TParamT<f32> mAnmRate;
        TParamT<s16> mRapidTime;
        TParamT<s16> mLimitTime;
        TParamT<f32> mAnmRapid;
        TParamT<f32> mDescentSp;
    };

    struct THangRoofParams : public TParams {
        THangRoofParams() : SMS_TPARAM_INIT(mAnmMult, 0.3f) { load("/Mario/HangRoof.prm"); }

        TParamT<f32> mAnmMult;
    };

    struct TWireParams : public TParams {
        TWireParams();

        TParamT<s16> mRotSpeed;
        TParamT<s16> mRotSpeedTrgHover;
        TParamT<s16> mRotSpeedTrgTurbo;
        TParamT<s16> mRotSpeedTrgRocket;
        TParamT<s16> mRotSpeedMax;
        TParamT<s16> mRotStop;
        TParamT<s16> mRotGravity;
        TParamT<f32> mRotBrake;
        TParamT<f32> mJumpRate;
        TParamT<f32> mSwingRate;
        TParamT<f32> mWireJumpAccelControl;
        TParamT<f32> mWireJumpSlideControl;
        TParamT<f32> mWireJumpMult;
        TParamT<f32> mWireJumpBase;
        TParamT<f32> mWireSwingBrake;
        TParamT<f32> mWireSwingMax;
    };

    struct TPullParams : public TParams {
        TPullParams(const char *prm)
            : SMS_TPARAM_INIT(mPullRateV, 0.3f), SMS_TPARAM_INIT(mPullRateH, 0.05f),
              SMS_TPARAM_INIT(mOilPullRateV, 0.1f), SMS_TPARAM_INIT(mOilPullRateH, 0.01f) {
            load(prm);
        }

        TParamT<f32> mPullRateV;
        TParamT<f32> mPullRateH;
        TParamT<f32> mOilPullRateV;
        TParamT<f32> mOilPullRateH;
    };

    struct TClimbParams : public TParams {
        TClimbParams()
            : SMS_TPARAM_INIT(mClimbSp, 0.035f), SMS_TPARAM_INIT(mRotateSp, 3.0f),
              SMS_TPARAM_INIT(mClimbAnmRate, 1.0f / 256.0f), SMS_TPARAM_INIT(mCatchRadius, 100.0f),
              SMS_TPARAM_INIT(mCatchAngle, 0.8f) {
            load("/Mario/Bar.prm");
        }

        TParamT<f32> mClimbSp;
        TParamT<f32> mRotateSp;
        TParamT<f32> mClimbAnmRate;
        TParamT<f32> mCatchRadius;
        TParamT<f32> mCatchAngle;
    };

    struct TSurfingParams : public TParams {
        TSurfingParams(const char *prm);

        TParamT<f32> mRotMin;
        TParamT<f32> mRotMax;
        TParamT<f32> mPowMin;
        TParamT<f32> mPowMax;
        TParamT<f32> mAccel;
        TParamT<f32> mWaistRoll;
        TParamT<f32> mWaistPitch;
        TParamT<s16> mWaistRollMax;
        TParamT<s16> mWaistPitchMax;
        TParamT<f32> mRoll;
        TParamT<f32> mPitch;
        TParamT<s16> mRollMax;
        TParamT<s16> mPitchMax;
        TParamT<f32> mAngleChangeRate;
        TParamT<f32> mWaistAngleChangeRate;
        TParamT<f32> mScaleMin;
        TParamT<f32> mScaleMax;
        TParamT<f32> mScaleMinSpeed;
        TParamT<f32> mScaleMaxSpeed;
        TParamT<f32> mJumpPow;
        TParamT<f32> mJumpXZRatio;
        TParamT<f32> mClashSpeed;
        TParamT<s16> mClashAngle;
    };

    struct THoverParams : public TParams {
        THoverParams()
            : SMS_TPARAM_INIT(mRotSp, 128), SMS_TPARAM_INIT(mAccelRate, 0.03f),
              SMS_TPARAM_INIT(mBrake, 0.95f) {
            load("/Mario/HHover.prm");
        }

        TParamT<s16> mRotSp;
        TParamT<f32> mAccelRate;
        TParamT<f32> mBrake;
    };

    struct TDivingParams : public TParams {
        TDivingParams()
            : SMS_TPARAM_INIT(mRotSp, 128), SMS_TPARAM_INIT(mGravity, 0.5f),
              SMS_TPARAM_INIT(mAccelControl, 0.02f), SMS_TPARAM_INIT(mSeaBrake, 0.999f),
              SMS_TPARAM_INIT(mSeaBrakeY, 0.98f) {
            load("/Mario/Diving.prm");
        }

        TParamT<s16> mRotSp;
        TParamT<f32> mGravity;
        TParamT<f32> mAccelControl;
        TParamT<f32> mSeaBrake;
        TParamT<f32> mSeaBrakeY;
    };

    struct TYoshiParams : public TParams {
        TYoshiParams()
            : SMS_TPARAM_INIT(mRunYoshiMult, 1.2f), SMS_TPARAM_INIT(mJumpYoshiMult, 1.0f),
              SMS_TPARAM_INIT(mRotYoshiMult, 1.5f), SMS_TPARAM_INIT(mHeadFront, 80.0f),
              SMS_TPARAM_INIT(mHeadRadius, 50.0f), SMS_TPARAM_INIT(mHoldOutAccCtrlF, 0.01f),
              SMS_TPARAM_INIT(mHoldOutAccCtrlB, 0.023f), SMS_TPARAM_INIT(mHoldOutSldCtrl, 0.3f),
              SMS_TPARAM_INIT(mDecBrake, 1.0f) {
            load("/Mario/Yoshi.prm");
        }

        TParamT<f32> mRunYoshiMult;
        TParamT<f32> mJumpYoshiMult;
        TParamT<f32> mRotYoshiMult;
        TParamT<f32> mHeadFront;
        TParamT<f32> mHeadRadius;
        TParamT<f32> mHoldOutAccCtrlF;
        TParamT<f32> mHoldOutAccCtrlB;
        TParamT<f32> mHoldOutSldCtrl;
        TParamT<f32> mDecBrake;
    };

    struct TWaterEffectParams : public TParams {
        TWaterEffectParams()
            : SMS_TPARAM_INIT(mJumpIntoMdlEffectSpY, 10.0f), SMS_TPARAM_INIT(mJumpIntoMinY, 20.0f),
              SMS_TPARAM_INIT(mJumpIntoMaxY, 50.0f), SMS_TPARAM_INIT(mJumpIntoScaleMin, 0.75f),
              SMS_TPARAM_INIT(mJumpIntoScaleWidth, 1.0f),
              SMS_TPARAM_INIT(mRunningRippleSpeed, 30.0f),
              SMS_TPARAM_INIT(mRunningRippleDepth, 30.0f) {
            load("/Mario/WaterEffect.prm");
        }

        TParamT<f32> mJumpIntoMdlEffectSpY;
        TParamT<f32> mJumpIntoMinY;
        TParamT<f32> mJumpIntoMaxY;
        TParamT<f32> mJumpIntoScaleMin;
        TParamT<f32> mJumpIntoScaleWidth;
        TParamT<f32> mRunningRippleSpeed;
        TParamT<f32> mRunningRippleDepth;
    };

    struct TControllerParams : public TParams {
        TControllerParams()
            : SMS_TPARAM_INIT(mAnalogLRToZeroVal, 30), SMS_TPARAM_INIT(mAnalogLRToMiddleVal, 90),
              SMS_TPARAM_INIT(mAnalogLRToMaxVal, 150), SMS_TPARAM_INIT(mAnalogLRMiddleVal, 0.1f),
              SMS_TPARAM_INIT(mStartToWalkLevel, 15.0f), SMS_TPARAM_INIT(mStickRotateTime, 24),
              SMS_TPARAM_INIT(mLengthMultTimes, 10), SMS_TPARAM_INIT(mLengthMult, 0.935f),
              SMS_TPARAM_INIT(mSquatRotMidAnalog, 0.7f), SMS_TPARAM_INIT(mSquatRotMidValue, 0.05f) {
            load("/Mario/Controller.prm");
        }

        TParamT<u8> mAnalogLRToZeroVal;
        TParamT<u8> mAnalogLRToMiddleVal;
        TParamT<u8> mAnalogLRToMaxVal;
        TParamT<f32> mAnalogLRMiddleVal;
        TParamT<f32> mStartToWalkLevel;
        TParamT<s16> mStickRotateTime;
        TParamT<s16> mLengthMultTimes;
        TParamT<f32> mLengthMult;
        TParamT<f32> mSquatRotMidAnalog;
        TParamT<f32> mSquatRotMidValue;
    };

    struct TGraffitoParams : public TParams {
        TGraffitoParams();

        TParamT<s16> mSinkTime;
        TParamT<s16> mSinkDmgTime;
        TParamT<f32> mSinkHeight;
        TParamT<f32> mSinkMoveMin;
        TParamT<f32> mSinkMoveMax;
        TParamT<f32> mSinkRecover;
        TParamT<f32> mSinkJumpRateMin;
        TParamT<f32> mSinkJumpRateMax;
        TParamT<f32> mSinkPumpLimit;
        TParamT<f32> mSinkDmgDepth;
        TParamT<f32> mFireHeight;
        TParamT<s16> mDizzySlipCtMax;
        TParamT<s16> mDizzyWalkCtMax;
        TParamT<s16> mDizzyAngleY;
        TParamT<f32> mDizzyAngleRate;
        TParamT<f32> mDizzyPowerRate;
        TParamT<f32> mDizzyPower;
        TParamT<s16> mFireInvincibleTime;
        TParamT<s16> mFootEraseTimes;
        TParamT<f32> mFootEraseSize;
        TParamT<f32> mFootEraseFront;
    };

    struct TDirtyParams : public TParams {
        TDirtyParams()
            : SMS_TPARAM_INIT(mIncRunning, 0.1f), SMS_TPARAM_INIT(mIncCatching, 0.3f),
              SMS_TPARAM_INIT(mIncSlipping, 0.2f), SMS_TPARAM_INIT(mDecSwimming, 0.5f),
              SMS_TPARAM_INIT(mDecWaterHit, 0.2f), SMS_TPARAM_INIT(mDecRotJump, 0.1f),
              SMS_TPARAM_INIT(mBrakeStartValSlip, 0.99f), SMS_TPARAM_INIT(mBrakeStartValRun, 0.98f),
              SMS_TPARAM_INIT(mDirtyTimeSlip, 600), SMS_TPARAM_INIT(mDirtyTimeRun, 600),
              SMS_TPARAM_INIT(mPolSizeSlip, 200.0f), SMS_TPARAM_INIT(mPolSizeRun, 80.0f),
              SMS_TPARAM_INIT(mPolSizeFootPrint, 200.0f), SMS_TPARAM_INIT(mPolSizeJump, 200.0f),
              SMS_TPARAM_INIT(mSlopeAngle, 0.99f), SMS_TPARAM_INIT(mDirtyMax, 200.0f),
              SMS_TPARAM_INIT(mSlipAnmSpeed, 3.0f), SMS_TPARAM_INIT(mSlipRunSp, 0.01f),
              SMS_TPARAM_INIT(mSlipCatchSp, 0.01f), SMS_TPARAM_INIT(mSlipRotate, 100),
              SMS_TPARAM_INIT(mSlipCatchRotate, 100), SMS_TPARAM_INIT(mBrakeSlipNoPollute, 0.98f),
              SMS_TPARAM_INIT(mFogTimeYellow, 240), SMS_TPARAM_INIT(mFogTimeRed, 600) {
            load("/Mario/Dirty.prm");
        }

        TParamT<f32> mIncRunning;
        TParamT<f32> mIncCatching;
        TParamT<f32> mIncSlipping;
        TParamT<f32> mDecSwimming;
        TParamT<f32> mDecWaterHit;
        TParamT<f32> mDecRotJump;
        TParamT<f32> mBrakeStartValSlip;
        TParamT<f32> mBrakeStartValRun;
        TParamT<s16> mDirtyTimeSlip;
        TParamT<s16> mDirtyTimeRun;
        TParamT<f32> mPolSizeSlip;
        TParamT<f32> mPolSizeRun;
        TParamT<f32> mPolSizeFootPrint;
        TParamT<f32> mPolSizeJump;
        TParamT<f32> mSlopeAngle;
        TParamT<f32> mDirtyMax;
        TParamT<f32> mSlipAnmSpeed;
        TParamT<f32> mSlipRunSp;
        TParamT<f32> mSlipCatchSp;
        TParamT<s16> mSlipRotate;
        TParamT<s16> mSlipCatchRotate;
        TParamT<f32> mBrakeSlipNoPollute;
        TParamT<s16> mFogTimeYellow;
        TParamT<s16> mFogTimeRed;
    };

    struct TMotorParams : public TParams {
        TMotorParams()
            : SMS_TPARAM_INIT(mMotorReturn, 25), SMS_TPARAM_INIT(mMotorTrample, 8),
              SMS_TPARAM_INIT(mMotorHipDrop, 15), SMS_TPARAM_INIT(mMotorWall, 6) {
            load("/Mario/MarioMotor.prm");
        }

        TParamT<s16> mMotorReturn;
        TParamT<s16> mMotorTrample;
        TParamT<s16> mMotorHipDrop;
        TParamT<s16> mMotorWall;
    };

    struct TParticleParams : public TParams {
        TParticleParams()
            : SMS_TPARAM_INIT(mMeltInWaterMax, 0.5f), SMS_TPARAM_INIT(mWaveEmitSpeed, 5.0f),
              SMS_TPARAM_INIT(mWaveAlphaDec, 5), SMS_TPARAM_INIT(mBubbleDepth, 10.0f),
              SMS_TPARAM_INIT(mBodyBubbleSpMin, 0.0f), SMS_TPARAM_INIT(mBodyBubbleSpMax, 40.0f),
              SMS_TPARAM_INIT(mBodyBubbleEmitMin, 0.0f), SMS_TPARAM_INIT(mBodyBubbleEmitMax, 0.5f),
              SMS_TPARAM_INIT(mBubbleToRipple, 0.3f), SMS_TPARAM_INIT(mToroccoWind, 0.001f),
              SMS_TPARAM_INIT(mToroccoSpark, 0.001f) {
            load("/Mario/MarioParticle.prm");
        }

        TParamT<f32> mMeltInWaterMax;
        TParamT<f32> mWaveEmitSpeed;
        TParamT<s16> mWaveAlphaDec;
        TParamT<f32> mBubbleDepth;
        TParamT<f32> mBodyBubbleSpMin;
        TParamT<f32> mBodyBubbleSpMax;
        TParamT<f32> mBodyBubbleEmitMin;
        TParamT<f32> mBodyBubbleEmitMax;
        TParamT<f32> mBubbleToRipple;
        TParamT<f32> mToroccoWind;
        TParamT<f32> mToroccoSpark;
    };

    struct TEffectParams : public TParams {
        TEffectParams()
            : SMS_TPARAM_INIT(mDashInc, 1.0f / 3.0f), SMS_TPARAM_INIT(mDashDec, 1.0f / 6.0f),
              SMS_TPARAM_INIT(mDashMaxBlendInBlur, 180), SMS_TPARAM_INIT(mDashMaxBlendInIris, 180),
              SMS_TPARAM_INIT(mDashBlendScale, 0.2f) {
            load("/Mario/MarioEffect.prm");
        }

        TParamT<f32> mDashInc;
        TParamT<f32> mDashDec;
        TParamT<u8> mDashMaxBlendInBlur;
        TParamT<u8> mDashMaxBlendInIris;
        TParamT<f32> mDashBlendScale;
    };

    struct TSlipParams : public TParams {
        TSlipParams(const char *prm)
            : SMS_TPARAM_INIT(mSlipFriction, 0.9f), SMS_TPARAM_INIT(mSlopeAcceleUp, 0.0f),
              SMS_TPARAM_INIT(mSlopeAcceleDown, 0.0f), SMS_TPARAM_INIT(mSlideAcceleUp, 0.0f),
              SMS_TPARAM_INIT(mSlideAcceleDown, 0.0f), SMS_TPARAM_INIT(mSlideStopNormal, 15.0f),
              SMS_TPARAM_INIT(mSlideStopCatch, 15.0f), SMS_TPARAM_INIT(mJumpEnable, 1),
              SMS_TPARAM_INIT(mMissJump, 1), SMS_TPARAM_INIT(mSlideAngleYSp, 512),
              SMS_TPARAM_INIT(mStickSlideMult, 0.05) {
            load(prm);
        }

        TParamT<f32> mSlipFriction;
        TParamT<f32> mSlopeAcceleUp;
        TParamT<f32> mSlopeAcceleDown;
        TParamT<f32> mSlideAcceleUp;
        TParamT<f32> mSlideAcceleDown;
        TParamT<f32> mSlideStopNormal;
        TParamT<f32> mSlideStopCatch;
        TParamT<u8> mJumpEnable;
        TParamT<u8> mMissJump;
        TParamT<s16> mSlideAngleYSp;
        TParamT<f32> mStickSlideMult;
    };

    struct TUpperBodyParams : public TParams {
        TUpperBodyParams()
            : SMS_TPARAM_INIT(mPumpWaitTime, 10), SMS_TPARAM_INIT(mPumpAnmSpeed, 0.01f),
              SMS_TPARAM_INIT(mHoverHeadAngle, -8192), SMS_TPARAM_INIT(mFeelDeepHeadAngle, 8192),
              SMS_TPARAM_INIT(mFrontWallHeadAngle, -8192) {
            load("/Mario/UpperBody.prm");
        }

        TParamT<s16> mPumpWaitTime;
        TParamT<f32> mPumpAnmSpeed;
        TParamT<s16> mHoverHeadAngle;
        TParamT<s16> mFeelDeepHeadAngle;
        TParamT<s16> mFrontWallHeadAngle;
    };

    struct TDmgEnemyParams : public TParams {
        TDmgEnemyParams(const char *prm)
            : SMS_TPARAM_INIT(mDamage, 1), SMS_TPARAM_INIT(mDownType, 1),
              SMS_TPARAM_INIT(mWaterEmit, 0), SMS_TPARAM_INIT(mMotor, 25),
              SMS_TPARAM_INIT(mMinSpeed, 16.0f), SMS_TPARAM_INIT(mDirty, 0.0f),
              SMS_TPARAM_INIT(mInvincibleTime, 300) {
            load(prm);
        }

        TParamT<u8> mDamage;
        TParamT<u8> mDownType;
        TParamT<u8> mWaterEmit;
        TParamT<u8> mMotor;
        TParamT<f32> mMinSpeed;
        TParamT<f32> mDirty;
        TParamT<s16> mInvincibleTime;
    };

    struct TDemoParams : public TParams {
        TDemoParams()
            : SMS_TPARAM_INIT(mWarpInBallsDispTime, 6), SMS_TPARAM_INIT(mWarpInBallsTime, 70),
              SMS_TPARAM_INIT(mWarpInCapturedTime, 120), SMS_TPARAM_INIT(mWarpInTremble, 15.0f),
              SMS_TPARAM_INIT(mWarpInVecBase, 0.3f), SMS_TPARAM_INIT(mWarpTransTremble, 50.0f),
              SMS_TPARAM_INIT(mReadRotSp, 1024) {
            load("/Mario/AutoDemo.prm");
        }

        TParamT<s16> mWarpInBallsDispTime;
        TParamT<s16> mWarpInBallsTime;
        TParamT<s16> mWarpInCapturedTime;
        TParamT<f32> mWarpInTremble;
        TParamT<f32> mWarpInVecBase;
        TParamT<f32> mWarpTransTremble;
        TParamT<s16> mReadRotSp;
    };

    struct TSoundParams : public TParams {
        TSoundParams() : SMS_TPARAM_INIT(mStartFallVoiceSpeed, 60.0f) { load("/Mario/Sound.prm"); }

        TParamT<f32> mStartFallVoiceSpeed;
    };

    struct TOptionParams : public TParams {
        TOptionParams()
            : SMS_TPARAM_INIT(mZ, -1000.0f), SMS_TPARAM_INIT(mXMin, 846.0f),
              SMS_TPARAM_INIT(mXMax, 2000.0f) {
            load("/Mario/Option.prm");
        }

        TParamT<f32> mZ;
        TParamT<f32> mXMin;
        TParamT<f32> mXMax;
    };

    enum State : u32 {
        STATE_NUMBER          = 0x0000000F,
        STATE_DOJUMP          = 0x00000080,
        STATE_AIRBORN         = 0x00000800,
        STATE_CUTSCENE        = 0x00001000,
        STATE_WATERBORN       = 0x00002000,
        STATE_RUNNING         = 0x04000440,
        STATE_IDLE            = 0x0C400201,
        STATE_STOP            = 0x0C00023D,
        STATE_SPIN            = 0x00000441,
        STATE_JUMPSPIN        = 0x00000890,
        STATE_JUMPSPINR       = 0x00000895,
        STATE_JUMPSPINL       = 0x00000896,
        STATE_JUMP            = 0x02000880,
        STATE_SLIP_JUMP       = 0x02000885,
        STATE_D_JUMP          = 0x02000881,
        STATE_TRIPLE_J        = 0x00000882,
        STATE_BACK_FLIP       = 0x00000883,
        STATE_JMP_LAND        = 0x04000470,
        STATE_HVY_LAND        = 0x04000473,
        STATE_D_LAND          = 0x04000472,
        STATE_T_LAND          = 0x0800023A,
        STATE_LAND_RECOVER    = 0x0C000230,
        STATE_D_LAND_RECOVER  = 0x0C000231,
        STATE_JUMPSIDE        = 0x00000880,
        STATE_BOUNCE          = 0x00000884,
        STATE_SIDE_STEP       = 0x0C008220,
        STATE_SIDE_STEP_LEAVE = 0x0C008222,
        STATE_SIDE_FLIP       = 0x00000887,
        STATE_FALL            = 0x0000088C,
        STATE_SWIM            = 0x000024D7,
        STATE_DIVE            = 0x0080088A,
        STATE_DIVEJUMP        = 0x02000889,
        STATE_DIVESLIDE       = 0x00800456,
        STATE_GOOPSLIDE       = 0x0084045D,
        STATE_CLIMB           = 0x18100340,
        STATE_CLIMBUP         = 0x10100343,
        STATE_WALLJUMP        = 0x02000886,
        STATE_WALLSLIDE       = 0x000008A7,
        STATE_ROOFHANG        = 0x00200349,
        STATE_HANG            = 0x3800034B,
        STATE_HANGCLIMB       = 0x3000054F,
        STATE_SLAMSTART       = 0x008008A9,
        STATE_SLAM            = 0x0080023C,
        STATE_SPRAY           = 0x0C008220,
        STATE_THROWN          = 0x000208B8,
        STATE_HOVER           = 0x0000088B,
        STATE_STUCK           = 0x0002033C,
        STATE_TALKING         = 0x10001308,
        STATE_TURNING_MID     = 0x00000443,
        STATE_TURNING         = 0x00000444,
        STATE_YOSHI_ESC       = 0x0000089C,
        STATE_SHINE_C         = 0x00001302,  // Collected Shine Sprite
        STATE_DEATH           = 0x00020467,
        STATE_DOOR_F_O        = 0x00001321,  // Door open fail
        STATE_WALL_S_L        = 0x04000471,
        STATE_F_KNCK_H        = 0x000208B0,  // hard knockback from front (bumping wall from dive)
        STATE_KNCK_LND        = 0x00020462,  // Landing from front knockback
        STATE_KNCK_GND        = 0x00020466,  // Front knockback while grounded
        STATE_FIRE_HIT        = 0x000208B7,
        STATE_FIRE_RVR        = 0x08000239,  // Recover from fire on ground
        STATE_HOVER_F         = 0x0000088D,  // Falling from hover
        STATE_SLIDE_R1        = 0x000008A6,  // Recover from slide by flipping
        STATE_SLIDE_R2        = 0x00000386,  // Recover from slide by getting up
        STATE_R_SPRAY         = 0x0C008220,  // Recover from spray
        STATE_G_POUND         = 0x008008A9,  // Ground pounding
        STATE_NPC_PUTDOWN     = 0x80000387,
        STATE_NPC_THROW       = 0x80000588,
        STATE_NPC_JUMPTHROW   = 0x820008AB,
        STATE_NPC_BOUNCE      = 0x02000890
    };

    enum Status : u32 {
        STATUS_ALIVE                  = 0x1,
        STATUS_SEWER_FLOOR            = 0x2,
        STATUS_INVISIBLE              = 0x4,
        STATUS_NPC_TALKING            = 0x8,
        STATUS_LEAVING_WATER          = 0x10,
        STATUS_SLIP                   = 0x80,
        STATUS_GAMEOVER               = 0x400,
        STATUS_UP_GROUND_POUND        = 0x800,
        STATUS_HASHELMET_FOLLOWCAMERA = 0x1000,
        STATUS_HASHELMET              = 0x2000,
        STATUS_HASFLUDD               = 0x8000,
        STATUS_SPLASH                 = 0x10000,
        STATUS_PUDDLE                 = 0x20000,
        STATUS_SHIRT                  = 0x100000,
        STATUS_GONE                   = 0x200000
    };

    enum Voice : u32 {
        VOICE_CANNON_WAIL     = 30911,
        VOICE_TRIPLE_JUMP     = 30902,
        VOICE_JUMP            = 30891,
        VOICE_DIVE_OUT        = 30897,
        VOICE_FALL_LEDGE_GRAB = 30944
    };

    enum Animation : u32 {
        ANIMATION_WALLHANG      = 0x33,
        ANIMATION_FALL          = 0x4C,
        ANIMATION_BOUNCE        = 0x50,
        ANIMATION_STEADY_STANCE = 0x98,
        ANIMATION_IDLE          = 0xC3,
        ANIMATION_SHINEGET      = 0xCD,
        ANIMATION_SPINJUMP      = 0xF4
    };

    enum Effect : u32 {
        EFFECT_SMOKE_CLOUD          = 0x1,
        EFFECT_ROCKET_SPRAY_MIST    = 0x2,
        EFFECT_ROCKET_SPRAY_DROPS   = 0x3,
        EFFECT_BURNING_SMOKE        = 0x6,
        EFFECT_GROUND_SHARP_SHOCK   = 0x7,
        EFFECT_STARS                = 0xC,
        EFFECT_SPARKLE              = 0xE,
        EFFECT_WATER_RECHARGE       = 0xF,
        EFFECT_GROUND_SHOCK         = 0x10,
        EFFECT_GROUND_SMOKE_PLUME   = 0x11,
        EFFECT_WARP_IN_BODY_PIECES  = 0x24,
        EFFECT_WARP_IN_BELT_PIECES  = 0x25,
        EFFECT_WARP_IN_HAT_PIECES   = 0x26,
        EFFECT_WARP_IN_RED_PIECES   = 0x27,
        EFFECT_WARP_IN_BLUE_PIECES  = 0x29,
        EFFECT_WARP_IN_BROWN_PIECES = 0x2A,
        EFFECT_WARP_IN_FLUDD_PIECES = 0x2D,
        EFFECT_WATER_RIPPLE         = 0x30
    };

    TMario();
    virtual ~TMario();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void loadAfter() override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *receiver, u32 msg) override;
    virtual Mtx44 *getTakingMtx() override;
    virtual u32 moveRequest(const TVec3f &destPosition) override;
    virtual void initValues();
    virtual void checkReturn();
    virtual void checkController(JDrama::TGraphics *);
    virtual void playerControl(JDrama::TGraphics *);
    virtual void initModel();
    virtual void drawSpecial(JDrama::TGraphics *);
    virtual void checkCollision();
    virtual void damageExec(THitActor *, int, int, int, f32, int, f32, s16);
    virtual u8 getVoiceStatus();
    virtual void drawSyncCallback(u16) override;

    bool actnMain();
    void addCallBack(JDrama::TGraphics *);
    void addDamageFog(JDrama::TGraphics *);
    void addDirty();
    void addUpper();
    void addVelocity(f32);
    void animSound();
    bool askJumpIntoWaterEffectExist() const;
    bool barClimb();
    bool barProcess();
    bool barWait();
    void blurEffect();
    bool boardJumping();
    void boxDrawPrepare(Mtx);
    void bubbleFromBody();
    void bubbleFromMouth(int);
    void calcAnim(u32, JDrama::TGraphics *);
    void calcBaseMtx(Mtx);
    void calcDamagePos(const TVec3f &);
    void calcGroundMtx(const TVec3f &);
    void calcView(JDrama::TGraphics *);
    bool canBendBody();
    bool canPut();
    bool canSleep();
    bool canSlipJump();
    bool canSquat() const;
    bool canTake(THitActor *actor);
    bool catching();
    bool catchStop();
    void changeHand(int hand);
    void changeMontemanWaitingAnim();
    int changePlayerDropping(u32 state, u32 jumpSlipState);
    int changePlayerJumping(u32 state, u32 jumpSlipState);
    int changePlayerStatus(u32 state, u32 jumpSlipState, bool ifGrounded);
    bool changePlayerTriJump();
    void changeWireHanging();
    bool checkAllMotions();
    int checkBackTrig();
    void checkCurrentPlane();
    void checkDescent();
    void checkEnforceJump();
    void checkGraffito();
    void checkGraffitoElec();
    void checkGraffitoFire();
    void checkGraffitoSlip();
    bool checkGroundAtJumping(const Vec &, int);
    int checkGroundAtWalking(Vec *);
    int checkGroundPlane(f32 x, f32 y, f32 z, f32 *out, const TBGCheckData **);
    void checkPlayerAction(JDrama::TGraphics *);
    void checkPlayerAround(int, f32);
    bool checkPumpEnable();
    void checkPumping();
    void checkRideMovement();
    void checkRideReCalc();
    f32 checkRoofPlane(const Vec &, f32, const TBGCheckData **);
    void checkSink();
    bool checkStatusType(s32) const;
    int checkStickRotate(int *out);
    bool checkSwimJump();
    void checkThrowObject();
    void checkWallPlane(Vec *pos, f32 width, f32 height);
    void checkWet();
    bool considerJumpRotate();
    bool considerRotateJumpStart();
    bool considerRotateStart();
    void considerTake();
    void considerWaist();
    void decHP(int);
    bool demoMain();
    void dirtyLimitCheck();
    bool diving();
    void doJumping();
    void doPushingAnimation(const Vec &);
    u8 doRoofMovingProcess();
    void doRunning();
    void doRunningAnimation();
    void doSliding();
    void doSurfing();
    void doSwimming();
    f32 downingCommon(int, f32, int);
    void drawLogic();
    void dropObject();
    void elecEffect();
    void elecEndEffect();
    bool electricDamage();
    void emitBlurHipDrop();
    void emitBlurHipDropSuper();
    void emitBlurSpinJump();
    void emitDirtyFootPrint();
    void emitFootPrintWithEffect(int, int);
    void emitGetCoinEffect(TVec3f *);
    void emitGetEffect();
    void emitGetWaterEffect();
    bool emitParticle(int id);
    bool emitParticle(int id, const TVec3f *);
    bool emitParticle(int id, s16);
    void emitRotateShootEffect();
    void emitSandEffect();
    void emitSmoke(s16);
    void emitSweat(s16);
    void emitSweatSometimes();
    void entryModels(JDrama::TGraphics *);
    void fallProcess();
    bool fenceMove();
    bool fencePunch();
    void finalDrawInitialize();
    bool fireDashing();
    bool fireDowning();
    void floorDamageExec(int damage, int type, int emitcount, int tremble);
    void floorDamageExec(const TEParams &);
    void flowMove(const TVec3f &);
    bool footDowning();
    void frontSlipEffect();
    s16 getAttackAngle(const THitActor *);
    Mtx *getCenterAnmMtx();
    f32 getChangeAngleSpeed();
    f32 getCurrentFrame(int);
    TPullParams *getCurrentPullParams(f32 *, f32 *);
    void *getDmgMapCode(int) const;
    void *getGesso(THitActor *);
    f32 getJumpAccelControl() const;
    f32 getJumpSlideControl() const;
    J3DFrameCtrl *getMotionFrameCtrl();
    void getOffYoshi(bool knockedOff);
    s16 getOnWirePosAngle(TVec3f *);
    f32 getPumpFrame() const;
    void getRidingMtx(Mtx out);
    Mtx *getRootAnmMtx();
    void getSideWalkValues(E_SIDEWALK_TYPE *out, f32 *, f32 *);
    f32 getSlideStickMult();
    f32 getSlideStopCatch();
    f32 getSlideStopNormal();
    f32 getSlopeNormalAccele(f32 *, f32 *);
    f32 getSlopeSlideAccele(f32 *, f32 *);
    TSurfingParams *getSurfingParamsWater();
    Mtx *getTakenMtx();
    u8 getTrampleCt();
    s16 getWallAngle() const;
    void gunExec();
    bool hanging();
    u8 hangingCheckRoof(TVec3f *);
    bool hangonCheck(const TBGCheckData *, const Vec &, const Vec &);
    void hangPole(THitActor *);
    bool hipAttacking();
    void hitNormal(THitActor *);
    void incHP(int);
    void initMirrorModel();
    void initParticle();
    void inOutWaterEffect(f32);
    int isAnimeLoopOrStop();
    bool isForceSlip();
    bool isFrontSlip(int);
    bool isInvincible() const;
    int isLast1AnimeFrame();
    int isMario();
    bool isRunningInWater();
    bool isSlipStart();
    bool isTakeSituation(THitActor *);
    bool isUnderWater() const;
    bool isUnUsualStageStart();
    bool isUpperPumpingStyle() const;
    bool isWallInFront() const;
    bool isWearingCap();
    bool isWearingHelm();
    bool jumpCatch();
    u8 jumpDownCommon(int, int, f32);
    bool jumpEndCommon(int, int);
    bool jumpEndEvents(u32);
    int jumpingBasic(int, int, int);
    bool jumpingDemoCommon(u32, int, f32);
    int jumpMain();
    int jumpProcess(int);
    u8 jumpSlipCommon(s16, u32);
    bool jumpSlipEvents(JumpSlipRecord *);
    void keepDistance(const THitActor &, f32);
    void keepDistance(const TVec3f &, f32, f32);
    void kickFruitEffect();
    void kickRoofEffect();
    u8 landing();
    void loadAnmTexPattern(J3DAnmTexPattern **, char *, J3DModelData *);
    void loadBas(void **, const char *);
    bool loserDown();
    bool loserExec();
    void makeHistory();
    void meltInWaterEffect();
    bool moveMain();
    void moveParticle();
    bool moveRoof();
    void normalizeNozzle();
    bool oilRun();
    bool oilSlip();
    int onYoshi() const;
    void playerRefrection(int);
    bool pulling();
    bool readBillboard();
    void resetHistory();
    void resetNozzle();
    bool returnStart(const TVec3f *, f32, bool, int);
    void rippleEffect();
    int rocketCheck();
    void rocketEffectStart();
    bool rocketing();
    bool rollingStart(const TVec3f *, f32);
    bool roofCommonEvents();
    bool rotating();
    void rumbleStart(int, int);
    bool running();
    bool runningRippleEffect();
    f32 setAnimation(int id, f32 speed);
    void setDivHelm();
    void setGamePad(TMarioGamePad *);
    void setNormalAttackArea();
    void setPlayerVelocity(f32 velocity);
    void setPositions();
    f32 setReverseAnimation(int id, f32 speed);
    u32 setStatusToJumping(u32 status, u32);
    void setUpperDamageRun();
    void sinkInSandEffect();
    bool sleepily();
    bool sleeping();
    void sleepingEffect();
    void sleepingEffectKill();
    void slideProcess(f32, f32);
    bool slipBackCommon(int, int, int);
    bool slipFalling();
    bool slipForeCommon(int, int, int);
    void slippingBasic(int, int, int);
    void slopeProcess();
    void smallTouchDownEffect();
    void soundMovement();
    void soundTorocco();
    bool specMain();
    bool squating();
    bool squatStandup();
    bool startHangLanding(u32);
    bool startJumpWall();
    JAISound *startSoundActor(u32 id);
    bool startTalking();
    u32 startVoice(u32 id);
    u32 startVoiceIfNoVoice(u32 id);
    void stateMachine();
    void stateMachineUpper();
    bool stayWall();
    bool stopCommon(int, int);
    void stopProcess();
    void stopVoice();
    void strongTouchDownEffect();
    bool surfing();
    void surfingEffect();
    bool swimMain();
    void swimmingBubbleEffect();
    bool swimPaddle();
    void takeOffGlass();
    bool taking();
    void thinkDirty();
    void thinkHeight();
    void thinkParams();
    void thinkSand();
    void thinkSituation();
    void thinkWaterSurface();
    void thinkYoshiHeadCollision();
    void throwMario(const TVec3f &rotation, f32 strength);
    bool thrownDowning();
    void toroccoEffect();
    bool toroccoStart();
    bool trampleExec(THitActor *);
    void treeSlipEffect();
    bool turnEnd();
    bool turnning();
    bool waiting();
    bool waitingCommonEvents();
    bool waitingStart(const TVec3f *, f32);
    bool waitMain();
    u8 waitProcess();
    int walkEnd();
    u8 walkProcess();
    bool warpIn();
    void warpInEffect();
    void warpInLight();
    bool warpOut();
    void warpOutEffect();
    bool warpRequest(const TVec3f &, f32);
    void wearGlass();
    bool winDemo();
    void windMove(const TVec3f &);
    bool wireHanging();
    void wireMove(f32);
    bool wireRolling();
    bool wireSWait();
    bool wireWait();

    u32 mActionState;    // 0x0074
    u32 mJumpingState;   // 0x0078
    u32 mState;          // 0x007C
    u32 mPrevState;      // 0x0080
    u16 mSubState;       // 0x0084
    u16 mSubStateTimer;  // 0x0086
    u32 mJumpSlipState;
    f32 mBaseAcceleration;       // 0x008C
    u16 mAccelerationDirection;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0090
    u16 _92;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_10[2];
                     // padding?
    TVec3s mAngle;
#else
                     // padding?
    TVec3s mAngle;
#endif
               // 0x0094
    s16 mModelAngleY;
#if __SIZEOF_POINTER__ == 8
#else
            // 0x009A
    s16 _9C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    s16 _9E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _A0;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_15[8];
    TVec3f mSpeed;
#else
    TVec3f mSpeed;
#endif
      // 0x00A4
    f32 mForwardSpeed;  // 0x00B0
    TVec3f mPrevSpeed;
#if __SIZEOF_POINTER__ == 8
#else
  //?
    f32 _C0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _C4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _C8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _CC;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _D0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _D4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _D5;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_25[28];
    const TBGCheckData *mWallTriangle;
#else
    const TBGCheckData *mWallTriangle;
#endif
        // 0x00D8
    const TBGCheckData *mRoofTriangle;        // 0x00DC
    const TBGCheckData *mFloorTriangle;       // 0x00E0
    const TBGCheckData *mFloorTriangleWater;  // 0x00E4
    f32 mCeilingAbove;                        // 0x00E8
    f32 mFloorBelow;                          // 0x00EC
    f32 mWaterHeight;
#if __SIZEOF_POINTER__ == 8
#else
                         // 0x00F0
    u16 _F4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _F6;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_34[4];
    u16 mLightID;
#else
    u16 mLightID;
#endif

    u16 mAnimationID;
#if __SIZEOF_POINTER__ == 8
#else
    u16 _FC;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _FE;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _100;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _104;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_40[12];
    TMarioControllerWork *mControllerWork;
#else
    TMarioControllerWork *mControllerWork;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x0108
    u32 _10C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                               // unknown
    u32 _110;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                               // unknown
    u32 _114;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_44[12];
                               // unknown

    struct {
        u32 _00                   : 1;
        u32 mAboveSewerFloor     : 1;
        u32 mIsInvisible         : 1;
        u32 mTalkingNPC          : 1;
        u32 mLeftRecentWater     : 1;
        u32 _01                   : 5;
        u32 mIsGameOver          : 1;
        u32 mIsGroundPoundSitUp  : 1;
        u32 mGainHelmetFlwCamera : 1;
        u32 mGainHelmet          : 1;
        u32 mIsFluddEmitting     : 1;
        u32 mHasFludd            : 1;
        u32 mIsShallowWater      : 1;
        u32 mIsWater             : 1;
        u32 _03                   : 2;
        u32 mIsShineShirt        : 1;
        u32 mIsPerforming        : 1;
        u32 _04                   : 10;
    } mAttributes;
#else
                               // unknown

    struct {
        u32 _00                   : 1;
        u32 mAboveSewerFloor     : 1;
        u32 mIsInvisible         : 1;
        u32 mTalkingNPC          : 1;
        u32 mLeftRecentWater     : 1;
        u32 _01                   : 5;
        u32 mIsGameOver          : 1;
        u32 mIsGroundPoundSitUp  : 1;
        u32 mGainHelmetFlwCamera : 1;
        u32 mGainHelmet          : 1;
        u32 mIsFluddEmitting     : 1;
        u32 mHasFludd            : 1;
        u32 mIsShallowWater      : 1;
        u32 mIsWater             : 1;
        u32 _03                   : 2;
        u32 mIsShineShirt        : 1;
        u32 mIsPerforming        : 1;
        u32 _04                   : 10;
    } mAttributes;
#endif
  // 0x0118

    struct {
        u32 _00                   : 1;
        u32 mAboveSewerFloor     : 1;
        u32 mIsInvisible         : 1;
        u32 mTalkingNPC          : 1;
        u32 mLeftRecentWater     : 1;
        u32 _01                   : 5;
        u32 mIsGameOver          : 1;
        u32 mIsGroundPoundSitUp  : 1;
        u32 mGainHelmetFlwCamera : 1;
        u32 mGainHelmet          : 1;
        u32 mIsFluddEmitting     : 1;
        u32 mHasFludd            : 1;
        u32 mIsShallowWater      : 1;
        u32 mIsWater             : 1;
        u32 _03                   : 2;
        u32 mIsShineShirt        : 1;
        u32 mIsPerforming        : 1;
        u32 _04                   : 10;
    } mPrevAttributes;  // 0x011C

    u16 mHealth;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0120
    u16 _122;
#endif

#if __SIZEOF_POINTER__ == 8
#else
     // 0x0122
    u16 _124;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _126;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _128;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_51[10];
    f32 mWaterHealth;
#else
    f32 mWaterHealth;
#endif
         // 0x012C
    f32 mMaxWaterHealth;
#if __SIZEOF_POINTER__ == 8
#else
      // 0x0130
    u32 _134;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_54[4];
                 // unknown
    f32 mGraffitiSlopeAngle;
#else
                 // unknown
    f32 mGraffitiSlopeAngle;
#endif
  // unknown
    u16 mDirtyTimeRun;
#if __SIZEOF_POINTER__ == 8
#else
        // unknown
    u32 _140;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                 // unknown
    u32 _144;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _148;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_59[22];
    s16 mInvincibilityFrames;
#else
    s16 mInvincibilityFrames;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _14E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _150;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // unknown
    u32 _154;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _158;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_64[22];
    f32 mCollisionXZSize;
#else
    f32 mCollisionXZSize;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x015C
    f32 _160;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _164;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _168;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _16C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _170;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _174;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _178;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _17C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _180;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _184;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _188;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _18C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _190;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _194;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _198;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _19C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _1A0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _1A4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _1A8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _1AC;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _1B0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _1B4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _1B8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _1BC;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    Mtx _1C0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    Mtx _1F0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    Mtx _220;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    Mtx _250;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_93[288];
    TVec3f mLastPosition;
#else
    TVec3f mLastPosition;
#endif
  //?       0x0280
    TVec3f mLastRotation;  //?       0x028C
    u32 mInitialWater;     // 0x0298
    TVec3f mLastPos;
    TVec3f mLastGroundedPos;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x02A8
    u32 _2B4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _2B8;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_100[6];
    u16 mOBTimer;
#else
    u16 mOBTimer;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x02BA
    f32 _2BC;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_102[8];
    TLiveActor *mPriorityCollisionOwner;
#else
    TLiveActor *mPriorityCollisionOwner;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    Mtx _2C4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _2F4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _2F8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _2FC;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _300;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _304;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _308;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _30C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _310;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _314;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    Mtx _318;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _348;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _34C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _34E;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_117[140];
    u32 mGraffitiState;
#else
    u32 mGraffitiState;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _354;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _358;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _35C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _360;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _362;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _364;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _366;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _368;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _36C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _370;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _374;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _378;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _37C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u16 _37E;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_132[44];
    u32 mFluddUsageState;
#else
    u32 mFluddUsageState;
#endif

    TTakeActor *mGrabTarget;  // 0x0384
    u8 _388;
    u8 mSurfGessoID;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_136[2];
        // 0x0389
    f32 mHolderHeightDiff;
#else
        // 0x0389
    f32 mHolderHeightDiff;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x038C
    u32 _390;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_138[8];
    J3DDrawBuffer *mDrawBufferA;
#else
    J3DDrawBuffer *mDrawBufferA;
#endif
  // 0x0394
    J3DDrawBuffer *mDrawBufferB;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0398
    u32 _39C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _3A0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _3A4;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_143[24];
                      // unknown
    M3UModelMario *mModelData;
#else
                      // unknown
    M3UModelMario *mModelData;
#endif
     // 0x03A8
    J3DModelData *mBodyModelData;  // unknown
    J3DModel *mHandModel2R;
    J3DModel *mHandModel2L;
    J3DModel *mHandModel3R;
    J3DModel *mHandModel3L;
    J3DModel *mHandModel4R;
#if __SIZEOF_POINTER__ == 8
#else
  // unknown
    u8 _3C4;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_151[1];
    u8 mBindBoneIDArray[12];
#else
    u8 mBindBoneIDArray[12];
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _3D1;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _3D2;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _3D3;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u16 _3D4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _3D6;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _3D8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _3DC;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_159[19];
    TMarioCap *mCap;
#else
    TMarioCap *mCap;
#endif
    // 0x03E0
    TWaterGun *mFludd;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x03E4
    u32 _3E8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _3EC;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_163[8];
    TYoshi *mYoshi;
#else
    TYoshi *mYoshi;
#endif
      // 0x03F0
    void *mSurfGesso;    // 0x03F4
    MActor *mTorocco;    // 0x03F8
    MActor *mPinnaRail;  // 0x03FC
    MActor *mKoopaRail;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0400
    f32 _404;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _408;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _40C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _410;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _412;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _414;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _418;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _41C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _420;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _424;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _428;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _42C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _430;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _434;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _438;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _43C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _440;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _444;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _448;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _44C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _450;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _454;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _458;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _45C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _460;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _464;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _468;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _46C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_196[120];
    void *mGoopTexture;
#else
    void *mGoopTexture;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // bti
    THitActor _474;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_198[128];
    MAnmSound *mAnmSound;
#else
    MAnmSound *mAnmSound;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x04DC
    u32 _4E0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4E4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4E8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _4EC;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _4ED;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _4F0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _4F4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _4F8;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_207[40];
    TMarioGamePad *mController;
#else
    TMarioGamePad *mController;
#endif
      // 0x04FC
    TMarioSoundValues mSoundValues;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x0500
    u32 _530;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _534;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _536;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _538;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _53A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _53B;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _53C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _540;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _544;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _548;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _54C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _54D;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _54E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _550;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _552;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _554;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _558;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _55C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _560;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _564;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _568;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _56C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _570;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_232[80];
    TDeParams mDeParams;
#else
    TDeParams mDeParams;
#endif
                             // 0x0574
    TBodyAngleParams mBodyAngleFreeParams;           // 0x09A0
    TBodyAngleParams mBodyAngleWaterGunParams;       // 0x0A20
    TAttackParams mPunchFenceParams;                 // 0x0AA0
    TAttackParams mKickRoofParams;                   // 0x0AD0
    TJumpParams mJumpParams;                         // 0x0B00
    TRunParams mRunParams;                           // 0x0EA0
    TSwimParams mSwimParams;                         // 0x104C
    THangFenceParams mHangFenceParams;               // 0x1298
    THangRoofParams mHangRoofParams;                 // 0x1318
    TWireParams mWireParams;                         // 0x1334
    TPullParams mPullBGBeakParams;                   // 0x147C
    TPullParams mPullBGTentacleParams;               // 0x14D4
    TPullParams mPullBGFireWanWanBossTailParams;     // 0x152C
    TPullParams mPullFireWanWanTailParams;           // 0x1584
    TClimbParams mClimbParams;                       // 0x15DC
    TSurfingParams mSurfingWaterRedParams;           // 0x1648
    TSurfingParams mSurfingGroundRedParams;          // 0x181C
    TSurfingParams mSurfingWaterYellowParams;        // 0x19F0
    TSurfingParams mSurfingGroundYellowParams;       // 0x1BC4
    TSurfingParams mSurfingWaterGreenParams;         // 0x1D98
    TSurfingParams mSurfingGroundGreenParams;        // 0x1F6C
    THoverParams mHoverParams;                       // 0x2140
    TDivingParams mDivingParams;                     // 0x2184
    TYoshiParams mYoshiParams;                       // 0x21F0
    TWaterEffectParams mWaterEffectParams;           // 0x22AC
    TControllerParams mControllerParams;             // 0x2340
    TGraffitoParams mGraffitoParams;                 // 0x2410
    TDirtyParams mDirtyParams;                       // 0x25BC
    TMotorParams mMotorParams;                       // 0x27A4
    TParticleParams mParticleParams;                 // 0x27FC
    TEffectParams mEffectParams;                     // 0x28E0
    TSlipParams mSlipNormalParams;                   // 0x294C
    TSlipParams mSlipOilParams;                      // 0x2A30
    TSlipParams mSlipAllParams;                      // 0x2B14
    TSlipParams mSlipAllSliderParams;                // 0x2BF8
    TSlipParams mSlip45Params;                       // 0x2CDC
    TSlipParams mSlipWaterSlopeParams;               // 0x2DC0
    TSlipParams mSlipWaterGroundParams;              // 0x2EA4
    TSlipParams mSlipYoshiParams;                    // 0x2F88
    TUpperBodyParams mUpperBodyParams;               // 0x306C
    TDmgEnemyParams mDmgEnemyCommonParams;           // 0x30D8
    TDmgEnemyParams mDmgHamukuriParams;              // 0x316C
    TDmgEnemyParams mDmgNamekuriParams;              // 0x3200
    TDmgEnemyParams mDmgHinokuriParams;              // 0x3294
    TDmgEnemyParams mDmgFireParams;                  // 0x3328
    TDmgEnemyParams mDmgBGTentacleParams;            // 0x33BC
    TDmgEnemyParams mDmgBossEelParams;               // 0x3450
    TDmgEnemyParams mDmgHanachanBossParams;          // 0x34E4
    TDmgEnemyParams mDmgPoihanaParams;               // 0x3578
    TDmgEnemyParams mDmgKillerParams;                // 0x360C
    TDmgEnemyParams mDmgLampTrapIronParams;          // 0x36A0
    TDmgEnemyParams mDmgLampTrapSpikeParams;         // 0x3734
    TDmgEnemyParams mDmgEnemyMarioParams;            // 0x37C8
    TDmgEnemyParams mDmgCannotBreathParams;          // 0x385C
    TDmgEnemyParams mDmgGraffitoFireParams;          // 0x38F0
    TDmgEnemyParams mDmgGraffitoPoisonParams;        // 0x3984
    TDmgEnemyParams mDmgGraffitoElecParams;          // 0x3A18
    TDmgEnemyParams mDmgGraffitoLavaParams;          // 0x3AAC
    TDmgEnemyParams mDmgGraffitoWaterSurfaceParams;  // 0x3B40
    TDmgEnemyParams mDmgMapCode0Params;              // 0x3BD4
    TDmgEnemyParams mDmgMapCode1Params;              // 0x3C68
    TDmgEnemyParams mDmgMapCode2Params;              // 0x3CFC
    TDmgEnemyParams mDmgMapCode3Params;              // 0x3D90
    TDmgEnemyParams mDmgMapCode4Params;              // 0x3E24
    TDmgEnemyParams mDmgMapCode5Params;              // 0x3EB8
    TDmgEnemyParams mDmgMapCode6Params;              // 0x3F4C
    TDmgEnemyParams mDmgMapCode7Params;              // 0x3FE0
    TDmgEnemyParams mDmgMapCode8Params;              // 0x4074
    TDmgEnemyParams mDmgMapCode9Params;              // 0x4108
    TDemoParams mDemoParams;                         // 0x419C
    TSoundParams mSoundParams;                       // 0x4230
    TOptionParams mOptionParams;                     // 0x424C
};

extern TMario *gpMarioOriginal;
extern TMario *gpMarioAddress;
extern TVec3f *gpMarioPos;
extern f32 *gpMarioAngleX;
extern f32 *gpMarioAngleY;
extern f32 *gpMarioAngleZ;
extern f32 *gpMarioSpeedX;
extern f32 *gpMarioSpeedY;
extern f32 *gpMarioSpeedZ;
extern s32 *gpMarioLightID;
extern u32 *gpMarioFlag;
extern f32 *gpMarioThrowPower;
extern TBGCheckData *gpMarioGroundPlane;