# VR and motion telemetry

The headset frontends (PC VR with `--vr` on Windows, the Quest app) and the motion telemetry
are gcn-recomp's (`openxr/`, `android/`, `runtime/telemetry.*`). What is this game's is in
`src/`:

- `race.cpp` reads the race out of the game's memory;
- `vr_mkdd.cpp` answers the frontend's hooks (`vr::GameHooks`) and decides, once a frame
  on the guest thread (`gx::set_guest_frame_hook`), whether to be in stereo;
- `motion.cpp` turns the player's kart into telemetry.

Worn for the first time on 2026-10-12, on a Quest 3, in first person. Most of what is
below was checked with the desktop eye preview (`--eye`, which renders through the stereo
path with the game's own VR settings and eye filter) and the stereo log (`MKDD_VRLOG=1`);
what the headset showed was reproduced on the headset itself, with no PC build at hand, by
replaying the session's input log (`inputs/<timestamp>/` in the app's files directory)
through the device-side tool: `mkdd_egl` pushed to `/data/local/tmp`, run from the files
directory over `adb shell` with `GCN_REPLAY=inputs/<timestamp>`, `--eye --first-person
--eye-fov=43,52` and `--dump-dir`, and the frames pulled back (`gcn-recomp
docs/diagnostics.md`, "On a headset"). `GCN_DRAWLOG=<frame>` and `GCN_PNMLOG=<frame>`
there, in the flat view, are what found the item boxes' question marks below.

## What the game's memory says

From the decompilation's headers (`RaceMgr.h`, `RaceDirector.h`, `RaceInfo.h`,
`PauseManager.h`), the offsets confirmed in the retail code and by RAM dumps
(`GCN_RAM_DUMP_AT`) over the route in `routes.md`, a pause, and the title's attract demo.

| where | what |
|---|---|
| 0x803CB7E8 | `RaceMgr::sRaceManager`; 0 outside a race scene |
| RaceMgr+0x18 | its `RaceDirector` |
| RaceMgr+0x38 | its `RaceInfo` (`isRaceModeMiniGame` reads the mode through it) |
| RaceDirector+0x0C | the phase. 1 the course flyover; 17 from the camera's drop onto the grid to the finish (`RaceDirector::calc` checks for the race's end only in 17); 16, 18, 19 run `doSafety`, `doWait` and the ending (by `calc`'s jump table) |
| RaceDirector+0x30 | within phase 17: 0 on the grid, 1 the countdown, 2 racing |
| RaceInfo+0x08 | the mode: 1 time trial, 2 Grand Prix, 3 versus, 4-7 battles, 8 award, 9 credits |
| RaceInfo+0x1E | human players; 0 in the attract demo |
| RaceInfo+0x20 | screens (split screen) |
| RaceInfo+0x28 | demo type: nonzero in the attract demo |
| 0x803CB6DC | `PauseManager::mIsPause`, 1 on the pause menu (found by diffing a pause against racing either side of it; it sits after `PauseManager::mThis`, as in the decompilation) |

## When to be in stereo

`race_live`: a race scene, not the attract demo, at least one human player, one screen, a
racing or battle mode, phase 17 past its opening shot (run state 1 or 2), not paused. So
stereo from the countdown to the finish; the flat screen for the menus, the flyover, the
opening shot on Lakitu and the drop onto the grid (see "First person" for why), the pause
menu, the results, the award ceremony and split screen. After a pause, stereo comes back
only once the race has been live for 45 frames, so quitting from the pause menu does not
blink into stereo on the way out. On the scripted route, before the opening shot was left
out: stereo at frame 6306, the flat screen at the pause (7202), stereo again at 7508.

## Scale and pitch

- `units_per_metre` 100: in view space the player's kart is about 200 units long, its
  wheelbase about 100 and its track about 150, its wheels about 65 across. (Its speed says
  otherwise: about 43 units a frame at roughly 30 mph on the speedometer would be nearer
  190 units a metre. Kart games are not to scale; the size is what a headset judges.)
- The chase camera looks down at the kart, by about 18 degrees on the flat (its position
  and look-at point, and the wheels' lowest points in view space, agree). The world is
  levelled by the camera's own pitch every frame: `camera_pitch` reads `KartCam`'s view
  matrix on the guest thread (atan2 of the world's up in the camera's frame), and the
  renderer takes it through `render_set_world_pitch_hook` on its own thread, eased over
  about a tenth of a second since the guest may be a frame or two ahead.
  `follow_camera_pitch 0` in `vr.txt` uses the fixed `world_pitch_deg` (18) instead.
- `far_m` 4000: the sky dome is drawn up to about 1.8 km out.

| where | what |
|---|---|
| 0x803CC588 | the kart controller: the eight karts' bodies first, the players' cameras (`KartCam*`) at +0x200 |
| KartCam+0x00 | the `KartBody` it follows |
| KartCam+0x80 | its view matrix, GX 3x4 by rows (+0x148 field of view, 80; +0x150 near 10; +0x154 far 200000; +0x1E8 position; +0x1F4 look-at) |
| KartBody+0x110 | its world matrix: columns the kart's left, up and forward (the direction of travel), then its position (also at +0x23C; the last frame's at +0x248) |
| KartBody+0x444 | `mSpeed`, units a frame; `KartCtrl::GetCarSpeed` is it times 2.16, km/h at 100 units a metre |

## First person

The game has no cockpit camera, so first person (the right thumbstick click; `--first-person`
with `--eye` on the desktop) is an eye hook that puts the eye in the driver's seat. Each
frame the guest thread reads the kart's matrix in the camera's view space (`kart_in_view`:
the body's world matrix through the camera's view). The eye hook, on the render thread,
looks in that frame's batch for the kart body model's position matrix and places the eye
at (`fp_x`, `fp_y`, `fp_z`) = (0, 105, -10) in it -- a foot above and behind the driver's
head at (0, 75, 20), where it sat until a rider asked for it higher and further back -- looking along the kart's nose and tilting
with it. Taking the kart from the batch rather than from memory keeps the eye on exactly
the kart that frame draws; when the batch has nothing, the eye is placed from the memory
matrix with the body model's last offset. Draws whose position matrix's origin is within
`fp_hide` (45 units) of the driver's head -- the head and shoulders -- are left out of the
eyes.

Which matrix is the body model's took three tries, each checked by replaying a headset
session on the device (`MKDD_FPLOG=1` lists every matrix near the kart in the kart's own
frame, with its rotation's distance from the kart's, its draws and vertices and its
vertices' heights, and the one taken). What the batch holds near a kart, with the kart's
rotation:

- the body model: one draw, 1,400 vertices or so, its origin 60 units up the kart's axis on
  Mario's kart and 99 on Daisy's, its vertices from 40 to 190 units up;
- a common mesh of 1,704 vertices in two draws at every kart, its origin 13 units up on one
  kart and 36 on another, level when the kart rears or banks, and reaching 120 units below
  the body's origin -- under the ground -- and 190 above: an envelope round the whole kart,
  not a part of it;
- the four wheels, 350-400 vertices each, far off in rotation as they spin;
- the riders' joints, many small draws, whose matrices are skinning matrices (the joint
  times its inverse bind pose), so their origins are not positions and cannot anchor
  anything.

The first version took the matrix with the kart's rotation, to a tight tolerance, nearest
the body's origin: the envelope until the race started, when the body model won instead,
so the eye sat at the wheels' height and jumped to the seat at the start; and whenever the
drawn kart's rotation was outside the tolerance -- it lags the guest's by a frame or two,
and the body model carries the suspension's tilt and the wheelie of a rocket start -- the
hook found nothing and handed the frame back to the chase camera, so the view flashed
between the two. The second anchored 60 units up, which is Mario's kart's body model and
within reach of Daisy's kart's envelope, so on her kart the eye sat at the axle, among the
suspension arms. Now it is the biggest part that stays above the ground: of the matrices
within 150 units with the kart's rotation to a wide tolerance, the one drawing the most
vertices none of which reach more than 30 units below the body's origin. Both karts take
their body model on every frame of their sessions.

The race opens with the game's camera on Lakitu and his signal, then dropping onto the grid
(`RaceDirector`'s run state 0), and the game draws only what that camera sees: the player's
kart body is in those frames but its wheels are not, nor most of the other karts -- in the
headset, karts without wheels at the start, in the chase view as much as the seat. It is
the game's own cinematic, so it is watched on the panel: `race_live` starts at the
countdown (run state 1), and the world opens out around the viewer there. The dark
checkerboard over the opening shot is the game's own fade-in.

## The item boxes' question marks

In the headset, in the driver's seat, the question marks hung in the sky and sank onto
their boxes as they neared. Each is a billboard: a six-vertex sprite whose position matrix
is the identity rotation with the box's view-space position (`GCN_PNMLOG` shows them as
`ident=1`), which J3D builds for a billboard. The renderer reads a perspective draw with an
untilted view-space matrix as something the game holds in front of its camera (a rig of
lights, in the game that taught it) and draws it with the head's transform only -- not the
world's pitch correction, and not this eye hook -- so the sprites stayed where the chase
camera saw them while the eye moved into the seat. `view_space_world` (`vr.txt`; set by
`config_defaults`) tells it that this game's view-space draws are billboards in the world,
and they go through the world's transform like everything else, facing the chase camera
rather than the eye, which inside a translucent box does not show.
## What the eyes leave out

The race's post-processing is orthographic and samples EFB copies: draws that put a
half-size copy of the frame back over it (an alpha mask, an RGB565 copy and an RG8 one,
made every frame). An eye hangs orthographic draws on the HUD frame, where these showed as
a translucent rectangle of smeared scenery. `eye_filter` hides every orthographic draw that
samples a copy. The HUD itself samples ordinary textures.

An orthographic draw over the whole picture that samples no copy is a film over the
screen: the lightning's darkening and flash, among others. On the HUD frame it was a
translucent rectangle in front of the viewer with its edges in plain view; `eye_filter`
makes it EyeScreen, drawn across each whole eye. Whole means its box spans -1..1 both ways
in clip space and its triangles cover at least 3.9 of the 4 that is the picture once, which
a frame or letterbox bars with the same box do not. `MKDD_VRLOG=1` prints each one. Not yet
seen in a headset.

## Telemetry

See the comment at the top of `motion.cpp`. In short: the body the first player's camera
follows; velocity and acceleration differenced from its position (1/60 s a frame, 100 units
a metre) and eased, put in the kart's frame (surge along the nose, sway to the right, heave
along its up); pitch and roll against the horizontal, since gravity is the world's -y here;
the accelerations scaled by `accel_scale` (0.3); inactive whenever the eyes are not in
stereo (`race_live` and the wait after a pause), and a jump of over 300 units in a frame
(Lakitu putting the kart back) sent as a discontinuity.

What the platform does with it was retuned after a first ride (below, "On a rig").

The scale is the game's own: `mSpeed` (KartBody+0x444, from the decompilation's
`KartBody` layout, which agrees with the retail offsets of the matrix and the position) times
2.16 is `GetCarSpeed`'s km/h, which is 3.6 * 60 / 100, and the speed differenced from the
position matched it to within a km/h from 0 to 95 km/h on Luigi Circuit. Steering right
with the scripted route (`SR` from frame 7000) raised the heading, gave a positive sway (into
the turn) and a negative slide (the kart sliding outward), as `telemetry.h` defines them.
The accelerations at that scale are a kart's: about 4 m/s^2 accelerating at 80 km/h, 20-50
m/s^2 sideways through the first turn at 95 km/h, and 50-90 bouncing over the grass into the
wall. Unscaled, every channel sat at the shared 15 m/s^2 limit through the turn, hence 0.3.
`MKDD_MOTIONLOG=1` prints what goes into the telemetry module, with the kart's status
bits, the ground under it and its revs (`MKDD_MOTIONLOG_EVERY=n` every nth frame), and
`GCN_TELEMETRY_LOG=1` what comes out; `--telemetry=127.0.0.1:<another port>` keeps it from a
running SimRacingStudio. A recorded session (`saves/inputs/`) replayed with `--headless
--fast` and these logs is how the numbers below were found.

| where | what |
|---|---|
| KartBody+0x2C | `mBodyGround`, a `CrsGround`: its attribute at +0x4C (KartBody+0x78), its material at +0x54 (+0x80) |
| KartBody+0x44C | `mCarRPM`: the wheels' spin summed, times 40, plus 300, at most 16300 |
| KartBody+0x570 | `mCarStatus`, a u64; its low word at +0x574 holds every bit used here |
| KartBody+0x5A4 | `mTouchNum`, wheels on the ground |
| KartBody+0x04 | `mKartSus[4]`: front right, front left, rear right, rear left |
| KartSus+0xB8 | the wheel's extension along its arm, in units: 40 in the air, about 36 at rest, near 0 on a hard landing (`DoSusAction` is undecompiled; `KartDisp` offsets the wheel by it) |

From the decompilation, and seen in a replayed session: `mCarStatus` & 3 is a power slide
(`KartCtrl::GetDriftCnt`), 0x40 airborne, 0x100000 any crash, with 0x80000 a spin
(`KartCrash::MakeSpin`: 0x180000 seen, the kart turning through 708 degrees in about 80
frames), 0x200000 a tumble (0x300000 seen, the kart pitching through 70 degrees and more), and
0x1000000 a half spin. 0x8000, 0x20000 and 0x4000 were seen at full speed and are not
known. Ground attributes seen on Luigi Circuit and the session's other course: 01 the road
(materials 00, 02, and 03, a rumble), 03 with material 07 the grass (the kart slowed to 85-99
km/h), 08 with no material at full speed, and 11 with material 1C driven at 95 km/h for 25
seconds -- the decompilation's water, but not here.

### On a rig

The first version followed the heading with yaw (washed out), sent no engine and no road
texture, and passed pitch through as it was. On a rig, yawing with every turn was jarring;
the platform sat dead still on a straight; a steep climb tilted it a couple of degrees; the
shaker had no engine to play. So now (the top of `motion.cpp`):

- Yaw is the slip angle, the kart's facing against its direction of travel: 3 degrees
  median and 17 at the 90th percentile on the road, 25-75 in a slide. It was first only in a
  slide, scaled by 0.25 to at most 15 degrees, with a thrash in a crash: 12, then 20,
  degrees times the sine of how far the heading had turned. A rider hit by a shell felt the
  platform barely move, and the motion log of a replayed Grand Prix showed why: through
  most crashes the heading turns 0-20 degrees (the model spins, the body does not). The
  thrash was made to advance at a steady rate, and then the rider asked for the plain slip
  angle at all times instead. Below 4 m/s it fades out, to nothing at 1. The shared module
  sends the yaw as it is, and the yaw rate limit is raised to 360 degrees a second for this
  game.
- Pitch doubled. Roll not: Luigi Circuit's banked turn is 30-38 degrees for five seconds,
  which already holds roll at its limit. Both fade out in a crash.
- A road texture. First made up in the shared module from the ground's type, as filtered
  noise: on the rig it felt like white noise, all one note. Now it is the kart's suspension
  (KartSus+0xB8, the wheel's extension along its arm: 40 in the air, about 36 at rest, near 0
  on a hard landing), its movement about a running mean on heave, pitch and roll. On the
  replayed ride, as sent: heave 0.5-0.8 m/s^2 RMS on the road, 1.8 on the grass; roll 1.8
  degrees RMS on the rumbling material; landings 10-15 m/s^2. A bump comes through on the
  front pair, then the rear.
- The engine from the speed (900-8000 rpm to 130 km/h), with the game's revs only near a
  standstill: `mCarRPM` sits at its ceiling at any speed and jumps by thousands between frames
  as wheels leave the ground.

SimRacingStudio's listener (its installed `apiListener` and `api_definition` modules, whose
strings are zlib-compressed Cython constants) reads rpm, max rpm, four suspension travels and
four wheel terrains as well, and has per-wheel rumble; what values its terrain wants is not
known, so those are left at zero.

## Still to look at

- Wearing it, further: scale, the HUD's distance and size, the pitch, comfort at speed.
- First person in a headset, further: the seat's height, whether tilting with the kart on
  slopes and bumps is comfortable or the view should be levelled, Lakitu up close.
- The session's input log counts 47,716 pad polls (120 Hz: about 400 s) against 3,764
  frames, with catch-up jumps of tens of thousands of edges: the app was running the game
  well below full speed in the headset, or the clock jumped through the loads. Not yet
  measured.
- Lens flare: the game tests the sun's visibility by copying 2x2 pixels of the EFB and
  reading them back; what an eye does to that is unexamined.
- The theater screen as a stereo pair for the 3D menus (`theater_stereo`).
- Telemetry on a rig, the second ride: the slide's yaw and the spin's thrash, the road
  texture's size, pitch doubled, the engine on the shaker; `accel_scale`; what Lakitu's
  rescue does.

## The Vision Pro (2026-10-22)

A standalone Vision Pro build, on gcn-recomp's `visionos/` frontend (how it works, and what
the first games on a device overturned, is in `gcn-recomp/docs/visionos.md`): the Quest's
frontend core on a small OpenXR runtime over CompositorServices, ARKit and GameController,
the renderer on ANGLE. `VISIONOS_BUNDLE_ID` in `CMakeLists.txt` turns it on, and
`build-visionos.sh` builds, installs and pushes the disc image, as Prime's and Blue Storm's
do. The platform's own defaults apply over this game's: `eye_scale` 1, since the
recommendation is the foveated texture's size, and no GPU budget, since ANGLE's timer
overstates (gcn-recomp `docs/visionos.md`, "Resolution").

Nothing new was needed from this game. It has no `map_pad`, so a PS VR2 Sense controller,
mapped onto the Touch profile, plays as a Touch controller on a Quest, the right stick
click for the driver's seat included; a gamepad gets the desktop's GameCube layout and the
same click. Built and installed; not yet played on the device.

## Shader keys from the Vision Pro (2026-10-22)

`src/shader_keys.bin` (`SHADER_KEYS`) is the list gcn-recomp compiles on a fresh install
before the game starts, so a first play-through does not hitch on each new program (a draw
whose program is not built yet holds its frame, about 80 ms through ANGLE). It is the
Vision Pro's cache after the first sessions there, made canonical by
`gcn-recomp/tools/shader_keys`: 144 keys. The desktop's `saves/shaders.bin` is in an older
key format the tool does not read. To extend the list, put it first so its order holds:
`shader_keys src/shader_keys.bin src/shader_keys.bin <new caches>`.

