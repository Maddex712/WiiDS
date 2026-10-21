# Step 3: every game's art built on the device

`wiids/art` (C++, linked into `libwiidsdisc.so`) can build companion art from the player's own
imported disc. The current release also contains the owner's approved presentation snapshot;
the generated fallback remains available. `rom/WiiDsArt.kt` runs background preparation after
each import, and the Games screen can rebuild missing content.

| Art | Source on the disc | Output (app storage) | Check against the PC tools |
|---|---|---|---|
| NSMB / MSMBWii lower-screen HUD | Layout .arc, BRFNT fonts | external/nsmbw/ThorHud, external/msmbwii/ThorHud | 29 images + 3 fonts, pixel-identical |
| Sunshine lower-screen art | data/*.szs (RARC, BTI) in the ISO | external/sms/ThorHud | 148 images, pixel-identical |
| Mario Kart HUD (rows, timer, faces, ranks, font) | Scene/UI Race, Race_E, Font | files/WiiCompiled/ThorMap | identical (layout renderer port) |
| Mario Kart Wii-menu chrome + native settings art | MenuOther, MenuSingle, StaticR.rel colours | ThorMap/menu*.png, thor_settings/ | 105 images and every .rgba identical |
| Map faces, held-item icons | Race.szs | ThorMap/face_NN, item_NN | identical |
| Launcher sounds | sound/revo_kart.brsar | ThorMap/launcher-audio | all 7 WAVs sample-identical |
| Race maps (32 courses) | Race/Course/*.szs (model, KMP, KCL) | ThorMap/course_NN.png/.json | same frames; see below |
| Launcher cases | opening.bnr banners, game fonts, Sunshine title logo | ThorMap/launcher + artwork.json | new compositions |

- Layout baking (`art_layout.cpp`) ports menu_layout.py's renderer including TEV stages and
  reproduces numpy 2's float32/float64 rules and Pillow's integer blend; built without fused
  multiply-add so the phone matches the PC.
- Race maps (`art_maps.cpp`): course frame from the KMP and roof removal from the KCL exactly as
  race_map_bake.py, the relief/occlusion/shadow finish ported line for line. The course model is
  read directly (BRRES/MDL0 per noclip) and shaded with a per-pixel GX TEV evaluation of each
  material (stages, colour/konst registers, alpha test, blending). The 3DS exporter's
  hand-written per-course material rules are not reproduced, so a few surfaces differ from the
  old PC bakes (Rainbow Road shows the Earth below instead of the cloud layer). 32 maps in ~16 s
  on the PC.
- Cases (`art_cases.cpp`): Mario Kart (logo, Mario's kart, items, chequered strip), NSMB
  (port of compose_case_art.py), MSMBWii (band over the NSMB case), Sunshine (title logo over
  sky, sea and sand). Text uses each game's own fonts. Every case pair is optional now.
- The current public presentation bundle includes the owner-selected covers/discs,
  Wii Menu music and UI/HUD artwork. See `RELEASE-DRAFT.md` and its hashed
  `presentation/manifest.json`; the generators above remain available for missing
  or regenerated content. This supersedes the earlier generator-only art plan.
- Host test: `experiments/release/disc-test/build.bat` builds `wiids_art_tool`
  (`nsmb`, `sms`, `mkw`, `mkwaudio`, `maps`, `mapNN`, `casesmkw`, `casesnsmb`, `casessms`, `banner`).

The launcher also gained, for players without Mario Kart: "Your games" and buttons for the other
games in the fallback menu, and the Wii-menu launcher appears as soon as Mario Kart's art exists.
