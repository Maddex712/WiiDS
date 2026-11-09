// Motion telemetry, for a motion platform or anything else that moves the player with the
// game: once a game frame the game project describes how its vehicle is moving
// (telemetry::publish), and this sends it wherever the user pointed it.
//
// Off unless configured: `--telemetry` on the desktop, or GCN_TELEMETRY in the environment,
// with an optional `host:port` (default 127.0.0.1:33001). What is sent is SimRacingStudio's
// custom-game "API" packet, version 102, over UDP -- see telemetry.cpp for its layout. A
// `telemetry.txt` in the working directory, `key value` per line like vr.txt, scales or
// inverts each channel (`pitch_gain -1`) and holds a game's own keys (telemetry::get).
#pragma once

namespace telemetry {

// How the vehicle is moving, in the vehicle's own frame and in SI units. Angles are degrees.
//
// Nothing a game sends can jerk the platform: what goes out eases in and out of rest and fades
// across anything marked a discontinuity -- see publish() in telemetry.cpp. A motion platform
// moves a person, and a step in the data is a blow to them, however brief.
struct Motion {
    bool active = false;   // false in menus, between races and paused: the platform settles to rest
    // The motion jumped -- a respawn, the vehicle lost and found again: fade across to it
    // rather than following.
    bool discontinuity = false;
    float speed_kmh = 0;
    float pitch = 0;       // nose up is positive
    float roll = 0;        // right side down is positive
    // The angle the platform itself should be turned to, right positive: never the vehicle's
    // heading. A platform turns a couple of dozen degrees at most, so yaw is always relative
    // -- the nose against the way the vehicle is moving in a slide, a spin -- and the game
    // decides when there is any. Sent as it is, eased to rest and faded like the rest.
    float yaw = 0;
    float surge = 0;       // acceleration forward, m/s^2
    float sway = 0;        // acceleration to the right, m/s^2
    float heave = 0;       // acceleration up, m/s^2, without gravity: 0 at rest
    float sway_velocity = 0;  // m/s to the right: sliding sideways
    // The engine, for a shaker's engine effect: revolutions a minute, the most it reaches, and
    // the gear. Sent as they are while active; the revs wind down to zero and back up with the
    // ease to rest, rather than cutting out.
    float rpm = 0;
    float max_rpm = 0;
    int gear = 1;
    // How rough the ground under the vehicle is, 0 for none to 1 for the roughest: the module
    // makes up a road texture for heave, pitch and roll that grows with this and with speed
    // (see publish() in telemetry.cpp). 0 adds nothing. For a vehicle without suspension to
    // read; one with it is better served by `suspension`.
    float roughness = 0;
    // Each wheel's suspension compression in metres, front left, front right, rear left, rear
    // right, when the game has it (has_suspension): sent in the packet, and its movement --
    // the bumps the wheels ride over, a front pair then a rear pair, a landing -- is the road
    // texture, on heave, pitch and roll.
    bool has_suspension = false;
    float suspension[4] = {};
};

// "host:port", "" or null for the default. Reads telemetry.txt. Returns false if the
// destination does not parse or the socket cannot be made.
bool configure(const char* spec);
// Whether anything takes the game's Motion: the packets, or a listener.
bool enabled();
void publish(const Motion& m);
// Another taker of every Motion published, as the game sent it, before the fades: the
// wheel's force feedback (ffb.h). Called on the game's thread. Null for none.
void set_listener(void (*fn)(const Motion&));

// A key from telemetry.txt that this module does not know, for the game's own use.
float get(const char* key, float fallback);

// The game's own default for a key, the module's or its own: telemetry.txt still overrides
// it. For a game whose vehicle wants other gains or limits than the module's defaults.
void set_default(const char* key, float value);

}  // namespace telemetry
