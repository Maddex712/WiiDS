# WiiDS 1.2.5

Thanks for all the reports! This update adds easier player-profile tools and includes the recent fixes for launching, artwork, imports and saves.

## Player profiles

- Added **Options > Player profiles** for Mario Kart Wii and Retro Rewind, with a direct **Import existing player save** option.
- Added **Rename driver**. The linked Mii and matching licenses are updated together, keeping saved account IDs, friend-code data and progress.
- Added **Choose driver Mii** to repair a missing Mii link after importing a save.
- Added import for matching Retro Rewind ratings from **RRRating.pul**, keeping other local accounts' ratings.
- Added automatic player backups before edits, with restore/export tools and a direct link to the selected game's save backups.

## Launching and artwork fixes included

- Fixed games being blocked while artwork is being prepared.
- Fixed **Your games** being unavailable during background artwork work, including setup and file-picker handling.
- Fixed missing NSMB Wii/MSMBWii HUDs and Mario Kart Wii/Retro Rewind HUDs/maps staying missing after artwork becomes available.
- Mario Kart HUD and map artwork is prepared in staging before publication, avoiding reads of partially built presentation files.
- Added fallback rendering when Brawl presentation files are missing, so an otherwise installed game can open.
- Fixed **Manage games > Double Dash Plus > Set up game** opening outdated instructions instead of the Plus setup card.

## Import, save and storage fixes included

- Fixed startup recovery after an interrupted disc replacement, preserving usable previous imports and uncertain recovery copies.
- Fixed custom NAND settings blocking storage management for unrelated games, while protecting custom save locations and extra save profiles.
- Fixed abandoned save-import, restore and partial-backup files taking up storage after an interrupted operation.
- Fixed temporary backup-import files being left behind after cancellation, closing the confirmation screen or a blocked restore.
- Fixed unsafe reads and excessive memory allocations from malformed font and compressed-disc files.
- Strengthened Wii extraction checks for partition boundaries, file paths, directory nesting and output sizes.
- Fixed the public app-icon entry accepting internal game-selection or restart instructions; frontend launches still use the validated launch route.

## Using the player tools

Close any running game, then open **Options > Player profiles**. Choose Mario Kart Wii or Retro Rewind and the license you want to edit. Renaming a shared Mii also renames its linked licenses in both kart games; the confirmation explains this before anything changes.

For existing progress, import the matching game's **rksys.dat** or supported PAL RMCP Wii **data.bin** save. Import replaces all licenses in that game's save and creates a backup first. Your Miis can be imported through **Options > Mii Data**. Retro Rewind ratings can be imported separately from **RRRating.pul** after the matching player save. Online login and rating synchronization follow the server's normal account checks.

## Update

Use **Options > Check for updates**, or install **WiiDS-1.2.5-arm64.apk** over your current WiiDS installation to keep saves and settings. The matching source archive and SHA-256 checksums are attached below.
