# Galaxy attack-scene lighting investigation

Owner request: correct the excessive red/orange character lighting during
Bowser's opening attack. No publication is authorized by this work.

## Reproduction and evidence

The scene was reproduced on Thor in Kaizo's shared Castle Gardens stage using
the existing zero-star file, without saving progress. Dev22 and dev23 still
showed excessive warm character lighting. Original and Smooth exhibit it
independently of the optional picture glow.

A temporary dev23 diagnostic captured one bounded GPU frame before and after
the attack, including light positions/colors, channel controls, TEV registers
and representative character vertices. The diagnostic was removed afterward;
the clean dev25 library contains neither its log prefix nor property name.
Captures, private disc-table inspection and native logs stay under the ignored
`build/feature-1.1/galaxy-attack-lighting-2026-10-25` directory.

The captured post-attack player and nearby NPC lights match the disc's volcano
eruption preset. Its broad light volume becomes active through a stage switch.
Warmth is intentional scene data. The initially inspected castle-light preset
is used by other nearby light volumes and was insufficient to identify the
actual state at the reported location.

## Corrections

`gpu::Builder::fillLights` rescaled positions farther than 100,000 units to
10,000 units from the view origin. These values represent points, not direction
vectors. The shader subsequently subtracts each character's view-space
position; moving the light first changes that direction. The attack key light
at `(100000, 27000, -5500)` became approximately `(9641, 2603, -530)`. For a
character at view Z -5000 this incorrectly moves the key light from behind its
front-facing normal to ahead of it. Retain the exact GX position and normalize
the light-minus-vertex vector in the shader, as before.

The production ARM64 packing regression fails 13 of 20 checks with the old
code and passes all 20 with the correction. The erroneous front-facing diffuse
contribution changes from 0.408547 to zero. Fifty-seven representative captured
vertices independently show changes up to 0.330933 in the front-facing light
direction component.

A separate signed-diffuse error was also corrected: GX_DF_SIGN must retain a
negative dot product until the accumulated lighting is clamped. The old emitted
expression fails 36 of 108 numeric cases; the corrected expression passes all
108. This correction alone did not resolve the visible scene on dev22 and is
not presented as its sole cause.

No game light preset, disc asset, save or achievement source was edited.

## Build and verification status

Clean dev25 native library:
`5980f14df47ddab0495d7841e3911133e07ccfcb0e72b0f86b0278f999ce5ea9`.

Owner-signed/nondebuggable dev25 APK:
`5ad7d5fb6eb808f4e0dd01f0236cd09430fc16a6daeb624c089fec1e85a01a19`.
Other 24 native libraries and all 1474 game/menu assets match the accepted
dev15 profile; 91 paused earning-client files retain their hashes.

An incremental dev24 build accidentally retained the diagnostic object after
restoring an older-timestamp source. Its transfer was interrupted before
installation, and the superseded APK was removed after hash verification.
The incremental helper now forces explicitly selected sources to rebuild.

Dev25 still visibly reproduces the orange attack scene. Its clean renderer
corrections alone do not resolve the owner's report. One first-load allocation
panic also occurred; a second launch reached the scene. The diagnostic build
now records the requested allocation and native backtrace if that repeats.

## Material-animation boundary investigation

Dev26 shows valid ColorChange frames (0 through 4) and material target IDs, but
`getMaterialAnm()` returns null for every inspected Toad and all 29 town
materials. Toads consequently retain the same default red palette. The town's
Before/After BRKs contain different real RGB values (white versus 255/180/200).

The production getter contains a stale `< 0xC0000000` pointer cutoff. The
integrated port moved cached MEM2 to exactly 0xC0000000; valid buffers in that
bank are rejected. Restore the normal getter's stored pointer rather than
filtering by an obsolete address map. The actual production-header ARM64
regression fails five of eight cases before the change and passes all eight
afterward, including relocated MEM2, both uncached mirrors and null.

Dev27 confirms the raw Toad buffer at 0xC2172048. All 46 inspected Toads now
produce the five distinct palette values expected from the disc data. The
town's buffer at 0xC2878D38 applies Before RGB 255/255/255 and After RGB
255/180/200. The physical scene changes to its pink tint, with varied Toad
colors instead of identical red palettes.

Clean dev28 removes the temporary material diagnostic and also reproduces the
corrected scene on Thor. Its library contains neither material/GPU capture log
prefix. Original Galaxy's existing Luigi file loads Gateway, with healthy
upper rendering and the live lower planet; no gameplay save was made. Both
games returned normally to the library. The three task-only device fixtures
were removed. Final readback preserves all 48 accessible external save/settings
files byte-for-byte; internal unreadable files are outside that count.

Clean dev28 native SHA-256:
`c38568d95106051fa50db7d4c1b9221f0cb4e0fda3457d40163fbbdd664ff2e0`.
Installed owner-signed/nondebuggable dev28 APK:
`508ad5e29b2378cdcaf25852eb4898376ed9364c7fdf71f8ef69a0a75b2b2c02`.
Local release-version APK, not installed or published:
`dd89b9c7c667a8f405c45afe3d1dfee166eaf2bd180c8915ff1df2420f6ed35e`.
All 25 native libraries and 1474 game/menu assets are identical between those
two final APKs. Release audit: 25 libraries, 39 source windows, zero findings.

The earlier dev25 allocation panic occurred in the selected-file menu, before
any town-scene startup, rather than during a confirmed scene load. Dev28 stays
in that menu for 235 seconds and then loads normally; dev26/dev27 also load
successfully. No failure repeats in the final scoped logs. This does not prove
the historical allocation failure fixed; its cause remains unestablished.
Bounded native allocation request/backtrace reporting remains for diagnosis.

## References

- [Dolphin lighting generator](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/LightingShaderGen.cpp)
  preserves the submitted point position when calculating the light direction.
- The owner's SDK sources define GX_DF_SIGN and channel control packing.
- [Captured retail collection gameplay](https://www.gamerguides.com/super-mario-3d-all-stars/guide/super-mario-galaxy/introduction/controls)
  provides a visual check that warm character rims are present in the original
  scene. This is not a claim of pixel parity with a matched Wii frame.
