// What this game's own memory says about the race, for the headset hooks (vr_mkdd.cpp).
// See docs/dev/vr.md for how each value was found.
#pragma once
#include <cstdint>

namespace mkdd {

// RaceDirector's phase. Only the values seen so far are named.
enum RacePhase : int32_t {
    kCourseDemo = 1,  // the course flyover before a race
    kSafety = 16,
    kRunning = 17,    // from the camera's drop onto the grid, through the countdown, to the finish
    kWait = 18,
    kEnding = 19,     // after the finish: the winning run, the results
};

// RaceInfo's mode.
enum RaceMode : int32_t {
    kTimeAttack = 1, kGrandPrix = 2, kVersus = 3, kBalloonBattle = 4, kRobberyBattle = 5,
    kBombBattle = 6, kEscapeBattle = 7, kAwardDemo = 8, kStaffRoll = 9,
};

struct Race {
    bool exists = false;   // a race scene is up (RaceMgr::sRaceManager is set)
    int32_t phase = 0;
    int32_t run_state = 0; // within kRunning: 0 the grid, 1 the countdown, 2 racing
    int32_t mode = 0;
    int players = 0;       // human players
    int screens = 0;       // the split-screen count
    bool demo = false;     // the title's attract-mode race
    bool paused = false;   // the pause menu
};

Race race_read();

// How far the first player's camera looks down, in radians: atan2 of the world's up in the
// camera's frame (KartCam's view matrix). False when there is no camera.
bool camera_pitch(float* out);

// The first player's kart in their camera's view space: the kart body's world matrix
// (KartBody+0x110; its columns are the kart's left, up and forward, the fourth its position)
// through the camera's view matrix. GX 3x4, by rows. False when there is no camera.
bool kart_in_view(float out[3][4]);

// The first player's kart: the body their camera follows. `m` is its world matrix
// (KartBody+0x110, a GX 3x4 by rows; its columns are the kart's left, up and forward, the
// fourth its position, in game units -- 100 to the metre). `speed` is KartBody::mSpeed, in
// units a frame, which the game turns into km/h by times 2.16 (KartCtrl::GetCarSpeed).
struct KartPose {
    float m[3][4];
    float speed;
    uint32_t status;     // mCarStatus's low word: see the kStatus bits
    float rpm;           // mCarRPM: 300 at rest to 16300; the game reads 5000 as flat out
    int wheels_down;     // mTouchNum: wheels on the ground
    // Each wheel's suspension (KartSus+0xB8, the wheel's offset along its arm in the display
    // code), front right, front left, rear right, rear left; NaN for a wheel not found.
    float sus[4];
    uint32_t ground;     // the course attribute under the body (CrsGround), 0xFF for none
    uint32_t material;   //   and its material
};
constexpr uint32_t kStatusDrift = 0x3;          // a power slide (KartCtrl::GetDriftCnt)
constexpr uint32_t kStatusAirborne = 0x40;
constexpr uint32_t kStatusSpin = 0x80000;       // KartCrash::MakeSpin
constexpr uint32_t kStatusCrash = 0x100000;     // any crash: a spin, a shell, a bomb
constexpr uint32_t kStatusHalfSpin = 0x1000000; // KartCrash::MakeHalfSpin
bool kart_pose(KartPose* out);

// A race the player drives on one screen, from the countdown to the finish, and not
// paused. Not the opening shot before it (run state 0: the game's camera on Lakitu and his
// signal, then dropping onto the grid): the game draws only what that camera sees, so in
// stereo a viewer looking elsewhere saw karts without their wheels. It is the game's own
// cinematic, and is watched on the panel.
inline bool race_live(const Race& r) {
    return r.exists && !r.demo && r.players >= 1 && r.screens == 1 && r.mode >= kTimeAttack &&
           r.mode <= kEscapeBattle && r.phase == kRunning && r.run_state != 0 && !r.paused;
}

}  // namespace mkdd
