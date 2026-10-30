#pragma once

// Brawl's online play menu: a full-screen panel (F9, or the F10 bar's Online) to set a name, host a room (on this
// network, or listed on the online server for anyone to join), find rooms (the online server's list, this network,
// Tailscale) or join one by its room code or address, then everyone restarts into the lockstep session
// (netplay_lobby.h, netplay_start.h). During a session it shows the players, their ping and a way out.
// The launcher's Online page starts the game with BRAWL_ONLINE set ("host:<players>", "host-public:<players>",
// "browse" or "join:<code or address>"), which opens the panel on that step.
namespace OnlineMenu {

// Once per frame inside the ImGui frame (settings_overlay::Draw): the launcher's request, then the panel if open.
void Draw();
bool IsOpen();
void SetOpen(bool open);

} // namespace OnlineMenu
