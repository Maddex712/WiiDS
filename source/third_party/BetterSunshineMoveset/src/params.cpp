#include "player.hxx"
#include "settings.hxx"
#include <BetterSMS/module.hxx>
#include <SMS/Player/Mario.hxx>
#include <SMS/System/MarDirector.hxx>

using namespace BetterSMS;

#define SCALE_PARAM(param, scale) param.set(param.get() * scale)

BETTER_SMS_FOR_CALLBACK void onPlayerInit(TMario *player, bool isMario) {
    PlayerMovementData *data = new PlayerMovementData();
    Player::registerData(player, "_movement_data", data);

    PlayerMovementParams *params = new PlayerMovementParams();
    {
        JKRArchive *archive = JKRFileLoader::getVolume("mario");
        SMS_DEBUG_ASSERT(archive, "Mario archive could not be located!");

        void *resource = archive->getResource("/better_movement.prm");
        if (resource) {
            JSUMemoryInputStream stream(resource, archive->getResSize(resource));
            params->load(stream);
        }
    }
    Player::registerData(player, "_movement_params", params);

    if (!isMario) {
        return;
    }

    f32 speedMultiplier   = params->mSpeedMultiplier.get();
    f32 jumpMultiplier    = params->mBaseJumpMultiplier.get();
    f32 gravityMultiplier = params->mGravityMultiplier.get();
    if (player->onYoshi()) {
        speedMultiplier = 1.0f;
        jumpMultiplier  = 1.0f;
    }

    SCALE_PARAM(player->mDeParams.mRunningMax, speedMultiplier);
    SCALE_PARAM(player->mDeParams.mDashMax, speedMultiplier);
    SCALE_PARAM(player->mDeParams.mClashSpeed,
                speedMultiplier *
                    (gFastDiveSetting.getBool() || gFastTurboSetting.getBool() ? 2.25f : 1.0f));
    SCALE_PARAM(player->mJumpParams.mGravity, gravityMultiplier);
    SCALE_PARAM(player->mJumpParams.mSpinJumpGravity, gravityMultiplier);
    SCALE_PARAM(player->mJumpParams.mJumpSpeedAccelControl, speedMultiplier);
    SCALE_PARAM(player->mJumpParams.mPopUpSpeedY, jumpMultiplier);
    SCALE_PARAM(player->mJumpParams.mJumpingMax, jumpMultiplier);
    SCALE_PARAM(player->mJumpParams.mFenceSpeed, speedMultiplier);
    SCALE_PARAM(player->mJumpParams.mFireBackVelocity, jumpMultiplier);
    SCALE_PARAM(player->mJumpParams.mBroadJumpForceY, jumpMultiplier);
    SCALE_PARAM(player->mJumpParams.mRotateJumpForceY, jumpMultiplier);
    SCALE_PARAM(player->mJumpParams.mBackJumpForceY, jumpMultiplier);
    SCALE_PARAM(player->mJumpParams.mHipAttackSpeedY, gravityMultiplier);
    SCALE_PARAM(player->mJumpParams.mSuperHipAttackSpeedY, gravityMultiplier);
    SCALE_PARAM(player->mJumpParams.mRotBroadJumpForceY, jumpMultiplier);
    SCALE_PARAM(player->mJumpParams.mSecJumpForce, jumpMultiplier);
    SCALE_PARAM(player->mJumpParams.mUltraJumpForce, jumpMultiplier);
    SCALE_PARAM(player->mJumpParams.mTurnJumpForce, jumpMultiplier);
    SCALE_PARAM(player->mRunParams.mMaxSpeed, speedMultiplier);
    SCALE_PARAM(player->mRunParams.mAddBase, speedMultiplier);
    SCALE_PARAM(player->mRunParams.mDecBrake, speedMultiplier);
    SCALE_PARAM(player->mSwimParams.mMoveSp, speedMultiplier);
    SCALE_PARAM(player->mSwimParams.mGravity, gravityMultiplier);
    SCALE_PARAM(player->mSwimParams.mRush, speedMultiplier);
    SCALE_PARAM(player->mSwimParams.mPaddleSpeedUp, speedMultiplier);
    SCALE_PARAM(player->mSwimParams.mPaddleJumpUp, speedMultiplier);
    SCALE_PARAM(player->mSwimParams.mPaddleDown, speedMultiplier);
    SCALE_PARAM(player->mHangFenceParams.mMoveSp, speedMultiplier);
    SCALE_PARAM(player->mHangFenceParams.mDescentSp, speedMultiplier);
    SCALE_PARAM(player->mDirtyParams.mSlipRunSp, speedMultiplier);
    SCALE_PARAM(player->mDirtyParams.mSlipCatchSp, speedMultiplier);
}

// BETTER_SMS_FOR_CALLBACK void onPlayerUpdate(TMario *player, bool isMario) {
//     auto *data = getPlayerMovementData(player);
//     if (!data)
//         return;
//
//     auto *params = getPlayerMovementParams(player);
//     if (!params)
//         return;
//
//     f32 speedMultiplier   = params->mScaleMultiplier.get();
//     f32 jumpMultiplier    = params->mBaseJumpMultiplier.get();
//     f32 gravityMultiplier = params->mGravityMultiplier.get();
//     if (player->onYoshi()) {
//         speedMultiplier = 1.0f;
//         jumpMultiplier  = 1.0f;
//     }
//
//     SCALE_PARAM(player->mDeParams.mRunningMax, speedMultiplier);
//     SCALE_PARAM(player->mDeParams.mDashMax, speedMultiplier);
//     SCALE_PARAM(player->mDeParams.mClashSpeed,
//                 speedMultiplier *
//                     ((gFastDiveSetting.getBool() && isMario) || gFastTurboSetting.getBool()
//                          ? 2.25f
//                          : 1.0f));
//     SCALE_PARAM(player->mJumpParams.mGravity, gravityMultiplier);
//     SCALE_PARAM(player->mJumpParams.mSpinJumpGravity, gravityMultiplier);
//     SCALE_PARAM(player->mJumpParams.mJumpSpeedAccelControl, speedMultiplier);
//     SCALE_PARAM(player->mJumpParams.mPopUpSpeedY, jumpMultiplier);
//     SCALE_PARAM(player->mJumpParams.mJumpingMax, jumpMultiplier);
//     SCALE_PARAM(player->mJumpParams.mFenceSpeed, speedMultiplier);
//     SCALE_PARAM(player->mJumpParams.mFireBackVelocity, jumpMultiplier);
//     SCALE_PARAM(player->mJumpParams.mBroadJumpForceY, jumpMultiplier);
//     SCALE_PARAM(player->mJumpParams.mRotateJumpForceY, jumpMultiplier);
//     SCALE_PARAM(player->mJumpParams.mBackJumpForceY, jumpMultiplier);
//     SCALE_PARAM(player->mJumpParams.mHipAttackSpeedY, gravityMultiplier);
//     SCALE_PARAM(player->mJumpParams.mSuperHipAttackSpeedY, gravityMultiplier);
//     SCALE_PARAM(player->mJumpParams.mRotBroadJumpForceY, jumpMultiplier);
//     SCALE_PARAM(player->mJumpParams.mSecJumpForce, jumpMultiplier);
//     SCALE_PARAM(player->mJumpParams.mUltraJumpForce, jumpMultiplier);
//     SCALE_PARAM(player->mJumpParams.mTurnJumpForce, jumpMultiplier);
//     SCALE_PARAM(player->mRunParams.mMaxSpeed, speedMultiplier);
//     SCALE_PARAM(player->mRunParams.mAddBase, speedMultiplier);
//     SCALE_PARAM(player->mRunParams.mDecBrake, speedMultiplier);
//     SCALE_PARAM(player->mSwimParams.mMoveSp, speedMultiplier);
//     SCALE_PARAM(player->mSwimParams.mGravity, gravityMultiplier);
//     SCALE_PARAM(player->mSwimParams.mRush, speedMultiplier);
//     SCALE_PARAM(player->mSwimParams.mPaddleSpeedUp, speedMultiplier);
//     SCALE_PARAM(player->mSwimParams.mPaddleJumpUp, speedMultiplier);
//     SCALE_PARAM(player->mSwimParams.mPaddleDown, speedMultiplier);
//     SCALE_PARAM(player->mHangFenceParams.mMoveSp, speedMultiplier);
//     SCALE_PARAM(player->mHangFenceParams.mDescentSp, speedMultiplier);
//     SCALE_PARAM(player->mDirtyParams.mSlipRunSp, speedMultiplier);
//     SCALE_PARAM(player->mDirtyParams.mSlipCatchSp, speedMultiplier);
// }

#undef SCALE_PARAM