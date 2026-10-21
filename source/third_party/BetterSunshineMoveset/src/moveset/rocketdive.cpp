#include <SMS/Player/Mario.hxx>

#include <SMS/GC2D/GCConsole2.hxx>
#include <SMS/MSound/MSound.hxx>
#include <SMS/MSound/MSoundSESystem.hxx>
#include <SMS/Player/Watergun.hxx>
#include <SMS/raw_fn.hxx>

#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>

#include "common.hxx"

BETTER_SMS_FOR_CALLBACK void checkRocketNozzleDiveBlast(TMario *player, bool isMario) {
    if (!isMario) {
        return;
    }

    TWaterGun *fludd = player->mFludd;
    if (!fludd || !gRocketDiveSetting.getBool())
        return;

    if (fludd->mCurrentNozzle != TWaterGun::Rocket)
        return;

    TNozzleBase *nozzle = fludd->mNozzleList[fludd->mCurrentNozzle];
    nozzle->mEmitParams.mReactionPow.set(player->mState != TMario::STATE_DIVE ? 0.0f : 0.5f);
}