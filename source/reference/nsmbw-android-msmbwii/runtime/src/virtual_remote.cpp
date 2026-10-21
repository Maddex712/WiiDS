#include "virtual_remote.h"

#include "input_bindings.h"
#include "keybinds.h"
#include "runtime_log.h"

#include <dolphin/pad.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace VirtualRemote {
namespace {

// WPAD_BUTTON_* bits as the game reads them from KPADStatus.hold.
constexpr uint32_t kWpadLeft = 0x0001, kWpadRight = 0x0002, kWpadDown = 0x0004, kWpadUp = 0x0008,
                   kWpadPlus = 0x0010, kWpadTwo = 0x0100, kWpadOne = 0x0200, kWpadA = 0x0800,
                   kWpadMinus = 0x1000;

// The remote's D-pad by screen direction. Held sideways with the D-pad under the left thumb, the
// pad's "up" arrow points left, "right" points up, "down" points right and "left" points down.
constexpr uint32_t kScreenLeft = kWpadUp, kScreenRight = kWpadDown, kScreenUp = kWpadRight,
                   kScreenDown = kWpadLeft;

// What the game's motion code looks for (dGameKeyCore_c::setShakeY and the tilt code after
// setConfigKey): a shake is acc.y changing by at least 0.28 g between consecutive frames on four
// frames (gaps of up to two allowed), then a five-frame cool-down; the tilt angle is -acc.z, through
// KPAD's acc_vertical, at 16384 angle units per g. A shake request swings y by +-1.2 g around rest
// for at least kShakeFrames reads, enough for one detection, and holding it keeps shaking.
constexpr float kShakeSwingG = 1.2f;
constexpr int kShakeFrames = 10;
constexpr float kMaxTiltRadians = 0.7853982f;  // 45 degrees
constexpr float kTiltFollow = 0.3f;            // share of the way to the requested tilt per frame
constexpr float kTiltDeadZone = 0.25f;         // of the right stick's travel
constexpr int kStickThreshold = 48;            // of aurora's +-127 stick range
constexpr float kDiagonalSlope = 0.4142136f;   // tan(22.5 degrees): eight-way stick sectors
constexpr int kTriggerThreshold = 128;         // of 255
// aurora's PADRead polls SDL; the game probes and reads all four channels each frame.
constexpr uint64_t kPadRefreshNs = 4'000'000;

struct Intent {
    uint32_t buttons = 0;  // WPAD bits, already in the sideways layout
    bool shake = false;
    float tilt = 0.0f;     // -1 counter-clockwise (left) .. 1 clockwise (right)
};

struct Motion {
    int shakeFramesLeft = 0;
    uint32_t shakePhase = 0;
    float tilt = 0.0f;
};

// Written by the Android UI thread (SetTouchState), read on the guest thread. The *SinceRead
// latches keep a press shorter than one 60 Hz input read (a quick tap) visible for one read.
std::atomic<uint32_t> g_touchButtons{0};
std::atomic<uint32_t> g_touchButtonsSinceRead{0};
std::atomic<bool> g_touchShake{false};
std::atomic<bool> g_touchShakeSinceRead{false};
std::atomic<float> g_touchTilt{0.0f};

std::mutex g_mutex;
std::array<PADStatus, PAD_MAX_CONTROLLERS> g_pads{};
uint64_t g_padsReadNs = 0;
bool g_keyboardOnAPort = false;
std::array<Motion, PAD_MAX_CONTROLLERS> g_motion{};
std::array<bool, PAD_MAX_CONTROLLERS> g_announced{};

void RefreshPadsLocked() {
    const uint64_t now = SDL_GetTicksNS();
    if (g_padsReadNs != 0 && now - g_padsReadNs < kPadRefreshNs) {
        return;
    }
    g_padsReadNs = now;
    PADRead(g_pads.data());
    InputBindings::Apply(g_pads.data());
    g_keyboardOnAPort = false;
    for (uint32_t port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
        uint32_t count = 0;
        if (PADGetKeyButtonBindings(port, &count) != nullptr) {
            g_keyboardOnAPort = true;
        }
    }
}

// The built-in layout for port 1, used while no port has aurora's keyboard switched on.
Intent KeyboardIntent() {
    Intent intent;
    if (SDL_GetKeyboardFocus() == nullptr) {
        return intent;
    }
    // The keys come from the game's Keybinds tab (Config.toml [keyboard]); none count while one
    // is being picked there, or until the key picked is let go.
    if (Keybinds::Suppressed()) {
        return intent;
    }
    using namespace Keybinds;
    if (Held(kLeft)) intent.buttons |= kScreenLeft;
    if (Held(kRight)) intent.buttons |= kScreenRight;
    if (Held(kUp)) intent.buttons |= kScreenUp;
    if (Held(kDown)) intent.buttons |= kScreenDown;
    if (Held(kJump)) intent.buttons |= kWpadTwo;
    if (Held(kRun)) intent.buttons |= kWpadOne;
    if (Held(Keybinds::kA)) intent.buttons |= kWpadA;
    if (Held(kPlus)) intent.buttons |= kWpadPlus;
    if (Held(kMinus)) intent.buttons |= kWpadMinus;
    intent.shake = Held(kShake);
    intent.tilt = (Held(kTiltRight) ? 1.0f : 0.0f) - (Held(kTiltLeft) ? 1.0f : 0.0f);
    return intent;
}

// The on-screen remote and phone motion set through SetTouchState.
Intent TouchIntent() {
    Intent intent;
    const uint32_t touch = g_touchButtons.load(std::memory_order_relaxed) |
                           g_touchButtonsSinceRead.exchange(0, std::memory_order_relaxed);
    if (touch & kTouchLeft) intent.buttons |= kScreenLeft;
    if (touch & kTouchRight) intent.buttons |= kScreenRight;
    if (touch & kTouchUp) intent.buttons |= kScreenUp;
    if (touch & kTouchDown) intent.buttons |= kScreenDown;
    if (touch & kTouchOne) intent.buttons |= kWpadOne;
    if (touch & kTouchTwo) intent.buttons |= kWpadTwo;
    if (touch & kTouchA) intent.buttons |= kWpadA;
    if (touch & kTouchPlus) intent.buttons |= kWpadPlus;
    if (touch & kTouchMinus) intent.buttons |= kWpadMinus;
    intent.shake = g_touchShake.load(std::memory_order_relaxed) ||
                   g_touchShakeSinceRead.exchange(false, std::memory_order_relaxed);
    intent.tilt = std::clamp(g_touchTilt.load(std::memory_order_relaxed), -1.0f, 1.0f);
    return intent;
}

// A GameCube-style pad state (after the user's remapping) as the sideways remote.
Intent PadIntent(const PADStatus& pad) {
    Intent intent;
    const uint32_t b = pad.button;
    if (b & PAD_BUTTON_LEFT) intent.buttons |= kScreenLeft;
    if (b & PAD_BUTTON_RIGHT) intent.buttons |= kScreenRight;
    if (b & PAD_BUTTON_UP) intent.buttons |= kScreenUp;
    if (b & PAD_BUTTON_DOWN) intent.buttons |= kScreenDown;
    const int sx = pad.stickX;
    const int sy = pad.stickY;  // +y up
    if (sx * sx + sy * sy >= kStickThreshold * kStickThreshold) {
        if (std::abs(sx) >= kDiagonalSlope * std::abs(sy)) intent.buttons |= sx < 0 ? kScreenLeft : kScreenRight;
        if (std::abs(sy) >= kDiagonalSlope * std::abs(sx)) intent.buttons |= sy < 0 ? kScreenDown : kScreenUp;
    }
    if (b & PAD_BUTTON_A) intent.buttons |= kWpadTwo;
    if (b & (PAD_BUTTON_B | PAD_BUTTON_X)) intent.buttons |= kWpadOne;
    if (b & PAD_BUTTON_Y) intent.buttons |= kWpadA;
    if (b & PAD_BUTTON_START) intent.buttons |= kWpadPlus;
    if ((pad.extButton & PAD_BUTTON_BACK) != 0 || (b & PAD_TRIGGER_L) != 0 || pad.triggerLeft >= kTriggerThreshold) {
        intent.buttons |= kWpadMinus;
    }
    intent.shake = (b & (PAD_TRIGGER_Z | PAD_TRIGGER_R)) != 0 || pad.triggerRight >= kTriggerThreshold;
    const float x = static_cast<float>(pad.substickX) / 127.0f;
    intent.tilt = std::fabs(x) < kTiltDeadZone ? 0.0f : std::clamp(x, -1.0f, 1.0f);
    return intent;
}

// Accelerometer for the requested motion, in KPAD's frame (rest: y = -1). Tilting clockwise lowers
// the right end and raises the left one, where the remote's pointer end is, so gravity shows up on
// the remote's long axis (KPAD z) as +sin(angle).
void ApplyMotion(Motion& motion, const Intent& intent, WiiRemoteInput::KpadSample& sample) {
    if (intent.shake) {
        motion.shakeFramesLeft = std::max(motion.shakeFramesLeft, kShakeFrames);
    }
    motion.tilt += (intent.tilt - motion.tilt) * kTiltFollow;
    if (std::fabs(motion.tilt) < 0.001f) {
        motion.tilt = 0.0f;
    }
    const float angle = motion.tilt * kMaxTiltRadians;
    sample.acc[0] = 0.0f;
    sample.acc[1] = -std::cos(angle);
    sample.acc[2] = std::sin(angle);
    if (motion.shakeFramesLeft > 0) {
        --motion.shakeFramesLeft;
        sample.acc[1] += (motion.shakePhase++ & 1u) != 0 ? kShakeSwingG : -kShakeSwingG;
    }
}

void AnnounceLocked(uint32_t chan, bool pad) {
    if (g_announced[chan]) {
        return;
    }
    g_announced[chan] = true;
    const char* source = "keyboard";
    if (pad) {
        const s32 index = PADGetIndexForPort(chan);
        SDL_Gamepad* gamepad = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
        const char* name = gamepad != nullptr ? SDL_GetGamepadName(gamepad) : nullptr;
        source = name != nullptr ? name : g_keyboardOnAPort ? "keyboard (settings bindings)" : "controller";
    }
    RT_LOGF(RT_TAG_RUNTIME, "player %u: %s as a sideways Wii Remote\n", chan + 1, source);
}

} // namespace

void SetTouchState(uint32_t touchButtons, bool shake, float tilt) {
    g_touchButtons.store(touchButtons, std::memory_order_relaxed);
    g_touchButtonsSinceRead.fetch_or(touchButtons, std::memory_order_relaxed);
    g_touchShake.store(shake, std::memory_order_relaxed);
    if (shake) {
        g_touchShakeSinceRead.store(true, std::memory_order_relaxed);
    }
    g_touchTilt.store(std::isfinite(tilt) ? tilt : 0.0f, std::memory_order_relaxed);
}

bool Present(uint32_t chan) {
    if (chan >= PAD_MAX_CONTROLLERS) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    RefreshPadsLocked();
    return chan == 0 || g_pads[chan].err == PAD_ERR_NONE;
}

bool Sample(uint32_t chan, WiiRemoteInput::KpadSample& sample) {
    if (chan >= PAD_MAX_CONTROLLERS) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    RefreshPadsLocked();
    const bool pad = g_pads[chan].err == PAD_ERR_NONE;
    if (chan != 0 && !pad) {
        return false;
    }
    AnnounceLocked(chan, pad);
    Intent intent;
    if (pad) {
        intent = PadIntent(g_pads[chan]);
    }
    const auto merge = [&intent](const Intent& other) {
        intent.buttons |= other.buttons;
        intent.shake = intent.shake || other.shake;
        if (other.tilt != 0.0f) {
            intent.tilt = other.tilt;
        }
    };
    if (chan == 0 && !g_keyboardOnAPort) {
        merge(KeyboardIntent());
    }
    if (chan == 0) {
        merge(TouchIntent());
    }
    if (InputBindings::InputBlocked()) {
        intent = {};  // the settings overlay owns input; the remote stays connected at rest
    }
    sample = {};
    sample.hold = intent.buttons;
    ApplyMotion(g_motion[chan], intent, sample);
    return true;
}

} // namespace VirtualRemote
