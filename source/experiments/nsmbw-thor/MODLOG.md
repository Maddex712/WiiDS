# NSMB Wii Thor journal

## 2026-10-18 initial work

- User asked for a native Thor NSMB Wii version from ral1009/NSMBWiicompiled.
  Confirmed keep Wii gameplay; adapt screens and physical controls. Later added
  the existing Mario Kart launcher as the shared launcher; Claude owns design.
- Read mod-any-game, game-recon, game-research-websearch, share-field-notes and
  retro-decomp guidance. Searched universal-modder knowledge index; no NSMB
  native-port note found. Read primary repository sources and requirements.
- Inspected the local disc header: SMNE01, revision 2, legacy NKit marker.
  Existing dtk disc info could not read its apploader; it treats legacy NKit as
  raw ISO. Official NKit 3.2.0 successfully extracted 1195 files in a new ignored
  directory. Original image unchanged. Extraction itself was not full-disc verified.
- Cloned/pinned both candidate runtimes. Chose WitherBlitz ARM64 Android runtime
  as the portability base. The linked source remains a reference, not falsely
  described as an already working Android port.
- E2 preparation linked 4 private RELs with 198511 relocation records. Heap
  chaining matched all four E2 allocations. 39582 named upstream seeds mapped
  into valid E2 code; E2 call/pointer discovery supplements these to 41207 seeds.
  Source audit distinguishes actual addresses from division magic and dead MKW
  constants. See ignored `build/nsmbw-thor/e2-prepared/preparation.json`.
- Added separate Thor Android package, fixed launch/return transport, disc-revision
  and executable-hash checks. Touch gameplay controls default off for Thor;
  16:9 default, motion inputs default off. Explicit saved preferences otherwise
  remain upstream behavior. No game physics/value changes were requested here.
- NSMB `compileDebugJavaWithJavac -PthorBuild=true` passed; MKW
  `compileDebugKotlin` with existing Java-only verification init script passed.
  First MKW command used a wrong relative init-script path; absolute path fixed it.
- Native Android dependency setup and E2 translation are underway. No playable
  APK or on-device NSMB verification yet. No publication or upstream messages.

See MODDING_PLAN.md for pins/input hashes and LAUNCHER_INTEGRATION.md for Claude.

## Verification during native compilation

- Six E2 preparation/mapping checks passed. Translation verification confirmed
  41420 metadata functions, 41067 shared compiled functions and 72 shards with
  zero emitted unsupported instructions/invalid SSA functions. Two speculative
  seeds skipped, and the branch to 0x60 warning, remain recorded for first boot.
- Eight host Android import checks passed against the actual AppFiles class and
  owner extraction. A deliberate missing-native-receipt check refused packaging.
- Actual thor_state.cpp compiled as an ARM64 shell test with a read-only mock
  Memory backend and passed 35 checks on Thor, including 2000 concurrent capture/
  reader iterations. The exact temporary executable was removed afterward.
  This was a non-UI test and is not NSMB gameplay evidence.
- Review corrected timer display from truncation to the game's Q12 ceiling
  `(remaining + 4095) >> 12`, preserving the displayed second at tick boundaries.
- Source patch export applies cleanly to the exact NSMB source pin in a temporary
  Git index. It exports only enumerated authored sources. No private index reset,
  game files, extracted art, generated game code or binaries were included.
- Thor had Bonk Demake foreground during read-only status inspection. A timing
  question is pending before any game Activity takeover; compilation, validation
  and source work continue independently.

## Final local milestone, gameplay deferred by owner

- Full E2 ARM64 build passed, then incremental build added the state bridge.
  ELF64/AArch64, SDL_main and nativeReadThorState exports verified. Native output
  and E2 translation provenance are recorded under ignored build/nsmbw-thor.
- Debug and non-debuggable release APKs packaged and signature-verified. Release
  SHA256 58a831099e7b188a6966ef1afc8bcd738854111ce2ff8488ae213e62e6a8f46b;
  debug 1543eee2f8c893ea5db88dd4c71abb549d3aa1fc28f49984298c741e447b4188.
- Separate release installed without opening an Activity; on-device base.apk hash
  matched and package flags excluded DEBUGGABLE. No existing MKW app was replaced.
- The files/sys data tree (1189 files) was staged into the isolated NSMB profile.
  Initial sha256sum check failed because a Windows CRLF manifest added CR to file
  names. Corrected the generator to force LF; every device file then verified.
  Promoted only that verified incoming directory, removed its exact temporary tar.
  No saves/config were replaced; no game process was started.
- Owner answered "Leave the Thor as it is" to the foreground-test timing question.
  No NSMB Activity or gameplay inputs were sent. Boot, rendering, physical inputs,
  audio, saves, performance and live telemetry remain unverified.
- Existing launcher source now has a temporary NSMB action under X > Options.
  Final Kotlin compile passed. The installed launcher remains pending Claude's
  visual pass/integration deployment; no case rendering was redesigned here.
- Source-only patches refreshed and NSMB patch application verified at source pin.
  Final private summary is build/nsmbw-thor/verification.json. Nothing published.


## Owner-requested first launch: blocked before title

- Owner explicitly requested "launch it on thor", superseding the earlier request
  to leave the foreground app alone. Used the fixed ThorLaunchActivity contract.
- First release PID26264 hung before the guest scheduler/VI existed. An opt-in,
  read-only bootwatch on a diagnostic build PID26969 proved a queued IOS callback
  was stranded while the original game polled completion. No guest fibers existed.
- Fixed generic IOS completion delivery before guest fibers are registered, using
  the existing scratch-context dispatcher and actual backend result/output. Once
  fibers exist, behavior remains deferred. Fiber registry query fails closed on
  lock contention. No BCA data, successful result, or game-check bypass was added.
- 27 actual-source IOS/callback ARM64 shell checks passed, including error/output
  preservation, callback context isolation, deferred scheduling and nonrecursive
  nested callbacks. Temporary shell executable removed and absence verified.
- Native incremental build and signed release packaging passed. Updated release
  SHA256 b00af32eef1638d48ff79deab959453bf23eb0ad514f5de20f6e2a7b2a28cf19
  installed and device APK hash matched; package remains non-debuggable. Debug APK
  is the older pre-launch build and does not contain this fix.
- New release PID27711 passed the hang, then guest OSFatal was called by
  __DVDShowFatalMessage through the disc/BCA startup path. The backend does not
  implement this read; no BCA sidecar was extracted/staged. The image's 0x100
  64-byte range is all zero (this observation does not establish a BCA source).
  No title, gameplay, controls, live lower state, audio or save test succeeded.
- Scoped logs/crash diagnostics saved privately under build/nsmbw-thor/first-launch.
  App exited; no unrelated Activity was reopened. Cleared debug.nsmbw.bootwatch.
  Existing MKW launcher APK was not updated. No publication.


## Disc interface follow-up

- Owner requested the boot fix and confirmed only the ISO is available, without
  the original dump's BCA sidecar. Read-only audit confirms absent /dev/di open
  support and no genuine BCA source in the extracted DATA or alongside the ISO.
- The existing unknown-IOS-command success fallback also uses the wrong result
  convention for a disc request. Fixing descriptor/results alone would still not
  provide the missing disc metadata. Do not represent that as a playable fix.
- CleanRip's official source reads BCA separately and saves a .bca sidecar:
  https://github.com/emukidid/cleanrip/blob/master/source/main.c
- No fabricated BCA bytes, forced pass, game check patch or game data download.
  No additional app build, installation, launch, or device interaction in this
  follow-up; the installed release remains the previously verified failing build.


## Revision-1 input supplied and verified

- Found owner's already-extracted Downloads RVZ. DTK identifies SMNE01 revision1
  and full-disc verification matches Redump SHA1 aa7b134a7606435ddcd735f2964ebdecde38c2ef.
- DTK validated extraction and NKit independent extraction produce identical DOL
  SHA256166eb6b7cca21418e316a6cfbf1bfbf0a213eb8f6396d42126a1482bb764a955.
  This is a verified clean disc input. Upstream's required DOL SHA2566d755ec...
  differs, and its unmodified unpacker rejects the file; discrepancy unresolved.
- Prepared isolated reference/nsmbw-android-e1 with Thor launch/input/state changes.
  E1 Java compile passed. Generalized native receipt/packaging/staging paths with
  explicit revision selection; six provenance checks pass. Preserved hash guards.
- Exported source-only nsmbw-thor-e1-preparation.patch and checked against source pin
  using temporary Git index. No E1 game translation, native build, APK install or
  launch was performed. Existing MKW and installed E2 app remain unchanged.
- E1_INPUT_AUDIT.md and private build/nsmbw-thor/e1/verification.json record limits.


## Executable fingerprint provenance follow-up

- Traced upstream6d755 fingerprint to first NSMB input configuration beeaaf612b20
  and copied unpacker constant in fe4b1539ce06. No documented derivation, reference
  disc fingerprint or canonicalization step explains it. Original Android import
  checks header only, so successful upstream import is not a DOL fingerprint proof.
- Ruled out0..28 trailing-zero trims and zero appends through64KiB. Dolphin's
  ExportDOL exports raw bytes; no executable rewrite. DTK/NKit already agree.
- Clean E1 startup still reaches original disc/BCA check; changing accepted input
  hash alone cannot solve the absent genuine disc metadata and DI backend gaps.
- Exact byte difference from upstream assumed executable remains unknown from
  hashes alone. No claim that owner's verified file or upstream file is modified.
- No guard weakening, disc-check bypass, synthetic metadata, native rebuild,
  installation or launch in this follow-up. E1_INPUT_AUDIT.md records sources.


## Real disc metadata backend, canonical E1 build and actual startup

- Implemented read-only DI metadata transport in isolated E1 checkout: actual
  disc/bca.bin exact64 bytes and sys/boot.bin first32, valid32-byte command buffer,
  complete guest-range validation, no output writes on error. Dedicated descriptor
  published to original SDK; IOS routes open/close/scalar requests and rejects
  unsupported vectors/stale handles. No original check/data bytes replaced.
-55 actual-source ARM64 checks passed, including exact input size33 rejection,
  real file errors/verbatim copy/output tails/closed descriptors. Fixtures remained
  in temporary shell-test directory and were removed/absence-verified.
- Pinned canonical clean E1 DOL166eb6... and four RELs in isolated profile; previous
  undocumented6d755 input receipts reject. Eight provenance, five real unpacker and
  four metadata-staging checks passed. Optional --bca only copies an existing64-byte
  source verbatim; no data generated when omitted.
- Fresh translation41418 metadata functions/41065 shared/72 shards passed emitted
  quality/artifact checks. First attempt encountered a YAML BOM/preamble problem;
  normalized that source-config encoding and reran all translation steps successfully.
- Full native ARM64 build and signed release packaging passed. Release54dc4ca8a0a2b3280a7cfeeb09f8c71f4f06022af1a1f2c10db1a5d66be95c0e
  installed/hash matched/non-debuggable. All1189 canonical E1 game files device-hash
  verified. No existing MKW install changed.
- Old E2 APK copied/hash verified locally. Host NAND pull was incomplete due to
  Android read permission denial; no permissions altered. Preserved full original
  game/NAND/Config via checked exact on-device rename before E1 staging/launch.
- One normal ThorLaunchActivity launch PID3789 reached new backend. It logged
  missing game/disc/bca.bin, then unchanged guest OSFatal through disc startup;
  secondary signal11/fault0x60 logged after fatal. No title/gameplay reached.
- New ZIP inspected: only RVZ, no sidecar. No repeated launch or attempts to force
  success. Bootwatch property empty, test helpers removed. Existing profile backup
  entries still present after test; no fixture metadata staged into app data.
- Source-only clean-E1 patch export checked at upstream pin. No publication.
  Current receipts build/nsmbw-thor/e1; source docs DISC_BACKEND.md/E1_INPUT_AUDIT.md.


## Owner-supplied metadata: native boot verified

- Owner provided C:/Users/super/game/disc/bca.bin and stated original CleanRip.
  Read-only inspection:64 bytes, unlike posted generated payload; no nearby
  dumpinfo record. No forensic authenticity claim made. The existing source file
  was copied unchanged, hash verified on device, and original game checks retained.
- APK SHA54dc... unchanged and non-debuggable; source executable/checks unchanged.
  New normal launch PID9719 advanced past previous disc error and loaded all four
  RELs at matching E1 addresses. Original opening cutscene visually confirmed in
  upper.png (preview inspected). Earlier static expectation of rejection was too
  categorical and is superseded by this actual unchanged-game observation.
- No agent gameplay input sent. Initial capture guard used dumpsys window windows,
  which omitted focus info; corrected read-only query to dumpsys window and captured
  only the confirmed NSMB upper display. No unrelated screenshot taken.
- Retained supplied file unchanged after successful startup. No generation, patch,
  check replacement, metadata downloads or publication. Game left running.
- Scoped console lacks previous OSFatal/signal11 patterns in captured interval.
  No physical controls/full level/save persistence/audio/haptics/performance or final
  dual-screen/launcher validation claimed. Current receipt and native evidence:
  build/nsmbw-thor/e1/supplied-metadata and e1/verification.json.

## NSMB2-style lower screen (completed bounded pass)

Owner explicitly authorized a stored power-up. Native course/Star Coin/score,
world graph and reserve modules are implemented; original Wii assets extracted.
Read HANDHELD_SCREEN.md for schema, source proof, tests, build session and exact
remaining integration/validation steps. Claude launcher is untouched. Current
full native rebuild completed; final releasea9498fbd... installed/hash matched.
PID595 human play verified course progress, world graph, matching counters, two
natural reserve fills and three R3 uses (one controlled test seed). Owner confirmed
visible drops and collection. Seed consumed and current sidecar empty; original
.sav unchanged.151 actual-source ARM64 mocked checks passed. See current receipt
and HANDHELD_SCREEN.md for unverified Star Coin/restart/broader-course limits.
