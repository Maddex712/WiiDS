#include <Dolphin/types.h>
#include <SMS/Map/MapCollisionData.hxx>
#include <SMS/Player/Mario.hxx>
#include <SMS/raw_fn.hxx>

#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>

#include "player.hxx"
#include <BetterSMS/libs/constmath.hxx>

using namespace BetterSMS;

u32 MultiJumpState = 0xF00001C1;
extern u32 PoundJumpState;

BETTER_SMS_FOR_CALLBACK void checkStartInAir(TMario *player) {
    const TBGCheckData *floor = nullptr;
    f32 groundY               = gpMapCollisionData->checkGround(
        player->mTranslation.x, player->mTranslation.y + 80.0f, player->mTranslation.z, 0, &floor);

    if (fabsf(player->mTranslation.y - groundY) <= 10.0f) {
        return;
    }

    auto *playerData = getPlayerMovementData(player);
    if (!playerData)
        return;

    auto *params = getPlayerMovementParams(player);
    if (!params)
        return;

    playerData->mCurJump = params->mMaxJumps.get();
}

BETTER_SMS_FOR_CALLBACK void checkForMultiJump(TMario *player, bool isMario) {
    auto *playerData = getPlayerMovementData(player);
    if (!playerData || !isMario)
        return;

    auto *params = getPlayerMovementParams(player);
    if (!params)
        return;

    const bool isYoshi = player->mYoshi ? player->mYoshi->mState == TYoshi::MOUNTED : false;

    const bool isAirBorn =
        ((player->mState & TMario::STATE_AIRBORN) && (player->mActionState & 0x4)) ||
        (!(player->mState & TMario::STATE_WATERBORN) &&
         (player->mTranslation.y > player->mFloorBelow + 10));

    const bool isInvalidState =
        isYoshi || player->mState == 0x1337 || (player->mState & 0x800000) ||
        player->mState == TMario::STATE_SLIP_JUMP || player->mState == TMario::STATE_THROWN ||
        player->mAttributes.mIsGameOver || player->mState == PoundJumpState;

    bool isRopeState =
        (player->mState == 0x350 || player->mState == 0x351 || player->mState == 0x352 ||
         player->mState == 0x353 || player->mState == 0x35B || player->mState == 0x35C ||
         player->mState == 0x10000357 || player->mState == 0x10000358);
    bool isClimbState = (player->mState == 0x10100341);                          // Pole Climb
    isClimbState |= (player->mState & 0x30000000) != 0;                          // Ladder climbs
    isClimbState |= (player->mState == 0x200349 || player->mState == 0x20054A);  // Roof climbs;

    if (isInvalidState || isClimbState || isRopeState) {
        playerData->mCurJump = 1;
        return;
    }

    if (!isAirBorn) {
        playerData->mCurJump = 1;
        return;
    }

    if (player->mFludd && player->mFludd->mIsEmitWater &&
        player->mFludd->mCurrentNozzle == TWaterGun::Hover)
        return;

    const s32 jumpsLeft = (params->mMaxJumps.get() - playerData->mCurJump);

    if ((player->mController->mButtons.mFrameInput & TMarioGamePad::EButtons::A) && jumpsLeft > 0 &&
        player->mState != TMario::STATE_WALLSLIDE && player->mState != TMario::STATE_F_KNCK_H) {
        player->changePlayerStatus(MultiJumpState, 0, false);
        playerData->mLastDropY = player->mTranslation.y;
    }
}

BETTER_SMS_FOR_CALLBACK bool processMultiJump(TMario *player) {
    auto *playerData = getPlayerMovementData(player);
    if (!playerData)
        return true;

    auto *params = getPlayerMovementParams(player);
    if (!params)
        return true;

    const s32 jumpsLeft = (params->mMaxJumps.get() - playerData->mCurJump);

    u32 state   = player->mState;
    int voiceID = 0;
    int animID  = 0;

    if (jumpsLeft == 1) {
        state = static_cast<u32>(TMario::STATE_D_JUMP);
    } else if (jumpsLeft % 2) {
        state = static_cast<u32>(TMario::STATE_JUMP);
    } else {
        state = static_cast<u32>(TMario::STATE_D_JUMP);
    }

    const TMarioGamePad *controller = player->mController;
    const f32 stickMagnitude        = controller->mControlStick.mLengthFromNeutral;

    if (stickMagnitude > 0.1f) {
        player->mAngle.y =
            gpCamera->mHorizontalAngle +
            convertAngleFloatToS16(radiansToAngle(
                atan2f(controller->mControlStick.mStickX, -controller->mControlStick.mStickY)));
    }

    playerData->mIsLongJumping = false;
    playerData->mCurJump += 1;

    player->mForwardSpeed *= stickMagnitude;
    player->changePlayerJumping(state, 1);

    return true;
}

static bool avoidSlipJumpOnMultiJump(TMario *player) {
    auto *bsmsData = Player::getData(player);
    if (!bsmsData->isMario())
        return player->isForceSlip();

    auto *playerData = getPlayerMovementData(player);
    if (!playerData)
        return player->isForceSlip();

    auto *params = getPlayerMovementParams(player);
    if (!params)
        return player->isForceSlip();

    if (playerData->mCurJump <= 1) {
        return player->isForceSlip();
    }
    return false;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80253A8C, 0, 0, 0), avoidSlipJumpOnMultiJump);

static void playDoubleOrTripleAnim(TMario *player, int state, int anim, int unk_0) {
    auto *bsmsData = Player::getData(player);
    if (!bsmsData->isMario()) {
        player->jumpingBasic(state, anim, unk_0);
        return;
    }

    auto *playerData = getPlayerMovementData(player);
    if (!playerData) {
        player->jumpingBasic(state, anim, unk_0);
        return;
    }

    auto *params = getPlayerMovementParams(player);
    if (!params) {
        player->jumpingBasic(state, anim, unk_0);
        return;
    }

    size_t maxjumps = params->mMaxJumps.get();
    if (maxjumps > 1 && playerData->mCurJump >= maxjumps) {
        player->jumpingBasic(state, 0x6F, unk_0);
        if (playerData->mCurJump == maxjumps) {  // Hack
            player->startVoice(0x78B6);
            playerData->mCurJump += 1;
        }
    } else {
        player->jumpingBasic(state, anim, unk_0);
    }
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80249618, 0, 0, 0), playDoubleOrTripleAnim);