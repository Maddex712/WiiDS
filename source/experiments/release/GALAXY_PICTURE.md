# Galaxy picture modes — local 1.1 preview

The Reddit feedback reported washed-out textures without a gameplay image.
Investigation found an actionable presentation issue: the existing **Smoothing**
switch coupled FXAA to additional bloom and a 12% saturation boost. For a flat
80% gray input, that processing produced 82.8% gray. This demonstrates a change
to image brightness; it does not establish that it caused the reporter's scene.
The Thor offers an ordinary UNORM/sRGB-nonlinear Vulkan surface, so the observed
device does not reproduce the hypothetical sRGB-only surface fallback.

The same Display row is now **Picture**:

- **Original**: unchanged fast presentation, without added smoothing or color grade.
- **Smooth**: FXAA, with no added bloom or saturation adjustment.
- **Smooth + glow**: FXAA plus the previous bloom/saturation presentation.

Existing `post_process=1` now selects neutral smoothing; `0` remains Original,
`2` selects the optional old enhancement, and `-1` keeps the engine default.
All three values round-trip through the Thor settings bridge. This does not
remove the game's own lighting, bloom, fog or texture effects. The lower-screen
planet renderer is unchanged.

The Vulkan presentation pipeline declares a four-byte fragment push constant.
The presenter supplies the enhancement flag on every draw, including the fast
path's compatible layout. Only mode 2 enables the existing color block. No new
pipeline object or resource lifetime is introduced.

`test_galaxy_picture.py` compiles and validates all three real presentation
shaders as SPIR-V. It evaluates the exact shader color block against 216 flat
RGB samples: neutral preserves them, optional glow matches the former result.
This CPU calculation does not test edge quality or GPU precision. The complete
ARM64 engine compiled/linked from the accepted integrated native tree (its
pre-build stripped binary matched dev15 exactly).

Installed dev17 on Thor: Galaxy process 24009 reached its title; Original,
Smooth and Smooth + glow were selected and rendered. Help text and controller
selection were inspected. The initial Smooth selection was restored. All 197
prewarm pipelines succeeded, and normal Return to WiiDS released textures and
destroyed the process without a scoped fatal/crash. The first startup capture
was taken during a bright title transition; the settled title is the relevant
comparison. Two early captures named `original-choice` / `original-title`
still show glow because right-arrow choices clamp; use `original-verified` /
`original-render` for the actual Original check.

Evidence: `build/feature-1.1/dolphin-import-2026-10-25/{native,picture-tests}`,
`galaxy-boot.log`, `galaxy-final.log` and `build/evidence/galaxy-picture-*`.
No claim that every washed-out texture or the Reddit reporter's scene is fixed;
a matching gameplay comparison remains needed if the symptom persists in
Original or neutral Smooth mode.
