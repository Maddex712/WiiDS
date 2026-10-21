#pragma once

#include <string>

#include <cstdint>
#include <vector>

// Where New Super Mario Bros. Wii's own setup menus are (the GAME_SETUP scene's file select and
// "Select Players" screens), read from its objects. The object tree and state IDs sit at fixed
// addresses in the game's code and its pre-linked RELs.
namespace GameMenus {

enum class SelectPlayers {
    Hidden,     // not on screen
    Appearing,  // sliding in
    Choosing,   // story mode: the player picks how many play (StartMemberSelect)
    Leaving,    // a number was picked, or Back
    Other,      // the Free-for-All / Coin Battle variant
};
SelectPlayers SelectPlayersState();
// True while "Select Players" waits for input (not animating the cursor).
bool SelectPlayersSettled();
// The highlighted button on "Select Players": 0 = 1 player .. 3 = 4 players.
int SelectPlayersCursor();

// The file select: true while it waits for a pick; the highlighted file (0-2).
bool FileSelectWaiting();
int FileSelectCursor();
// The file select's cursor as the game keeps it (0-2 the files, higher the buttons below), -1 if unknown.
int FileSelectRawCursor();

// Whether an object of this profile exists (0x2BB EVENT_OPENING_TITLE: the title screen is up).
bool HasProfile(uint16_t profile);

// Every object profile in the tree, sorted (for finding which screen is up).
std::vector<uint16_t> Profiles();

// dScene_c::m_nowScene (0x003 world map, 0x005 stage/title, 0x00A game setup, ...).
uint16_t CurrentScene();
// The world map's + menu or a level's pause window is open, waiting for a choice.
bool PauseMenuOpen();
// Which is open: 0 none, 1 the world map's + menu (dCourseSelectMenu_c), 2 a level's pause window.
int PauseMenuKind();
// The world map's + menu object, and its cursor (0 Star Coins, 1 Add/Drop Players, 2 Quick Save,
// 3 Title Screen; dCourseSelectMenu_c +0x268), -1 without one.
uint32_t CourseSelectMenuObject();
int CourseMenuCursor();
// The SELECT_CURSOR object (the menus' corner brackets), 0 without one.
uint32_t SelectCursorObject();
// NSMBW_DUMP_STATES debugging: the StateIDs held by the listed profiles' objects.
std::string DumpStates();

} // namespace GameMenus
