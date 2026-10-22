# WiiDS 1.0 — first full release

Tag `v1.0.0`; app `1.0`, versionCode `1000`. One owner-signed, non-debuggable
ARM64 APK includes all seven entries. No Galaxy helper is required. Removed beta
release entries are not restored; their tags remain historical.

## Changes since beta.5

- Galaxy 1 integrated into WiiDS: isolated game/import processes, relocated
  memory, own-disc import, automatic lower-screen generation and verified copy-only migration.
- Galaxy live title backdrop, counters, textured GPU planets and live actor
  matrices/visibility. HUD/fonts/maps/models are generated from your disc.
- Galaxy Back menu with Quick Save, Controls > Buttons and Return to WiiDS; old dock removed.
- Galaxy texgen, retrace-clock, pointer and DSP mailbox lifetime fixes.
- Galaxy GPU texture allocations are released before device teardown, keeping
  decoded textures available for window resume and preventing the observed emulator exit crash.
- NSMB/MSMBWii Controller tab/Thor labels and WiiDS Options > Controller buttons.
- Galaxy case art, seventh-cover collage and setup card. Existing presentation,
  Sunshine polish, Eclipse/MSMB installers and official Retro Rewind route retained.

## Setup

Install the single APK and supply supported discs. Galaxy needs USA RMGE01 rev 0
and API 32+: see [Galaxy setup](GALAXY-SETUP.md). The other six entries retain
their existing setup and exact disc requirements. Galaxy 2 and wishlist games
are not supported. Update in place and keep backups; migration never deletes the helper.

The named source ZIP contains current source and exact curated presentation.
ROMs, cooked Galaxy data, generated Galaxy HUD/fonts/planets, saves, NAND/Mii,
private configs/keys and private backups are excluded.

## Validation scope

Gates include signature/package/version/non-debuggable checks, native input
identity, source coverage/byte equality, curated hashes, executable byte-window
and private-file audits, plus server SHA-256/length checks after publication.
Desktop Android/host tests cover relocated loading in fresh processes, embedded
Python dependencies, lower generation and transaction/migration failure handling.
Exact counts and scope are recorded in RELEASE-INVENTORY.json.

No physical Thor was available for the integrated build. Earlier Thor evidence
covers the older development runtime's opening/Gateway play, live lower screen,
remap/reset/save reload and NSMB rebinding. Gallery captions retain that scope.
MSMBWii shares the controller code but was not independently checked on Thor.
No all-stage, sustained guest FPS, audible-audio, haptic or long-session guarantee
is made. Galaxy's diorama/material limits are documented in its setup guide.

Use release/version.properties and the current integration staging scripts.
The named complete source ZIP includes large curated media omitted from GitHub's
automatic source download. An independent clean-machine rebuild is not claimed.
