# WiiDS 1.26.2 Prerelease

This preview adds screen setup for more handheld/display combinations and several targeted Pokémon XD rendering and presentation fixes. It includes the Project+ picker imports from 1.26.1.

## Install

Download **WiiDS-1.26.2-arm64.apk** and install it over your existing multi-game WiiDS app. The package and signing key are unchanged; do not uninstall to update. Android needs additional free internal space while installing.

This is a **prerelease**. Stable WiiDS **1.2.5** remains available. The stable in-app updater does not offer prereleases; install this preview from this release page. Games and game dumps are not included.

## Screen setup and more display combinations

- An unrecognized second display can now ask **Is this the top screen?** Choose Yes or No, or use the explicit top/bottom display lists.
- Open **Options > Screen setup** to change the arrangement while games and imports are stopped. The library moves to the chosen top screen and closes its old instance.
- Saved roles are matched when displays reconnect instead of relying on temporary display IDs. Ambiguous identical displays require a new choice.
- Adds a path for a single-screen handheld with an independent display attachment, including an attachment used as the top screen and the built-in display used as the companion.
- Companion touch, controller and menu input follow the selected layout. Native-window games retain their existing Android input pipeline.
- Double Dash can use an available controller on an unrecognized handheld while retaining preference for known internal controllers.
- Includes the earlier external-display timing guard that releases forced refresh requests when external outputs connect.

This targets devices such as **OneXSugar** and **Retroid Pocket 5/6 with a compatible dual-screen attachment**, where firmware exposes independent Android displays. Those named physical combinations have not been verified in this release. Mirroring alone cannot provide two independent screens; firmware rotation, touch alignment, controller mapping and dock behavior may still vary.

## Pokémon XD fixes

- Lower-screen scene/fade detection consumes each completed GPU sample once, instead of advancing its state repeatedly from stale samples. Mode changes request fresh samples and reject old-mode captures.
- Repeated notifications for an unchanged, healthy companion window retain its rendering target. Real window changes and failed-target recovery still rebuild it as needed.
- Draws waiting for an already-compiled shader wake before disk-cache cleanup. This removes an unnecessary wait when the last queued shader completes while cache pruning is pending.
- Repeated usable but suboptimal presentation results request one recovery configuration per episode instead of reconfiguring every frame. Actual resize, format/mode changes, outdated/lost surfaces and failed presentation retain recovery.
- Temporary acquisition timeouts no longer request unnecessary graphics reconfiguration. This is defensive across graphics backends; it is not an established trigger on the current Vulkan path.

These are specific software repairs, **not confirmation that every gameplay stutter or full-screen flicker is resolved**. The reporter's affected scene has not been reproduced, and sustained physical gameplay/FPS, display-status frequency and dock/firmware behavior still need acceptance testing. Shader compilation and other GPU/display waits can still cause hitches.

## Included from 1.26.1

Project+ can import a Dolphin ZIP, or separately selected `sd.raw` and `RSBE01.gct` files, through **Options > Your games > Project+ > Import Project+** after the supported USA Brawl revision 2 disc is imported. Existing setups and replaced mod files retain the previous backup/recovery behavior. Pokémon XD remains integrated into the main WiiDS library; standalone XD saves are not automatically migrated.

## Validation and source

The signed release build and vital lint checks pass. The earlier screen-routing/input checks and the XD source regression tests cover the repaired software paths; the physical acceptance limits above still apply. All 40 native libraries and 663 content assets match the preceding preview.

**WiiDS-1.26.2-source.zip**, **RELEASE-INVENTORY.json** and **SHA256SUMS.txt** accompany the APK. The source includes the Android changes, XD native overlays and host regression harnesses. As with preceding XD previews, this is a pinned runtime overlay, not a claim of a one-command clean-machine native rebuild. No game dumps, saves, signing keys or private device diagnostics are included.
