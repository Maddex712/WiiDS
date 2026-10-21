# Eclipse companion compatibility

Claude's Sunshine lower-screen design and drawing helpers are retained. The
Eclipse-only copy has these compatibility changes, reproducible through
`Prepare-App.py` (`adapt_companion`):

- BSE adds application context 10 (`include/BetterSMS/application.hxx`,
  `CONTEXT_DIRECT_SETTINGS_MENU`); Eclipse adds context 11
  (`include/menu/character_select.hxx`, `CONTEXT_CHARACTER_SELECT`). Both use the
  existing sky backdrop and blue message-window styling with truthful captions.
  They do not show invented gameplay counters while Mario is not in a stage.
- The real native fader still controls visibility, including transitions.
- Eclipse's custom stage IDs start at 61 (`include/stage.hxx`). Such stages use
  the same course brochure, original-art counters and FLUDD layout, with the
  real native area name when present, otherwise the actual scene archive
  basename (underscores displayed as spaces). Unknown areas borrow no retail
  postcard or area plate, and show no retail eight-episode/held-Shine slots.
- Original areas retain their original mapping. No controls, dimensions,
  original course drawing helpers, art loading or animations were redesigned.

`Test-Companion.py` compiles the actual Snapshot/Area source in a small host Java
fixture: seven checks cover tutorial, a custom stage with a retail-looking
basename, available native area text, original Bianco, unknown scenes, menu and
null snapshots. Preparation reproduces the adapted file byte-for-byte. This is
source compatibility evidence, not an on-device visual check. Main-task APK
rebuild/install and live evidence are separate.

Original Sunshine companion remains SHA-256
`ec945ac75f46669c534f7912b45a4211376cddec8cc27d7840b73e5d74bd3d9e`.
Adapted Eclipse companion SHA-256
`530fcbe35324ee7ca04967ebf43840c5109b757a9bc9c9084cefd381bade712a`.
Private fixture receipt: `build/eclipse-thor/companion-check/verification.json`.
