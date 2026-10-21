#include <SMS/Player/Mario.hxx>

#include <SMS/GC2D/GCConsole2.hxx>
#include <SMS/MSound/MSound.hxx>
#include <SMS/MSound/MSoundSESystem.hxx>
#include <SMS/Player/Watergun.hxx>
#include <SMS/raw_fn.hxx>

#include <BetterSMS/libs/constmath.hxx>
#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>

#include "common.hxx"

static f32 checkTurboSpecial() {
    TNozzleTrigger *nozzle;
    SMS_FROM_GPR(29, nozzle);

    if (nozzle->mFludd->mCurrentNozzle == TWaterGun::Turbo && gFastTurboSetting.getBool()) {
        return 0.01f;
    }

    return nozzle->mEmitParams.mInsidePressureMax.get();
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8026C5BC, 0, 0, 0), checkTurboSpecial);

static void checkFillMax() {
    TNozzleTrigger *nozzle;
    SMS_FROM_GPR(29, nozzle);

    if (nozzle->mFludd->mCurrentNozzle == TWaterGun::Turbo && gFastTurboSetting.getBool())
        return;

    nozzle->mTriggerFill = nozzle->mEmitParams.mInsidePressureMax.get();
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8026C5C8, 0, 0, 0), checkFillMax);

static void turboNozzleConeCondition() { /* TMarioEffect * */
    u32 *marioEffect;
    SMS_FROM_GPR(29, marioEffect);

    u32 state = 1;

    TMario *player = *(TMario **)((u8 *)marioEffect + SMS_OFFSET(TMarioEffect, 0x68));
    if (player->mFludd->mCurrentNozzle == TWaterGun::Spray && gFastTurboSetting.getBool())
        state = 0;

    *(u32 *)((u8 *)marioEffect + SMS_OFFSET(TMarioEffect, 0x7C)) = state;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80271ACC, 0, 0, 0), turboNozzleConeCondition);

// We return TMario to exploit registers
static TMario *turboNozzleConeCondition2(TMario *player) { /* TMarioEffect * */
    u32 *marioEffect;
    SMS_FROM_GPR(29, marioEffect);

    if (!player->mAttributes.mIsFluddEmitting)
        return nullptr;

    MActor *coneActor = *(MActor **)((u8 *)marioEffect + SMS_OFFSET(TMarioEffect, 0x80));
    if (player->mFludd->mNozzleTurbo.mSprayState == TNozzleTrigger::ACTIVE &&
        gFastTurboSetting.getBool()) {
        coneActor->mModel->mBaseScale.set(player->mController->mButtons.mAnalogR,
                                          player->mController->mButtons.mAnalogR,
                                          player->mController->mButtons.mAnalogR);
    } else {
        coneActor->mModel->mBaseScale.set(1.0f, 1.0f, 1.0f);
    }

    return player;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80271AD8, 0, 0, 0), turboNozzleConeCondition2);
SMS_WRITE_32(SMS_PORT_REGION(0x80271ADC, 0, 0, 0), 0x2C030000);

static void lerpTurboNozzleSpeed(TMario *player, f32 velocity) {
    auto *playerData = Player::getData(player);

    auto *controller = player->mController;
    if (!controller || !gFastTurboSetting.getBool() || !playerData->isMario()) {
        player->setPlayerVelocity(velocity);
        return;
    }

    auto analogR = controller->mButtons.mAnalogR;
    velocity     = lerp<f32>(40.0f, velocity, analogR);
    player->setPlayerVelocity(velocity);

    player->mRunParams.mDashRotSp.set(lerp<s16>(180, 60, analogR));
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8025B2B0, 0, 0, 0), lerpTurboNozzleSpeed);
SMS_PATCH_BL(SMS_PORT_REGION(0x80272D40, 0, 0, 0), lerpTurboNozzleSpeed);

static void lerpTurboNozzleJumpSpeed() {
    TMario *player;
    SMS_FROM_GPR(30, player);

    auto *playerData = Player::getData(player);

    auto *controller = player->mController;
    if (!controller || !gFastTurboSetting.getBool() || !playerData->isMario()) {
        player->mForwardSpeed = player->mJumpParams.mBroadJumpForce.get();
        player->mSpeed.y      = player->mJumpParams.mBroadJumpForceY.get();
        return;
    }

    auto analogR          = controller->mButtons.mAnalogR;
    player->mForwardSpeed = lerp<f32>(30.0f, player->mJumpParams.mBroadJumpForce.get(), analogR);
    player->mSpeed.y      = lerp<f32>(player->mJumpParams.mBroadJumpForceY.get() * 0.5f,
                                 player->mJumpParams.mBroadJumpForceY.get(), analogR);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80254990, 0, 0, 0), lerpTurboNozzleJumpSpeed);

#define SCALE_PARAM(param, scale) param.set(param.get() * scale)

struct TurboNozzleData {
    f32 mRunningMax;
    f32 mDashMax;
    f32 mJumpingMax;
    f32 mBroadJumpForce;
};

BETTER_SMS_FOR_CALLBACK void initFastTurbo(TMario *player, bool isMario) {
    if (!isMario)
        return;

    TurboNozzleData *data = new TurboNozzleData;
    data->mRunningMax     = player->mDeParams.mRunningMax.get();
    data->mDashMax        = player->mDeParams.mDashMax.get();
    data->mJumpingMax     = player->mJumpParams.mJumpingMax.get();
    data->mBroadJumpForce = player->mJumpParams.mBroadJumpForce.get();
    Player::registerData(player, "movement__turbo_nozzle_data", data);

    if (!player->mFludd || !gFastTurboSetting.getBool())
        return;

    SCALE_PARAM(player->mFludd->mNozzleTurbo.mEmitParams.mAmountMax, 8);
    SCALE_PARAM(player->mFludd->mNozzleTurbo.mEmitParams.mDamageLoss, 8);
    SCALE_PARAM(player->mFludd->mNozzleTurbo.mEmitParams.mSuckRate, 8);
    player->mDeParams.mDashStartTime.set(0.0f);
}

static void resetTurboParams(Player::TPlayerData *playerData) {
    TMario *player = playerData->getPlayer();

    auto *data = reinterpret_cast<TurboNozzleData *>(
        Player::getRegisteredData(player, "movement__turbo_nozzle_data"));

    playerData->mDefaultAttrs.mDeParams.mRunningMax.set(data->mRunningMax);
    playerData->mDefaultAttrs.mDeParams.mDashMax.set(data->mDashMax);
    playerData->mDefaultAttrs.mJumpParams.mJumpingMax.set(data->mJumpingMax);
    playerData->mDefaultAttrs.mJumpParams.mBroadJumpForce.set(data->mBroadJumpForce);
    player->mDeParams.mRunningMax.set(data->mRunningMax);
    player->mDeParams.mDashMax.set(data->mDashMax);
    player->mJumpParams.mJumpingMax.set(data->mJumpingMax);
    player->mJumpParams.mBroadJumpForce.set(data->mBroadJumpForce);
}

static void scaleTurboParams(Player::TPlayerData *playerData) {
    TMario *player = playerData->getPlayer();
    SCALE_PARAM(playerData->mDefaultAttrs.mDeParams.mRunningMax, 1.625f);
    SCALE_PARAM(playerData->mDefaultAttrs.mDeParams.mDashMax, 1.625f);
    SCALE_PARAM(playerData->mDefaultAttrs.mJumpParams.mJumpingMax, 1.625f);
    SCALE_PARAM(playerData->mDefaultAttrs.mJumpParams.mBroadJumpForce, 1.625f);
    SCALE_PARAM(player->mDeParams.mRunningMax, 1.625f);
    SCALE_PARAM(player->mDeParams.mDashMax, 1.625f);
    SCALE_PARAM(player->mJumpParams.mJumpingMax, 1.625f);
    SCALE_PARAM(player->mJumpParams.mBroadJumpForce, 1.625f);
}

static void updateFastTurbo(TMario *player) {
    Player::TPlayerData *playerData = Player::getData(player);
    resetTurboParams(playerData);
    scaleTurboParams(playerData);
}

BETTER_SMS_FOR_CALLBACK void updateTurboContext(TMario *player, bool isMario) {
    if (!isMario)
        return;

    Player::TPlayerData *playerData = Player::getData(player);
    resetTurboParams(playerData);

    auto *fludd = player->mFludd;

    if (!gFastTurboSetting.getBool() || !fludd || fludd->mCurrentNozzle != TWaterGun::Turbo ||
        fludd->mCurrentWater <= 0)
        return;

    if (fludd->mNozzleTurbo.mSprayState == TNozzleTrigger::ACTIVE)
        scaleTurboParams(playerData);

    const auto analogR = player->mController->mButtons.mAnalogR;

    fludd->mNozzleTurbo.mEmitParams.mNum.set(lerp<f32>(1.0f, 7.0f, analogR));
    fludd->mNozzleTurbo.mEmitParams.mDirTremble.set(lerp<f32>(0.01f, 0.08f, analogR));

    if (analogR < 0.1f || fludd->mNozzleTurbo.mSprayState == TNozzleTrigger::DEAD) {
        fludd->mNozzleTurbo.mTriggerFill = 0.0f;
        return;
    }

    fludd->mNozzleTurbo.mTriggerFill = fludd->getPressureMax() * ((analogR - 0.15f) * 1.17647f);
    player->mDeParams.mDashAcc.set(32.1f);  // 32.0f is max

    // Patch for bad land jumps
    if (player->mState == TMario::STATE_RUNNING) {
        if ((player->mController->mButtons.mFrameInput & TMarioGamePad::A)) {
            int rotate;
            if (player->checkStickRotate(&rotate)) {
                if (rotate == 3)
                    player->changePlayerStatus(TMario::STATE_JUMPSPINL, 0, false);
                else if (rotate == 2)
                    player->changePlayerStatus(TMario::STATE_JUMPSPINR, 0, false);
            } else {
                player->changePlayerStatus(0x888, 0, false);
            }
        }
    }
}