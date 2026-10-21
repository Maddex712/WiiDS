#ifndef PORT_WARP_H
#define PORT_WARP_H

// The warp menu (F8 or a controller's Back/Select button with the debug overlay
// open, platform/gx): a list of every stage and episode on the disc, and a warp to the one picked. The
// game fills the list when the menu first asks for it and starts the warp
// (patch zzz-warp-01); platform/misc/port_warp.cpp keeps both between them.

#define PORT_WARP_WORLD_LEN 40
#define PORT_WARP_NAME_LEN 64
#define PORT_WARP_SCENE_LEN 24

typedef struct PortWarpEntry {
	char world[PORT_WARP_WORLD_LEN]; // the stage's name; entries of one world are adjacent
	char name[PORT_WARP_NAME_LEN];   // the episode's
	char scene[PORT_WARP_SCENE_LEN]; // its scene file, without the extension
	int area;                        // TGameSequence's stage
	int episode;                     // and scenario
} PortWarpEntry;

#ifdef __cplusplus
extern "C" {
#endif

// The game's side, once a frame.
int port_warp_list_wanted(void); // the menu is waiting for the list
void port_warp_list_begin(void);
void port_warp_list_add(const char* world, const char* name, const char* scene, int area, int episode);
void port_warp_list_end(void);
void port_warp_set_ready(int ready);       // a warp can start now (in a stage)
int port_warp_take(int* area, int* episode); // the warp picked, once

// The menu's side.
void port_warp_want_list(void);
int port_warp_count(void);
int port_warp_entry(int index, PortWarpEntry* out);
int port_warp_ready(void);
void port_warp_request(int area, int episode);

// Code mods that name episodes their own way (BetterSunshineEngine) set this:
// the scenario name's index in scenarioname.bmg, or -1.
extern int (*port_warp_scenario_name_id)(int area, int episode);

#ifdef __cplusplus
}
#endif

#endif
