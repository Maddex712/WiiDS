# Wii first-run bootstrap payload

This directory contains the small `shared2/wc24` first-run tree needed when Mario
Kart Wii is launched directly without an imported Wii NAND. The runtime copies
these files only into a newly managed, per-user NAND and never overwrites an
existing profile.

The layout and required files follow Dolphin's direct-disc Wii bootstrap:
`Source/Core/Core/WiiRoot.cpp` and `Data/Sys/Wii/shared2/wc24`.
