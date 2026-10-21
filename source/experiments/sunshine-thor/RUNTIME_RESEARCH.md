# Super Mario Sunshine: native Android runtime research

Research date: 2026-10-19 (America/New_York). Read-only source/web reconnaissance;
this document is not boot, build, controller, or performance evidence.

## Recommended starting point

Use the existing `chasem-dev/sms-pc-port` **Android branch**, with its companion
Android app from `chasem-dev/sms-launcher`, as the first native candidate. This is
an explicit runtime substitution for the owner's linked `doldecomp/sms` project.
It compiles game C/C++ into ARM64 and supplies host implementations of SDK,
graphics, audio, disc, controller, and memory-card services. Its Android source
states there is no GameCube CPU emulator in that execution path.

Freeze these source versions before local Thor changes:

| Component | Exact pin | Reason |
|---|---|---|
| Native game port | `3f661cfda9d160425b46fea22ee3a81755eb2f55` | Android preview 18 native source, `feat/android-arm64` |
| Android app | `59ef545af26c9ef5026154415d8a5ae5db14c514` | Matching preview 18 app/configuration source |
| Decompilation submodule | `967436b8c623834d38a57d34923775f45eca8861` | `chasem-dev/sms-english`, recorded Git submodule object |
| SDL | `2.32.10` | Upstream build downloads source with SHA-256 `5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165` |
| Android NDK | `28.2.13676358` | Upstream pinned compiler/runtime headers |

Primary sources:

- [Native Android architecture and limitations](https://github.com/chasem-dev/sms-pc-port/blob/3f661cfda9d160425b46fea22ee3a81755eb2f55/docs/ANDROID.md)
- [Matching Android build/import guide](https://github.com/chasem-dev/sms-launcher/blob/59ef545af26c9ef5026154415d8a5ae5db14c514/android/README.md)
- [Preview 18 release provenance and testing limits](https://github.com/chasem-dev/sms-launcher/releases/tag/android-pr-107-v18)
- [Native CMake build](https://github.com/chasem-dev/sms-pc-port/blob/3f661cfda9d160425b46fea22ee3a81755eb2f55/CMakeLists.txt)

Live GitHub API inspection found PRs [native #98](https://github.com/chasem-dev/sms-pc-port/pull/98)
and [app #107](https://github.com/chasem-dev/sms-launcher/pull/107) closed **without
merge**. The public branches/commits and official preview release remain available.
Do not assume desktop `main` contains Android support, and do not describe the
Android branch as a merged upstream release. Web search cached older PR statuses;
the API state was checked directly during this research.

## Why this is the shortest credible route

The Android branch already addresses issues particularly relevant to Snapdragon
handhelds: Adreno shader compilation, safe alternate low-memory arena placement,
J3D texture/palette addresses after arena relocation, GLES pixel metrics/depth
peeks, worker scheduling at frame waits, and physical-controller configuration.
Its recent fixes are a reason to prefer its pinned version over an older fork;
they are not evidence that this exact build works on Thor.

The source builds `libsms.so`, `libsmsbootstrap.so`, and `libSDL2.so`. The
bootstrap loads the game library into a reserved compatible low address range
using `android_dlopen_ext`. This is needed because some decompiled game resource
structures still hold addresses in 32-bit fields. The process and game remain
64-bit; no ARM32 package or PPC runtime is implied.

The app requires Android 10+, ARM64, GLES 3.1+, and a controller. Upstream uses
SDK 35, build-tools 36.0.0, CMake 3.22.1, JDK 17+, and Python 3. Its APK build
has a CMake `smsPortRoot` parameter and `smsSdlRoot` override. Native source
compilation is capped at two jobs by default, independently of Gradle workers.
The exact app source uses AGP 9.0.1 / Gradle 9.2.1. Reusing this workspace's
older AGP/Gradle or NDK 29 is a local toolchain adaptation requiring a fresh
build; no source evidence found establishes NDK 28 as uniquely necessary.

The original game is compiled from the decompilation submodule plus authored
compatibility patches under the build tree. There is no reason to translate the
owner's DOL through the NSMB translator merely to reach the first Sunshine boot.
The game still needs the owner's original disc assets at runtime.

## Owner input and launch integration

This native Android importer accepts **GMSE01, North America revision 0**, raw
ISO/GCM. It does not promise RVZ import. Convert the owner's RVZ locally with an
existing verified disc tool, validate its header/revision and full-disc checksums,
and preserve the original RVZ. Different versions supported by the matching
decompilation are not automatically supported by this native source port.

The companion's original package is `dev.chasem.smslauncher`. `GameActivity` is
not exported and runs in a separate `:game` process; it reads the imported disc
from private `files/disc/game.iso`. A Thor integration should provide an explicit
validated launch entry, then start this private activity internally. It must not
export `GameActivity` and allow arbitrary arguments or filesystem paths merely
to make the carousel work. The existing NSMB transport is a useful pattern.

Game settings, bindings, saves, and logs are provided through dedicated private
paths. Android Back finishes the game activity and the session process is ended,
so the next launch starts fresh. A return bridge should target the shared Thor
launcher without copying the existing native heaps/session state across games.
Keep MKW/RR/NSMB storage, signing, settings, saves, and existing cases separate.

## Alternatives inspected

- [doldecomp/sms](https://github.com/doldecomp/sms): owner's reference, explicitly
  work in progress. Matching console executable reconstruction is not an Android
  runtime. Its broad ISO/RVZ/etc. intake support does not transfer automatically
  to source-port importers.
- [jbcgames/sms-android](https://github.com/jbcgames/sms-android), pin
  `cb8208c47e9a93a8fd994bab0714251b9c4075f9`: existing native ARM64/GLES3 Android
  source port with controller and touch input. Uses older decomp pin `03a91d6...`
  and NDK 26.3 in its docs. A real candidate, but not selected here over the
  more detailed memory/Adreno regression work above.
- [TheM3dium/mareshine](https://github.com/TheM3dium/mareshine), pin
  `0b36b2d100c7299b9e645ae26cd8b8093fc504f1`: fork of jbcgames, adds extensive UI,
  controller settings, shader warmup and media options. Potential source of
  future fixes. Its own README is not evidence of Thor performance or full-game
  correctness. Do not import optional texture/movie downloads into this task.
- [SunPad](https://github.com/chrissotraidis/sunpad): Apple-specific static
  recompilation with Metal and Dolphin-derived runtime. Its README explicitly
  describes interpreter/JIT fallback depending on platform. It is useful research,
  not an existing Android native source-port drop-in and not equivalent to the
  no-PPC-CPU source-port route above.
- [DolRecomp](https://github.com/ExpansionPak/DolRecomp): native-code translation
  tooling, not a complete game runtime. Taking this route still needs the full
  GameCube service stack and game-specific configuration.

Reusing WitherBlitz NSMB or DriftDroid directly would require a Sunshine game
translation/configuration, GameCube-specific SDK and hardware behavior, complete
DVD/memory-card/audio handling, and integration tests. Those runtime families can
inform Thor display, controls, and launch/return behavior, but neither existing
game-specific build is proof of Sunshine execution. An already authored native
Sunshine Android runtime avoids doing that work again.

## Verification still required locally

No download, build, install, or gameplay test was performed by this research
agent. Before calling the port working: build the exact locally adapted source,
verify ARM64-only packaging/signature, stage only owner data, inspect real title,
file selection, unskipped intro, first stage, movement/jump/camera/FLUDD, save and
restart, Android Home/Back, and carousel launch/return. Report lower-screen work
only if implemented and tested separately. No 60/120 FPS, audio, thermal, haptic,
full-game, or stable-session claim follows from upstream test counts.

## Search record and credibility

Queries: `Super Mario Sunshine native port recompiled Android GitHub`,
`Super Mario Sunshine PPCRecomp GitHub native recompilation`,
`site:github.com sunshinecompiled OR SunshineRecomp`. The installed knowledge
base command `um kb search "Super Mario Sunshine"` returned no matching field note.

Primary repository READMEs, exact raw source files, release notes, Git trees and
PR API metadata were inspected. Search-result aggregation and community posts
were used only to discover the repositories, not as performance proof. No dead
source required archive recovery. No game image, game attachment, binary APK,
extracted asset, or private credential was downloaded by this agent. No visual
claim was made and no screenshots were captured.
