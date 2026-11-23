# WiiDS 2.0 native source changes

The public 1.26.2 source tree is the base for the existing WiiDS runtimes. This directory adds the native changes carried by the 2.0 APK. The `android/` project in the 2.0 release ZIP uses the shipped ARM64 libraries so that the frontend and its bundled presentation can be rebuilt separately.

For the upstream projects in `UPSTREAM.json`, check out the indicated revision, apply the corresponding `changes.patch`, and copy the `added/` files to their recorded relative paths. GXRuntime is used inside Colosseum’s ModernGekko dependency. The Aurora changes for City Folk belong under its `extern/aurora` submodule.

- **Colosseum** uses the Aurora mirror Android host and the ModernGekko recompilation module template. The generation script documents how to produce module inputs from the required disc. The bundled host adds the WiiDS screen and battle-state hooks.
- **City Folk** uses its Android JNI source and Aurora changes. The release build used the Android ARM64 Dawn package and the app’s SDL3. Generated game data is prepared separately from a compatible disc.
- **Pokémon XD and XG** use the upstream XD runtime and the Android and companion code in the XD overlay. The combined and XG directories contain the replacement translation units, lower-screen changes and incremental build scripts used in the preview lineage.
- **WiiCompiled and Retro Rewind** use the hardened FaceLib reader, with replacement sources in `wiicompiled/`.

The incremental native scripts refer to their original staged build directories and assume the corresponding upstream builds and disc inputs exist. The supplied Android project is the direct build route using the release runtime binaries.
