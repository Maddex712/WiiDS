
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
#include "player.hxx"

using namespace BetterSMS;

extern u32 MultiJumpState;
u32 PoundJumpState = 0x88F;

constexpr int PoundJumpAnimationID = 112;

BETTER_SMS_FOR_CALLBACK void checkForPoundJump(TMario *player, bool isMario) {
    if (gpMarDirector->mCurState != TMarDirector::Status::STATE_NORMAL)
        return;

    if (!isMario || player->onYoshi() || !gPoundJumpSetting.getBool())
        return;

    if (player->mState != TMario::STATE_SLAM)
        return;

    if ((player->mActionState & 0x8) || player->isForceSlip())
        return;

    if ((player->mFloorTriangle->mType & 0x1FFF) == 7)
        return;

    auto *controller = player->mController;
    if (!controller)
        return;

    if (!(controller->mButtons.mFrameInput & TMarioGamePad::A))
        return;

    player->mSpeed.y = 75.0f;
    player->mTranslation.y += 20.0f;
    player->startVoice(TMario::VOICE_TRIPLE_JUMP);
    player->changePlayerStatus(PoundJumpState, 0, false);
    player->mSubState      = 0;
    player->mSubStateTimer = 0;
}

BETTER_SMS_FOR_CALLBACK bool processPoundJump(TMario *player) {
    if (player->checkBackTrig())
        return true;

    if (player->rocketCheck())
        return true;

    if (player->mSpeed.y > 40 && (player->mSubStateTimer % 10) == 0) {
        player->emitSmoke(0);
    }

    if (player->mSubStateTimer > 5) {
        if (auto *playerData = getPlayerMovementData(player)) {
            if (auto *params = getPlayerMovementParams(player)) {
                const s32 jumpsLeft = (params->mMaxJumps.get() - playerData->mCurJump);

                if ((player->mController->mButtons.mFrameInput & TMarioGamePad::EButtons::A) &&
                    jumpsLeft > 0) {
                    player->changePlayerStatus(MultiJumpState, 0, false);
                    playerData->mLastDropY = player->mTranslation.y;
                }
            }
        }
    }

    bool customAnimExists = BetterSMS::Player::isAnimationValid(PoundJumpAnimationID);

    int animID = 111;
    if (customAnimExists) {
        animID = PoundJumpAnimationID;
    }

    player->jumpingBasic(TMario::STATE_JMP_LAND, animID, 3);
    player->mSubStateTimer += 1;

    return false;
}