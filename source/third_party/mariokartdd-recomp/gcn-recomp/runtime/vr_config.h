// Runtime-tunable VR parameters.
//
// These exist as a file rather than constants because the right values cannot be known
// from the source: `a_pos` reaches the renderer in the game's own units, and nothing
// says how many of those make a metre. Getting that wrong makes the world giant or
// doll-sized, and it is only judgeable by wearing the headset. So they are read from
//   /sdcard/Android/data/<package>/files/vr.txt
// and can be changed with `adb push` between runs, with no rebuild.
//
// Format is one `key value` per line; blank lines and lines starting with # ignored.
// The defaults below are neutral; a game sets its own through
// vr::GameHooks::config_defaults (vr_game.h) before the file is read. Keys this struct
// does not know are kept in `extra`, for a game's own hooks to read.
#pragma once
#include <map>
#include <string>

struct VrConfig {
    // Game units per real-world metre. Scales both the interpupillary distance and
    // head movement, so it sets the apparent size of the world: raising it makes the
    // viewer bigger and the world smaller.
    float units_per_metre = 1.0f;

    // Where the viewpoint sits relative to the game's camera, in game units:
    // x right, y up, z back.
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float offset_z = 0.0f;

    // Near and far planes, in metres. Their ratio sets depth precision, so widen only
    // as far as the scene needs: too large a range shows up as z-fighting.
    float near_m = 0.1f;
    float far_m = 2000.0f;

    // Where the frame the game's 2D elements are painted on hangs, and how big it is
    // (render_hud_frame). The distance is what makes the HUD fuse: it gives the two eyes
    // one thing at one depth to agree on, which a fixed position in each eye's own NDC
    // never does.
    //
    //   scale       fraction of the vertical field of view the frame fills
    //   distance    how far ahead of the game's camera it stands
    //   height      how far above the forward axis its centre sits, in metres
    //   pitch       tilt about the frame's horizontal axis. 0 stands the frame vertical
    //               in the *room*; negative leans its top towards the viewer.
    float hud_scale = 0.5f;
    float hud_distance_m = 4.0f;
    float hud_height_m = 0.0f;
    float hud_pitch_deg = 0.0f;

    // Degrees of the game camera's pitch to take back out of the world in stereo, for a
    // game whose camera looks down at its subject at a fixed angle: vertices arrive in the
    // camera's own frame and are handed to the headset as though that frame were
    // gravity-aligned. 0 renders what the game draws. See render_set_world_pitch.
    float world_pitch_deg = 0.0f;

    // A game's depth layers in stereo, for a game that confines layers to bands of the depth
    // buffer: draws from `background_band` to the back are drawn at infinity, and draws no
    // further than `foreground_band` are scaled towards the eye by `foreground_scale`. 0
    // turns a layer off. See gx::render_set_depth_layers.
    float background_band = 0.0f;
    float foreground_band = 0.0f;
    float foreground_scale = 1.0f;
    // And a HUD layer inside the foreground's band: draws no further than `hud_band` are
    // scaled towards the eye by `hud_band_scale` instead, for a HUD modelled as geometry
    // far out in view space that should read at arm's length. 0 turns it off.
    float hud_band = 0.0f;
    float hud_band_scale = 1.0f;
    // A draw in that layer modelled nearer than `hud_push_below` (view-space units) is
    // pushed out to `hud_push_to` at the same angular size. 0 turns it off. See
    // gx::render_set_hud_push.
    float hud_push_below = 0.0f;
    float hud_push_to = 0.0f;

    // What a perspective draw the game placed in view space itself (a position matrix with
    // no tilt, in the camera's own frame) is in stereo. Off, it is something held in front
    // of the camera -- a rig of lights, say -- and goes with the camera rather than the
    // world. On, it is a billboard standing in the world, a sprite turned to face the camera
    // but placed at a point in the scene, and goes with the world: through the world's pitch
    // correction and the game's own eye, so that it stays where the scene puts it. See
    // gx::render_set_view_space_world.
    bool view_space_world = false;

    // How many samples the EFB keeps per hardware pixel, in each axis, in each view.
    // In theater the EFB *is* the picture, so more samples are filtered down into the
    // panel as antialiasing. In stereo the eyes are drawn at the headset's resolution and
    // the EFB is only scratch space for what they sample out of it. Capped at
    // gx::kMaxInternalScale.
    int theater_scale = 3;
    int stereo_scale = 1;

    // The eyes' own render targets: a multiple of the size the runtime recommends, and
    // how many samples each pixel gets (0 or 1 is none). Read once, at startup.
    float eye_scale = 1.4f;
    int msaa = 4;

    // Ask the runtime for its highest CPU and GPU levels (XR_EXT_performance_settings'
    // boost) rather than leaving them to its own governor, which on a Quest 3 held the GPU
    // two or three steps below its top clock while a heavy scene missed frames.
    bool perf_boost = true;

    // Tell the runtime which threads are which (XR_KHR_android_thread_settings): the
    // guest's as the application's main thread, the front end as its worker, the frame
    // loop as the renderer's main thread and the vertex transform as its workers. A Quest
    // decides from these which cores and what priority they get beside its own threads.
    bool thread_hints = true;

    // The display's refresh rate, where the runtime lets the app choose one
    // (XR_FB_display_refresh_rate): a rate in Hz, 0 for the runtime's default, or -1 for the
    // lowest one offered that is a multiple of the console's field rate, so that every game
    // frame stays up for the same number of display frames. Not -1 by default: the frame
    // loop takes a game frame only at a display frame, so at 120 Hz a stereo frame that
    // takes the render thread more than 8.3 ms holds the next one for a third display
    // frame, and the game ran at 40-48 frames a second where 72 Hz kept 60.
    float display_hz = 0.0f;

    // Dynamic resolution: the eyes' GPU time per game frame that the eyes are kept under by
    // drawing fewer of their pixels (0: always the full size), and the smallest fraction
    // of each side they may go down to.
    float gpu_budget_ms = 13.0f;
    float min_eye_res = 0.5f;

    // Trace the theater path one display frame at a time (adb logcat -s <game>).
    bool log_frames = false;

    // Write the EFB to <files>/frames every N presented frames. 0 is off.
    int dump_every = 0;

    // Start in stereo rather than theater. For a game with no stereo hook this is the
    // view, full stop: there is no switch by hand. One was tried on the left thumbstick
    // click and was only ever pressed by accident.
    bool start_in_stereo = false;

    // Start with the game's own alternative eye (vr::GameHooks::set_first_person) on,
    // until the viewer has chosen; after that the app remembers the choice in
    // <files>/view.txt.
    bool first_person = false;

    // Seconds the morph between theater and stereo takes -- the panel opening out into the
    // world, or the world folding back onto it. 0 snaps.
    float transition_s = 1.0f;

    // Show the theater panel as a stereo pair, like a 3D film: each eye's image rendered
    // from half an interpupillary distance to its side of the game's camera, converged on
    // the panel (gx::render_execute_stereo_pair). The panel is then a window: what the
    // game's frustum shows as wide as the panel sits on it, nearer things stand in front of
    // it, and the sky is at infinity. Costs a second flat pass per game frame.
    bool theater_stereo = false;
    // With theater_stereo, draws the game confines to a band of the depth buffer no deeper
    // than this sit on the panel itself, with no disparity (gx::render_set_panel_band):
    // a HUD layer drawn in front of the camera, which would otherwise stand in front of
    // the panel or, drawn far with a narrow frustum, at infinity. 0 pins nothing.
    float panel_band = 0.0f;
    // With theater_stereo, the fraction of the viewer's own eye separation the pair is
    // drawn with. 1 is true to the game's scale: a point at infinity has the eyes' full
    // separation, parallel. Less flattens everything towards the panel.
    float theater_depth = 1.0f;

    // Seconds the morph back to theater takes, for a game whose stereo exits had better be
    // cut than folded: by the time the game's answer changes it is already drawing something
    // that is wrong in stereo. Negative, the default, uses transition_s both ways.
    float transition_out_s = -1.0f;

    // Every key above that the file did not recognise, for a game's hooks.
    std::map<std::string, float> extra;
    float get(const char* key, float fallback) const {
        auto it = extra.find(key);
        return it == extra.end() ? fallback : it->second;
    }
};

// Reads `dir`/vr.txt over `defaults`. A missing file or missing keys keep the defaults.
VrConfig vr_config_load(const std::string& dir, const VrConfig& defaults);
