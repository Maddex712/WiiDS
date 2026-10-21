# Eclipse library case

The library adds Eclipse as case index 5, beside original Sunshine (index 4) in
the existing GameCube stack. It uses the same case opening/disc animation and
8 cm disc geometry. No gameplay companion renderer is changed by this integration.

Eclipse is a separate Android app and storage sandbox:

- Package: `com.wither.sms.eclipse.thor`
- Entry: `com.wither.sms.ThorLaunchActivity`
- Action: `dev.thor.launcher.action.PLAY_GAME`
- Extras: `thor_protocol=1`, `thor_game=eclipse`
- Return hint: `thor_launcher_package` is the calling library's actual package;
  the game must accept only the known Thor/WiiDS library package allowlist.
- Flags: `NEW_TASK | CLEAR_TOP | SINGLE_TOP`

The library checks whether the exported entry is installed and enabled. It cannot
inspect Eclipse's private imported files or saves. The lower library therefore
says "Ready to open" / "Eclipse app installed"; the app validates its own game
data on launch. A missing app blocks the animation with an explanatory message,
and an unavailable component restores the case through `launchFailed`.

The original Sunshine launch action, imported disc and save profile are untouched.
Eclipse is not routed through Mario Kart's runtime or Retro Rewind updater.

Private packaging images use the paired names `eclipse-cover` / `eclipse-disc` in
the existing schema-1 artwork manifest. Prepare them with:

```powershell
python scripts/Stage-ThorLauncher.py --eclipse-cover "C:\path\cover.png" --eclipse-disc "C:\path\disc.png"
```

The same manifest/hash/dimension validation applies on both screens. Images remain
private and are not embedded in the launcher APK by this source change. Stage to
the installed library's existing custom/launcher or ThorMap/launcher folder as
appropriate; use `--stage --serial SERIAL --package io.github.maddex712.wiids` for
the merged WiiDS app. Staging uses `run-as`, so requires a debuggable app; preserve
the normal release sandbox and use the established controlled packaging workflow
for its private art. The default package remains the standalone Thor library.

Compilation: `:app:compileReleaseKotlin -PthorBuild=true -PwiidsApp=true
-PthorRetroRewind=true` passed on 2026-10-20. This is source compilation only;
installation, game startup, case input and return routing need separate device
verification against the final installed packages.

The ignored `build/eclipse-thor/case-art` now holds the mod graphic designer
Some-Crappy-Edits' published cover and disc art, normalized by
`experiments/eclipse-thor/compose_case_art.py`. Input URLs, exact hashes and
adaptations are in its `provenance.json`. The cover is fitted to 900x1280; the
disc is cropped from the creator's flat mockup, removing the wooden tabletop
and centre-hole background, and normalized to transparent 1024x1024. The
preview is a composition sheet, not a device screenshot. These private images
are also prepared in `build/thor-map-data/launcher` without contacting a device.

For the installed non-debuggable WiiDS library, the dedicated
`experiments/eclipse-thor/Stage-Launcher.py --serial SERIAL` merges only the two
Eclipse images into the existing external custom set. It first backs up all
existing files into `build/eclipse-thor/before/launcher` without overwriting,
checks their hashes against the manifest, stages and hashes the two new images,
then atomically publishes the merged manifest last. Every old image and every
old manifest entry must remain unchanged. The successful 2026-10-20 staging
receipt is `build/eclipse-thor/launcher-art-staging.json`. This helper neither
installs an APK nor starts an app.
