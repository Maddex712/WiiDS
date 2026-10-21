#include <Dolphin/MTX.h>
#include <Dolphin/math.h>
#include <Dolphin/types.h>

#include <SMS/Enemy/EnemyMario.hxx>
#include <SMS/Player/Mario.hxx>

#include <BetterSMS/libs/constmath.hxx>
#include <BetterSMS/libs/triangle.hxx>
#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>

#include <BetterSMS/libs/geometry.hxx>

#include "player.hxx"
#include "settings.hxx"

using namespace BetterSMS;

#define EXPAND_WARP_SET(base)                                                                      \
    (base) : case ((base) + 10):                                                                   \
    case ((base) + 20):                                                                            \
    case ((base) + 30)
#define EXPAND_WARP_CATEGORY(base)                                                                 \
    (base) : case ((base) + 1):                                                                    \
    case ((base) + 2):                                                                             \
    case ((base) + 3):                                                                             \
    case ((base) + 4)

BETTER_SMS_FOR_CALLBACK void updateFallDamageContext(TMario *player, bool isMario) {
    if (gFallDamageSetting.getInt() == FallDamageSetting::MODE_NORMAL || !isMario)
        return;

    PlayerMovementData *movementData = getPlayerMovementData(player);

    if ((player->mState & TMario::STATE_AIRBORN) && player->mSpeed.y > 0.0f) {
        movementData->mLastDropY = player->mTranslation.y;
        return;
    }

    if (player->mState == TMario::STATE_HOVER) {
        movementData->mLastDropY = player->mTranslation.y;
        return;
    }

    if (player->mState == 0xF000FFFF) {
        movementData->mLastDropY = player->mTranslation.y;
        return;
    }

    if (!(player->mPrevState & TMario::STATE_AIRBORN)) {
        movementData->mLastDropY = player->mLastGroundedPos.y;
        return;
    }
}

static f32 getTrueFloorContactSpeed(TMario *player) {
    const f32 playerRotY    = convertAngleS16ToFloat(player->mAngle.y);
    const Vec playerForward = {sinf(angleToRadians(-playerRotY)), 0.0f,
                               cosf(angleToRadians(playerRotY))};
    const Vec up            = {0.0f, 1.0f, 0.0f};

    Vec floorNormal;
    PSVECNormalize(reinterpret_cast<Vec *>(player->mFloorTriangle->getNormal()), &floorNormal);

    const f32 slopeStrength = PSVECDotProduct(&up, &floorNormal);
    const f32 lookAtRatio   = Vector3::lookAtRatio(playerForward, floorNormal);
    if (isnan(lookAtRatio))
        return player->mSpeed.y;

    return (player->mSpeed.y - (((lookAtRatio - 0.5f) * 2.0f) * player->mForwardSpeed)) *
           slopeStrength;
}

static void dynamicFallDamage(TMario *player, int dmg, int type, int emitcount, int tremble) {
    if (gFallDamageSetting.getInt() == FallDamageSetting::MODE_NORMAL) {
        player->floorDamageExec(dmg, type, emitcount, tremble);
        return;
    }

    Player::TPlayerData *playerData = Player::getData(player);
    switch (playerData->mPrevCollisionFloorType & 0xFFF) {
    case EXPAND_WARP_SET(3060):
    case EXPAND_WARP_SET(3061):
    case EXPAND_WARP_SET(3062):
    case EXPAND_WARP_SET(3063):
    case EXPAND_WARP_SET(3064):
        return;
    }

    dmg *= static_cast<int>((player->mLastGroundedPos.y - player->mTranslation.y) /
                            (player->mDeParams.mDamageFallHeight.get() / 1.5f));

    const f32 terminalVelocity = -75.0f * player->mJumpParams.mGravity.get();
    const f32 trueContact      = getTrueFloorContactSpeed(player);
    dmg                        = static_cast<int>(dmg * (trueContact / terminalVelocity));
    dmg                        = Max(1, Min(dmg, player->mHealth - 1));

    if (dmg > 2) {
        type      = 0;  // shaky
        emitcount = (dmg - 2) * 3;
    }

    if (player->mSpeed.y <= (-75.0f * player->mJumpParams.mGravity.get()) + 5.0f)
        player->floorDamageExec(dmg, type, emitcount, tremble);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8024C73C, 0x80246BB4, 0, 0), dynamicFallDamage);