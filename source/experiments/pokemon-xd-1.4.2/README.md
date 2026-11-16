# WiiDS Pokémon XD 1.4.2 (v42)

Release source overlay for the existing XD runtime; 1.4.1 plus the 1080p choice only. Native settings persist value 7 for 1080p, mapped to framebuffer scale 2.25 (480 * 2.25 = 1080); existing values and defaults are preserved. The 16:9 target is 1920x1080.

Source basis: https://github.com/imyourhuckleberryponcho/Pokemon-XD-PC-Port at dd95191c360e92fe8f9283cef5a354c296521e96 and its XD decompilation source at b0e9895801ea0a17b924c57e3eab86acc2691b12. Apply the platform/game overlays from native/ to their matching upstream source paths; Android/ contains the dedicated XD frontend. WiiDS-specific menu and presentation files are additions. This directory contains source only; disc data, generated game assets, save files and signing keys are excluded.

The focused v42 changes are XdSettingsMenu.java, xd_runtime_controls.cpp, standard_settings.cpp and platform/video/presentation.cpp, plus version metadata. 1080p follows the existing native pause/settings-save path. All other native and Java sources are the v41 baseline.
