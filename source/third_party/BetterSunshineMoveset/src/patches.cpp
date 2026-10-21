#include <Dolphin/types.h>

#include <SMS/Player/Mario.hxx>

#include <SMS/raw_fn.hxx>

#include <BetterSMS/libs/constmath.hxx>
#include <BetterSMS/libs/geometry.hxx>
#include <BetterSMS/libs/triangle.hxx>
#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>
#include <BetterSMS/stage.hxx>

#include "common.hxx"

using namespace BetterSMS;

static void patchRoofCollisionSpeed(TMario *player, f32 _speed) {
    const TBGCheckData *roof = player->mRoofTriangle;
    if (!roof) {
        player->setPlayerVelocity(_speed);
        return;
    }

    TVec3f down(0.0f, -1.0f, 0.0f);

    TVec3f nroofvec;
    PSVECNormalize(*roof->getNormal(), nroofvec);

    const f32 ratio = Vector3::angleBetween(nroofvec, down);
    player->setPlayerVelocity(lerp(_speed, player->mForwardSpeed, ratio));
}
SMS_PATCH_BL(SMS_PORT_REGION(0x802569BC, 0x8024E748, 0, 0), patchRoofCollisionSpeed);

// Remove Dive While Wall Sliding
SMS_WRITE_32(SMS_PORT_REGION(0x8024BC10, 0x8024399c, 0, 0), 0x48000068);