# Private original NSMB Wii HUD assets

`Extract-ThorHud.py` decodes the owner's extracted clean USA Rev 1 assets into
ignored `build/nsmbw-thor/e1/lower-assets`. It checks the SMNE01 Rev 1 header and
pinned clean main DOL fingerprint first. The source-only script contains no game
assets; do not publish its PNGs, manifest, archive or extracted inputs.

Run from the workspace:

```powershell
& C:/Users/super/AppData/Local/Programs/Python/Python312/python.exe experiments/nsmbw-thor/Extract-ThorHud.py --archive build/nsmbw-thor/e1/ThorHud.tar
```

The decoder uses Pillow and the existing local private gclib checkout at
`reference/mkw-3ds/vendor/gclib`. No dependency source is copied or redistributed.
The optional tar has a `ThorHud/` root for the NSMB Android app's external-files
directory. It contains only the exact files referenced by the manifest plus the
manifest itself. Regeneration does not modify game source data or device state.

The schema-1 `manifest.json` exposes:

- `images`: `coin`, `clock`, `character0` Mario, `character1` Luigi,
  `character2` Blue Toad, `character3` Yellow Toad, `starCoin`, `starCoinOff`.
  Each entry includes `file`, `sha256`, `width`, `height`, original source and
  source SHA-256. All eight come from `Layout/gameScene/gameScene.arc`.
- `fonts.main`, `fonts.number`, `fonts.message`: native BRFNT atlas `file`,
  `sha256`, `lineHeight`, `ascent`, cell size, baseline and `glyphs` keyed by
  actual character. Each glyph has `x`, `y`, `w`, `h`, `left`, `advance`, original
  glyph index and decoded pixel hash. Sources are the three matching English
  `US/EngUS/Font` BRFNTs. The initial export contains 96/32/97 glyphs respectively.

For a glyph, draw the atlas rectangle at `(penX + left, topY)`, then advance by
`advance`. To use baseline coordinates, subtract `ascent` from the baseline Y.
Scale the rectangles and all metrics by the same factor. Missing characters must
use the same font's `?` glyph where available, not Android/system fonts.

Output pixels preserve original straight RGBA and intrinsic alpha. No generated
art, recoloring, added outline, shadow, scaling or background is baked. The main
font and number font are originally white glyphs; the portrait/coin artwork keeps
its original palette and transparent edges. No full-screen authored background
is claimed: gameScene contains sliced window textures, not a lower-screen scene.

This is a passive HUD asset set. It adds no reserve power-up, progress bar, touch
controls or gameplay behavior. Independent lower-screen placement is an explicit
presentation adaptation; it does not establish DS/3DS retail layout parity.

Verification in this session: the actual extraction completed, source/PNG/glyph
hashes and archive member coverage were checked, and the main/number font atlases,
Mario portrait and coin PNG were visually inspected. Device rendering and live
value correctness belong to the lower-screen integration verification.
