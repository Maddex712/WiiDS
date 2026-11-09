# mariokartdd-recomp

A static recompilation of *Mario Kart: Double Dash!!* (Nintendo GameCube, 2003) to native
code.

The game's PowerPC code is translated ahead of time into C, then compiled and linked
against a runtime that stands in for the GameCube hardware (graphics, audio, DVD,
controllers, memory card). The result is a native program that runs the original game logic
without an emulator's CPU core. The recompiler and runtime are
[gcn-recomp](https://github.com/ethannicholas/gcn-recomp).

> **This repository contains no game code or data.** You must supply your own disc image,
> dumped from a copy of the game that you own. The build process reads the code out of
> *your* image and recompiles it locally. No game code or assets are distributed here; what
> this repository holds about the game is a table of its function addresses and names.

## License

The code in this repository and in `gcn-recomp` is released under the [MIT License](LICENSE).

## Legal

- *Mario Kart: Double Dash!!*, GameCube and all related names and marks are property of
  Nintendo. This project is not affiliated with or endorsed by Nintendo.
- This repository contains only original source code plus metadata describing code layout
  (function addresses and names in `analysis/`, produced with
  [decomp-toolkit](https://github.com/encounter/decomp-toolkit) and named from the linker
  map that ships on the game's disc). It contains no
  copyrighted game code, assets, BIOS/IPL files or DSP microcode.
- You need a legally obtained copy of the game. Dump it from your own disc using your own
  GameCube/Wii (e.g. with [CleanRip](https://wiibrew.org/wiki/CleanRip)). Do not download
  game images, and do not share the image, anything extracted from it (`build/main.dol`),
  the generated C code (`build/gen/`), built binaries, or memory card files, since all of
  these contain or are derived from the game.

## Supported version

Only the North American release is supported:

| Title | Disc ID | `main.dol` SHA-1 |
|---|---|---|
| Mario Kart: Double Dash!! (USA) | `GM4E01` | `e389e50357d4a5076ab3d38672204658f411123d` |

The build verifies this hash and stops with an error for any other version.

## Status

Early. The game runs from a blank memory card through the opening movie, the title screen
and the menus into a Grand Prix race, with sound. See `docs/dev/status.md` for
how far it gets and what is known not to work.

## Building

### Requirements

**macOS (Apple Silicon)**

- Xcode (or the Command Line Tools), CMake 3.20+, Ninja, Python 3
- SDL2

```sh
brew install cmake ninja sdl2 python
```

**Windows (x64 or ARM64)**

- Clang, CMake 3.20+, Ninja, Python 3
- The Windows SDK and MSVC headers/libraries (the "Desktop development with C++" workload of
  Visual Studio or the standalone Build Tools): clang targets the MSVC ABI and uses them

```powershell
winget install Kitware.CMake Ninja-build.Ninja Python.Python.3.13 LLVM.LLVM Microsoft.VisualStudio.2022.BuildTools
```

**Quest 3**: see [Running on a Quest 3](#running-on-a-quest-3).

### 1. Provide your game image

Dump your disc, then place the image in `rom/` as an uncompressed **`.iso`** or a **`.ciso`**:

```
rom/game.iso
```

If your dump is in another format (`.rvz`, `.gcz`), convert it first, for example with
Dolphin (right-click the game → *Convert File…* → *Uncompressed Disc Image* or *CISO*),
`dolphin-tool convert`, or [`nodtool`](https://github.com/encounter/nod).

The build uses the first `.iso` or `.ciso` it finds in `rom/`. To use an image elsewhere,
pass `-DGAME_ISO=/path/to/game.iso` when configuring.

### 2. Build

```sh
./build.sh          # macOS
```

```powershell
.\build.ps1         # Windows
```

The first build fetches the `gcn-recomp` submodule if a plain `git clone` left it empty
(`git clone --recursive` avoids that).

### 3. Run

```sh
./build/mkdd            # uses the configured image / rom/*
./build/mkdd path/to/game.iso
```

On Windows the executable is `build\mkdd.exe`.

Run it from the repository root: the memory card is created in `saves/` relative to the
current directory. Every run also records the controller input to `saves/inputs/<timestamp>/`
so that a route can be played back with `--replay=<that directory>`; `--no-input-log` turns
that off.

## Playing in VR on a PC

On Windows, `--vr` plays the game on a PC VR headset through OpenXR, using whichever runtime
is set as active: SteamVR, Virtual Desktop, Meta Quest Link, or any other that supports
OpenGL.

```powershell
.\build\mkdd.exe --vr
```

Connect the headset and start its PC software first. Menus, the course flyovers, the
results and split-screen races are on a flat screen in front of you; your own race is all
around you, in stereo 3D, from the countdown on the grid to the finish, and
goes back to the flat screen while paused. Clicking the right thumbstick in a race puts you
in the driver's seat, and clicking it again goes back to the game's chase camera. The window
on the desktop mirrors the headset. A
gamepad and the keyboard work alongside the headset's controllers.

Settings are read from a `vr.txt` in the directory you run the game from; see
[Tuning the VR view](#tuning-the-vr-view). To use a runtime other than the active one, point
`XR_RUNTIME_JSON` at its manifest.

## Motion platforms

`--telemetry` sends your kart's motion to a motion simulator, in the format of
[SimRacingStudio](https://www.simracingstudio.com/)'s custom game API: UDP packets to
`127.0.0.1:33001`, where SimRacingStudio listens by default. `--telemetry=host:port` sends
them elsewhere. SimRacingStudio shows the game as *Mario Kart: Double Dash!!*. It works with
or without `--vr`:

```powershell
.\build\mkdd.exe --vr --telemetry
```

The platform moves only during your own race, from the grid to the finish: it eases to rest
in the menus, while paused, in split screen and at the end of a race, and eases back in when
one starts or you resume. Pitch and roll are your kart's against the horizontal; surge, sway
and heave are its accelerations. Karts corner at several g at the game's own scale, so those
are scaled down before they are sent.

The platform does not yaw with every turn. Its yaw is the angle between where the kart is
facing and where it is going: a few degrees on the road, far more in a power slide or when
it spins out. The road's texture is the
kart's own suspension riding over it, so the grass, the rumble strips and the landings feel
like what they are; the engine is sent for a shaker's engine effect, rising with your speed.

Every channel can be scaled, inverted or limited with a `telemetry.txt` in the directory you
run the game from (all optional):

```
accel_scale 0.3     # what the kart's accelerations are multiplied by before anything else
pitch_gain 2        # multiply each channel; a negative value inverts it
roll_gain 1
yaw_gain 1
texture_gain 1      # the road texture, from the kart's suspension
suspension_heave 40 #   heave, m/s^2 for each metre the springs move
suspension_pitch 0.6 #  pitch, times the angle front against rear
suspension_roll 0.6 #   roll, times the angle left against right
suspension_hp_s 0.25 #  how long the springs take to count as at rest again
idle_rpm 900        # the engine at a standstill
max_rpm 8000        #   and flat out
top_kmh 130         #   at this speed
surge_gain 1        # forward acceleration
sway_gain 1         # sideways acceleration
heave_gain 1        # acceleration along the kart's up
fade_s 1            # seconds to ease in and out of rest
max_angle 25        # the most pitch, roll or yaw sent, in degrees
angle_rate 120      #   and how fast pitch and roll may change, in degrees a second
yaw_rate 360        #   and yaw
max_accel 15        # the most acceleration sent, in m/s^2 (about 1.5 g)
accel_rate 300      #   and how fast it may change, in m/s^2 a second
accel_s 0.15        # seconds over which accelerations are smoothed; higher is gentler
velocity_s 0.05     # the same for the velocity they are taken from
log 0               # 1 prints what is sent, twice a second
```

## Running on a Quest 3

Early: a first race has been driven in the headset, in the driver's seat. The scripts are
PowerShell, so these steps are for a Windows PC.

### Requirements

On the PC, besides CMake, Ninja and Python as for the desktop build:

- [Android NDK](https://developer.android.com/ndk/downloads), unpacked to
  `%LOCALAPPDATA%\Android\Sdk\ndk\<version>\`
- [platform-tools](https://developer.android.com/tools/releases/platform-tools) (for `adb`),
  unpacked to `%LOCALAPPDATA%\Android\Sdk\platform-tools\`
- The Android SDK's `build-tools;34.0.0` and `platforms;android-34`, installed with
  `sdkmanager` from the
  [command-line tools](https://developer.android.com/studio#command-line-tools-only)
  (run `sdkmanager --licenses` first)
- A JDK, for signing the APK: `winget install Microsoft.OpenJDK.21`

On the headset, enable
[developer mode](https://developer.oculus.com/documentation/native/android/mobile-device-setup/),
connect it over USB, and accept the *Allow USB debugging* prompt.

### Build and install

With your disc image in `rom/`:

```powershell
.\build-android.ps1          # cross-compiles libmkdd.so for arm64
.\package-apk.ps1 -Install   # packages the APK, installs it, and pushes the disc image
```

The disc image is copied to `/sdcard/Android/data/com.ethannicholas.mkdd/files/`, which
takes a few minutes the first time. The memory card and the shader cache live there too.

### Playing

The app appears under **Library → Unknown Sources → Double Dash Recomp**, and behaves as
[on a PC](#playing-in-vr-on-a-pc).

| GameCube | Touch controller |
|---|---|
| Control stick | Left thumbstick |
| C stick | Right thumbstick |
| A / B | A / B (right) |
| X / Y | X / Y (left) |
| Z | Right grip |
| L / R | Left / right trigger |
| Start | Menu (left) |
| D-pad | Left thumbstick while holding the left grip |
| *(driver's seat on/off, in a race)* | Right thumbstick click |

`adb logcat -s mkdd` shows the app's log.

### Tuning the VR view

Settings are read at startup from `vr.txt` (beside the game on a PC; on the Quest,
`/sdcard/Android/data/com.ethannicholas.mkdd/files/vr.txt`, which `adb push` updates
between runs). One `key value` per line, `#` starts a comment; all optional. The values
shown are this game's defaults:

```
units_per_metre 100      # game units per real metre: sets the apparent size of the world
near_m 0.1               # near and far planes in metres
far_m 4000
hud_scale 0.5            # how much of the field of view the HUD's frame occupies
hud_distance_m 4         # how far in front of the camera that frame stands
hud_height_m 0           # how far above the forward axis its centre sits
world_pitch_deg 18       # the chase camera's downward pitch taken out of the world, in degrees
follow_camera_pitch 1    #   1 follows the camera's own pitch in a race instead
fp_x 0                   # where your eyes sit in the driver's seat, in the kart model's
fp_y 105                 #   units (100 a metre): left, up, forward
fp_z -10
fp_hide 45               # draws this near the driver's head are not drawn
eye_scale 1.4            # eye resolution, times what the headset recommends
msaa 4                   # antialiasing samples per pixel in stereo
transition_s 1           # seconds the switch between flat and stereo takes
theater_stereo 0         # 1 shows the flat screen as a 3D picture (a second render pass)
```

## Running on an Apple Vision Pro

New, and not yet played on a device.

### Requirements

- A Mac with Xcode 26 (with the visionOS SDK), CMake, and Python 3.
- An Apple developer account signed in to Xcode. The build signs with the team in
  `$GCN_APPLE_TEAM`.
- The Vision Pro paired with the Mac (Xcode → Window → Devices and Simulators) and in
  developer mode (Settings → Privacy & Security → Developer Mode).

### Build and install

With your disc image in `rom/`:

```sh
GCN_APPLE_TEAM=<your team id> ./build-visionos.sh --install --push-image
```

The first build also builds [ANGLE](https://chromium.googlesource.com/angle/angle) (OpenGL
ES on Metal) from WebKit's sources, which takes a few minutes. `--push-image` copies the disc
image into the app's Documents folder, which takes several minutes over Wi-Fi. You can
instead choose it from the Files app in the app's launcher window. The memory card, the
shader cache, `vr.txt` and the logs live in the same folder, which the Files app shows under
*On My Apple Vision Pro*.

### Playing

Open **Double Dash Recomp**. The game runs in a full immersive space; leaving it closes the app.

With **PlayStation VR2 Sense controllers**, the buttons are as on the Quest: the Sense
controller's two face buttons on each hand are the Touch controller's A and B on the right
and X and Y on the left. With a **gamepad** (Xbox, PlayStation or MFi), the buttons are laid
out as on the desktop, and a right stick click switches the driver's seat on and off.

`vr.txt` in the app's Documents folder takes the same settings as on the Quest, and one of its
own: `render_quality` (0 to 1, default 1) sets how many pixels the headset gives each eye.
On the Vision Pro, `eye_scale` defaults to 1.

## Controls

| GameCube | Keyboard | Controller |
|---|---|---|
| Control stick | Arrow keys | Left stick |
| C stick | – | Right stick |
| A / B / X / Y | X / Z / C / S | A / X / B / Y |
| L / R | Q / W | Triggers |
| Z | D | Right shoulder |
| D-pad | I / J / K / L | D-pad |
| Start | Return | Start |

Escape quits.

### Wheels, pedals and other controllers

Any device Windows sees as a game controller -- a wheel, pedals, a shifter, a button box,
a flight stick -- can be bound in a `controls.txt` in the directory you run the game from
(or the file `--controls=<path>` names). To find what to write for a control, run

```powershell
.\build\mkdd.exe --list-controls
```

It lists the devices, and prints a line for each key or button you press and each axis you
move, such as `"Simucube 2 Pro" axis 0` or `"Button Box" button 4`. Put that after a pad
control and `=`:

```
# GameCube control = source [options]
stick_x    = "Simucube 2 Pro" axis 0 gain 2   # full lock at half the wheel's rotation
A          = "P1000" axis 1                    # accelerate
B          = "P1000" axis 0                    # brake
R          = "Simucube 2 Pro" button 5         # right paddle
X          = "Simucube 2 Pro" button 4         # left paddle
Z          = "Button Box" button 0
start      = "Button Box" button 1
dpad_up    = "Simucube 2 Pro" hat 0 up
stick_up   = "Button Box" button 2             # menus
stick_down = "Button Box" button 3
```

- **Pad controls:** `A B X Y Z start`, `dpad_up dpad_down dpad_left dpad_right`, `L R`
  (the analog trigger, which also clicks once pushed past its threshold), `stick_x stick_y
  cstick_x cstick_y` (a whole axis), and `stick_left stick_right stick_up stick_down` and the
  same for `cstick_` (one direction each).
- **Sources:** `key <name>` (`key X`, `key Return`, `key "Left Shift"`), `"<device>" button
  <n>`, `"<device>" hat <n> up|down|left|right`, and `"<device>" axis <n>`. Bound to a
  button or trigger, an axis's whole travel counts, which is what a pedal resting at one end
  needs. `axis <n>+` and `axis <n>-` are each half of an axis, from its centre out. If two
  devices have the same name, the second is `"<name> #2"`.
- **Options:** `invert` (for a pedal that rests at the top of its range: `--list-controls`
  suggests it when it sees one), `deadzone <fraction>`, `gain <factor>`, and `threshold
  <fraction>` (where a button counts as pressed; 0.5 by default, 0.85 for L and R).

The game ignores small stick movements and reaches full lock before the stick's end, so a
bound stick axis starts just past the game's dead zone, as soon as your wheel leaves a
0.5% dead zone of its own (`deadzone` changes that), and reaches the game's full lock at the
binding's full lock. `stick_x_min`/`stick_x_max` (and `stick_y_`, `cstick_x_`,
`cstick_y_`) override where that is, in the pad's units from the centre.

A control can be bound more than once. The keyboard and gamepad defaults above keep
working, except on a device that `controls.txt` names. Problems with the file are printed
when the game starts, and those lines are skipped. Joysticks are read even when the window
isn't in front.

#### Force feedback

A wheel with force feedback gets it on the device that `stick_x` is bound to:

- **A stop at full lock.** The game's steering reaches full lock where `gain` puts it:
  with `gain 3` on a 900° wheel, at 150° each way. Past that point the wheel pushes back
  hard and is damped, so you feel the end of the steering instead of turning on into
  nothing. With `gain 1` there is no stop from the game; set your wheel's own rotation
  instead.
- **Centring**, light when standing still and in menus, firmer with speed and cornering
  load, and lighter in a slide.
- **The road**: bumps under the front wheels and knocks from collisions.
- **Rumble**, whenever the game runs the controller's rumble motor (a gamepad rumbles too).

To feel it without the game, and see where full lock is, run

```powershell
.\build\mkdd.exe --test-ffb
```

Each part has a strength in `controls.txt`, as a fraction of the wheel's strongest force:

```
ffb_gain = 0.4          # everything except the stop (default 0.4)
ffb_stop = 0.8          # the stop's force (default 0.8); not scaled by ffb_gain
ffb_centre = 0.35       # centring at speed (ffb_centre_rest 0.1 standing still)
ffb_damper = 0.1
ffb_road = 0.3
ffb_jolt = 0.5
ffb_rumble = 0.3
```

Also `ffb_load` (0.3, extra centring per g of cornering), `ffb_slide` (0.6, how much a
slide lightens it), `ffb_speed_kmh` (100, where centring is at its firmest),
`ffb_stop_stiffness` (40) and `ffb_stop_damper` (0.6). `ffb = off` turns force feedback
off, and `ffb_device = "<device>"` sends it to another device. Force feedback ramps up over
the first second and a half, so a wheel left past the lock is brought back, not thrown. On
a direct-drive base, start with the wheel's own strength turned down.

## Repository layout

- `gcn-recomp/`: the shared recompiler, GameCube runtime, renderer and frontends, as a
  submodule. Nothing in it is specific to this game.
- `analysis/`: function and data layout of `main.dol`, in decomp-toolkit's format.
- `recomp/`: the tables that steer the recompiler for this game.
- `src/`: the game's half of the headset and the motion rig: when to be in stereo, the
  world's scale, what the eyes leave out, and the kart's motion.
- `tools/`: `analyze.sh`, which regenerates `analysis/`; `name_from_map.py`, which names
  its functions from the linker map on your disc; `dis` and `disraw`, which print
  disassembly from it and from your extracted executable.
- `docs/dev/`: working notes.
