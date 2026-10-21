# NSMB Wii Thor workbench

Native ARM64 NSMB Wii bring-up using the owner's dumps. Keep Wii gameplay;
use physical controls and the existing Thor Mario Kart launcher. Claude owns
launcher and lower-screen visual design.

Latest handheld pass: NSMB2-style lower display is installed in releasea9498fbd...
with actual world graph, moving course-progress marker, Star Coins, score and a
stored power-up controlled by R3. Progress/counters and World1 graph have live
Thor evidence. Natural mushroom/Fire Flower reserves filled and were used; the
owner confirmed visible drops and collection. Read
[HANDHELD_SCREEN.md](HANDHELD_SCREEN.md) for current evidence and limits.

Previous second-screen pass: a passive E1 lower HUD was installed and live-verified
in World1-1 using the original font/art. Concurrent upper/lower captures matched
lives5, coins12 and time458. Gameplay later stopped at the original disc/BCA
fatal path; full-level stability is not claimed. See [SECOND_SCREEN.md](SECOND_SCREEN.md)
and private build/nsmbw-thor/e1/second-screen/verification.json. The following boot
result describes the earlier APK and remains historical evidence.

Current local result: canonical clean USA revision1 is now explicitly supported
as a build input, with exact executable and four REL hashes. Fresh 41,418-function
translation and full ARM64 native build passed. Signed non-debuggable release
SHA256 54dc4ca8a0a2b3280a7cfeeb09f8c71f4f06022af1a1f2c10db1a5d66be95c0e
is installed/hash-matched; all 1,189 data files passed device SHA256 checks.

The startup metadata adapter passed 55 ARM64 checks. After the owner supplied a
file stated to be an original CleanRip dump, it was copied unchanged and device-
hash verified. Native PID 9719 advanced through startup, loaded all four RELs at
matching addresses, and visibly rendered the original opening cutscene on Thor.
The agent sent no gameplay inputs and left it running. This verifies native boot
and the observed opening sequence, not physical play, a level completion, save
persistence, audio, performance or completed lower-screen/launcher integration.
The prior missing-file failed boot remains documented in first-launch/.

The previous NSMB APK is backed up locally; original game, NAND and Config are
preserved on Thor under *-e2-before-clean-e1 names. A host NAND copy was partial
because Android denies shell reads of some app-owned files. No permissions were
changed. The installed MKW launcher was not replaced. Private current receipt:
`build/nsmbw-thor/e1/verification.json`. See [DISC_BACKEND.md](DISC_BACKEND.md) and
[E1_INPUT_AUDIT.md](E1_INPUT_AUDIT.md) for supported scope and evidence.

Read [MODDING_PLAN.md](MODDING_PLAN.md) for the source substitution, pins and
limits. [LAUNCHER_INTEGRATION.md](LAUNCHER_INTEGRATION.md) is the handoff for
Claude: stable game ID `nsmbw`, fixed launch/return components and read-only
lower-display snapshot API. Claude has since integrated the third NSMB case;
this pass preserves that launcher design and changes the separate NSMB package.

Source patches contain no disc data, extracted art or generated game code:

- `nsmbw-thor.patch`: complete Android/runtime changes against WitherBlitz
  `20f0158dd1d96bd94f821adf80ee361e4fbd414e`. Includes the E2 opt-in changes.
- `runtime-e2-opt-in.patch`: only the E2 runtime migration, for independent review.
  Do not apply both patches to the same clean checkout.
- `mkw-launch-transport.patch`: the NSMB transport and narrow visibility addition
  for the existing private Thor launcher, not a patch for unmodified upstream MKW.

Local build sequence from the PORTING root, after preparing the isolated source
checkout and official tools described by the source repository:

1. Extract the owner's NKit ISO with official NKit 3.2.0; keep outputs under
   ignored `build/nsmbw-thor/extracted`.
2. Follow [E2_PREPARATION.md](E2_PREPARATION.md), then run `Build-E2Translation.ps1`.
3. Run `Configure-AndroidNative.ps1 -Build -BuildJobs 12 -TranslatedJobs 4`.
4. Run `Package-Nsmbw.ps1 -Variant Both`. It verifies/stages the native build,
   packages the separate Thor app and checks its APK signature.
5. Install the private APK on Thor, then run `stage_disc.py --serial <device>
   --disc <extracted-DATA-folder>`. It refuses an existing game folder, copies
   only game data, hashes every staged file and does not open an Activity.
6. Wire the existing launcher case through `ThorGameLaunch.launchNsmb(activity)`.
   Verify actual boot, controls, upper-HUD state and return on device.

`export_source_patches.py` refreshes the source allowlist and checks the complete
NSMB patch against its source pin using a temporary Git index. It never stages
the private workbench. `stage_native.py` and Gradle refuse packaging a Java-only
shell or a library inconsistent with the E2 native receipt.

The current canonical-E1 alternative uses the isolated E1 checkout and these
steps after extracting the verified owner's disc. They compile the original
checks unchanged; the later supplied metadata file is staged separately.

```powershell
python experiments/nsmbw-thor/prepare_clean_e1.py --repo reference/nsmbw-android-e1 --disc build/nsmbw-thor/e1/extracted --output build/nsmbw-thor/e1/prepared
./experiments/nsmbw-thor/Build-CleanE1Translation.ps1
python experiments/nsmbw-thor/verify_clean_e1_translation.py --repo reference/nsmbw-android-e1 --prepared build/nsmbw-thor/e1/prepared
./experiments/nsmbw-thor/Configure-AndroidNative.ps1 -RuntimeRoot reference/nsmbw-android-e1/runtime -BuildDirectory build/nsmbw-thor/e1/android-native -PreparedDirectory build/nsmbw-thor/e1/prepared -DiscRevision E1 -Build -Target WiiCompiled
./experiments/nsmbw-thor/Package-Nsmbw.ps1 -DiscRevision E1 -Variant Release
```

`nsmbw-thor-clean-e1.patch` is the current complete E1 authored source patch;
the older `nsmbw-thor-e1-preparation.patch` describes the prior unbuilt checkpoint.

All APKs, native libraries, translated game code, extracted files and private
verification receipts stay in ignored paths. Nothing has been published.
