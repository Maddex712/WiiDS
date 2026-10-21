# Original Sunshine presentation polish

2026-10-21 beta.5 candidate, version 905. This pass concerns Original Sunshine.
Eclipse's native runtime and the other twelve native library entries remain
byte-identical to beta.4; all 1452 packaged data assets are unchanged. Private
package and installation receipts live under `build/sunshine-polish`.

## Companion motion

`ThorSunshineScreen.Board` now requests display-aligned Choreographer callbacks,
with `ThorSunshineMotion` imposing a 60 Hz ceiling. Snapshot polling remains 33 ms;
the UI can animate between state samples. Cloud travel uses elapsed time,
fades/marker following use elapsed-time smoothing, and callbacks are removed
when the window is hidden or detached. Reattachment resets the frame deadline
instead of creating a catch-up burst. These are presentation changes; game
logic and the owner's frame-rate setting are unchanged.

`test_motion.py` compiles the actual pure-Java production helper and reports 7213
assertions, followed by source-contract checks. It covers 59.94/60/90/120/144 Hz
synthetic panels, 30 Hz limits, duplicate timestamps, stalls, reattachment,
rate-independent smoothing and cloud wrapping. These are host simulations,
not observations of those physical panel modes or real Home/sleep behavior.

The stable 120-interval device windows in
`build/sunshine-polish/pacing-comparison.json` give:

| Lower Android draw intervals | Before | Installed candidate |
| --- | --- | --- |
| Median |50.025ms|16.667ms|
| Mean-derived draw rate |20.083/s|59.899/s|
| Maximum within stable window |50.232ms|16.905ms|

The short after window contains a 109.295 ms gap; the other before window includes
a 332-second background interval. Neither is evidence of stable foreground
cadence. The steady-window result is not guest FPS, distinct sky image FPS,
or a sustained thermal/performance claim. See [SKY_CAPTURE.md](SKY_CAPTURE.md)
for the separate native capture pipeline and pending title measurements.

## Static overview framing

The old map accepted downward-facing collision and submerged terrain, then
framed its vertices at 0.4%-99.6% percentiles. The owner's Airstrip map also has a
ten-triangle dry island about 60000 world units away; that small component still
survived the former trimming. The result framed a large sea rectangle around a
tiny main island.

`ThorMapFraming` rejects downward, degenerate and nonfinite triangles and tests
submerged floor centroids against locally covering water surfaces. Local testing
matters: an elevated pool must not remove unrelated lower terrain elsewhere.
A 1%-99% centroid core plus 25% peripheral allowance excludes sparse remote
geometry; all vertices of the retained terrain then define the actual bounds.
The fit uses 90% horizontal/88% vertical space and a 1000-unit minimum world span
to bound extreme zoom. The map and Mario marker share the same scale/offset.
Water remains background context and does not enlarge the frame.

This is a presentation adaptation: the remote island is omitted from the
overview; collision data alone does not prove that it is decorative or
unplayable. Collision and player positions are not changed. The test does not
establish framing quality for every area.

`test_map_framing.py --owner-airport` compiles the actual Java helper and reports
817 assertions. It covers local pools, ceilings/sea floors, remote outliers,
substantial separate islands, padding, marker-transform consistency, zoom
limits, deterministic results, input preservation and invalid-data fallback.
The owner's 2358 exported Airstrip triangles produce 1505 framing triangles and
mainland bounds X [-10134.20,9000.00], Z [-9453.92,4642.05]. Owner collision is
decoded in memory and passed through an ephemeral private fixture; no collision
fixture is committed or redistributed. A real Thor Airstrip capture confirms
the enlarged main island in the installed candidate.

## HUD placement and verification limits

[HUD_FIT.md](HUD_FIT.md) records the stock font-pane and life-meter Bezier
anchoring repairs, 168 host assertions and ten-patch-chain checks. Real settled
Airstrip pixels show the complete MARIO lettering. Actual title video confirms
bird motion, and a bounded sleep/wake check restored both game displays.
The later `build/sunshine-polish/save-verification.json` verifies all three
Original Sunshine save files and `settings.txt` still match their backups.
The complete life-meter/damage animation remains unverified. Short lifecycle
checks do not establish long-background recovery; host lifecycle assertions are
not a substitute for those checks. There is no all-course sweep, long-session, audible-audio,
haptic or sustained upper-game-FPS claim.

Run the focused host checks from the workspace:

```powershell
python experiments/sunshine-thor/test_motion.py
python experiments/sunshine-thor/test_map_framing.py --owner-airport
python experiments/sunshine-thor/Test-HudAnchors.py
python experiments/sunshine-thor/Test-SkyCapture.py
```

The first two use JDK21; the HUD/sky harnesses use the existing local native
toolchain. `--owner-airport` requires the owner's extracted airport0 archive.
These commands do not navigate the Thor or alter game saves.
