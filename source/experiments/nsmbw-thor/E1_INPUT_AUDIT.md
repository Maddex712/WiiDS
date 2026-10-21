# Revision 1 input audit

The owner supplied a USA revision-1 RVZ in Downloads. This image verifies against
the disc database: CRC32 `1cc03c30`, MD5 `05022377c66dcc13dd8308b333c202db`, SHA-1
`aa7b134a7606435ddcd735f2964ebdecde38c2ef`. It is not rejected here as a corrupt dump.

DTK extracted the data partition with hash validation. NKit 3.2.0 independently
extracted the same 3,491,808-byte main.dol. Both produce SHA-256
`166eb6b7cca21418e316a6cfbf1bfbf0a213eb8f6396d42126a1482bb764a955`.

The pinned upstream project's unpacker and recomp.yml instead require
`6d755ec24c395fee600a856a85785e0e99c0b7ecb680d573a6cdd1aa6f7beee7`.
The unmodified upstream unpacker rejects the owner's executable. The cause of
that discrepancy is not established; do not label the owner's image modified,
or label the upstream expected executable modified, without further evidence.

An isolated checkout, reference/nsmbw-android-e1, contains Thor preparation work.
Its Java compile passed for `-PthorBuild=true -PthorDiscRevision=1`. Explicit E1
packaging/staging selection and six provenance checks passed. Guards retain the
upstream required executable hash. There is no E1 translation, native build,
APK installation, boot or gameplay verification. The installed E2 app is unchanged.

`nsmbw-thor-e1-preparation.patch` preserves authored preparation sources against
the same upstream commit as the E2 patch. It is an alternative patch for a clean
checkout, not an additional patch to apply after nsmbw-thor.patch. Patch application
was checked using a temporary Git index. It is not a playable E1-port claim.

Private evidence: build/nsmbw-thor/e1/verification.json and disc-verification.txt.
No game files or generated game code are included in this source note or patch.

## Follow-up: provenance and boot dependency

The expected `6d755...` appears literally in the first NSMB input configuration,
upstream commit `beeaaf612b20d802821081adce8face7f785547d`. The unpacker introduced
in `fe4b1539ce06f68227b4bd295eb67894dd67c54f` repeats it, hashes the input and copies
it. It performs no executable canonicalization. BUILDING.md describes a clean
Dolphin extraction but provides no originating whole-disc fingerprint or byte
comparison. The original Android importer only checks the disc header, so its
acceptance is not proof of a match to the compiled executable fingerprint.

All 0..28 trailing-zero trims and zero appends through 64 KiB failed to reproduce
the expected hash. Dolphin's official DiscExtractor.cpp ExportDOL uses the boot
DOL size and exports those raw bytes; it does not rewrite the executable. The
two independent extractors already agree. The remaining difference is between
the project's assumed executable and the verified clean executable, rather than
a demonstrated extraction-format issue. Its exact changed bytes cannot be
identified without a byte-level reference; neither hash reveals them.

Read-only inspection confirms the owner's revision-1 startup still reaches the
original disc/BCA verification routine. Merely accepting this executable hash
does not supply the missing drive metadata or resolve the known boot failure.
No checksum guard was weakened, game check patched, disc data synthesized or new
APK installed during this follow-up. A genuine metadata input and a functioning
disc backend remain necessary for this source-preserving route.

Sources:
- https://github.com/WitherBlitz/NSMB-Wii-Multiplayer-Recompilation/blob/beeaaf612b20d802821081adce8face7f785547d/projects/nsmbw/recomp.yml
- https://github.com/WitherBlitz/NSMB-Wii-Multiplayer-Recompilation/blob/fe4b1539ce06f68227b4bd295eb67894dd67c54f/projects/nsmbw/tools/unpack_game.py
- https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/DiscIO/DiscExtractor.cpp


## Explicit canonical-E1 profile and actual boot test

The clean owner executable is now an explicit exact-pinned E1 profile in the
isolated checkout. This corrects the input assumption; it does not disable hash
validation or reuse native code built for the undocumented6d755 input. The four
compressed RELs are pinned too. Old-assumption native/translation receipts reject.
The exact reason for upstream's different fingerprint remains unproven.

Fresh translation produced41418 metadata functions,41065 shared functions and72
shards, with zero emitted unsupported instructions or invalid SSA functions. The
full ARM64 build passed and its signed release was installed/hash-matched. All
1189 game-data files verified on Thor. The genuine metadata adapter passed55
actual-source ARM64 checks; see DISC_BACKEND.md for scope and no-data behavior.

One normal launch, PID3789, reached the real metadata adapter, which reported
missing game/disc/bca.bin. The original disc check called OSFatal; no title or
gameplay was reached. A later signal11 is also recorded in the scoped console.
The revision1 ZIP was inspected: it contains only the RVZ, no BCA sidecar. There
is no synthesized metadata, game-check patch or successful-boot claim.

Private final evidence: build/nsmbw-thor/e1/verification.json and first-launch/console.log.
The earlier no-translation/unmodified-profile statements above describe the
previous checkpoint and are superseded by this explicit profile and fresh build.


## Supplied-file follow-up: native boot observed

The owner subsequently supplied C:/Users/super/game/disc/bca.bin and stated that
it was an original CleanRip dump. It differs from the previously posted generated
payload. The agent copied this existing file unchanged and verified its device
hash; no payload was synthesized and no game checks or executable bytes changed.
File contents alone do not establish forensic provenance.

Actual PID 9719 advanced through native startup, loaded all four RELs at matching
addresses, and rendered the original opening cutscene on Thor. The upper capture
was visually inspected. This live result supersedes the prior predicted rejection
and missing-file failed boot. Do not assert that the supplied file must fail based
only on its contents or confuse a host read error with the original game's checks.

No agent gameplay inputs were sent. Interactive controls, full level play, save
persistence, audible audio, performance and final lower/launcher work are unverified.
Evidence: build/nsmbw-thor/e1/supplied-metadata/verification.json, console.log and
upper.png. The running game was left open. Current release APK is unchanged.
