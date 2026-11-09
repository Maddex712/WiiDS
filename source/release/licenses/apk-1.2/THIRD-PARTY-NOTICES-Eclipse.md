# Super Mario Eclipse 1.1.0

Creator: **Eclipse Team**, including JoshuaMKW and the contributors credited by
the official project and in-game credits. The original credits remain in the
unmodified official mod payload.

- Official release: https://gamebanana.com/mods/536309
- Mod source: https://github.com/JoshuaMKW/Super-Mario-Eclipse
- Release license: Creative Commons Attribution-NonCommercial-NoDerivatives 4.0
  International (CC BY-NC-ND 4.0), as recorded by the pinned native port's
  `docs/ECLIPSE.md`.
- License URI: https://creativecommons.org/licenses/by-nc-nd/4.0/
- Complete legal code: `LICENSE-CC-BY-NC-ND-4.0.txt` in this folder, obtained from
  https://creativecommons.org/licenses/by-nc-nd/4.0/legalcode.txt.

The official 1.1.0 VCDIFF patch is included **without changing its bytes** as
`assets/mods/eclipse-v1.1.0.xdelta`. Its original archive member was
`patches/v1.1.0.xdelta` in `super_mario_eclipse_v110.7z`:

- Patch length: 1,191,516,980 bytes.
- Patch SHA-256: `25c6b08333802bca17c1e6571c7289a6382a32a85af1aa5bd224acfbac6a8bf0`.
- Archive SHA-256: `51a9144c59fffff275e6484705325501a687af78f5247d550137169b481f0407`.

Its APK ZIP compression and filename are packaging changes only. WiiDS applies
the patch locally to the player's own supported Sunshine disc and verifies the
result. Neither the original disc nor the patched disc is redistributed. WiiDS is
a noncommercial, unofficial project and does not claim Eclipse Team endorsement.
The mod's license includes a disclaimer of warranties; see its complete legal code.

The archive's separate `license.md` is MIT, copyright 2024 Mr. Mendelli, and applies
to its accompanying shell patcher rather than replacing the released mod's license.
That file and its README are retained verbatim as `LICENSE-MIT-Eclipse-patcher.md`
and `README-Eclipse-patcher.md`. The shell patcher executable/script is not used by
the Android installer.

The Eclipse/BSE/SunshineHeaderInterface/BetterSunshineMoveset **code** is separately
GPL-3.0; its pinned sources, adaptation/build scripts and applicable notices travel
with the corresponding-source package. The Android VCDIFF decoder is adapted from
`sms-pc-port/tools/mods/vcdiff.py`, MIT, copyright 2026 SoftwareAura LLC; see
`LICENSE-MIT-sms-pc-port.txt`. These code licenses do not replace the mod payload's
CC BY-NC-ND 4.0 license.
