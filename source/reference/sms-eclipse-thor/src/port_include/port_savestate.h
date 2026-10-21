#ifndef PORT_SAVESTATE_H
#define PORT_SAVESTATE_H

// Save states (platform/savestate): a snapshot of the game at one moment,
// written to <save folder>/../savestates/*.smsstate and loaded back in place.
//
// A state holds two restores. The exact one is the game's whole memory (MEM1,
// ARAM and the game's own globals, platform/savestate/savestate.cpp), which
// only the same build of the game can take back. The portable one (the area,
// Mario and the progress flags, PortSaveStatePortable) works in any build: it
// warps to the area and puts Mario where he was. A state from another build
// loads that way.
//
// Who calls what: the controls (platform/pad: SAVE_STATE, LOAD_STATE) and the
// state menu (platform/gx) ask; the game takes the request at the top of its
// frame loop (patch zzz-savestate-01) and carries it out there, between frames.

#include <stddef.h>
#include <stdint.h>

#define PORT_SAVESTATE_NAME_LEN 64
#define PORT_SAVESTATE_PATH_LEN 512

#ifdef __cplusplus
extern "C" {
#endif

enum {
	PORT_SAVESTATE_HOTKEY_SAVE,        // SAVE_STATE pressed
	PORT_SAVESTATE_HOTKEY_LOAD_NEWEST, // LOAD_STATE pressed
};
void port_savestate_hotkey(int action);

// Where Mario and the game were; all that a state from another build can use.
// Little-endian on disk, field for field (savestate.cpp). Flag banks as
// TFlagManager numbers them (FlagManager.hpp): 1 card bools, 2 card ints,
// 3 game bools, 4 game ints.
#define PORT_SAVESTATE_CARD_BOOLS 0x3B4
#define PORT_SAVESTATE_CARD_INTS 0x15
#define PORT_SAVESTATE_GAME_BOOLS 0x1D
#define PORT_SAVESTATE_GAME_INTS 0x5
typedef struct PortSaveStatePortable {
	int32_t area, episode; // TGameSequence (gpApplication.mCurrArea)
	float pos[3], vel[3], forwardVel;
	int16_t faceAngle[3];
	uint32_t status, statusArg; // TMario::mStatus and the argument it was entered with
	int16_t health;
	int32_t water;                // FLUDD's tank
	uint8_t nozzle, secondNozzle; // TWaterGun::mCurrentNozzle, mSecondNozzle
	uint8_t hasFludd;
	uint8_t onYoshi;
	float cameraPos[3], cameraAt[3];
	uint8_t cardBools[PORT_SAVESTATE_CARD_BOOLS];
	int32_t cardInts[PORT_SAVESTATE_CARD_INTS];
	uint8_t gameBools[PORT_SAVESTATE_GAME_BOOLS];
	int32_t gameInts[PORT_SAVESTATE_GAME_INTS];
	char stageName[PORT_SAVESTATE_NAME_LEN];   // as the pause menu shows them
	char episodeName[PORT_SAVESTATE_NAME_LEN];
} PortSaveStatePortable;

// A state in the folder, for the menu.
typedef struct PortSaveStateInfo {
	char path[PORT_SAVESTATE_PATH_LEN];
	char stageName[PORT_SAVESTATE_NAME_LEN];
	char episodeName[PORT_SAVESTATE_NAME_LEN];
	int64_t created;   // seconds since 1970
	int exact;         // this build can load it exactly
	int sameGame;      // the same game (Sunshine, or the same mod)
	int hasCounts;     // the counts below are known (from its portable part)
	int shines, blueCoins, coins, lives;
	char when[32];     // when it was saved, as the menu shows it: "Today 9:13 AM"
	int area;          // its area (TGameSequence's stage), -1 when unknown
} PortSaveStateInfo;

// The state menu in the game (src/savestate_menu.cpp) reads the folder
// through these: a scan, newest first, then each state by its index.
int port_savestate_scan(void);
const PortSaveStateInfo* port_savestate_scanned(int index);
// The picture of scanned state `index` as a GX RGBA8 texture (its width and
// height multiples of four) in buffer `slot` (0 to 3, below 4 GiB), kept
// until another picture takes the slot. Null when it has none.
#define PORT_SAVESTATE_PICTURE_SLOTS 4
const void* port_savestate_picture(int index, int slot, int* w, int* h);
// A world's name as the game writes it (world as SMS_getShineStage numbers
// them, 0 the Airstrip to 9 Corona Mountain), from the disc: the episode
// select's colourful banner (banner 1), or the file select's white lettering
// (banner 0), each falling back to the other where a world has only one. A
// GX RGBA8 texture below 4 GiB, *w x *h (multiples of four, the lettering's
// own width in *artW); null when the disc has neither.
const void* port_savestate_world_art(int world, int banner, int* w, int* h, int* artW);

// The game's side, once a frame, between frames.
enum {
	PORT_SAVESTATE_NONE,
	PORT_SAVESTATE_SAVE,
	PORT_SAVESTATE_LOAD,
};
// The request waiting, if any; *path is the state to load.
int port_savestate_take(char* path, int pathLen);
// Whether a state can be saved or loaded right now (the game has to be in a
// stage with nothing loading); the menu and the controls say so when not.
void port_savestate_set_ready(int ready);
// Saves the game as it is now; the portable part from the caller. 1 if written.
int port_savestate_save(const PortSaveStatePortable* portable);
// Whether `path` would load exactly now (reads only its small chunks): the
// game fades out first when it will.
int port_savestate_can_load_exact(const char* path);
// Loads `path` exactly, if this build can: returns 1 and the game is then as
// it was when the state was saved. Otherwise 0, and *portable (if not null)
// holds the state's portable part for the game to warp with (when it has one).
int port_savestate_load_exact(const char* path, PortSaveStatePortable* portable, int* hasPortable);
// A one-line message on screen for a few seconds ("State saved").
void port_savestate_notify(const char* text);

// The window's side (platform/gx): the current message ("State saved") and
// how long it has left, in seconds (0: none).
float port_savestate_message(char* out, int outLen);
// The GX layer reaches it through this table, which platform/savestate hands
// it at start: the GX tests link that library without the game.
typedef struct PortSaveStateOverlayApi {
	float (*message)(char* out, int outLen);
} PortSaveStateOverlayApi;
void GXPC_SetSaveStateApi(const PortSaveStateOverlayApi* api);
// A picture of the last frame shown, as a PNG of *size bytes; free() it.
uint8_t* GXPC_SaveStateThumbnail(size_t* size);

// For the state menu (src/savestate_menu.cpp) and the hotkeys.
int port_savestate_ready(void);
int port_savestate_list(PortSaveStateInfo* out, int max); // newest first
// The thumbnail of a state, a PNG of *size bytes; free() it. Null when it has none.
uint8_t* port_savestate_thumbnail_png(const char* path, size_t* size);
void port_savestate_request_load(const char* path);
int port_savestate_delete(const char* path);

// The pause menu's hint (patch zzzz-savestate-02): SAVE_STATE's binding as the
// device in use shows it (the keyboard's keys, or a controller's buttons named
// for its kind: L1 + R1, LB + RB). Each part has a GX RGBA8 64x64 image when
// the launcher drew the prompt icons (drawn through the system font as
// character PORT_PROMPT_SAVESTATE + part), and a label to write otherwise.
// count 0: not bound.
#define PORT_PROMPT_SAVESTATE 0x10000
// A plus to join the two (the system font's own "+" is the X button glyph).
#define PORT_PROMPT_PLUS 0x10002
typedef struct PortSaveStateHint {
	int count;
	int hasImage[2];
	char label[2][16];
} PortSaveStateHint;
void port_savestate_hint(PortSaveStateHint* out);
// platform/pad: SAVE_STATE's first binding as `style` names it (0 keyboard,
// 1 Xbox, 2 PlayStation, 3 Steam Deck); the number of parts.
int port_pad_savestate_labels(int style, char labels[2][16]);

#ifdef __cplusplus
}
#endif

#endif
