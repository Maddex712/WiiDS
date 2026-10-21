#include <Dolphin/OS.h>
#include <JSystem/J2D/J2DOrthoGraph.hxx>
#include <JSystem/J2D/J2DTextBox.hxx>

#include <SMS/System/Application.hxx>

#include <BetterSMS/game.hxx>
#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>
#include <BetterSMS/stage.hxx>

#include "common.hxx"

extern void onPlayerInit(TMario *, bool);
// extern void onPlayerUpdate(TMario *, bool);

extern void initFastTurbo(TMario *, bool);
extern void updateTurboContext(TMario *, bool);

extern void checkSpamHover(TMario *, bool);
extern void checkRocketNozzleDiveBlast(TMario *, bool);

extern u32 CrouchState;
extern void checkForCrouch(TMario *, bool);
extern bool processCrouch(TMario *);

extern u32 PoundJumpState;
extern void checkForPoundJump(TMario *, bool);
extern bool processPoundJump(TMario *);

extern u32 WaterPoundState;
extern void checkForWaterPound(TMario *, bool);
extern bool processWaterPound(TMario *);

extern u32 MultiJumpState;
extern void checkStartInAir(TMario *player);
extern void checkForMultiJump(TMario *, bool);
extern bool processMultiJump(TMario *);

extern void updateFallDamageContext(TMario *, bool);

extern void checkForCameraDemoHover(TMarDirector *director);

// Module definition

using namespace BetterSMS;

Settings::SettingsGroup gSettingsGroup(1, 0, Settings::Priority::MODE);

static BetterSMS::ModuleInfo sModuleInfo("Better Sunshine Moveset", 1, 1, &gSettingsGroup);

static void initModule() {
    // Register settings
    gSettingsGroup.addSetting(&gLongJumpMappingSetting);
    gSettingsGroup.addSetting(&gLongJumpSetting);
    gSettingsGroup.addSetting(&gBackFlipSetting);
    gSettingsGroup.addSetting(&gHoverBurstSetting);
    gSettingsGroup.addSetting(&gHoverSlideSetting);
    gSettingsGroup.addSetting(&gRocketDiveSetting);
    gSettingsGroup.addSetting(&gFastTurboSetting);
    gSettingsGroup.addSetting(&gFastDiveSetting);
    gSettingsGroup.addSetting(&gFallDamageSetting);
    {
        auto &saveInfo        = gSettingsGroup.getSaveInfo();
        saveInfo.mSaveName    = Settings::getGroupName(gSettingsGroup);
        saveInfo.mBlocks      = 1;
        saveInfo.mGameCode    = 'GMSB';
        saveInfo.mCompany     = 0x3031;  // '01'
        saveInfo.mBannerFmt   = CARD_BANNER_CI;
        saveInfo.mBannerImage = reinterpret_cast<const ResTIMG *>(gSaveBnr);
        saveInfo.mIconFmt     = CARD_ICON_CI;
        saveInfo.mIconSpeed   = CARD_SPEED_FAST;
        saveInfo.mIconCount   = 2;
        saveInfo.mIconTable   = reinterpret_cast<const ResTIMG *>(gSaveIcon);
        saveInfo.mSaveGlobal  = false;
    }

    // Register module
    BetterSMS::registerModule(sModuleInfo);

    Player::addInitCallback(onPlayerInit);
    // Player::addUpdateCallback("_moveset_update", onPlayerUpdate);
    Player::addInitCallback(initFastTurbo);
    Player::addLoadAfterCallback(checkStartInAir);
    Player::addUpdateCallback(updateTurboContext);
    Player::addUpdateCallback(checkSpamHover);
    Player::addUpdateCallback(checkRocketNozzleDiveBlast);
    Player::addUpdateCallback(checkForMultiJump);
    Player::addUpdateCallback(checkForCrouch);
    Player::addUpdateCallback(checkForPoundJump);
    Player::addUpdateCallback(checkForWaterPound);
    Stage::addUpdateCallback(checkForCameraDemoHover);
    Player::addUpdateCallback(updateFallDamageContext);
    Player::registerStateMachine(MultiJumpState, processMultiJump);
    Player::registerStateMachine(CrouchState, processCrouch);
    Player::registerStateMachine(PoundJumpState, processPoundJump);
    Player::registerStateMachine(WaterPoundState, processWaterPound);
}


// Definition block
KURIBO_MODULE_BEGIN("Better Sunshine Moveset", "JoshuaMK", "v1.0") {
    // Set the load and unload callbacks to our registration functions
    KURIBO_EXECUTE_ON_LOAD { initModule(); }
}
KURIBO_MODULE_END()

// Map on D Pad down
SMS_WRITE_32(SMS_PORT_REGION(0x8017A830, 0x801706F4, 0, 0), 0x5400077B);
SMS_WRITE_32(SMS_PORT_REGION(0x80297A60, 0x8028F8F8, 0, 0), 0x5400077B);
