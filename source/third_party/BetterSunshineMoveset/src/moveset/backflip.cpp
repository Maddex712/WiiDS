
#include <SMS/MSound/MSound.hxx>
#include <SMS/MSound/MSoundSESystem.hxx>
#include <SMS/Player/Mario.hxx>
#include <SMS/Player/Watergun.hxx>
#include <SMS/System/Application.hxx>
#include <SMS/System/MarDirector.hxx>
#include <SMS/raw_fn.hxx>

#include <BetterSMS/memory.hxx>
#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>

#include "common.hxx"

using namespace BetterSMS;

u32 CrouchState = 0xF00001C0;
constexpr int CrouchAnimationID = 113;

BETTER_SMS_FOR_CALLBACK void checkForCrouch(TMario *player, bool isMario) {
    if (gpMarDirector->mCurState != TMarDirector::Status::STATE_NORMAL)
        return;

    if (!isMario || player->onYoshi() || !gBackFlipSetting.getBool())
        return;

    if (player->mState != TMario::STATE_IDLE && player->mState != TMario::STATE_STOP &&
        player->mState != TMario::STATE_JMP_LAND && player->mState != TMario::STATE_HVY_LAND &&
        player->mState != TMario::STATE_LAND_RECOVER && player->mState != TMario::STATE_D_LAND &&
        player->mState != TMario::STATE_D_LAND_RECOVER)
        return;

    if ((player->mActionState & 0x8) || player->isForceSlip())
        return;

    if (player->mForwardSpeed > 0.01f)
        return;

    auto *controller = player->mController;
    if (!controller)
        return;

    if (!(controller->mMeaning & getCrouchAndLongJumpButtonMeaning()))
        return;

    if ((player->mFloorTriangle->mType & 0x1FFF) == 7)
        return;

    player->changePlayerStatus(CrouchState, 0, false);
    player->mSpeed.scale(0.2f);
}

BETTER_SMS_FOR_CALLBACK bool processCrouch(TMario *player) {
    if (player->mTranslation.y - player->mFloorBelow > 10.0f) {
        player->changePlayerStatus(TMario::STATE_FALL, 0, false);
        return true;
    }

    if (player->mActionState & 0x8 || player->isForceSlip()) {
        player->changePlayerStatus(0x50, 0, false);
        return true;
    }

    auto *fludd = player->mFludd;
    if (fludd) {
        if (fludd->mIsEmitWater && fludd->mCurrentNozzle != TWaterGun::Spray) {
            player->changePlayerStatus(TMario::STATE_IDLE, 0, false);
            return true;
        }
    }

    auto *controller = player->mController;
    if (!controller) {
        player->changePlayerStatus(TMario::STATE_SIDE_STEP_LEAVE, 0, false);
        return true;
    }

    if (!(controller->mMeaning & getCrouchAndLongJumpButtonMeaning())) {
        player->changePlayerStatus(TMario::STATE_SIDE_STEP_LEAVE, 0, false);
        return true;
    }

    auto &controlStick = controller->mControlStick;

    const bool backHeld = controlStick.mAngle < 0x4000 && controlStick.mAngle > -0x4000;
    if (controller->mControlStick.mLengthFromNeutral > 0.8f && !backHeld) {
        player->changePlayerStatus(TMario::STATE_RUNNING, 0, false);
        return true;
    }

    if (controller->mButtons.mFrameInput & TMarioGamePad::A) {
        player->changePlayerStatus(TMario::STATE_BACK_FLIP, 0, false);
        return true;
    }

    // Check walls
    float normalThing = player->mFloorTriangle ? player->mFloorTriangle->mNormal.y : 0.0f;
    TVec3f succ{player->mTranslation.x + (player->mSpeed.x * normalThing * 0.25f),
                player->mTranslation.y,
                player->mTranslation.z + (player->mSpeed.z * normalThing * 0.25f)};
    player->checkGroundAtWalking(succ);
    player->checkCollision();

    player->mSpeed.y = 0;
    player->mSpeed.scale(0.95);
    player->mTranslation.add(player->mSpeed);
    player->mForwardSpeed *= 0.95;

    int animationID = TMario::ANIMATION_STEADY_STANCE;
    if (Player::isAnimationValid(CrouchAnimationID)) {
        animationID = CrouchAnimationID;
    }

    player->setAnimation(animationID, 1.0f);
    return false;
}