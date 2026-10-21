#include <SMS/Player/Mario.hxx>

#include <SMS/MSound/MSound.hxx>
#include <SMS/MSound/MSoundSESystem.hxx>
#include <SMS/Player/Watergun.hxx>
#include <SMS/System/MarDirector.hxx>
#include <SMS/raw_fn.hxx>

#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>

#include "player.hxx"
#include "settings.hxx"

static void uncapDiveSpeed(f32 diveSpeed) {
    TMario *player;
    SMS_FROM_GPR(30, player);

    auto *playerData = Player::getData(player);

    if (gFastDiveSetting.getBool() && playerData->isMario()) {
        auto *params = getPlayerMovementParams(player);
        if ((player->mState & TMario::STATE_AIRBORN)) {
            player->mForwardSpeed =
                Max(48.0f * params->mSpeedMultiplier.get(), 15.0f + player->mForwardSpeed);
        } else {
            player->mForwardSpeed =
                Min(48.0f * params->mSpeedMultiplier.get(), 15.0f + player->mForwardSpeed);
        }
    } else {
        player->mForwardSpeed = Min(48.0f, 15.0f + player->mForwardSpeed);
    }
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80254920, 0, 0, 0), uncapDiveSpeed);

static void uncapDiveRolloutSpeed() {
    TMario *player;
    SMS_FROM_GPR(30, player);

    auto *playerData = Player::getData(player);

    if (gFastDiveSetting.getBool() && playerData->isMario())
        player->mForwardSpeed =
            Max(player->mJumpParams.mRotBroadJumpForce.get(), player->mForwardSpeed);
    else
        player->mForwardSpeed = player->mJumpParams.mRotBroadJumpForce.get();
}
SMS_PATCH_BL(SMS_PORT_REGION(0x802549B0, 0, 0, 0), uncapDiveRolloutSpeed);

static void uncapDiveAirSpeed(f32 speed) {
    TMario *player;
    SMS_FROM_GPR(30, player);

    auto *playerData = Player::getData(player);

    if (!gFastDiveSetting.getBool() || !playerData->isMario())
        player->setPlayerVelocity(speed);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x802496AC, 0, 0, 0), uncapDiveAirSpeed);
SMS_PATCH_BL(SMS_PORT_REGION(0x8024976C, 0, 0, 0), uncapDiveAirSpeed);
SMS_PATCH_BL(SMS_PORT_REGION(0x8024999C, 0, 0, 0), uncapDiveAirSpeed);
SMS_PATCH_BL(SMS_PORT_REGION(0x80249C70, 0, 0, 0), uncapDiveAirSpeed);
SMS_PATCH_BL(SMS_PORT_REGION(0x8024C284, 0, 0, 0), uncapDiveAirSpeed);
SMS_PATCH_BL(SMS_PORT_REGION(0x8024C3A8, 0, 0, 0), uncapDiveAirSpeed);