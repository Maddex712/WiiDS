// AYN Thor lower screen: samples Super Mario Galaxy's state on the game thread, once a frame
// after the scene's update (GameSystem::frameLoop), into a ThorLowerState and hands it to the
// platform (port_thor_lower_commit, platform/mobile/thor/ThorLowerBridge.cpp). Read-only: only
// the game's own query functions are called, and only in the scene states where they are valid;
// nothing here changes progression, inventory, camera or saves. No heap allocation: the state
// lives in a static buffer (scene heaps come and go under this thread).
#ifdef TARGET_MOBILE

#include "Game/Scene/GameScene.hpp"
#include "Game/Scene/ScenarioSelectScene.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataConst.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "port/port.h"
#include "port/thor_lower.h"

#include <stdio.h>
#include <string.h>

// GameScene's step (GameScene.cpp, beside its nerves): a ThorLowerPhase.
int thorGameScenePhase(const GameScene* pScene);
// Whether the title ran in this update (FileSelector.cpp); taking it clears it.
bool thorFileSelectorTakeTitle();
// Whether the prologue's picture book ran in this update (ProloguePictureBook.cpp); taking it clears it.
bool thorStorybookTake();

extern "C" __attribute__((weak)) void port_thor_lower_commit(const ThorLowerState* state);

namespace {
    bool sTitleLive;
    s32 sStorybookHold;  // frames the picture book still counts as open (bridges a skipped update)
    ThorLowerState sState;
    char sLastStage[32];
    s32 sLastScenario = -1;
    s32 sLastPhase = -1;
    s32 sLastTotalStars = -1;
    s32 sGeneration;
    s32 sFrame;
    s32 sLedgerAge = 1 << 20;

    // Game text (UTF-16 with -fshort-wchar) without its control tags: a tag is 0x001A, then a
    // unit whose high byte is the tag's whole size in bytes.
    void copyText(uint16_t* pOut, s32 capacity, const wchar_t* pText) {
        s32 n = 0;
        if (pText != nullptr) {
            for (s32 i = 0; pText[i] != 0 && n + 1 < capacity && i < 512; i++) {
                const uint16_t unit = static_cast< uint16_t >(pText[i]);
                if (unit == 0x001A) {
                    const s32 size = static_cast< uint16_t >(pText[i + 1]) >> 8;
                    if (size < 4) {
                        break;
                    }
                    i += size / 2 - 1;
                    continue;
                }
                if (unit == 0x000A) {
                    if (n > 0 && pOut[n - 1] != 0x20) {
                        pOut[n++] = 0x20;
                    }
                    continue;
                }
                pOut[n++] = unit;
            }
        }
        while (n > 0 && pOut[n - 1] == 0x20) {
            n--;
        }
        pOut[n] = 0;
        for (s32 i = n + 1; i < capacity; i++) {
            pOut[i] = 0;
        }
    }

    bool isScene(const GameSystemSceneController* pController, const char* pName) {
        return strcmp(pController->mCurrSceneControlInfo.mScene, pName) == 0;
    }

    s32 phaseOf(GameSystemSceneController* pController) {
        if (pController->isChangingScene()) {
            return THOR_PHASE_LOADING;
        }
        if (pController->mScenarioSelectScene != nullptr && pController->mScenarioSelectScene->isActive() &&
            pController->mScenarioSelectScene->isScenarioSelecting()) {
            return THOR_PHASE_MISSION_SELECT;
        }
        if (isScene(pController, "Logo")) {
            return THOR_PHASE_LOGO;
        }
        if (isScene(pController, "Intermission") || isScene(pController, "PlayTimer")) {
            return THOR_PHASE_INTERMISSION;
        }
        if (!isScene(pController, "Game") || pController->mScene == nullptr) {
            return THOR_PHASE_NONE;
        }
        if (strcmp(pController->mCurrSceneControlInfo.mStage, "FileSelect") == 0) {
            return THOR_PHASE_FILE_SELECT;
        }
        if (!pController->isSceneInitializeState(SceneInitializeState_End)) {
            return THOR_PHASE_LOADING;
        }
        s32 phase = thorGameScenePhase(static_cast< const GameScene* >(pController->mScene));
        if (phase == THOR_PHASE_PLAY && MR::isDemoActive()) {
            phase = THOR_PHASE_CUTSCENE;
        }
        return phase;
    }

    // This galaxy's Power Stars and the selected mission's name.
    void sampleGalaxy(const char* pStage, s32 selected) {
        GalaxyStatusAccessor accessor = MR::makeGalaxyStatusAccessor(pStage);
        if (accessor.mScenarioData == nullptr) {
            return;
        }
        const s32 stars = accessor.getPowerStarNum();
        if (stars <= 0 || stars > THOR_LOWER_STARS) {
            return;
        }
        sState.galaxyStars = stars;
        sState.galaxyStarsOwned = accessor.getPowerStarNumOwned();
        for (s32 i = 0; i < THOR_LOWER_STARS; i++) {
            s32 flags = 0;
            const s32 scenario = i + 1;
            if (scenario <= stars) {
                if (accessor.hasPowerStar(scenario)) flags |= THOR_STAR_OWNED;
                if (accessor.isValidNormalComet(scenario)) flags |= THOR_STAR_COMET;
                if (accessor.isHiddenStar(scenario)) flags |= THOR_STAR_HIDDEN;
                if (accessor.isValidCoin100(scenario)) flags |= THOR_STAR_PURPLE;
                if (GameDataConst::isGrandStar(pStage, scenario)) flags |= THOR_STAR_GRAND;
                if ((flags & THOR_STAR_OWNED) && MR::isPowerStarGreen(pStage, scenario)) flags |= THOR_STAR_GREEN;
                if (scenario == selected) flags |= THOR_STAR_SELECTED;
            }
            sState.starFlags[i] = flags;
            // Names only for missions the player has earned or is playing (the game reveals no more).
            const bool named = scenario <= stars && (flags & (THOR_STAR_OWNED | THOR_STAR_SELECTED)) != 0;
            copyText(sState.starNames[i], THOR_LOWER_NAME, named ? MR::getScenarioNameOnCurrentLanguage(pStage, scenario) : nullptr);
        }
        sState.valid |= THOR_VALID_STARS;
    }

    // The Observatory ledger: open galaxies with stars, as the game's Power Star list builds it.
    void sampleLedger(const char* pStage) {
        s32 count = 0;
        for (ScenarioDataIter iter = MR::makeBeginScenarioDataIter(); !iter.isEnd() && count < THOR_LOWER_GALAXIES; iter.goNext()) {
            GalaxyStatusAccessor accessor = iter.makeAccessor();
            if (accessor.getPowerStarNum() == 0 || !MR::isOnGameEventFlagGalaxyOpen(accessor.getName())) {
                continue;
            }
            sState.galaxyOwned[count] = accessor.getPowerStarNumOwned();
            sState.galaxyTotal[count] = accessor.getPowerStarNum();
            sState.galaxyFlags[count] = (accessor.isCompleted() ? THOR_GALAXY_COMPLETED : 0) |
                                        (strcmp(accessor.getName(), pStage) == 0 ? THOR_GALAXY_CURRENT : 0);
            copyText(sState.galaxyNames[count], THOR_LOWER_SHORT, MR::getGalaxyNameShortOnCurrentLanguage(accessor.getName()));
            count++;
        }
        for (s32 i = count; i < THOR_LOWER_GALAXIES; i++) {
            sState.galaxyOwned[i] = sState.galaxyTotal[i] = sState.galaxyFlags[i] = 0;
            sState.galaxyNames[i][0] = 0;
        }
        sState.galaxyCount = count;
        sState.valid |= THOR_VALID_LEDGER;
    }
}  // namespace

// The title is space itself (its logo is 2D, drawn after HUD_BEGIN), so GameScene::draw marks
// the frames it draws for the lower screen's live space (PORT_GX_MARK_THOR_SPACE). The file
// screens add 3D file planets and heads, which the lower screen does not mirror, and a galaxy's
// sky is drawn among its planets: there nothing is marked and the last title picture stays.
// Decided after this update, for the frame drawn from it, so the camera cut into the file screens
// is never taken.
bool thorLowerTitleLive() {
    return sTitleLive;
}

void thorLowerPublish() {
    const bool title = thorFileSelectorTakeTitle();
    if (thorStorybookTake()) sStorybookHold = 10;
    else if (sStorybookHold > 0) sStorybookHold--;
    if (port_thor_lower_commit == nullptr) {
        sTitleLive = false;
        return;
    }
    sFrame++;
    GameSystem* pSystem = SingletonHolder< GameSystem >::get();
    GameSystemSceneController* pController = pSystem != nullptr ? pSystem->mSceneController : nullptr;
    const s32 phase = pController != nullptr ? phaseOf(pController) : THOR_PHASE_NONE;
    sTitleLive = title && phase == THOR_PHASE_FILE_SELECT;
    const char* pStage = pController != nullptr ? pController->mCurrSceneControlInfo.mStage : "";
    const s32 scenario = pController != nullptr ? pController->getCurrentScenarioNo() : 0;
    const s32 selected = pController != nullptr ? pController->getCurrentSelectedScenarioNo() : 0;

    // Anything that changes what the screen describes starts a new generation; the UI drops the old
    // picture rather than showing a previous galaxy as live.
    const bool newStage = strncmp(pStage, sLastStage, sizeof sLastStage) != 0;
    if (newStage || scenario != sLastScenario || (phase == THOR_PHASE_LOADING) != (sLastPhase == THOR_PHASE_LOADING)) {
        sGeneration++;
        strncpy(sLastStage, pStage, sizeof sLastStage - 1);
        sLastScenario = scenario;
        sLedgerAge = 1 << 20;
    }
    sLastPhase = phase;


    sState.version = THOR_LOWER_VERSION;
    sState.phase = phase;
    sState.generation = sGeneration;
    sState.frame = sFrame;
    sState.valid = 0;
    sState.flags = 0;

    const bool inGalaxy = phase == THOR_PHASE_MISSION_SELECT || phase == THOR_PHASE_PLAY || phase == THOR_PHASE_PAUSE ||
                          phase == THOR_PHASE_STAR_GET || phase == THOR_PHASE_CUTSCENE || phase == THOR_PHASE_MISS ||
                          phase == THOR_PHASE_MAP;
    if (!inGalaxy || pStage[0] == 0) {
        // Diagnostics only (never shown to the player): "scene/stage".
        memset(sState.stage, 0, sizeof sState.stage);
        if (pController != nullptr)
            snprintf(sState.stage, sizeof sState.stage, "%.15s/%.15s", pController->mCurrSceneControlInfo.mScene, pStage);
        port_thor_lower_commit(&sState);
        return;
    }

    strncpy(sState.stage, pStage, sizeof sState.stage - 1);
    sState.stage[sizeof sState.stage - 1] = 0;
    copyText(sState.galaxyName, THOR_LOWER_NAME, MR::getGalaxyNameOnCurrentLanguage(pStage));
    sState.valid |= THOR_VALID_STAGE;

    const bool astro = MR::isStageAstroLocation();
    if (astro) sState.flags |= THOR_FLAG_ASTRO;
    if (MR::isPlayerLuigi()) sState.flags |= THOR_FLAG_LUIGI;
    if (port_is_infinite_starbits()) sState.flags |= THOR_FLAG_INFINITE_BITS;
    if (pController->isScenarioDecided()) sState.flags |= THOR_FLAG_SCENARIO_DECIDED;
    if (sStorybookHold > 0) sState.flags |= THOR_FLAG_STORYBOOK;

    // Where Mario is, for the lower screen's map.
    if (MR::isExistMario()) {
        const TVec3f* pPos = MR::getPlayerPos();
        TVec3f up(0.0f, 1.0f, 0.0f);
        MR::getPlayerUpVec(&up);
        sState.marioPos[0] = pPos->x; sState.marioPos[1] = pPos->y; sState.marioPos[2] = pPos->z;
        sState.marioUp[0] = up.x; sState.marioUp[1] = up.y; sState.marioUp[2] = up.z;
        sState.valid |= THOR_VALID_MARIO;
    }

    const s32 mission = selected > 0 ? selected : scenario;
    sState.scenarioNo = scenario;
    sState.selectedScenarioNo = selected;
    if (!astro && mission > 0) {
        copyText(sState.scenarioName, THOR_LOWER_NAME, MR::getScenarioNameOnCurrentLanguage(pStage, mission));
        sState.valid |= THOR_VALID_SCENARIO;
    } else {
        sState.scenarioName[0] = 0;
    }

    // Counters exist only while the stage's own result holder does (not on the mission select).
    const bool counters = phase != THOR_PHASE_MISSION_SELECT && MR::isExistSceneObj(SceneObj_ScenePlayingResult);
    if (counters) {
        sState.totalStars = MR::getPowerStarNum();
        sState.starBitsRun = MR::getStarPieceNum();
        sState.starBitsBanked = MR::getStockedStarPieceNum();
        sState.coins = MR::getCoinNum();
        sState.lives = MR::getPlayerLeft();
        sState.purpleCoins = MR::getPurpleCoinNum();
        sState.valid |= THOR_VALID_COUNTERS;
    } else {
        sState.totalStars = MR::getPowerStarNum();
        sState.starBitsBanked = MR::getStockedStarPieceNum();
        sState.lives = MR::getPlayerLeft();
    }

    // Stars and the ledger change rarely: re-read on a new generation, a new star, or twice a second.
    if (sState.totalStars != sLastTotalStars) {
        sLastTotalStars = sState.totalStars;
        sLedgerAge = 1 << 20;
    }
    if (++sLedgerAge >= 30) {
        sLedgerAge = 0;
        if (astro) {
            sampleLedger(pStage);
            sState.galaxyStars = sState.galaxyStarsOwned = 0;
        } else {
            sampleGalaxy(pStage, mission);
            sState.galaxyCount = 0;
        }
    } else {
        // Unchanged since the last read: keep the earlier result's validity.
        if (sState.galaxyStars > 0 && !astro) sState.valid |= THOR_VALID_STARS;
        if (sState.galaxyCount > 0 && astro) sState.valid |= THOR_VALID_LEDGER;
    }
    if (!astro && sState.galaxyStars > 0 && mission > 0 && mission <= sState.galaxyStars &&
        (sState.starFlags[mission - 1] & THOR_STAR_PURPLE) && counters) {
        sState.valid |= THOR_VALID_PURPLE;
    }
    port_thor_lower_commit(&sState);
}

#endif  // TARGET_MOBILE
