# WiiDS 1.1 Thor preview test

This is a local, owner-signed, nondebuggable preview with the existing dev7
achievement browser and motion behavior. The experimental Casual earning client
is absent. Version code stays 906 for the existing development installation.
Local previews are being tested in place on the owner's Thor. No public release
has been performed. The current acceptance record is
[`release/1.1-THOR-POLISH.md`](release/1.1-THOR-POLISH.md).

## Current lighting follow-up: dev28 on Thor

- [x] Restore the original town Before/After animation during Bowser's attack.
- [x] Restore all five Toad palettes; confirm actual relocated MEM2 buffers.
- [x] Reproduce the correction in the clean build without capture hooks.
- [x] Original Galaxy existing-save Gateway rendering and lower planet check.
- [x] Selected-file idle for 235 seconds, then normal scene loading and play.
- [x] Both games exit normally; final 48 external files unchanged; helpers removed.

The earlier dev25 file-menu allocation panic did not recur in these checks.
Its cause remains unestablished; no claim of an allocation-failure fix is made.
The attack retains the warm rim light also visible in the retail collection
reference. This is a bounded scene comparison, not pixel-parity certification.

## Historical nightly review: dev21

- [x] Relative touch, distant re-touch and shared cursor handoff.
- [x] Actual ARM64 mailbox/camera/permission/gamepad fixtures and 36 shaders.
- [x] Explicit Camera-mode native horizontal turn and vertical restriction.
- [x] First-play five-second hint; no title/storybook hint or resume reset.
- [x] One-tap settings and cross-row drag cancellation.
- [x] Attack scene compared in Original/Smooth; intentional warmth remains.
- [x] All four base games and four existing mod profiles reviewed within the
  bounds recorded in [the game review](release/1.1-GAME-REVIEW.md).
- [x] Kart shortcut Home/re-entry retains the original native process.
- [x] MSMBWii lower title correctly names the mod.
- [x] Final 48 external files and five original Galaxy NAND payloads unchanged.
- [x] Normal library return; no embedded game/import process; task helpers removed.
- [ ] Matched retail comparison of attack-scene excess lighting intensity.

## Included

- Per-game Android shortcuts, including a separate Retro Rewind shortcut.
- Shared save manager: dated backup, export, restore, previous-save backup and
  interrupted-restore recovery.
- Dolphin in-game save imports for all eight default profiles, using supported
  raw Wii saves/Wii exports and Sunshine/Eclipse GCI entries; automatic backup
  and confirmation before replacement.
- Galaxy Original/neutral Smooth/optional Smooth + glow Picture choices.
- NSMB/MSMBWii rendering guard: framebuffer copies stop if frame startup did not
  succeed after its bounded retry. The next GX call can retry after resume.
- Native overlay lifetime check: skip menu drawing when Android surface changes
  leave ImGui without a new frame/current window. The recorded native 0x40 fault
  was reproduced and mapped to this menu draw-list access on the Thor.
- Options rows fit the added save and shortcut entries.
- Full-screen save tools use the existing menu art, support controller input and
  show selected-game status on the lower screen.

Dev15 passed 13 NSMB sleep/wake cycles, in-game achievement-browser return and
four MSMBWii startup sleep/wake cycles without a new crash artifact. The old
overlay segfaults against real ImGui; each corrected engine passes 58 Android
checks. The separate retry guard remains useful but was insufficient on its
own. The owner subsequently accepted extended gameplay and controls/gyro; no claim
is made that every pause/resume crash has been fixed.

October 25 follow-up: the owner confirmed physical controls, NSMB tilt/shake and
longer play are working well on dev15. Actual Galaxy checks passed lower-screen
menu pointing and confirmation, Planet rotation, touch-to-stick handoff, four
sleep/wake cycles and Home/shortcut return. Kaizo Gardens collected three star
bits using lower touch alone; pointing also worked after sleep/wake. The original
Gateway starting zone has no placed star bits, so that collection result belongs
to Kaizo's scene and the shared pointer implementation. All 48 checked external
save/settings files remained byte-identical. See the local
[1.1 release notes](release/1.1-RELEASE-NOTES.md). The code1100 release candidate
has been built locally. The Thor now runs dev21 with the asset-install fix,
Dolphin imports and Galaxy Picture controls. The actual NSMB same-save
file-picker import passed with all 48 accessible save/settings files unchanged.
The 124 import checks also passed in isolated actual Android profile storage.

## Suggested test order

1. Finish any running game normally. Before installation, retain the installed
   APK and fresh readable saves/settings backups using the established update
   procedure. Install in place; do not uninstall or clear app data.
2. Check that the achievement browser still shows the existing account/library.
   Check NSMB tilt and shake as usual.
3. From a freshly restarted WiiDS, open **Options > Save manager**, choose NSMB,
   create a backup, and export it. Confirm the local backup appears in the list.
4. Restore that same backup, then start NSMB in a fresh game process. Confirm
   save slots and progress. Try a second game with an existing save. The former
   save remains in a dated **before restore** backup.
5. Set Cocoon as the default launcher. In WiiDS, choose **Options > Game
   shortcuts > Retro Rewind > Add shortcut** and accept in Cocoon. Test it from
   an idle WiiDS, then another game's shortcut. Imported/setup data is required.
   Game exit currently returns to the WiiDS library.
6. In NSMB, save progress first, then test Home/recents, screen off/on, and return
   to the same game. Repeat in MSMBWii if used. Check both screens, input and
   continuing gameplay. The test should not interrupt an unsaved run.
7. For Galaxy's existing lower-screen pointer, switch between Planet and Star
   Pointer, collect visible star bits, release the touch, and check stick handoff.
8. In Save manager, choose the matching original/mod and import a supported
   Dolphin in-game save. Verify progress after loading it, and keep/export the
   dated before-import backup. See [formats and region limits](SAVE_MANAGER.md).
9. In Galaxy's **Display > Picture**, compare Original and Smooth. Added glow
   is a separate optional choice. Retain a matching gameplay screenshot if the
   reported washed-out appearance persists with neutral colors.

If a game crashes, retain its current run's logs and describe the action that
preceded it. The earlier crash remains historical evidence, not a result from
this preview.

## Still planned

Additional save formats/regions, favorites/recently played, broader controller-setting
consistency and measured graphics/battery presets remain planned. Existing last
selection behavior and control mappings have not been redesigned by this pass.
