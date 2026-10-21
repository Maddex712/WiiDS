#include <Dolphin/types.h>

#include <SMS/Player/Mario.hxx>

#include <SMS/raw_fn.hxx>

#include <BetterSMS/libs/constmath.hxx>
#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>
#include <BetterSMS/stage.hxx>

#include "common.hxx"
#include "player.hxx"

using namespace BetterSMS;

f32 calcJumpPower(TMario *player, f32 factor, f32 base, f32 jumpPower) {
    base             = Min(base, 100.0f);

    auto *playerData = Player::getData(player);

    if (!playerData->isMario()) {
        return Max(base, (base * factor) + jumpPower);
    }

    auto *moveData = getPlayerMovementData(player);
    if (!moveData)
        return Max(base, (base * factor) + jumpPower);

    auto *params = getPlayerMovementParams(player);
    if (!params)
        return Max(base, (base * factor) + jumpPower);

    auto *stageConfig = Stage::getStageConfiguration();

    jumpPower *= params->mBaseJumpMultiplier.get();
    if (player->mState & TMario::STATE_AIRBORN) {
        f32 multiplier = params->mMultiJumpMultiplier.get();
        for (u32 i = 1; i < moveData->mCurJump; ++i) {
            multiplier *= multiplier;
        }
        jumpPower *= multiplier;
        player->mForwardSpeed *= params->mMultiJumpFSpeedMulti.get();
    }

    return Max(base, (base * factor) + jumpPower);
}

static void setJumpOrLongJump(TMario *player, u32 state, u32 unk_0) {
    constexpr f32 LongJumpMinSpeed = 10.0f;

    auto *playerData = Player::getData(player);

    auto *moveData           = getPlayerMovementData(player);
    moveData->mIsLongJumping = false;

    if (!moveData || !playerData->isMario()) {
        player->setStatusToJumping(state, unk_0);
        return;
    }

    if (gLongJumpSetting.getInt() == LongJumpSetting::MODE_OFF) {
        player->setStatusToJumping(state, unk_0);
        return;
    }

    if (gLongJumpSetting.getInt() == LongJumpSetting::MODE_NO_FLUDD &&
        player->mAttributes.mHasFludd) {
        player->setStatusToJumping(state, unk_0);
        return;
    }

    if (player->mTranslation.y - player->mFloorBelow > 10.0f) {
        player->setStatusToJumping(state, unk_0);
        return;
    }

    if ((player->mState & TMario::STATE_AIRBORN) != 0 ||
        (player->mState & TMario::STATE_WATERBORN) != 0 ||
        player->mState == TMario::STATE_DIVESLIDE || player->onYoshi() ||
        player->mState == 0x80000588 || player->mState == 0x820008AB ||
        player->mState == 0xF00001C1) /* Multi jump */ {
        player->setStatusToJumping(state, unk_0);
        return;
    }

    if (player->mState == 0x810446) /* Blooper */ {
        player->setStatusToJumping(state, unk_0);
        return;
    }

    auto &buttons = player->mController->mButtons;

    if ((player->mController->mMeaning & getCrouchAndLongJumpButtonMeaning()) &&
        (buttons.mFrameInput & TMarioGamePad::EButtons::A) &&
        player->mForwardSpeed > LongJumpMinSpeed) {

        // NOTE: This checks if the player is mario! Does this affect other characters?
        //
        if (gLongJumpSetting.getInt() == LongJumpSetting::MODE_NO_FLUDD)
            moveData->mIsLongJumping = playerData->isMario() && (player->mActionState & 0x8) == 0 &&
                                       !player->mAttributes.mHasFludd;
        else
            moveData->mIsLongJumping = playerData->isMario() && (player->mActionState & 0x8) == 0;

        /*if (gLongJumpSetting.getInt() == LongJumpSetting::MODE_NO_FLUDD)
            moveData->mIsLongJumping =
                (player->mActionState & 0x8) == 0 && !player->mAttributes.mHasFludd;
        else
            moveData->mIsLongJumping = (player->mActionState & 0x8) == 0;*/
        state = TMario::STATE_JUMP;
    }

    player->setStatusToJumping(state, unk_0);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x802541BC, 0x8024BF48, 0, 0), setJumpOrLongJump);

static void processJumpOrLongJumpAnim(TMario *player, int state, int anim, int unk_0) {
    auto *moveData = getPlayerMovementData(player);
    if (moveData && moveData->mIsLongJumping)
        anim = 0xF6;

    player->jumpingBasic(state, anim, unk_0);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80249554, 0x802412E0, 0, 0), processJumpOrLongJumpAnim);

static void processJumpOrLongJump() {
    TMario *player;
    SMS_FROM_GPR(30, player);

    constexpr f32 LongJumpSpeedForward = 36.0f;
    constexpr f32 LongJumpSpeedUp      = 50.0f;

    auto *playerData                   = Player::getData(player);

    auto *moveData = getPlayerMovementData(player);
    if (!moveData || !playerData->isMario()) {
        player->mSpeed.y = calcJumpPower(player, 0.25f, player->mForwardSpeed, 42.0f);
        return;
    }

    if (!moveData->mIsLongJumping) {
        player->mSpeed.y = calcJumpPower(player, 0.25f, player->mForwardSpeed, 42.0f);
        return;
    }

    auto *params = getPlayerMovementParams(player);
    if (!params) {
        player->mSpeed.y = calcJumpPower(player, 0.25f, player->mForwardSpeed, 42.0f);
        return;
    }

    player->startVoice(TMario::VOICE_TRIPLE_JUMP);

    player->mSpeed.y += LongJumpSpeedUp * params->mBaseJumpMultiplier.get();
    player->mForwardSpeed += LongJumpSpeedForward * params->mSpeedMultiplier.get();
    player->mPrevState = player->mState;
    player->mState     = static_cast<u32>(TMario::STATE_JUMP);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80254534, 0x8024c2c0, 0, 0), processJumpOrLongJump);
SMS_WRITE_32(SMS_PORT_REGION(0x80254538, 0x8024c2c4, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x8025453C, 0x8024c2c8, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80254540, 0x8024c2cc, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80254544, 0x8024c2d0, 0, 0), 0x60000000);

static bool checkDivingWhenLongJumping(TMario *player) {
    const bool onYoshi = player->onYoshi();

    auto *bsmsData     = Player::getData(player);
    if (!bsmsData->isMario())
        return onYoshi;

    auto *moveData = getPlayerMovementData(player);
    if (!moveData)
        return onYoshi;

    return onYoshi || moveData->mIsLongJumping;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8024C394, 0x80244120, 0, 0), checkDivingWhenLongJumping);

static bool checkRotatingWhenLongJumping(TMario *player, int *unk_0) {
    const bool rotated = player->checkStickRotate(unk_0);

    auto *bsmsData = Player::getData(player);
    if (!bsmsData->isMario())
        return rotated;

    auto *moveData = getPlayerMovementData(player);
    if (!moveData)
        return rotated;

    return rotated && !moveData->mIsLongJumping;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8024C3F8, 0x80244184, 0, 0), checkRotatingWhenLongJumping);

static bool checkQuickFallWhenLongJumping() {
    TMario *player;
    SMS_FROM_GPR(30, player);

    const bool slowFalling = ((player->mActionState & 0x80) != 0);

    auto *bsmsData = Player::getData(player);
    if (!bsmsData->isMario())
        return slowFalling;

    auto *moveData = getPlayerMovementData(player);
    if (!moveData)
        return slowFalling;

    auto *playerData = Player::getData(player);
    if (!playerData)
        return slowFalling || moveData->mIsLongJumping;

    return slowFalling || moveData->mIsLongJumping || playerData->mCollisionFlags.mIsSpinBounce;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x802565D4, 0x8024E360, 0, 0), checkQuickFallWhenLongJumping);
SMS_WRITE_32(SMS_PORT_REGION(0x802565D8, 0x8024E364, 0, 0), 0x2C030000);

static bool preserveRegisterCheckQuickFall() {
    TMario *player;
    SMS_FROM_GPR(31, player);

    return player->mState == TMario::STATE_JUMPSPINR || player->mState == TMario::STATE_JUMPSPINL;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80256618, 0x8024E3A4, 0, 0), preserveRegisterCheckQuickFall);
SMS_WRITE_32(SMS_PORT_REGION(0x8025661C, 0x8024E3A8, 0, 0), 0x2C030000);
SMS_WRITE_32(SMS_PORT_REGION(0x80256620, 0x8024E3AC, 0, 0), 0x41820024);