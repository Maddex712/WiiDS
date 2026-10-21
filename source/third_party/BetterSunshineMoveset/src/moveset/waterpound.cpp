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

u32 WaterPoundState                  = 0x24DF;
constexpr int GroundPoundAnimationID = 60;

BETTER_SMS_FOR_CALLBACK void checkForWaterPound(TMario *player, bool isMario) {
    if (gpMarDirector->mCurState != TMarDirector::Status::STATE_NORMAL)
        return;

    if (!isMario || !gWaterPoundSetting.getBool())
        return;

    if (player->mState == WaterPoundState || !(player->mState & TMario::STATE_WATERBORN) ||
        player->mAttributes.mIsFluddEmitting)
        return;

    auto *controller = player->mController;
    if (!controller)
        return;

    if (!(controller->mButtons.mFrameInput & TMarioGamePad::L))
        return;

    player->changePlayerStatus(WaterPoundState, 0, false);
    player->mSpeed = TVec3f::zero();

    player->mSubState      = 0;
    player->mSubStateTimer = 0;
}

BETTER_SMS_FOR_CALLBACK bool processWaterPound(TMario *player) {
    player->checkCollision();

    if (!player->checkGroundPlane(player->mTranslation.x, player->mTranslation.y + 80.0f,
                                  player->mTranslation.z, &player->mFloorBelow,
                                  &player->mFloorTriangle)) {
        player->changePlayerStatus(TMario::STATE_FALL, 0, false);
        return true;
    }

    if (player->mFloorBelow > player->mTranslation.y - 10.0f) {
        player->mTranslation.y = player->mFloorBelow;
        if (player->mSpeed.y < -15.0f) {
            player->strongTouchDownEffect();
        } else {
            player->smallTouchDownEffect();
        }
        player->changePlayerStatus(0x22D1, 0, false);
        return true;
    }

    player->checkWallPlane(player->mTranslation, 80.0f, player->mCollisionXZSize);

    if (player->mSubState == 0) {
        if (player->mSubStateTimer++ == 0) {
            if (player->onYoshi()) {
                player->mYoshi->mActor->setBckFromIndex(8);
                MSoundSE::startSoundActor(0x792B, player->mTranslation, 0, nullptr, 0, 4);
            }
            player->setAnimation(GroundPoundAnimationID, 0.5f);
        }
        if (player->isAnimeLoopOrStop()) {
            player->mSpeed.y       = -20.0f;
            player->mSubState      = 1;
            player->mSubStateTimer = 0;
        }
    }

    else if (player->mSubState == 1) {
        if (player->mSubStateTimer++ > 20) {
            player->mSubState      = 2;
            player->mSubStateTimer = 0;
            MSoundSE::startSoundActor(0x1921, player->mTranslation, 0, nullptr, 0, 4);
        }
    }

    else if (player->mSubState == 2) {
        player->mTranslation.y += player->mSpeed.y;
        player->mSpeed.y *= 0.97f;
        if (fabsf(player->mSpeed.y) < 1.0f) {
            player->mSpeed.y = 0.0f;
            player->changePlayerStatus(0x22D1, 0, false);
            return true;
        }
    }

    return false;
}

static void thinkYoshiAnimationForWaterPound(TYoshi *yoshi) {
    if (yoshi->mMario->mState == WaterPoundState) {
        u32 bckId = 8;
        if (yoshi->mMario->mSubState == 2) {
            bckId = 7;
        }
        f32 frameRate = yoshi->mMario->getMotionFrameCtrl()->mFrameRate;
        yoshi->mActor->getFrameCtrl(MActor::BCK)->mFrameRate = frameRate;
        if (yoshi->mActor->getCurAnmIdx(MActor::BCK) == bckId) {
            return;
        }
        yoshi->mActor->setBckFromIndex(bckId);
        yoshi->thinkBtp(bckId);

        void **anmSound = *(void ***)(((u8 *)yoshi) + SMS_OFFSET(TYoshi, 0x11C));
        initAnmSound__9MAnmSoundFPvUlf(*(MAnmSound **)(((u8 *)yoshi) + SMS_OFFSET(TYoshi, 0x118)),
                                       *(u32 **)(anmSound + bckId), 1, 0.0f);
        if (yoshi->mActor->mBckInfo) {
            yoshi->mActor->mBckInfo->setMotionBlendRatio(0.0f);
        }
        return;
    }

    yoshi->thinkAnimation();
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8026E3A4, 0, 0, 0), thinkYoshiAnimationForWaterPound);

static u32 getMarioCameraStatus() {
    if (gpMarioAddress->mState == WaterPoundState) {
        return 0x8008A9;
    }
    return gpMarioAddress->mState;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80021E5C, 0, 0, 0), getMarioCameraStatus);