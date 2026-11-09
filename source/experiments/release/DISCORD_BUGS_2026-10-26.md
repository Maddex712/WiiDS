# Discord bug reports — local fixes, device acceptance pending

Reconciled October 28, 2026. Owner requested genuine bug fixes after the GitHub issue pass, without adding features. The isolated preview preserves the earlier Galaxy cutscene, Spin/Quick Save and microSD fixes.

| Report | Implemented correction | Verification / limit |
| --- | --- | --- |
| W1→W5 cannon reveals skipped worlds below | Read each selected-save world-open bit; stop inferring all worlds below the highest reached | Actual native130checks pass on host/Thor ARM64; actual Java parser/cache/renderer24pass. Sparse fixture covers W1+W5. Live cannon traversal not performed. |
| NSMB tilt points the opposite way | Correct virtual accelerometer Z sign in NSMB and MSMB | Actual motion/KPAD/translated game conversion61pass on host/Thor ARM64; sensor/settings63pass. Physical tilt-controlled actor check pending. |
| Imported image cannot load; retry chooses the wrong file | Validate GameCube referenced data before conversion; existing Import again opens the existing picker | Complete reader35pass; UI branch96pass. Valid trimmed/compressed inputs remain accepted. The screenshot's game/file is unspecified, so its exact cause remains unproven. |
| Turnip cannot be selected/saved | Retain previous explanation that this bug-only build uses the system driver | No custom-driver backend added to this preview. Later experiments are separate. |

The frozen owner-signed, nondebuggable APK is [WiiDS-1.1-discord-bugfix-preview.apk](M:/WiiDS-discord-bugfix-2026-10-26/WiiDS-1.1-discord-bugfix-preview.apk), version1.1-bugfix-preview/code1102. SHA256 `3f52815e9bfbc350b0b0a8597dddb03e04e6f5ec884fa9bc38b257d092faa885`, 1848054054bytes. All2412APK members and1713build inputs were freshly reconciled. Previous Galaxy native is exact; only NSMB/MSMB/disc-reader libraries change from the preceding bug preview. Other22libraries and1477presentation/game assets are identical.

Historical October26 install and27readable-file preservation receipts exist. The Thor was unavailable during the October28 resume; its current installed build is unknown because later experiments exist. No fresh live gameplay, save preservation or current-install claim is made.

The [combined source patch](../../build/discord-bugfix-2026-10-26/combined-bugfix-source.patch) includes32reviewed paths over public1102 and has exact apply/reverse/reapply proof. The paused disc-reader implementation is excluded: its fix comes from public source in an isolated overlay. All91paused workbench hashes match their baseline. No commits, public releases, issue comments/closures or Discord messages were made by this resume.

Reconnect wireless debugging to finish live acceptance. Inspect the foreground and current installed APK before any mutation; retain newer experiments and owner saves. Detailed evidence and remaining steps are in [the task record](../../build/discord-bugfix-2026-10-26/README.md).

## October 28 live follow-up

Connected and tested the existing later1.1.1/code1111 installation in place (APK SHA256 `93cb0e15b51c72d82db53b6c3a5c639a614f33ff0fddd99c1383e02eb8431296`); its retained bug-fix sources and native lineage were verified. No replacement APK was installed.

The existing NSMB file1 renderedW1 unlocked andW2–W9 locked on the lower display, matching the upper world selector and native mask1. Import again opened Android's picker; cancellation retained Installed/Ready. The exact installed disc-reader JNI passed16scratch-only checks, including identical full ISO/RVZ decoded bytes and rejection of14damaged images without replacing existing test data. No owner disc reimport or level completion was needed.

All26freshly readable external save/settings files remained exact;22inaccessible and one absent historical path were excluded. Temporary files/helpers removed;91paused hashes unchanged. Thor returned to Select Game, Sunshine selected, with no native game running. Physical tilt during gameplay and an actual cannon traversal were not observed; source/ARM64 tilt and sparse-world fixture results retain those explicit limits.

[Live verification](../../build/discord-bugfix-2026-10-26/live-2026-10-28/verification.json).
