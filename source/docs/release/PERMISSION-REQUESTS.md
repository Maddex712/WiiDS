# Permission requests for the public AYN Thor release

Send these yourself (from your own accounts). Nothing here has been sent. Edit freely.
Keep any replies; their written permission should be recorded in `docs/release/PERMISSIONS.md`.

---

## 1. Chase Myers (chasem-dev): Super Mario Sunshine PC port

**Where:** a GitHub issue on `chasem-dev/sms-pc-port` or a direct message.
**Why it's needed:** `sms-pc-port` has no licence file, so its code can't legally be redistributed
without his permission. The Android shell (`sms-launcher`) is MIT, and the decomp is CC0; neither covers
the port's platform code.

> Hi Chase,
>
> I've been running your Super Mario Sunshine PC port (`sms-pc-port`, Android arm64 branch) on an AYN Thor
> dual-screen handheld. I added a themed second-screen companion, 16:9/60–120 fps settings, and two fixes
> for Android's relocated memory arena (DrawSync and the J3D vertex-array base).
>
> I'd like to include it in a free, open-source AYN Thor game launcher. Players supply their own disc
> image, and no game data, ROMs or extracted assets would ever be distributed. The repository doesn't have
> a licence yet, so I wanted to ask first: may I redistribute builds of the port as part of that app? If you
> pick a licence (GPL-3.0 would match the rest of the launcher), that would answer it too. Of course I'll
> credit you, link the original repository, and publish my changes back.
>
> Thanks for the port, it's great.

---

## 2. Retro Rewind team (ZPL and team)

**Where:** the Retro Rewind Discord or the contact listed on rwfc.net / the `Retro-Rewind-Team/rr-pulsar` repo.
**Why it's needed:** the app ships Retro Rewind's `Code.pul` translated to native ARM64 code
(the recompiled engine). The pack itself is always downloaded from your official servers, never rehosted.

> Hi Retro Rewind team,
>
> I'm working on a free AYN Thor (dual-screen Android handheld) launcher that runs Mario Kart Wii natively
> through a static recompilation (based on DriftDroid / WiiCompiled). Retro Rewind works in it: the app
> installs and updates the pack only from your official update.rwfc.net / cdn.update.rwfc.net servers,
> checks the version, and never rehosts any of your files.
>
> To run Retro Rewind natively, the app includes a recompiled build of Retro Rewind's `Code.pul` (6.13.x).
> Before publishing, I wanted to ask whether you're OK with that being distributed inside the app, and
> whether you'd like any specific credit, link or wording. If you'd rather it not be distributed, I'll leave
> Retro Rewind out of the public build.
>
> Thanks for everything you do for the game.

---

## 3. MidiGuyDP (and Treeki / Skawo for the AnotherRes code): MSMBWii "Just a Little Adventure"

**Where:** the mod's page on riivolution.nanolx.org, or the authors' contact details.
**Only needed if** you want the mod built into the app. Without permission, the app has players download
`MSMBWii.zip` from the official page themselves and checks it, which needs no permission.

> Hi MidiGuyDP,
>
> I've got "MSMBWii: Just a Little Adventure" running natively (no emulator) on an AYN Thor handheld,
> as part of a free fan launcher for New Super Mario Bros. Wii. Right now the app asks players to download
> MSMBWii.zip from its official page themselves.
>
> Would you be OK with the app downloading it for them, or including it directly, with full credit to you,
> Fredo and the tileset authors? The code changes come from Another Super Mario Bros. Wii (Treeki/Skawo),
> so I'll ask them too. If you'd prefer players keep downloading it from your page, that's completely fine.
>
> Thanks for making it!
