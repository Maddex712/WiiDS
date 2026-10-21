// Switch controller input. SDL3 has no Switch joystick backend, so the console's controllers are
// exposed to the rest of aurora as SDL virtual gamepads, one per player: everything downstream
// (mapping, port assignment, events) sees ordinary SDL_Gamepads.
//
// Player 1 is the console's player-1 slot plus the attached Joy-Cons (handheld), and is always
// present. Players 2-4 are the console's slots 2-4 and appear and disappear as controllers
// connect, in the order the console itself numbers them (the player lights on each controller).
#include "input_switch.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <switch.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace aurora::input::switch_pad {
namespace {

constexpr size_t kPlayers = 4;
constexpr size_t kSentAxisCount = 6;

constexpr uint16_t kButtonCount = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
constexpr uint16_t kAxisCount = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER + 1;

struct ButtonMap {
  u64 hid;
  SDL_GamepadButton sdl;
};

// SDL names buttons by position; the Switch's A is the east face button, B the south one.
constexpr ButtonMap kButtons[] = {
    // By label, not position: aurora binds the game's A/B/X/Y to SOUTH/EAST/WEST/NORTH (Xbox
    // layout), so a positional mapping would swap A<->B and X<->Y on Nintendo controllers.
    {HidNpadButton_A, SDL_GAMEPAD_BUTTON_SOUTH},
    {HidNpadButton_B, SDL_GAMEPAD_BUTTON_EAST},
    {HidNpadButton_X, SDL_GAMEPAD_BUTTON_WEST},
    {HidNpadButton_Y, SDL_GAMEPAD_BUTTON_NORTH},
    {HidNpadButton_Minus, SDL_GAMEPAD_BUTTON_BACK},
    {HidNpadButton_Plus, SDL_GAMEPAD_BUTTON_START},
    {HidNpadButton_StickL, SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {HidNpadButton_StickR, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {HidNpadButton_L, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {HidNpadButton_R, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {HidNpadButton_Up, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {HidNpadButton_Down, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {HidNpadButton_Left, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {HidNpadButton_Right, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
};

Sint16 clamp_axis(s32 value) {
  if (value > SDL_JOYSTICK_AXIS_MAX) {
    return SDL_JOYSTICK_AXIS_MAX;
  }
  if (value < SDL_JOYSTICK_AXIS_MIN) {
    return SDL_JOYSTICK_AXIS_MIN;
  }
  return static_cast<Sint16>(value);
}

// SDL's virtual joystick keeps the last value it was given, and every SDL_SetJoystickVirtual*
// call looks the joystick up under SDL's object rwlock. Sending all 21 inputs on every update was
// ~0.9% of a race frame in lock traffic, so only send the ones that changed.
struct Player {
  PadState pad{};
  SDL_Joystick* joystick = nullptr;
  SDL_JoystickID joystickId = 0;
  bool sentButtons[std::size(kButtons)]{};
  Sint16 sentAxes[kSentAxisCount]{};
  bool sentAny = false;
};

Player g_players[kPlayers];
bool g_attached = false;
// Players 2-4 are looked for a few times a second, not on every poll.
uint32_t g_updateCount = 0;

constexpr const char* kNames[kPlayers] = {
    "Nintendo Switch Controller",
    "Nintendo Switch Controller (Player 2)",
    "Nintendo Switch Controller (Player 3)",
    "Nintendo Switch Controller (Player 4)",
};

void SDLCALL update(void* userdata);

void open_player(size_t index) {
  Player& player = g_players[index];
  if (player.joystick != nullptr) {
    return;
  }
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = kAxisCount;
  desc.nbuttons = kButtonCount;
  desc.button_mask = (1u << kButtonCount) - 1u;
  desc.axis_mask = (1u << kAxisCount) - 1u;
  desc.name = kNames[index];
  desc.userdata = &player;
  desc.Update = update;
  player.joystickId = SDL_AttachVirtualJoystick(&desc);
  if (player.joystickId == 0) {
    return;
  }
  player.joystick = SDL_OpenJoystick(player.joystickId);
  if (player.joystick != nullptr) {
    // The game's controller port follows the console's player number.
    SDL_SetJoystickPlayerIndex(player.joystick, static_cast<int>(index));
  }
  player.sentAny = false;  // a fresh joystick has none of the cached values yet
}

void close_player(size_t index) {
  Player& player = g_players[index];
  if (player.joystick != nullptr) {
    SDL_CloseJoystick(player.joystick);
    player.joystick = nullptr;
  }
  if (player.joystickId != 0) {
    SDL_DetachVirtualJoystick(player.joystickId);
    player.joystickId = 0;
  }
}

// Tilt steering for player 1: the roll of the console (handheld) or controller, read from its
// accelerometer as the direction of gravity, replaces the left stick's X axis. Same response
// curve as the Android app (MotionSteering.kt): a small dead zone, then linear to full lock.
std::atomic<bool> g_motionEnabled{false};
std::atomic<bool> g_motionInverted{false};
std::atomic<float> g_motionSensitivity{1.0f};
std::atomic<bool> g_motionRecenter{true};
std::atomic<bool> g_motionSeen{false};
HidSixAxisSensorHandle g_motionHandles[4]{};  // handheld, full controller, Joy-Con pair (left, right)
bool g_motionStarted = false;
float g_motionCenter = 0.0f;

void start_motion_sensors() {
  if (g_motionStarted) {
    return;
  }
  g_motionStarted = true;
  hidGetSixAxisSensorHandles(&g_motionHandles[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
  hidGetSixAxisSensorHandles(&g_motionHandles[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
  hidGetSixAxisSensorHandles(&g_motionHandles[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
  for (const HidSixAxisSensorHandle& handle : g_motionHandles) {
    hidStartSixAxisSensor(handle);
  }
}

// Steering from -1 (full left) to 1, or false when no sensor answers.
bool motion_steering(const PadState& pad, float& out) {
  start_motion_sensors();
  const u32 style = padGetStyleSet(&pad);
  int which = -1;
  if ((style & HidNpadStyleTag_NpadHandheld) != 0) {
    which = 0;
  } else if ((style & HidNpadStyleTag_NpadFullKey) != 0) {
    which = 1;
  } else if ((style & HidNpadStyleTag_NpadJoyDual) != 0) {
    which = 3;  // the right Joy-Con of the pair
  }
  HidSixAxisSensorState state{};
  if (which < 0 || hidGetSixAxisSensorStates(g_motionHandles[which], &state, 1) == 0) {
    return false;
  }
  const float x = state.acceleration.x;
  const float rest = std::hypot(state.acceleration.y, state.acceleration.z);
  if (std::hypot(x, rest) < 0.08f) {
    return false;  // free fall or a glitch: no usable direction
  }
  g_motionSeen.store(true, std::memory_order_relaxed);
  // Roll: how far gravity leans to the device's left or right, whatever its pitch.
  const float angle = std::atan2(x, rest);
  if (g_motionRecenter.exchange(false, std::memory_order_relaxed)) {
    g_motionCenter = angle;
  }
  constexpr float kDeadZone = 0.045f;
  const float sensitivity = std::fmin(2.0f, std::fmax(0.5f, g_motionSensitivity.load(std::memory_order_relaxed)));
  const float delta = angle - g_motionCenter;
  const float magnitude = std::fabs(delta);
  if (magnitude <= kDeadZone) {
    out = 0.0f;
    return true;
  }
  const float fullLock = 0.70f / sensitivity;
  float value = std::fmin(1.0f, (magnitude - kDeadZone) / (fullLock - kDeadZone));
  value = delta < 0.0f ? -value : value;
  out = g_motionInverted.load(std::memory_order_relaxed) ? -value : value;
  return true;
}

void set_axis(Player& player, size_t slot, SDL_GamepadAxis axis, Sint16 value) {
  if (!player.sentAny || player.sentAxes[slot] != value) {
    player.sentAxes[slot] = value;
    SDL_SetJoystickVirtualAxis(player.joystick, axis, value);
  }
}

void SDLCALL update(void* userdata) {
  Player& player = *static_cast<Player*>(userdata);
  if (player.joystick == nullptr) {
    return;
  }
  padUpdate(&player.pad);
  const u64 held = padGetButtons(&player.pad);
  for (size_t i = 0; i < std::size(kButtons); ++i) {
    const bool down = (held & kButtons[i].hid) != 0;
    if (!player.sentAny || player.sentButtons[i] != down) {
      player.sentButtons[i] = down;
      SDL_SetJoystickVirtualButton(player.joystick, kButtons[i].sdl, down);
    }
  }
  const HidAnalogStickState left = padGetStickPos(&player.pad, 0);
  const HidAnalogStickState right = padGetStickPos(&player.pad, 1);
  // libnx sticks are +Y up; SDL gamepad axes are +Y down.
  s32 steerX = left.x;
  // Tilt steers player 1 unless the stick itself is pushed sideways (menus stay usable).
  if (&player == &g_players[0] && g_motionEnabled.load(std::memory_order_relaxed) &&
      std::abs(left.x) < SDL_JOYSTICK_AXIS_MAX / 3) {
    float tilt = 0.0f;
    if (motion_steering(player.pad, tilt)) {
      steerX = static_cast<s32>(tilt * static_cast<float>(SDL_JOYSTICK_AXIS_MAX));
    }
  }
  set_axis(player, 0, SDL_GAMEPAD_AXIS_LEFTX, clamp_axis(steerX));
  set_axis(player, 1, SDL_GAMEPAD_AXIS_LEFTY, clamp_axis(-left.y));
  set_axis(player, 2, SDL_GAMEPAD_AXIS_RIGHTX, clamp_axis(right.x));
  set_axis(player, 3, SDL_GAMEPAD_AXIS_RIGHTY, clamp_axis(-right.y));
  // ZL/ZR are digital on every Switch controller.
  set_axis(player, 4, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, (held & HidNpadButton_ZL) != 0 ? SDL_JOYSTICK_AXIS_MAX : 0);
  set_axis(player, 5, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, (held & HidNpadButton_ZR) != 0 ? SDL_JOYSTICK_AXIS_MAX : 0);
  player.sentAny = true;
}

} // namespace

void attach() noexcept {
  if (g_attached) {
    return;
  }
  g_attached = true;
  // Accept every player slot: supporting only No1+Handheld makes HID disconnect any other
  // controller at launch. Slots 5-8 have no port in the game, so they steer player 1 as before.
  padConfigureInput(8, HidNpadStyleSet_NpadStandard);
  padInitialize(&g_players[0].pad, HidNpadIdType_No1, HidNpadIdType_Handheld, HidNpadIdType_No5, HidNpadIdType_No6,
                HidNpadIdType_No7, HidNpadIdType_No8);
  padInitialize(&g_players[1].pad, HidNpadIdType_No2);
  padInitialize(&g_players[2].pad, HidNpadIdType_No3);
  padInitialize(&g_players[3].pad, HidNpadIdType_No4);
  open_player(0);
  poll_connections();
}

void poll_connections() noexcept {
  if (!g_attached) {
    return;
  }
  if ((g_updateCount++ % 20u) != 0) {
    return;
  }
  static constexpr HidNpadIdType kIds[kPlayers] = {HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3,
                                                   HidNpadIdType_No4};
  for (size_t i = 1; i < kPlayers; ++i) {
    const bool connected = hidGetNpadStyleSet(kIds[i]) != 0;
    if (connected && g_players[i].joystick == nullptr) {
      open_player(i);
    } else if (!connected && g_players[i].joystick != nullptr) {
      close_player(i);
    }
  }
}

void detach() noexcept {
  for (size_t i = 0; i < kPlayers; ++i) {
    close_player(i);
  }
  g_attached = false;
}

} // namespace aurora::input::switch_pad

// Called from the settings menu (runtime/src/settings_overlay.cpp).
extern "C" void AuroraSwitchSetMotionSteering(int enabled, float sensitivity, int inverted) {
  using namespace aurora::input::switch_pad;
  const bool wasEnabled = g_motionEnabled.exchange(enabled != 0, std::memory_order_relaxed);
  g_motionSensitivity.store(sensitivity, std::memory_order_relaxed);
  g_motionInverted.store(inverted != 0, std::memory_order_relaxed);
  if (enabled != 0 && !wasEnabled) {
    g_motionRecenter.store(true, std::memory_order_relaxed);  // straight ahead = how it is held now
  }
}

extern "C" void AuroraSwitchRecenterMotionSteering() {
  aurora::input::switch_pad::g_motionRecenter.store(true, std::memory_order_relaxed);
}

// True once the tilt sensor has delivered a reading.
extern "C" int AuroraSwitchMotionSteeringSeen() {
  return aurora::input::switch_pad::g_motionSeen.load(std::memory_order_relaxed) ? 1 : 0;
}
