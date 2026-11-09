#pragma once

#include <cstdint>
#include <string>

// Network play, the lockstep part. Every device runs the game in deterministic mode (det_clock.h)
// from power-on and the devices exchange nothing but controller input: each frame's input for every
// player is latched at that frame's VI retrace (frame_input.h), and a device waits there until the
// other players' input for the frame has arrived. Local input is sent `inputDelay` frames ahead, so
// on a LAN the wait is normally zero.
//
// Star topology: clients talk only to the host (player 1), which relays every player's input to
// everyone, so a phone that reaches the PC over Tailscale but not the other phones still plays.
// Packets carry the unacknowledged input history, so a lost datagram costs nothing but a resend.
// Each frame every device also fingerprints one 1/64th slice of guest memory; the host compares
// the slices and reports the first frame two devices disagree on.
namespace NetplaySession {

struct Config {
    uint64_t sessionId = 0;
    uint8_t localSlot = 0;     // player number - 1; slot 0 hosts
    uint8_t playerCount = 2;   // slots 0 .. playerCount-1 play; the other channels stay unplugged
    uint32_t inputDelay = 3;   // frames between sampling a controller and that input taking effect
    uint16_t port = 0;         // local UDP port; 0 = the default port (any free one for clients)
    std::string host;          // clients: the host's "ip[:port]" or name
    // After a restart from a room: drive player 1 from the title through the file select and the
    // player count (every device does the same, from the game's own state).
    bool autopilot = false;
    int saveFile = 0;
};

// NSMBW_NETPLAY="id=<n>;slot=<n>;players=<n>;delay=<n>;port=<n>;host=<ip:port>" (testing), or
// debug.nsmbw.net on Android. False when no session is configured.
bool LoadConfigFromEnvironment(Config& out);

// Turns deterministic mode on and makes the session the source of every frame's input. Must run
// before the guest starts.
bool Start(const Config& config);
bool Active();

struct Status {
    bool active = false;
    uint8_t localSlot = 0;
    uint8_t playerCount = 0;
    uint32_t frame = 0;
    int waitingForSlot = -1;   // slot whose input the game is waiting for, -1 when not waiting
    uint32_t waitedMs = 0;     // how long the current wait has lasted
    uint32_t desyncFrame = 0;  // first frame two devices disagreed on, 0 = none
    int lostSlot = -1;         // a player whose device stopped answering, -1 = none
    uint32_t pingMs[4] = {};   // round trip to each slot (host <-> client)
};
Status GetStatus();

// Called on the game thread every ~50 ms while a frame waits for another player, so the host can
// keep its window alive and show what the game is waiting for.
using StallHandler = void (*)();
void SetStallHandler(StallHandler handler);

} // namespace NetplaySession
