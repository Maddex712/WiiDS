# Building the 1.1.2 Brawl integration

The main Android source is `reference/driftdroid/android`. Brawl's runtime/translator sources
are under `reference/brawl-recompiled`; their local worktree changes are included. The pinned
upstream base is BrawlRecompiled 0.4.0 (`7f3ac07`); no upstream root licence or distribution
permission has been recorded. The root WiiDS GPL licence does not purport to relicense that code.
Retain all component notices. See the prerelease notes for unresolved Nintendo-byte payloads.

Restore the upstream binary dependencies at their recorded pins. Generate Brawl's translation
privately from your own supported USA RSBE01 revision 2 disc using the project tools/config.
`experiments/brawl-thor/Configure-BrawlAndroid.ps1` documents the NDK29 arm64 flags; pass your
actual SourceRoot, BuildDirectory and ToolsRoot. Strip libmain.so with NDK29 llvm-strip, then
stage it in the Android jniLibs/arm64-v8a directory as libwiids_brawl.so. Keep the matching PNG
library as libwiids_brawl_png16.so. Build wiids/brawl-integration/brawl_jni.cpp as a shared library
using the NDK29 aarch64-linux-android30 target, -shared -fPIC -O2 -Wl,-z,max-page-size=16384,
-Wl,-soname,libwiids_brawl_jni.so and -llog -ldl; stage it as libwiids_brawl_jni.so.

The JNI bridge registers the relocated dev.wiids.brawl Java class against the existing engine's
17 exported methods. All three Brawl libraries have 16 KiB ELF LOAD alignment. The main Android
source reuses the compatible core SDL3 Java classes; Brawl has scoped data and separate runtime
processes in the same package.

Stage the audited build/wiids-merge/assets tree into the Android app's src/main/assets.
This tree contains the exact packaged runtime/presentation assets, including existing bootstrap
data and the three approved Brawl packaging scans; it is not a claim that all Nintendo data has
been removed. No Brawl lower-screen art, owner save, Project+ SD image or disc is included.
That lower art is generated on the device by BrawlArt.java after the player's disc import.

Build with JDK21 and Gradle8.8 using -PthorBuild=true -PthorRetroRewind=true -PwiidsApp=true
-PwiidsVersionName=1.1.2 -PwiidsVersionCode=1112. Use your own release key outside the source tree.
The prior game runtimes retain their existing build instructions and dependency pins. This is
an inspectable matching source snapshot, not a verified clean-machine build kit.
