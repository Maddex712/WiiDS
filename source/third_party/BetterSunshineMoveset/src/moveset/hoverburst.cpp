#include <SMS/MSound/MSound.hxx>
#include <SMS/MSound/MSoundSESystem.hxx>
#include <SMS/Player/Mario.hxx>
#include <SMS/Player/Watergun.hxx>
#include <SMS/raw_fn.hxx>

#include "common.hxx"
#include "player.hxx"
#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>
#include <BetterSMS/settings.hxx>

using namespace BetterSMS;

// This patches delayed fludd usage
static void snapNozzleToReady() {
    TWaterGun *fludd;
    SMS_FROM_GPR(30, fludd);

    if (fludd->mCurrentNozzle == TWaterGun::TNozzleType::Hover) {
        *(float *)((u8 *)fludd + SMS_OFFSET(TWaterGun, 0x1CEC)) = 0.0f;
    } else {
        *(float *)((u8 *)fludd + SMS_OFFSET(TWaterGun, 0x1CEC)) -= 0.1f;
        if (*(float *)((u8 *)fludd + SMS_OFFSET(TWaterGun, 0x1CEC)) < 0.0f)
            *(float *)((u8 *)fludd + SMS_OFFSET(TWaterGun, 0x1CEC)) = 0.0f;
    }
}
SMS_PATCH_BL(SMS_PORT_REGION(0x802699CC, 0, 0, 0), snapNozzleToReady);

// extern -> fluddgeneral.cpp
BETTER_SMS_FOR_CALLBACK void checkSpamHover(TMario *player, bool isMario) {
    TWaterGun *fludd = player->mFludd;
    if (!fludd)
        return;

    if (fludd->mCurrentNozzle != TWaterGun::Hover || !gHoverBurstSetting.getBool())
        return;

    TNozzleTrigger *nozzle =
        reinterpret_cast<TNozzleTrigger *>(fludd->mNozzleList[fludd->mCurrentNozzle]);
    auto &emitParams = nozzle->mEmitParams;

    emitParams.mNum.set(1.0f);
    emitParams.mDirTremble.set(0.0f);

    bool isTouchGround4cm = player->mTranslation.y - player->mFloorBelow <= 4.0f;
    bool isWalkingRope =
        (player->mState == 0x350 || player->mState == 0x10000357 || player->mState == 0x10000358);
    bool isClimbState = (player->mState == 0x10100341);                          // Pole Climb
    isClimbState |= (player->mState & 0x30000000) != 0;                          // Ladder climbs
    isClimbState |= (player->mState == 0x200349 || player->mState == 0x20054A);  // Roof climbs;

    bool isValidResetState = isTouchGround4cm || isClimbState || (player->mState & TMario::STATE_WATERBORN) ||
                             (player->mState == TMario::STATE_NPC_BOUNCE) || isWalkingRope;

    PlayerMovementData *moveData = getPlayerMovementData(player);
    if (isValidResetState)
        moveData->mIsHoverBurstValid = true;

    Player::TPlayerData *playerData = Player::getData(player);

    if (!BetterSMS::areExploitsPatched()) {
        moveData->mIsHoverBurstValid = true;
        playerData->setCanSprayFludd(true);
    }

    if (!moveData->mIsHoverBurstValid)
        return;

    if ((player->mState & TMario::STATE_WATERBORN) || !playerData->getCanSprayFludd())
        return;

    if (player->mController->mButtons.mAnalogR < 0.8f ||
        !(player->mController->mFrameMeaning & 0x80))
        return;

    if ((emitParams.mTriggerTime.get() - nozzle->mSprayQuarterFramesLeft) >= 20)
        return;

    if (nozzle->mFludd->mCurrentWater < 510)
        return;

    emitParams.mNum.set(255.0f);
    emitParams.mDirTremble.set(0.5f);
    nozzle->emit(0);
    nozzle->emit(1);

    nozzle->mSprayQuarterFramesLeft = 0;
    nozzle->mSprayState             = TNozzleTrigger::DEAD;
    nozzle->mFludd->mCurrentWater -= 255;

    if ((player->mState & 0x800000) == 0) {
        player->mState = TMario::STATE_HOVER_F;
        player->mSpeed.y += 70.0f - Min(player->mSpeed.y, 70.0f);
        player->mJumpingState &= 0xFFFFFEFF;
    } else if (player->mState == TMario::STATE_DIVE) {
        player->mSpeed.y += 35.0f - Min(player->mSpeed.y, 35.0f);
        player->mForwardSpeed += 15.0f;
    } else if (player->mState == TMario::STATE_DIVESLIDE) {
        player->mSpeed.y += 35.0f - Min(player->mSpeed.y, 35.0f);
        player->mForwardSpeed += 30.0f;
    }

    playerData->setCanSprayFludd(false);
    moveData->mIsHoverBurstValid = false;
    return;
}

BETTER_SMS_FOR_CALLBACK void checkForCameraDemoHover(TMarDirector *director) {
    PlayerMovementData *moveData = getPlayerMovementData(gpMarioAddress);

    if (!moveData->mIsPreservedHover && gpMarioAddress->mFluddUsageState == 0 &&
        gpCamera->getRestDemoFrames() > 0) {
        moveData->mIsPreservedHover = true;
    }
}

static TMarioControllerWork* getMarioControllerWorkForKeepHover() {
    TMario *player;
    SMS_FROM_GPR(31, player);

    PlayerMovementData *moveData = getPlayerMovementData(player);

    player->mController->_B0 = player->mController->mButtons.mTriggerL;
    player->mController->_B4 = player->mController->mButtons.mTriggerR;
    player->checkController(nullptr);

    return player->mControllerWork;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8026231C, 0, 0, 0), getMarioControllerWorkForKeepHover);