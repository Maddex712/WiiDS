# Sunshine Thor work log

2026-10-19: owner supplied Sunshine RVZ and requested native execution plus the
shared launcher case. Full reconstructed disc matches Redump GMSE01 revision0;
original Downloads RVZ/archive remain untouched. ISO conversion and extraction
are under ignored build/sunshine-thor. No game downloads.

Chosen runtime is chasem-dev's native ARM64 Android source port, with its pinned
decompilation submodule and matching Android shell; see MODDING_PLAN.md and
RUNTIME_RESEARCH.md. Not a direct Android build of the linked matching decomp.

Additive fourth-case transport compiled against Android API34. Claude's carousel
renderer and NSMB companion are untouched. Owner explicitly authorized switching
from NSMB to Sunshine. Native source compilation completed, the separate signed
release and four-case launcher were installed and device hashes matched. All five
MKW native library entries match the prior installed launcher byte for byte.

Actual fourth-case open/cancel/launch was observed. The first native run rendered
the original opening movie using the owner disc, native ARM64 game and Adreno740.
Later runs remain at the faint logo, including a run with no early controller
input. The second run loaded70 shader programs and finished warm-up; this alone
does not establish a shader-cache defect. Non-root debuggerd is denied. A bounded
opt-in in-app thread/context probe is being prepared before choosing a fix.

SDL's unbounded native-thread join prevented its intended own-process cleanup on
Back. A Java-only bounded own-game shutdown guard was built, installed and tested:
Back returned to the library, logged the stalled UI/SDL threads and ended only
Sunshine's game process. This fixes terminal return; native startup still needs
investigation. No title/gameplay, physical controls, save persistence or long-run
performance claim. Current private receipt: build/sunshine-thor/device-verification.json.

2026-10-20 overhaul recommendation research (no install or device input):
Queries: "Super Mario Sunburn official mod github installation";
"Super Mario Eclipse official mod 240 shines native sms pc port android";
"site:github.com/chasem-dev/sms-pc-port Sunburn Eclipse Android".
The installed um knowledge-base search produced no output before cancellation;
no knowledge-base findings are claimed.

Primary sources checked:
- https://github.com/JoshuaMKW/Super-Mario-Sunburn describes a Kuribo module,
  not a demonstrated drop-in native Android mod.
- https://github.com/chasem-dev/sms-android and its README now document a
  separate ARM64 Eclipse APK, original-disc import, approximately 851 MB mod
  download, verification and on-device patching, with separate disc/saves.
- https://github.com/chasem-dev/sms-android/releases lists v1.0.0 on October 20,
  with an Eclipse music-start relocation fix and preceding memory fixes.
- https://github.com/chasem-dev/sms-android/issues still lists address-range
  issue #11; release fixes do not establish physical Thor performance.
- https://github.com/JoshuaMKW/Super-Mario-Eclipse is the mod source. Existing
  local docs/ECLIPSE.md in reference/sms-android describes its native hooks.

Recommendation: Eclipse is the best-supported overhaul integration candidate
found, superseding the earlier desktop-only assessment. Upstream installation
is already automated. Our older customized com.wither.sms.thor build still
needs deliberate integration to retain the companion, launcher and fixes;
not a claim that a downloaded mod works in the installed app. Official docs
explicitly do not claim AYN Thor performance measurements. No mod/APK downloaded,
no patch/build/run tested, no screenshots captured; evidence here is documentary.
