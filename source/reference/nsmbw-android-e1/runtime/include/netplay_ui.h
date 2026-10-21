#pragma once

#include "wii_remote_input.h"

#include <cstdint>

// The network-play menus, drawn with the game's own window and font graphics (netplay/game_layout.h):
//   - a Couch / LAN button at the top right of the file select (Couch by default);
//   - for LAN: "Join" or "Create Room"; Create Room goes on to the game's own player-count screen
//     and then the room, where the host starts or cancels it; Join lists the rooms found on the LAN
//     and over Tailscale;
//   - starting a room restarts every device into the network session.
namespace NetplayUi {

// Once per presented frame, inside the ImGui frame (settings_overlay::Draw).
void Draw();

// Every Wii Remote sample the game reads in normal play passes through here: while a menu is up
// the game sees the remote at rest (and the menu takes the buttons), and in a room's player-count
// step the confirm press is taken from the game.
void FilterGameInput(uint32_t chan, WiiRemoteInput::KpadSample& sample);

// Network session stalls (NetplaySession::SetStallHandler): the game is frozen waiting for another
// player, so keep the window responsive and present frames showing who it waits for.
void OnSessionStall();

// Touch and mouse on the menus: a tap at (x, y), as fractions of the game's window (any thread).
// On the game's own menu screens it becomes the remote presses that move the cursor there and pick
// it; on these menus it presses the button under it. Mouse clicks on desktop arrive the same way.
void Tap(float x, float y);
// True while a screen that taps can drive is up (the strap and title screens, the file select,
// "Select Players", these menus): the Android app hides its on-screen remote then.
bool TapScreenUp();
// The settings window changed the render scale, the aspect or the FPS counter since the last call:
// the Android app copies them into its own settings, which it writes to Config.toml at every start.
bool TakeSettingsChange(float& renderScale, bool& showFps, int& aspect, bool& spinningCoins, bool& frameGraph);

} // namespace NetplayUi
