# Sunshine upper HUD anchoring (local, unpublished)

Two draw-time placement problems were isolated on 2026-10-21. The original
Sunshine runtime and profile are the scope; Eclipse and the published beta.4
artifacts remain unchanged by this task.

## Evidence and root causes

The owner's `game_6.szs` / `scrn/standard_1.blo` defines:

| Pane | Local bounds x,y,width,height |
| --- | --- |
| lives root `m_0` | 223,74,10,10 |
| MARIO lettering `m_tx` | -12,-34,72,34 |
| life root `l_0` | 500,50,10,10 |

The prior Thor helper put the lives **root** at six pixels, although the name
extends twelve pixels left of it. That placed the name's left edge around -6
and clipped it. It also computed fractional widescreen padding while sms_gx
rounds the widened EFB to an even width (854 at 16:9, hence padding107).

The life meter's authored 40-frame Bezier uses start(-150,+180),
control(-10,+10), end(0,0). Those values are retained. In the default centre
HUD mode, the whole path previously remained in the old centred 4:3 space.
The additional draw-only right padding now places the complete path in the
widened right-hand space, from its first frame. The optional `edges` mode
previously classified the current animated pane centre: the life root crosses
its two-thirds threshold and changes anchor mid-animation. Its semantic `l_0`
identity now keeps the right anchor for the whole path in that mode.

## Change

- `src/port_include/port_thor_hud_layout.h`: exact even-EFB padding and pure
  placement helpers, C++03 compatible.
- `android/thor_snapshot.cpp`: lives helper accepts actual child artwork x;
  additional life helper supplies the constant draw offset.
- `decomp-patches/zzz-thor-01-hud.patch`: uses the actual `m_tx` bound, translates
  the life group only during split-HUD drawing, then restores both local pane
  offsets. Original visibility, timings and Bezier control points remain.
- `decomp-patches/zzz-thor-02-stable-hud-anchors.patch`: only on Android split HUD,
  the life root reports a right-side anchor to the existing widescreen mapper.
  No generic GX renderer changes were required.

The new screen-space left edge of the lettering is six native pixels in 4:3,
16:9 centre mode and 16:9 edges mode. The life path gets the same fixed right-side
translation throughout its stock animation. This does not remove its original
150-pixel diagonal motion or change gameplay health behavior.

## Host verification

`Test-HudAnchors.py` compiles the actual native helper bodies, original stock
Bezier routine and actual renderer offset routine into a small Windows host
executable, using owner-disc pane bounds. The baseline failed; all 168 assertions
pass after the repair. A separate fixture reapplies the complete current patch
chain for both affected game files: all 10 applicable patches pass. Source proof
and binaries stay private under `build/sunshine-hud-fit`; this is not an Android
build, device playtest or rendered-pixel assertion.

Run from the workspace:

```powershell
& C:/Users/super/AppData/Local/Programs/Python/Python312/python.exe experiments/sunshine-thor/Test-HudAnchors.py
$env:PATH = 'C:/Program Files/Git/usr/bin;' + $env:PATH
& .tools/host-tools/cmake/data/bin/cmake.exe --build build/sunshine-thor/android-native-wiids --target sms smsbootstrap SDL2 --parallel 8
```

The existing CMake build regenerates the patched copies; do not modify
`decomp/` itself. The host assertions establish the coordinate calculation and
stock Bezier preservation; they do not establish how the whole animation looks
on the Thor.

## Installed candidate evidence

The combined beta.5 candidate (version 905, APK SHA256
`5cca806cc6ce8f07cc718c5f072673a7ebe8e221af4e8136c1bde1123f1588cb`)
has now been built and installed by the parent task. A real Airstrip capture
shows the complete MARIO lettering. Evidence is private under
`build/evidence/sunshine-polish-airstrip-settled-upper.png`; installation and
save-file comparison are recorded in `build/sunshine-polish/installed-verification.json`.
The three checked Original Sunshine save files were unchanged at that checkpoint.
The later `build/sunshine-polish/save-verification.json` also matches all three
save files and `settings.txt` against their backups after the bounded live checks.

Title video now confirms actual bird motion, and a game sleep/wake check
restored both screens. The complete life-meter/damage animation remains
unverified. A settled HUD image does not prove the meter's complete moving
path, and a short sleep/wake check does not establish long-background recovery.
No all-course, long-session, audio, haptic or
sustained game-FPS claim follows from this anchoring check. The change affects
Original Sunshine; Eclipse's native runtime is unchanged. See [POLISH.md](POLISH.md)
for the companion framing and draw-cadence evidence.
