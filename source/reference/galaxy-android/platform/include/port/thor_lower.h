// AYN Thor lower screen: one read-only snapshot of Super Mario Galaxy's state.
//
// Sampled on the game thread after each frame's update (decomp/src/Game/System/ThorLowerState.cpp,
// called from GameSystem::frameLoop), published under a seqlock by the platform
// (platform/mobile/thor/ThorLowerBridge.cpp) and copied by the Java lower screen as raw bytes
// (GalaxyActivity.nativeLowerState, little-endian, this exact layout). No game pointers leave the
// game thread; strings are copied. Nothing here writes game state.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    THOR_LOWER_VERSION = 2,
    THOR_LOWER_STARS = 24,      // Power Stars per galaxy (Galaxy 1's largest has 7; bounded)
    THOR_LOWER_GALAXIES = 48,   // open galaxies in the Observatory ledger (Galaxy 1 has 42)
    THOR_LOWER_NAME = 48,       // UTF-16 code units, NUL-terminated
    THOR_LOWER_SHORT = 32,
};

// What the game is doing, from the scene controller and GameScene's own steps.
enum ThorLowerPhase {
    THOR_PHASE_NONE = 0,           // no scene yet / unknown
    THOR_PHASE_LOGO = 1,           // LogoScene (boot logos)
    THOR_PHASE_FILE_SELECT = 2,    // the FileSelect stage (title and file select)
    THOR_PHASE_LOADING = 3,        // scene change under way, or the stage still initializing
    THOR_PHASE_MISSION_SELECT = 4, // ScenarioSelectScene choosing a mission
    THOR_PHASE_PLAY = 5,           // GameScene action
    THOR_PHASE_PAUSE = 6,          // GameScene pause menu
    THOR_PHASE_STAR_GET = 7,       // Power Star / Grand Star get
    THOR_PHASE_CUTSCENE = 8,       // opening camera, scenario starter, event demo, movie
    THOR_PHASE_MISS = 9,           // game over, retry after a miss, save after game over, time up
    THOR_PHASE_MAP = 10,           // the galaxy map
    THOR_PHASE_CREDITS = 11,       // staff roll
    THOR_PHASE_INTERMISSION = 12,  // IntermissionScene / PlayTimerScene
};

// Per-field validity (ThorLowerState.valid).
enum {
    THOR_VALID_STAGE = 1 << 0,     // stage, galaxyName
    THOR_VALID_SCENARIO = 1 << 1,  // scenarioNo, selectedScenarioNo, scenarioName
    THOR_VALID_STARS = 1 << 2,     // galaxyStars, galaxyStarsOwned, starFlags, starNames
    THOR_VALID_COUNTERS = 1 << 3,  // totalStars, starBitsRun, starBitsBanked, coins, lives
    THOR_VALID_LEDGER = 1 << 4,    // galaxyCount, galaxy* arrays (Observatory)
    THOR_VALID_PURPLE = 1 << 5,    // purpleCoins (a 100-purple-coin mission)
    THOR_VALID_MARIO = 1 << 6,     // marioPos, marioUp (the player is in the scene)
};

// ThorLowerState.flags
enum {
    THOR_FLAG_ASTRO = 1 << 0,          // MR::isStageAstroLocation (Observatory, dome, library)
    THOR_FLAG_LUIGI = 1 << 1,          // MR::isPlayerLuigi
    THOR_FLAG_INFINITE_BITS = 1 << 2,  // port setting: Star Bits read 999
    THOR_FLAG_SCENARIO_DECIDED = 1 << 3,
    THOR_FLAG_STORYBOOK = 1 << 4,      // the prologue's picture book is open (the lower screen stays black)
};

// starFlags[i] (scenario i + 1)
enum {
    THOR_STAR_OWNED = 1 << 0,     // GalaxyStatusAccessor::hasPowerStar
    THOR_STAR_COMET = 1 << 1,     // isValidNormalComet (Prankster Comet mission)
    THOR_STAR_HIDDEN = 1 << 2,    // isHiddenStar
    THOR_STAR_GREEN = 1 << 3,     // MR::isPowerStarGreen
    THOR_STAR_PURPLE = 1 << 4,    // isValidCoin100 (100 purple coins)
    THOR_STAR_GRAND = 1 << 5,     // GameDataConst::isGrandStar
    THOR_STAR_SELECTED = 1 << 6,  // the mission being played / chosen
};

// galaxyFlags[i]
enum {
    THOR_GALAXY_COMPLETED = 1 << 0,  // isCompleted
    THOR_GALAXY_CURRENT = 1 << 1,
};

typedef struct ThorLowerState {
    int32_t version;               // THOR_LOWER_VERSION
    int32_t phase;                 // ThorLowerPhase
    int32_t valid;                 // THOR_VALID_*
    int32_t flags;                 // THOR_FLAG_*
    int32_t generation;            // changes with the stage/scenario (and on every scene change)
    int32_t frame;                 // game frames since boot, as sampled
    int32_t scenarioNo;            // MR::getCurrentScenarioNo (1-based; 0 none)
    int32_t selectedScenarioNo;    // MR::getCurrentSelectedScenarioNo
    int32_t galaxyStars;           // GalaxyStatusAccessor::getPowerStarNum
    int32_t galaxyStarsOwned;      // getPowerStarNumOwned
    int32_t totalStars;            // MR::getPowerStarNum (the save's Power Stars)
    int32_t starBitsRun;           // MR::getStarPieceNum (this run; in the Observatory the bank)
    int32_t starBitsBanked;        // MR::getStockedStarPieceNum
    int32_t coins;                 // MR::getCoinNum
    int32_t purpleCoins;           // MR::getPurpleCoinNum
    int32_t lives;                 // MR::getPlayerLeft
    int32_t galaxyCount;           // entries in the Observatory ledger
    int32_t starFlags[THOR_LOWER_STARS];
    int32_t galaxyOwned[THOR_LOWER_GALAXIES];
    int32_t galaxyTotal[THOR_LOWER_GALAXIES];
    int32_t galaxyFlags[THOR_LOWER_GALAXIES];
    char stage[32];                // internal stage name: diagnostics only
    uint16_t galaxyName[THOR_LOWER_NAME];    // MR::getGalaxyNameOnCurrentLanguage
    uint16_t scenarioName[THOR_LOWER_NAME];  // the selected mission's name
    uint16_t starNames[THOR_LOWER_STARS][THOR_LOWER_NAME];  // only owned or selected missions
    uint16_t galaxyNames[THOR_LOWER_GALAXIES][THOR_LOWER_SHORT];  // short names
    float marioPos[3];             // MR::getPlayerPos, world units (version 2: the lower screen's map)
    float marioUp[3];              // MR::getPlayerUpVec
} ThorLowerState;

// Byte offsets the Java reader uses (GalaxyLowerScreen.java); checked at compile time.
#define THOR_LOWER_OFF_STAR_FLAGS 68
#define THOR_LOWER_OFF_GALAXY_OWNED (THOR_LOWER_OFF_STAR_FLAGS + 4 * THOR_LOWER_STARS)
#define THOR_LOWER_OFF_GALAXY_TOTAL (THOR_LOWER_OFF_GALAXY_OWNED + 4 * THOR_LOWER_GALAXIES)
#define THOR_LOWER_OFF_GALAXY_FLAGS (THOR_LOWER_OFF_GALAXY_TOTAL + 4 * THOR_LOWER_GALAXIES)
#define THOR_LOWER_OFF_STAGE (THOR_LOWER_OFF_GALAXY_FLAGS + 4 * THOR_LOWER_GALAXIES)
#define THOR_LOWER_OFF_GALAXY_NAME (THOR_LOWER_OFF_STAGE + 32)
#define THOR_LOWER_OFF_SCENARIO_NAME (THOR_LOWER_OFF_GALAXY_NAME + 2 * THOR_LOWER_NAME)
#define THOR_LOWER_OFF_STAR_NAMES (THOR_LOWER_OFF_SCENARIO_NAME + 2 * THOR_LOWER_NAME)
#define THOR_LOWER_OFF_GALAXY_NAMES (THOR_LOWER_OFF_STAR_NAMES + 2 * THOR_LOWER_STARS * THOR_LOWER_NAME)
#define THOR_LOWER_OFF_MARIO (THOR_LOWER_OFF_GALAXY_NAMES + 2 * THOR_LOWER_GALAXIES * THOR_LOWER_SHORT)
#define THOR_LOWER_SIZE (THOR_LOWER_OFF_MARIO + 24)

// Published by the game thread (platform side; absent on non-Thor builds).
void port_thor_lower_commit(const ThorLowerState* state);
// Whether the lower screen is up and polling (platform/src/gx/vk/vk_thor_capture.cpp). While it is,
// coins and Star Bits are shown there, and the upper HUD leaves them out (Sunshine's split).
int port_thor_lower_visible(void);

#ifdef __cplusplus
}
static_assert(sizeof(ThorLowerState) == THOR_LOWER_SIZE, "ThorLowerState layout");
static_assert(__builtin_offsetof(ThorLowerState, starFlags) == THOR_LOWER_OFF_STAR_FLAGS, "starFlags offset");
static_assert(__builtin_offsetof(ThorLowerState, stage) == THOR_LOWER_OFF_STAGE, "stage offset");
static_assert(__builtin_offsetof(ThorLowerState, galaxyNames) == THOR_LOWER_OFF_GALAXY_NAMES, "galaxyNames offset");
static_assert(__builtin_offsetof(ThorLowerState, marioPos) == THOR_LOWER_OFF_MARIO, "marioPos offset");
#endif
