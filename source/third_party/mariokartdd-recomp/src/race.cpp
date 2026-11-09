// Reading the race out of the game's memory. See race.h, and docs/dev/vr.md for how each
// address was found.
#include "race.h"

#include <cmath>
#include <cstring>

#include "recomp.h"
#include "runtime.h"

namespace mkdd {
namespace {

constexpr uint32_t kRaceManager = 0x803CB7E8;  // RaceMgr::sRaceManager
constexpr uint32_t kPaused = 0x803CB6DC;       // PauseManager::mIsPause
constexpr uint32_t kKartCtrl = 0x803CC588;     // the kart controller: the karts' bodies, then
constexpr uint32_t kCameras = 0x200;           //   the players' cameras (KartCam*)
// KartCam
constexpr uint32_t kTargetBody = 0x00;         // the KartBody it follows
constexpr uint32_t kViewMatrix = 0x80;         // its view matrix, a GX 3x4 by rows
// KartBody
constexpr uint32_t kBodyMatrix = 0x110;        // its world matrix, a GX 3x4 by rows
constexpr uint32_t kBodySpeed = 0x444;         // mSpeed: units a frame
constexpr uint32_t kBodyGround = 0x2C;         // mBodyGround, a CrsGround: its attribute at
constexpr uint32_t kGroundAttr = 0x4C;         //   +0x4C and its material at +0x54
constexpr uint32_t kGroundMaterial = 0x54;
constexpr uint32_t kBodyRpm = 0x44C;           // mCarRPM
constexpr uint32_t kBodyStatus = 0x574;        // mCarStatus, a u64 at 0x570: its low word
constexpr uint32_t kBodyTouch = 0x5A4;         // mTouchNum
constexpr uint32_t kBodySus = 0x04;            // mKartSus[4]: FR, FL, RR, RL
constexpr uint32_t kSusTravel = 0xB8;

// RaceMgr
constexpr uint32_t kDirector = 0x18;
constexpr uint32_t kInfo = 0x38;
// RaceDirector
constexpr uint32_t kPhase = 0x0C;
constexpr uint32_t kRunState = 0x30;
// RaceInfo
constexpr uint32_t kMode = 0x08;
constexpr uint32_t kPlayers = 0x1E;
constexpr uint32_t kScreens = 0x20;
constexpr uint32_t kDemoType = 0x28;

bool in_ram(uint32_t a, uint32_t size) { return a >= 0x80000000u && a + size <= 0x80000000u + RAM_SIZE && !(a & 3); }

}  // namespace

Race race_read() {
    Race r;
    const uint32_t mgr = mem_r32(kRaceManager);
    if (!in_ram(mgr, 0x100)) return r;
    const uint32_t dir = mem_r32(mgr + kDirector), info = mem_r32(mgr + kInfo);
    if (!in_ram(dir, 0x48) || !in_ram(info, 0x2A0)) return r;
    r.exists = true;
    r.phase = (int32_t)mem_r32(dir + kPhase);
    r.run_state = (int32_t)mem_r32(dir + kRunState);
    r.mode = (int32_t)mem_r32(info + kMode);
    r.players = (int16_t)mem_r16(info + kPlayers);
    r.screens = (int16_t)mem_r16(info + kScreens);
    r.demo = mem_r32(info + kDemoType) != 0;
    r.paused = mem_r8(kPaused) != 0;
    return r;
}

static float rd_f(uint32_t a) {
    const uint32_t u = mem_r32(a);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint32_t player_camera() {
    const uint32_t ctrl = mem_r32(kKartCtrl);
    if (!in_ram(ctrl, kCameras + 4)) return 0;
    const uint32_t cam = mem_r32(ctrl + kCameras);
    return in_ram(cam, 0x294) ? cam : 0;
}

bool kart_in_view(float out[3][4]) {
    const uint32_t cam = player_camera();
    if (!cam) return false;
    const uint32_t body = mem_r32(cam + kTargetBody);
    if (!in_ram(body, 0x300)) return false;
    float v[3][4], k[3][4];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++) {
            v[i][j] = rd_f(cam + kViewMatrix + (i * 4 + j) * 4);
            k[i][j] = rd_f(body + kBodyMatrix + (i * 4 + j) * 4);
            if (!std::isfinite(v[i][j]) || !std::isfinite(k[i][j])) return false;
        }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            out[i][j] = v[i][0] * k[0][j] + v[i][1] * k[1][j] + v[i][2] * k[2][j] + (j == 3 ? v[i][3] : 0.0f);
    return true;
}

bool kart_pose(KartPose* out) {
    const uint32_t cam = player_camera();
    if (!cam) return false;
    const uint32_t body = mem_r32(cam + kTargetBody);
    if (!in_ram(body, kBodyTouch + 4)) return false;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++) {
            out->m[i][j] = rd_f(body + kBodyMatrix + (i * 4 + j) * 4);
            if (!std::isfinite(out->m[i][j])) return false;
        }
    out->speed = rd_f(body + kBodySpeed);
    out->rpm = rd_f(body + kBodyRpm);
    out->status = mem_r32(body + kBodyStatus);
    out->wheels_down = (int32_t)mem_r32(body + kBodyTouch);
    for (int w = 0; w < 4; w++) {
        const uint32_t sus = mem_r32(body + kBodySus + w * 4);
        out->sus[w] = in_ram(sus, 0x2CC) ? rd_f(sus + kSusTravel) : NAN;
    }
    out->ground = mem_r32(body + kBodyGround + kGroundAttr);
    out->material = mem_r32(body + kBodyGround + kGroundMaterial);
    return std::isfinite(out->speed) && std::isfinite(out->rpm);
}

bool camera_pitch(float* out) {
    const uint32_t cam = player_camera();
    if (!cam) return false;
    // The world's up is the rotation's second column: (m[0][1], m[1][1], m[2][1]).
    const float y = rd_f(cam + kViewMatrix + 0x14), z = rd_f(cam + kViewMatrix + 0x24);
    if (!std::isfinite(y) || !std::isfinite(z) || y * y + z * z < 0.5f) return false;
    *out = atan2f(z, y);
    return true;
}

}  // namespace mkdd
