#include <Dolphin/MTX.h>
#include <Dolphin/math.h>
#include <Dolphin/types.h>

#include <SMS/Enemy/EnemyMario.hxx>
#include <SMS/Player/Mario.hxx>
#include <SMS/macros.h>

#include "player.hxx"
#include "settings.hxx"
#include <BetterSMS/libs/constmath.hxx>
#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>

using namespace BetterSMS;

static void addVelocity(TMario *player, f32 velocity) {
    auto *playerData = Player::getData(player);

    if (!gFastDiveSetting.getBool() || !playerData->isMario()) {
        player->mForwardSpeed = Min(player->mForwardSpeed + velocity, 99.0f);
        return;
    }

    player->mForwardSpeed += velocity;
}
SMS_PATCH_B(SMS_PORT_REGION(0x802558A4, 0x8024D630, 0, 0), addVelocity);

static f32 checkGroundSpeedLimit() {
    TMario *player;
    SMS_FROM_GPR(31, player);

    auto *playerData = Player::getData(player);

    if (!playerData->isMario()) {
        return 1.0f;
    }

    bool isOnYoshi = player->onYoshi();
    if (isOnYoshi) {
        return player->mYoshiParams.mRunYoshiMult.get();
    }

    auto *params = getPlayerMovementParams(player);
    if (!params) {
        return 1.0f;
    }

    if (playerData->isMario()) {
        return params->mSpeedMultiplier.get();
    }

    return 1.0f;
}
SMS_WRITE_32(SMS_PORT_REGION(0x8025B8BC, 0x80253648, 0, 0), 0x60000000);
SMS_PATCH_BL(SMS_PORT_REGION(0x8025B8C0, 0x8025364C, 0, 0), checkGroundSpeedLimit);
SMS_WRITE_32(SMS_PORT_REGION(0x8025B8C4, 0x80253650, 0, 0), 0xEFFF0072);

static f32 checkSlideSpeedMulti() {
    TMario *player;
    SMS_FROM_GPR(30, player);

    constexpr f32 speedCap         = 100.0f;
    constexpr f32 rocketMultiplier = 1.8f;
    constexpr f32 hoverMultiplier  = 1.2f;
    constexpr f32 brakeRate        = 0.005f;

    auto *playerData = Player::getData(player);

    if (!playerData->isMario()) {
        return speedCap;
    }

    auto *data = getPlayerMovementData(player);
    if (!data)
        return speedCap;

    auto *params = getPlayerMovementParams(player);
    if (!params)
        return speedCap;

    if (player->mFludd && isEmitting__9TWaterGunFv(player->mFludd)) {
        if (player->mFludd->mCurrentNozzle == TWaterGun::Hover ||
            player->mFludd->mCurrentNozzle == TWaterGun::Underwater)
            data->mSlideSpeedMultiplier = hoverMultiplier;
        else if (player->mFludd->mCurrentNozzle == TWaterGun::Rocket) {
            const f32 multiplier =
                (rocketMultiplier * ((speedCap * rocketMultiplier) / player->mForwardSpeed));
            data->mSlideSpeedMultiplier = rocketMultiplier;
            player->mPrevSpeed.scale(multiplier);
        } else {
            data->mSlideSpeedMultiplier = Max(data->mSlideSpeedMultiplier - brakeRate, 1.0f);
        }
    } else {
        data->mSlideSpeedMultiplier = Max(data->mSlideSpeedMultiplier - brakeRate, 1.0f);
    }

    if (!player->onYoshi() && player->isMario()) {
        return speedCap * params->mSlideMultiplier.get() * data->mSlideSpeedMultiplier;
    } else {
        return speedCap;
    }
}
SMS_WRITE_32(SMS_PORT_REGION(0x8025C3D8, 0x80254164, 0, 0), 0x40810028);
SMS_WRITE_32(SMS_PORT_REGION(0x8025C3FC, 0x80254188, 0, 0), 0xFC800018);
SMS_WRITE_32(SMS_PORT_REGION(0x8025C400, 0x8025418C, 0, 0), 0xD09E00B0);
SMS_PATCH_BL(SMS_PORT_REGION(0x8025C404, 0x80254190, 0, 0), checkSlideSpeedMulti);
SMS_WRITE_32(SMS_PORT_REGION(0x8025C408, 0x80254194, 0, 0), 0xFC400890);
SMS_WRITE_32(SMS_PORT_REGION(0x8025C410, 0x8025419C, 0, 0), 0x60000000);

static void scaleHangSpeed(TMario *player) {
    auto *playerData = Player::getData(player);

    auto *data = getPlayerMovementData(player);
    if (!data)
        return;

    auto *params = getPlayerMovementParams(player);
    if (!params)
        return;

    player->mForwardSpeed += 1.0f;

    if (playerData->isMario())
        player->mForwardSpeed = Min(player->mForwardSpeed, 4.0f * params->mSpeedMultiplier.get());
    else
        player->mForwardSpeed = Min(player->mForwardSpeed, 4.0f);
}
// SMS_PATCH_BL(SMS_PORT_REGION(0x802615AC, 0, 0, 0), scaleHangSpeed);
// SMS_WRITE_32(SMS_PORT_REGION(0x802615B0, 0, 0, 0), 0x60000000);

static void checkJumpSpeedLimit(f32 speed) {
    TMario *player;
    SMS_FROM_GPR(31, player);

    f32 speedCap     = 32.0f;
    f32 speedReducer = 0.2f;

    auto *playerData = Player::getData(player);

    if (auto *params = getPlayerMovementParams(player)) {
        if (!player->onYoshi() && playerData->isMario()) {
            speedCap *= params->mSpeedMultiplier.get();
            speedReducer *= scaleLinearAtAnchor<f32>(params->mSpeedMultiplier.get(), 3.0f, 1.0f);
        }
    }

    if (speed > speedCap) {
        player->mForwardSpeed = (speed - speedReducer);
    }
}
SMS_WRITE_32(SMS_PORT_REGION(0x8024CB00, 0x8024488C, 0, 0),
             SMS_PORT_REGION(0xC162EF70, 0xC162EDE8, 0, 0));
SMS_WRITE_32(SMS_PORT_REGION(0x8024CC50, 0x802449DC, 0, 0), 0xED600072);
SMS_WRITE_32(SMS_PORT_REGION(0x8024CC60, 0x802449EC, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x8024CC64, 0x802449F0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x8024CC68, 0x802449F4, 0, 0), 0x60000000);
SMS_PATCH_BL(SMS_PORT_REGION(0x8024CC6C, 0x802449F8, 0, 0), checkJumpSpeedLimit);

static TMario *checkJumpSpeedMulti(TMario *player, f32 factor, f32 max) {
    auto *playerData = Player::getData(player);

    if (!playerData->isMario()) {
        player->mForwardSpeed = (factor * max) + player->mForwardSpeed;
        return player;
    }

    auto *data = getPlayerMovementData(player);
    if (!data) {
        player->mForwardSpeed = (factor * max) + player->mForwardSpeed;
        return player;
    }

    auto *params = getPlayerMovementParams(player);
    if (!params) {
        player->mForwardSpeed = (factor * max) + player->mForwardSpeed;
        return player;
    }

    if (playerData->isMario() && !player->onYoshi()) {
        player->mForwardSpeed =
            ((factor * params->mSpeedMultiplier.get()) * max) + player->mForwardSpeed;
        return player;
    } else {
        player->mForwardSpeed = (factor * max) + player->mForwardSpeed;
        return player;
    }
}
SMS_WRITE_32(SMS_PORT_REGION(0x8024CCF8, 0x80244A84, 0, 0), 0xEC0B007A);
SMS_WRITE_32(SMS_PORT_REGION(0x8024CD24, 0x80244AB0, 0, 0), 0xEC0B007A);
SMS_PATCH_BL(SMS_PORT_REGION(0x8024CC2C, 0x802449B8, 0, 0), checkJumpSpeedMulti);
SMS_WRITE_32(SMS_PORT_REGION(0x8024CC30, 0x802449BC, 0, 0), 0x57C5043E);