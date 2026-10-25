# Galaxy controls and character lighting follow-up

The owner requested relative lower touch, camera restrictions and investigation
of red/orange Mario/Toads during the opening Bowser attack. Read-only Thor capture
confirmed the character glow in PeachCastleGardenGalaxy while the town retained
comparatively normal lighting. No input was sent to the owner's open game.

## Causes and changes

Lower touch previously converted a finger's position directly to normalized
screen coordinates. GalaxyTrackpad now emits ordered relative deltas. The native
TrackpadMailbox applies them to the shared overlay cursor, preserving the anchor
after lifts, R3 centering and right-stick use. Begin does not move the cursor.
Multiple gestures arriving between frames retain their movements. Cancellation,
pause and settings discard queued motion; overflow refuses the gesture. Other
fingers do not replace the controlling finger. The Planet gesture path is intact.

CameraDirector previously added unbounded horizontal orbit and a large vertical
offset after calculating the game's camera pose. Its permission methods always
returned true. That entire pose override is removed. Horizontal right-stick
deflections now generate a native camera turn once, with neutral hysteresis;
all same-frame readers see the same request. The active game camera's permission,
collision, upside-down handling and demo/start/subjective restrictions apply.
Vertical camera position remains controlled by the game. Physical axes persist
between Android events so virtual-touch decay cannot re-arm a held nudge.

The vertex shader previously added unmodulated ambient and material-scaled lights,
then clamped the result. GX clamps accumulated ambient/lights and multiplies by
the material afterward. The correction applies to color and alpha channels.
This is a specific material/clamp correction, not a claim of complete GX lighting
equivalence or proof that the attack-scene glow is resolved. Existing signed
diffuse and specular/spot attenuation differences are separate findings, not
speculative changes in this preview. The current shader cache hashes full source,
so changed shader code gets a distinct entry without clearing owner caches.

Reference: [Dolphin lighting generator](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/LightingShaderGen.cpp),
checked against the local GX light/channel implementation. The two-line material
correction was independently written; no reference source was copied.

The disc's LightData also contains separate castle actor-lighting presets
before and after Bowser's attack. The latter uses yellow key light and red
ambient (Player key RGB 255/255/189, ambient 91/40/0; Strong key 204/200/150,
ambient 58/19/0). Warm character lighting after the attack is intentional data.
The remaining question is excessive intensity, not whether all warmth should
be removed. The data was inspected read-only from the owner's retained disc
archive; no retail asset was added to the app.

## Verification

Artifacts: `build/feature-1.1/galaxy-controls-lighting-2026-10-25`.

- Twelve actual Java gesture checks pass.
- Six numeric fixtures reproduce the old emitted GLSL expression failures;
  corrected expressions pass those fixtures and 405 scalar combinations.
- Actual ARM64 game compilation/link and signed nondebuggable dev18 APK build pass.
- The other 24 native libraries and 1474 presentation/game assets match dev15;
  all 91 paused achievement experiment source hashes remain unchanged.
- Actual Thor ARM64 execution passes 100144 mailbox/camera/concurrency checks,
  29 permission checks and 171 GamepadInput checks. The old permission fixture
  fails as expected. All 36 generated vertex-shader variants compile and validate.
- Relative slide, distant re-touch, R3 centering and shared cursor handoff pass
  on Thor. Kaizo Gardens collected a star bit using lower touch alone.
- With Camera explicitly selected on dev19, a horizontal deflection performs
  the native turn; maximum vertical deflection does not override camera height.
  The earlier dev18 Gateway capture did not record the selected stick mode and
  is not used as proof of Camera-mode restrictions.
- The attack scene was reproduced in Kaizo's shared castle stage. Warm actor
  color remains in Original and Smooth, independently of optional glow. This
  is not a retail pixel comparison or a claim that all excess intensity is fixed.
- Dev19's hint appears during the first five seconds of playable control, then
  hides its border, wording and hit target. Title/storybook do not display it.
  Same-PID Home/resume does not restart it. Controls > Lower screen remains
  available after expiration. Nineteen timing/lifecycle checks pass.
- Dev20 fixes first-tap menu activation and cancels cross-row drags. The old
  actual handler fails the regression; the corrected handler passes nine checks.

The owner subsequently explicitly authorized normal Galaxy exit and unattended
per-game review. Galaxy was exited through Return to WiiDS, and installation
proceeded from the idle library. All 48 accessible external save/settings files
matched the fresh baseline after dev18 and dev19 installation. Final nightly
readback and the other games' review are tracked separately. No public release.
