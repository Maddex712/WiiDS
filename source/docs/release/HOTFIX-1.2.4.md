# WiiDS 1.2.4 hotfix

## Game launching and artwork

- Games can open while case artwork and lower-screen assets are being prepared. The launcher no longer blocks play with a waiting-for-artwork message.
- **Your games** stays available during background artwork work, with setup and file-picker handling corrected.
- NSMB Wii/MSMBWii lower HUD and Mario Kart Wii/Retro Rewind HUD/maps retry when missing artwork becomes available, including the current course.
- Missing Brawl presentation files use fallback rendering instead of preventing an otherwise installed game from opening.
- **Manage games → Double Dash Plus → Set up game** opens the existing Plus setup card.

## Imports, saves and storage

- Startup recovers a usable previous disc import if replacement was interrupted between directory swaps. Uncertain recovery copies are preserved with an error rather than discarded.
- Manage Games supports custom NAND settings without blocking unrelated games. Custom save paths and extra save profiles remain protected from offload, including TOML quoted/dotted keys, inline tables and Unicode paths.
- Save Manager cleans abandoned extraction, Dolphin-import and partial-backup staging under its operation lock while preserving verified backups and recovery copies.
- Imported backup cache files are released when confirmation is canceled, its Activity is destroyed, or a restore is blocked before it starts.

## Input validation and launch handling

- Hardened malformed BRFNT fonts and GCZ/WIA/RVZ/WBFS image handling with section, table, file-range and decoder-memory checks.
- Wii extraction validates partition ranges, file paths, directory nesting, DOL extents and total output before writing game files.
- The public app-icon entry no longer accepts internal game-selection or restart instructions. Frontend requests continue through the validated handoff.

## Updating

Open **Options → Check for updates**, or install **WiiDS-1.2.4-arm64.apk** over your current WiiDS installation. Use the existing install to keep saves and settings. The APK uses the same WiiDS signing certificate and Android versionCode **1204**.

## Verification and limits

509 checks passed across native parsers/extraction, save archives, Dolphin save adapters, installed Android behavior and focused loader/lifecycle fixtures. An additional 23,600 deterministic input mutations completed under AddressSanitizer. An Android 11 emulator install-over test preserved the existing import, save and backup fixtures. The focused loader tests use framework/decoder stubs and do not establish physical rendering.

All 35 other native libraries and 661 content assets match 1.2.3 byte for byte. This hotfix has not had a fresh physical Thor gameplay pass or a complete Plus installation test. The matching source archive, package inventory and SHA-256 checksums are attached to this release.
