# Thor: after the intro (Claude, 2026-10-19)

## Fixed: freeze on the title ("faint logo")

On the Thor, MEM1 cannot sit at 0x80000000: ART's large object space occupies
0x75943000-0x95943000 in every app process (inherited from zygote), so the port
relocates MEM1 to 0x40800000. `TDrawSyncManager::threadFunc` told FIFO pointers
from its shutdown message by `msg >= 0x80000000`; a relocated FIFO pointer
(0x40xxxxxx) read as shutdown, the thread exited on the first breakpoint push,
and the next draw-sync token blocked the game thread forever on the full
20-entry queue (all emulated threads idle, Thread-4 in the idle loop).

Evidence: /proc/<pid>/task/*/syscall SP + /proc/<pid>/mem stack scan, symbolized
with the unstripped libsms.so (`OSSendMessage` <- `GXSetDrawSync` <-
`TMarDirector::direct` line 238); the scheduler's g_threads read from memory:
the DrawSync OSThread was MORIBUND (8) while main waited on its queue.

Fix: decomp-patches/android-04-drawsync-relocated-fifo.patch, under TARGET_PC the shutdown is
exactly 0x10000 and anything above it is a FIFO pointer. Verified on the Thor:
title screen ("PRESS START") and the memory-card prompt render; the game no
longer freezes.

## Fixed: every model drawn as shredded triangles (and the scene dark)

`J3DLoadArrayBasePtr` (J3DTevs.cpp) wrote `(u32)ptr & 0x7fffffff` as the CP array base: the
retail shortcut is a physical address only when MEM1 is at 0x80000000. With MEM1 at
0x40800000 every J3D vertex/normal/colour array pointed 8 MiB high (0x40DB4480 drawn from
0x415B4480). Found by instrumenting GDSetArray (correct, arena-relative) and the CP_ARRAY_BASE
handler (alternating good and raw values), then comparing the two memory ranges.
`decomp-patches/android-05-j3d-array-base.patch` uses the arena-relative OSCachedToPhysical on
Android (as android-03 does for textures). Both fixes are patch files; decomp/ stays pristine.

Verified on the Thor (release ee9cd938...): title, file select and Delfino Airstrip gameplay
render correctly at 16:9 with 1080 lines; 59.9 fps measured (SurfaceFlinger) at 60, panel mode
120 Hz and 118.6 fps at the 120 setting.

## Settings and display (sms-launcher-thor)

Thor defaults applied once (`thor_defaults`): frame rate 60, resolution "1080p" (scale 2.0455 of
the 528-line frame), widescreen 16:9; 120 FPS is a Game settings choice. GameActivity picks the
panel mode for the rate (60/120 Hz) and requests it on the surface; system bars are re-hidden
on focus/resume.

## Lower screen

`ThorSunshineScreen` (Presentation on "Screen-2", not focusable/touchable): area plate (disc's
sc_map_* art, or the area name for Delfino Plaza/Airstrip), episode or "Secret course", Shines,
blue coins, coins and lives in the game's coin digits, FLUDD tank level and nozzle. Data:
`android/thor_snapshot.cpp` publishes a read-only snapshot once per frame from
port_vi_frame_wait on the game thread (pointers followed only inside MEM1; seqlock), and
smsbootstrap's JNI `nativeSnapshot` copies it. The area comes from the game's own scenario
archive table (TApplication::unk30 + mCurrArea). Art: `Extract-ThorHud.py` (Yaz0/RARC/BTI) from
the owner's disc to build/sunshine-thor/thor-hud, staged to files/ThorHud (never packaged).
Verified: title card, Delfino Airstrip with Shines/blue coins/coins 00, lives 03 (matching the
upper HUD), "No FLUDD" before FLUDD is found. Not yet seen live: the FLUDD gauge and nozzle.

## Launcher

Case four art is composed from the owner's disc art (compose_case_art.py) until retail art is
supplied; launch from the carousel and Back to the library verified (game process exits).

## Notes
- File select: A is ignored while Mario is walking between blocks (stick moves); settle first.
- A session left in the background for ~50 minutes resumed to a black screen (Codex's run3);
  short backgrounding resumes correctly. Not reproduced with the fixed build.
