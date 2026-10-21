# Separate Android race test driver

This signed instrumentation is a local test tool. It is never included in the game APK. It sends bounded Android key and joystick events through the existing Odin Controller device and SDL handler; it does not change gameplay rules or fabricate race state.

Build `scripts/Build-DriverProbe.ps1` and `scripts/Build-GameDriver.ps1` with the configured local JDK/SDK. The driver and game must use the same signing certificate. Install the resulting `build/game-driver/thor-driver.apk`, forward TCP 32991 to `localabstract:thor-mkw-driver`, and run `am instrument -w dev.thor.mkw.driver/.Driver` against the Thor.

For the optional Retro Rewind build, use `am instrument -w -e product retro_rewind dev.thor.mkw.driver/.Driver`. Omission selects `base`. The native probe uses `RTLD_NOLOAD` to inspect the already loaded standalone runtime; it never initializes another game library. `state` also returns the course archive identity copied alongside that snapshot on the same thread.

`scripts/Drive-Thor.py` sends one command per invocation:

- `probe`: SDL focus, open devices, buttons, axes and actual PAD mappings. It never calls PADRead.
- `probestage 0` / `probestage 1`: isolate SDL button reads from PAD mapping getters during startup diagnosis. The mapping getters can load a saved mapping, just as normal PADRead and the settings overlay do; do not infer an input fix from running a probe.
- `heldprobe 96` / `heldprobe 97`: hold the printed A / B button's Android code briefly, inspect it, and release it.
- `input 250 0 0 96`: press A for 250 ms and release it. Use about 2 seconds of neutral settling between menu selections.
- `input 2000 0.6 0 96 103`: bounded acceleration, steering and right shoulder; releases all inputs afterward.
- `state`: read the same copied native race snapshot used by the lower map.
- `mapstats` / `mapstats reset`: read/reset lower-view display callbacks, draws and callback intervals. These measure Android map presentation cadence, not the guest game's frame rate.
- `lower off` / `lower on`: invoke the presentation controller's normal stop/start methods to check HUD fallback. This is not a physical display-disconnection test.
- `stop`: finish instrumentation. Remove the forward and uninstall `dev.thor.mkw.driver`, then launch the ordinary app.

Verified bindings for this installation in OEM Odin mode (`flip_button_layout=0`): Android 96 = printed A = PAD A, 97 = printed B = PAD B, 99 = X, 100 = Y. SDL calls these south/east/west/north respectively; those names are not the Thor's printed positions. The earlier east=A / south=B configuration was wrong and is superseded. Start/108 = pause, left shoulder/102 = item, right shoulder/103 = hop/drift; B brakes. The chosen in-game drifting mode determines drift behavior.

Do not count Android gfxinfo UI timings as game frame-rate measurements. Record real captures and copied race-state evidence separately. The regular installed release runs without this driver.

The probe's `graphics` object copies the runtime's locked presentation history and atomic interpolation counters. `present_fps` measures presentation, while `target_fps`, interpolation samples and transform matches establish the configured interpolation path. Guest simulation remains 60 Hz. Cross-field values are not one atomic snapshot. Final-release checks additionally measured the upper SDL SurfaceView's actual SurfaceFlinger presentation intervals and switched the real Display menu between 60 and 120 FPS. The final normal-launch races used kernel events from the identified Odin input device, with instrumentation uninstalled.
