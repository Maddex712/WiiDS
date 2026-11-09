// What a game tells the headset frontends (android/openxr_main.cpp, android/egl_main.cpp).
//
// The frontends know how to put a frame on a panel, how to draw it per eye, and how to
// morph between the two. When to do which, how big the world is, and where else an eye
// might stand are facts about a game. A game project supplies them by calling
// vr::set_game_hooks from a static initializer in one of its SOURCES (gcn_add_game);
// every hook is optional. This lives in the runtime rather than beside the frontends so
// that the desktop build carries the hooks too: GCN_STEREOLOG=1 there prints what
// wants_stereo answers, against frames that can be dumped and looked at.
#pragma once
#include "vr_config.h"
#include "gx/render.h"

struct PadState;

namespace vr {
// A gamepad as a platform reads it: its buttons by where they are, not by what they are
// called (an Xbox pad's A and a Nintendo pad's B are both `south`), its sticks from -1 to 1
// with up positive, its triggers from 0 to 1.
struct Gamepad {
    enum : uint32_t {
        South = 1u << 0, East = 1u << 1, West = 1u << 2, North = 1u << 3,
        LeftShoulder = 1u << 4, RightShoulder = 1u << 5, LeftStick = 1u << 6, RightStick = 1u << 7,
        Up = 1u << 8, Down = 1u << 9, Left = 1u << 10, Right = 1u << 11,
        Start = 1u << 12, Select = 1u << 13,
    };
    uint32_t buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;
    float lt = 0, rt = 0;
};

// The GameCube layout a gamepad gets by default, by position: south A, west B, east X,
// north Y, the right shoulder Z, the triggers L and R, the sticks the main and C sticks.
// Buttons are added to `p`, and a stick or trigger taken over only while it is pushed.
void default_gamepad_layout(const Gamepad& g, PadState& p);

struct GameHooks {
    // Adjusts the defaults before vr.txt is read over them: the game's units per metre,
    // its camera pitch, where its HUD reads well.
    void (*config_defaults)(VrConfig& c) = nullptr;

    // Called with the configuration once vr.txt has been read over those defaults, for a
    // game whose hooks run off the render thread and need its values or its own keys
    // (VrConfig::extra).
    void (*config_loaded)(const VrConfig& c) = nullptr;

    // Called once per game frame with that frame's batch, from the render thread: true to
    // present the world in stereo, false for the theater panel. The view follows a change
    // in the answer, held for two frames so a transient cannot flap it. Without this hook
    // the view is start_in_stereo's, for the whole run.
    bool (*wants_stereo)(const gx::Batch& b) = nullptr;

    // Called once per game frame shown on the theater panel with theater_stereo on, from
    // the render thread: false to draw that frame flat, the same image to both eyes, for a
    // screen that reads badly as a stereo pair. Without this hook every panel frame is a pair.
    bool (*wants_panel_pair)(const gx::Batch& b) = nullptr;

    // Turns the game's own alternative eye on or off (installing a gx::EyeHook and
    // calling gx::render_set_first_person, say). Called with on=false whenever the view
    // leaves stereo. Without this hook the right thumbstick click does nothing.
    void (*set_first_person)(bool on, const VrConfig& c) = nullptr;

    // Called from the render thread when the left thumbstick is clicked: a GameCube
    // controller has no stick clicks, so this one is the game's to use (the right one is
    // set_first_person's). Pressed, not held: once per click.
    void (*left_click)() = nullptr;

    // The controller state the headset frontend built from the Touch controllers, before
    // the game reads it and before it is logged, for a game that wants a button somewhere
    // else: swapping two, or a trigger standing in for a face button in one part of the
    // game and not another. The desktop frontend has its own controllers and does not
    // call it.
    void (*map_pad)(PadState& p) = nullptr;

    // A gamepad's place in the controller state (merged after map_pad, by a frontend that
    // reads one), for a game whose controls on a modern pad are not the GameCube's
    // buttons in its places. Called on the frontend's thread; whatever the game takes from
    // it beyond the pad, it reads on the guest thread through input_pose, so that a replay
    // has it too. Without this hook default_gamepad_layout.
    void (*map_gamepad)(const Gamepad& g, PadState& p) = nullptr;

    // The game's say over what the eyes draw: a filter the frontend installs in the
    // renderer (gx::EyeFilter, with what it is told, in gx/render.h). Null hides nothing.
    gx::EyeFilter eye_filter = nullptr;
};

void set_game_hooks(const GameHooks& h);
const GameHooks& game_hooks();

// The controllers, for a game that puts something in the viewer's hands. Each pose is the
// controller's aim pose (the ray it points along) in the frame the eyes are placed in:
// game units, x right, y up, z back, origin at the game's camera -- the eyes' own
// conversion (units_per_metre, the offsets, the head's zero) applied to it. A game whose
// vertices are turned on their way to the eye (world_pitch_deg, an EyeHook) undoes that
// itself. The headset frontend publishes them while it draws the eyes and withdraws them
// otherwise, so a game that follows them only does so while the viewer can see the result.
// Readable from any thread; the guest's is the one that wants them.
enum Hand { kLeftHand, kRightHand };
struct HandPose {
    float pos[3];
    float rot[4];  // a unit quaternion: x, y, z, w
};
void set_hand_pose(Hand h, const HandPose* p);  // null: not tracked, or not in stereo
bool hand_pose(Hand h, HandPose* out);

// The eyes, for a game that culls against its own camera's field of view and so has to be
// told what the viewer can see: turning the head shows what the camera's frustum left out.
// Each eye's pose is in the controllers' frame above, and its field is given as the
// tangents of its four edges (left and down negative), as located for the frame being shown.
// The game culls a frame or two before that frame is shown, so it should allow for the head
// moving in between. Published, like the controllers, only while the eyes are drawn.
struct EyeView {
    float pos[3];
    float rot[4];  // a unit quaternion: x, y, z, w
    float tan_left, tan_right, tan_up, tan_down;
};
void set_eye_views(const EyeView* views);  // two views, left then right; null: not in stereo
bool eye_views(EyeView out[2]);

// Whether the viewer plays with a gamepad rather than tracked controllers: the visionOS
// frontend's answer when no spatial controller is in hand, never the Quest's. A game that
// turns its camera, aims or scans after the head should then play straight, as on a
// television -- the head only looks around -- though it still culls to the eyes. Readable
// from any thread; the guest's reads are logged and replayed, like the poses.
void set_gamepad_play(bool on);
bool gamepad_play();

// The defaults with the game's adjustments applied, then the platform's, then `dir`/vr.txt over them.
VrConfig load_config(const std::string& dir, void (*platform_defaults)(VrConfig&) = nullptr);
}  // namespace vr
