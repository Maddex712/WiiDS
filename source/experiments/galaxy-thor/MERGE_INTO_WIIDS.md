# Folding the Galaxy add-on into WiiDS: feasibility (2026-10-22)

## Why Galaxy is a separate app today

The port runs the game in the Wii's own address layout, set in `platform/include/port/port.h`:

| Region | Address |
|---|---|
| MEM1 (cached) | `0x80000000` |
| MEM2 (cached) | `0x90000000` |
| Game library slot | `0x98000000` |
| Low pool | `0xA0000000`–`0xB0000000` |
| Uncached mirrors | `0xC0000000` and `0xD0000000` |
| Locked cache | up to `0xE0010000` |

In any app forked from Android's zygote, ART already owns part of this range. So the add-on is
debuggable and uses `wrap.sh` plus `libreserve.so` to grab the window before ART starts. WiiDS is
a release app and must stay non-debuggable, so it can't do that.

## What a zygote-forked (WiiDS-like) process looks like on the Thor

Measured with `Probe-Memory.py` (version 2, test app `dev.thor.galaxyprobe`, non-debuggable).
The map below 4 GB is the same on every launch:

| Range | Contents |
|---|---|
| `0x12c00000`–`0x32c00000` | ART main heap |
| `0x32c00000`–`0x6fdc9000` | **free, 977 MB** |
| `0x6fdc9000`–`0x7600d000` | boot image, zygote spaces |
| `0x7600d000`–`0x9600d000` | ART large-object space (512 MB) |
| `0x9600d000`–`0x9e00d000` | JIT caches |
| `0x9e00d000`–`0xebad6000` | **free, 1242 MB** |
| `0xebad7000`–`0x100000000` | **free, 325 MB** |

So only cached MEM1, cached MEM2 and the library slot clash. The uncached mirrors and the locked
cache are already free.

## A layout that fits

| Region | Today | Proposed |
|---|---|---|
| Cached base (MEM1, MEM2, lib, low pool: 768 MB) | `0x80000000` | `0xB0000000`–`0xE0000000` |
| Uncached base (MEM1/MEM2 mirrors) | `0xC0000000` | `0x40000000`–`0x58000000` |
| Locked cache | `0xE0000000` | `0xE0000000` (unchanged) |

Why these choices:
- Every cached pointer stays at or above `0x80000000`. The game uses "≥ `0x80000000` means a
  pointer" checks: `lyt_texMap.cpp:73`, `DrawSyncManager.cpp:81`, `Overwrite.cpp:1057`.
- `OSUncachedToPhysical` masks with `~OS_BASE_UNCACHED`. A base of `0x40000000` keeps that mask
  disjoint from physical addresses (below `0x30000000`).

## What has to change

1. **The layout constants.** `port.h` (`PORT_MEM1_BASE` and the rest), `OS_CACHED_REGION_PREFIX`
   and `OS_UNCACHED_REGION_PREFIX` in `decomp/libs/RVL_SDK/include/revolution/os.h`, `kWindowBase`
   and `kLibBase` in `platform/android/launcher.c` and `reserve.c`, and the diagnostics in
   `os_diag_heap.cpp`. The OSPhysicalToCached-style macros (24 uses) then follow by themselves.
2. **Physical to host conversion.**
   - `port_phys_to_host` and `port_host_to_phys` in `platform/src/os/os_mem.cpp`.
   - The "virtual or physical?" tests: `gpu_cp.cpp:19` and `ai_dsp.cpp:35` (`addr >= 0x80000000`).
   - Masks that strip the top bit to make a physical address: `J3DShape.cpp:126`
     (`& 0x7FFFFFFF`) and `GXFifo.c` `TOPHYSICAL` (`& 0x3FFFFFFF`). With the new base these give
     `0x30000000 + phys`. Make the decoder accept both forms (true physical `< 0x30000000`, masked
     cached `0x30000000`–`0x60000000`).
3. **Fixed low-memory reads** (`*(T*)0x800030C8` style). They appear only in the crash handler
   (`JUTException.cpp`) and asserts (`db_assert.cpp`). Point them at `OSPhysicalToCached(...)`.
4. **ARAM base.** `ARALT_AramStartAdr = 0x90000000` (`JKRHeap.cpp:19`) must move to the new MEM2
   base (check how `aralt` uses it).
5. **Folding in.**
   - Build libgame, libmain and libreserve into WiiDS (no `wrap.sh`).
   - Move the helper's Java (`dev.thor.galaxy.*`: GalaxyActivity, lower screen, settings menu,
     importer) into WiiDS as a `:galaxy` process.
   - Change the hard-coded data path `/sdcard/Android/data/io.github.maddex712.wiids.galaxy/files/GalaxyData`
     to WiiDS's own folder.
   - Bring the existing game data and saves over: re-import, plus a one-time save copy.
   - Then retire the add-on (the owner uninstalls it).

## Risks

- A literal search can miss address math done through variables. Anything missed shows up as
  broken textures, models or audio. It's quickest to find with a run through the title, file
  select and a few galaxies.
- Migrating saves between the two apps needs a one-time copy step.

## Plan

1. **Relocate and prove it.** Change the layout, build, and run the existing add-on non-debuggable
   without `wrap.sh` (the same kind of process WiiDS has). It must reach the title and gameplay.
   PC work, then about 10 minutes on the Thor.
2. **Fold into WiiDS.** Packaging, process and data path; move the data and saves.
3. **Regression run.** Title, file select, a galaxy, lower screen, settings, Quick Save, import.
