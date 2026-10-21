# Super Mario Sunshine for AYN Thor

Requested 2026-10-19: run Sunshine natively on Thor and launch it as another game
in the existing carousel. Preserve the other apps, saves, controls and design.

## Verified input

Owner-provided Downloads/Super Mario Sunshine (USA, Canada)/Super Mario Sunshine
(USA).rvz is GMSE01 revision0. dtk1.3.0 verifies the full reconstructed disc
against Redump: SHA1 `8d094f2c5c112aba9660f0478b16c7f2caf63cbf`,
MD5 `0c6d2edae9fdf40dfc410ff1623e4119`. Conversion to a full ISO verifies too.
Extracted main.dol SHA256:
`13934c863d649b1ddca1ca4d7748f49d28a571685cbee5fb1542545c32869955`.
Private inputs/outputs live in ignored build/sunshine-thor.

## Chosen route

The linked doldecomp/sms is a matching GameCube decompilation. This adaptation
uses its descendant native source port, rather than treating that decomp as an
already runnable Android program:

- chasem-dev/sms-pc-port feat/android-arm64 at
  `3f661cfda9d160425b46fea22ee3a81755eb2f55`.
- chasem-dev/sms-launcher Android shell at
  `59ef545af26c9ef5026154415d8a5ae5db14c514`.
- Decomp submodule `967436b8c623834d38a57d34923775f45eca8861`.
- SDL2.32.10/GLES renderer; game C/C++ compiles to native ARM64. No PowerPC
  interpreter or Dolphin fallback is used in this route.

Native rendering, audio, DVD, memory-card and operating-system replacements
already exist in that source. Android-specific MEM1/texture-address and Adreno
shader fixes make it a better starting point than the older jbcgames fork or a
new Sunshine integration in the Wii runtime. See RUNTIME_RESEARCH.md for sources.

## Isolation and integration

New package `com.wither.sms.thor`; validated exported entry
`com.wither.sms.ThorLaunchActivity`; game ID `sunshine`, protocol1. The Android
game retains its existing internal Java/JNI namespace. A shared root helper uses
the package's own external files directory for disc/game/saves, allowing local
staging without broad storage permission or a debuggable final APK.

The carousel appends case4 and uses its existing single launch commit point.
Return uses `com.driftdroid.android.thor/com.driftdroid.android.ModePickerActivity`
with REORDER_TO_FRONT. Keep the renderer, animation, audio and existing cases.
Do not label an installed app as a verified working game before actual boot.

## Build and validation

Local toolchain adaptation: installed NDK29/API29, SDK34, AGP8.5.2, Gradle8.8,
CMake/Ninja, JDK21. Upstream pins SDK35/NDK28/AGP9; record changes and build results.
First boot should use the original30FPS baseline and physical controls.

Before changing the installed shared launcher, pull/hash its APK and back up
its protected settings/saves with the existing same-signature debug/release
staging workflow if private artwork needs staging. Do not uninstall or clear it.
Sunshine receives a new save directory, never another game's NAND or memory card.
The owner is currently playing NSMB; build and stage independently, and coordinate
the actual switch before interrupting that session.

Required evidence: signed native APK, device APK/data hashes, genuine upper
screen title/gameplay capture inspected by an agent, physical input and normal
launcher launch/return. Keep build success, boot, gameplay and performance claims
separate. No ROMs/assets/generated game code/APKs/saves are published or committed.
