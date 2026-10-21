# Sunshine shared launcher contract

The Thor library appends Super Mario Sunshine as its fourth case, after Mario
Kart Wii, Retro Rewind and New Super Mario Bros. Wii. The existing carousel,
case geometry, sound, animation state machine and launch commit point are retained.
This source integration alone does not establish a working Sunshine runtime.

## Native app entry

The launcher uses an explicit intent:

- Package: `com.wither.sms.thor`
- Activity: `com.wither.sms.ThorLaunchActivity`
- Action: `dev.thor.launcher.action.PLAY_GAME`
- Integer extra: `thor_protocol=1`
- String extra: `thor_game=sunshine`
- Flags: `NEW_TASK | CLEAR_TOP | SINGLE_TOP`

The exported entry must validate this protocol and the owner's imported disc
data before starting the native runtime. An installed entry only establishes
app availability; it is not proof of imported data or gameplay readiness.
No emulator package or alternate runtime is launched as a fallback.

Return to the library through the existing explicit component:

`com.driftdroid.android.thor/com.driftdroid.android.ModePickerActivity`

Use `REORDER_TO_FRONT` so the library retains its own state. Provide Main Menu
in Sunshine's existing settings if the selected Android runtime supports it.
The actual Sunshine return action needs device verification after runtime bring-up.

## Private packaging art

The shared artwork manifest accepts the optional pair `sunshine-cover` and
`sunshine-disc`. Both must be present together. Existing required MKW/RR and
optional NSMB assets keep their identities and validation rules.

Add owner-provided PNG/JPEG artwork to the existing private set with:

```powershell
python scripts/Stage-ThorLauncher.py --sunshine-cover "C:\path\cover.png" --sunshine-disc "C:\path\disc.png"
```

This prepares and validates local files only. Device staging is separately
explicit through the script's `--stage --serial SERIAL` options. Do not stage
or replace the launcher while the owner is playing without coordination.

The current change includes no newly sourced packaging art, no downloaded game
files and no changes to the NSMB companion renderer. The lower library shows
Sunshine's own title, disc art and installed/missing status. Mario Kart update
gates, video options and race-map promises do not describe Sunshine.

## Validation scope

Host compilation and private verification receipts live under ignored
`build/sunshine-thor`. A successful Java/Kotlin compile is not a native build,
installation, launcher interaction or gameplay check. Record the exact APK
hash and runtime entry result when those checks are performed.
