# WiiDS 1.2 runtime asset reconstruction

The frozen private candidate 3 contains 602 content assets after excluding notices and the
Android dexopt profiles. The presentation manifest lists 535 payloads. The other 67 exact APK
paths, byte lengths, SHA-256 values, sources and reconstruction routes are in
`experiments/release/runtime_assets_1_2.json`. This inventory was read from the frozen APK, not
from a diagnostic Android staging tree.

| Remaining assets | Paths | Exact source or recipe |
|---|---:|---|
| Presentation manifest itself | 1 | Already included with the 535 hash-verified presentation payloads |
| Content catalog and MSMBWii file list | 2 | Already included as authored Android source metadata |
| TLS root certificates | 4 | One public Mozilla/curl CA bundle, duplicated for the four runtime roots. Pinned DriftDroid blob with CRLF line endings. MPL 2.0 text retained in `release/licenses/LICENSE-MPL-2.0-CA-bundle.txt`. |
| Free DSP coefficient table | 4 | One Dolphin-authored GPL table, duplicated. Exact pinned DriftDroid blob. The [Dolphin free DSP source](https://github.com/dolphin-emu/dolphin/tree/master/docs/DSP/free_dsp_rom) documents its origin. |
| WiiConnect24 first-run bootstrap | 48 | Four copies of 10 default Dolphin data files and two README files. The mailbox README uses CRLF. No owner NAND/account data. Attribution is in the retained DriftDroid/NSMB notices. |
| Aurora pipeline seed DB | 4 | Two pinned upstream SQLite recipe databases, not a GPU-driver cache. DriftDroid supplies the main seed; NSMB Wii supplies the other three copies. `runtime/cmake/PublicProducts.cmake` stages the seed and `aurora-main/lib/gfx/pipeline_cache.cpp` imports it. |
| Galaxy shader manifest | 1 | Exact pinned upstream generated `ShaderUid`/`PipelineKey` metadata. `tools/cook/generate_shader_manifest.py` can regenerate it from an owner's privately cooked disc tree. |
| Galaxy cooker scripts ZIP | 1 | Recreate from the 41 exact shipped Python/JSON inputs and the recorded ZIP member metadata. Existing `Build-Importer.py` documents the embedded-Python adaptations. |
| Galaxy Python home ZIP | 1 | Replay the pinned Galaxy base ZIP plus 1,089 members from the seven SHA-pinned Chaquopy wheels. The kit contains the recipe and member metadata, not the compiled wheel payloads. |
| MSMBWii creator archive | 1 | Exact unmodified 3,007,052-byte creator release from `http://riivolution.nanolx.org/MSMBWii.zip`. The existing owner-reported authorization and creator credits are in `docs/release/PERMISSIONS.md`; written replies were not retained there. |

The 62 non-generated runtime paths reduce to 17 unique fixed-hash blobs, stored in
`build/wiids-runtime-inputs/blobs/`. The 41 cooker source files live in
`build/wiids-runtime-inputs/cooker/`. These are explicit source inputs and approved dependencies;
the exporter does not sweep an asset directory. No source-kit copy of owner discs, NAND, saves,
generated translations, or compiled Python wheel libraries is needed.

The DriftDroid, NSMB Wii and Galaxy pins are recorded in the runtime asset manifest. The certificate
bundle's pinned Git bytes differ from its packaged bytes only by CRLF, as does the Dolphin mailbox
README. The bootstrap's separate first-run README is the exact WiiDS-authored text. The SQLite
files contain portable pipeline configurations; the main seed has 1,199 recipes. None of the 67
paths is a separate presentation UI/audio payload. The content manifest, not file extensions alone,
controls which assets may be staged.

## Stage a fresh isolated build tree

Use **CPython 3.12 with stock zlib 1.3.1**. The old ZIP containers depend on compression bytes as
well as contents. The default Python 3.14/zlib-ng on the workbench produces the same Python files
but a different ZIP digest. The replay tool checks both ZIP outputs against the frozen APK SHA-256.

1. Extract a source kit that includes this recipe and `build/wiids-runtime-inputs/`.
2. Obtain the exact base `pythonhome.zip` from the URL and hash in `runtime_assets_1_2.json`.
   Obtain the seven wheels from `experiments/galaxy-thor/python-packages.json`; its existing
   `Prepare-PythonPackages.py` can retrieve them in an isolated checkout. The stager rechecks all
   hashes before reading any Python dependency members.
3. Build a new, empty asset overlay directory:

```powershell
py -3.12 -I experiments/release/stage_runtime_assets_1_2.py `
  --kit-root '<extracted source kit>' --output '<new isolated asset overlay>' `
  --python-base '<pinned upstream pythonhome.zip>' --wheels '<pinned wheel directory>'
```

The overlay places all 602 content files and the candidate's exact license files under
`reference/driftdroid/android/app/src/main/assets`. It also supplies the appropriate native
build inputs at the four runtime source roots and Galaxy's shader-manifest source path. Merge
this verified overlay into a fresh source/build checkout before invoking Gradle. The frozen
integrated `build.gradle.kts` no longer reads old `build/wiids-merge/assets` directly, so leaving
the presentation files only in that retained archive path would omit them from a fresh APK.

The independent asset smoke test rebuilt both Galaxy ZIPs, all **602 content assets**, and all
**53 license assets** byte-for-byte against frozen candidate 3, using the archived presentation and
metadata plus these supplemental inputs. It did not build an APK or access the device. This covers
the asset layer, not an independently rebuilt full native APK or a public-release clearance.

## Frozen snapshot boundary

The existing candidate 3 source ZIP predates this supplemental asset recipe. It already contains
the 535 approved presentation payloads, the manifest, authored catalog files, notices and the
older component build scripts. The newer explicit 67-path manifest, 17 blobs, cooker source
inputs and exact ZIP replay/materialization tooling must be included when refreshing the next
production-bound source kit. Do not rebuild the source kit against temporary diagnostic Android
libraries. The current retained APK and source ZIP were not changed by this audit.
