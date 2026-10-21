# ARM64 Android

Android builds use the native PC port, SDL 2.32.10, and a GLES renderer.
No GameCube CPU emulation is involved. Build an APK through the Android
project in the accompanying SMS Launcher checkout; its Android README
describes the SDK, JDK, install and disc/save import steps.

To compile just the native libraries on a Linux host:

```sh
export ANDROID_SDK_ROOT=/path/to/android-sdk
./android/build-native.sh
```

NDK 28.2.13676358 is the pinned toolchain. The result in
`build/android-arm64/` includes `libsms.so`, `libsmsbootstrap.so` and
`SDL2/libSDL2.so`. These libraries run inside SDLActivity; they are not
standalone shell executables. All Android targets are `arm64-v8a`, PIC,
and use the NDK's 16 KiB-compatible ELF alignment. The launcher unpacks
native libraries to permit the bootstrap's explicit game load.

The game remains 64-bit code. The existing decompilation still stores some
pointers in 32-bit fields, so the bootstrap reserves a low address range
and loads the game library there with `android_dlopen_ext`. The existing
MEM1 and low-stack allocators preserve those fields, without changing
Android's normal address space for SDL, libc, the graphics driver or Java.
This is an internal layout compatibility constraint, not a 32-bit APK.

GLES 3.1 or newer is required. Base-vertex drawing uses GLES 3.2 or its
extension when available, with an index-rebasing fallback for GLES 3.1.
Ordered metric passes rebase each range once from retained CPU indices;
stream uploads avoid synchronous GPU readbacks and map/unmap round trips.
Android shader adapters cache uniform locations and retained values, with
invalidation on program deletion, linking and binary loading. Shader generation
uses GLSL ES, with explicit fragment locations and precision. Desktop-only
depth-range/clear-depth, point-size and multi-draw calls have GLES adapters.
Color logic operations use a destination snapshot per draw that writes colour. Numeric
pixel metrics use SSBO counters and ordered drawing, preserving counts that
GLES boolean visibility queries cannot provide. MSAA stays off on Android.
Clip distances and depth clamp use their GLES extensions when available.
The clipping extension predeclares `gl_ClipDistance`; constant indices size
its array implicitly. Avoid a standalone redeclaration, which the Adreno
750 V@0762.41 driver rejects with a reserved-built-in-name compile error.
Without those extensions the vertex shader clamps forward-facing depth;
this fallback passes the shared graphics tests but is not a substitute for
testing every scene on each device's driver.
Depth peeks sample the depth texture into RGB bytes because GLES does not
permit desktop depth readback. Sampler LOD bias is supplied in the shader.

The renderer adapts the shared GX code rather than maintaining a second
copy. Desktop paths remain available and should pass the existing GX tests.
Android crash reports use the NDK unwinder and module offsets in the game
log, rather than desktop `execinfo` or an external `addr2line` process.
Resolve offsets offline using the matching unstripped `libsms.so` and the
NDK's `llvm-addr2line`.

The launcher isolates each game session in its own process. Closing its SDL
window releases the renderer, flushes the game log, and ends that process
without running global destructors alongside still-running SDK workers.
The next Play starts with fresh heaps, threads, and caches. If an Android
frame is already due and lower-priority SDK work is runnable, frame pacing
temporarily blocks the frame producer for a cooperative background turn.
It resumes when workers block/exit, or at a scheduling checkpoint after a
1 ms budget. This preserves stage-loader progress after movies without
forcing a late frame to wait for another retrace. The budget does not
preempt uninterrupted native code; workers must reach SDK checks/yields.

For renderer diagnostics, `SMS_GX_FINISH_BEFORE_SWAP=1` forces `glFinish`
before presentation and logs render-completion wait and buffer-swap time
separately every 30 frames. It is off by default. This serializes rendering
and is not an asynchronous GPU timestamp or a normal performance benchmark.
The overlay's ordinary swap time includes work the driver finishes inside
`SDL_GL_SwapWindow`; its explicit GPU-wait field excludes that wait.

Validation must include installing the actual ARM64 APK, title/file selection,
an imported save, stage gameplay and unskipped movies. ARM64 translation in
an Android emulator is suitable for correctness checks, but physical handheld
testing is still needed for performance, thermal behavior and controller feel.

Android 13+ native performance hints register the SDK thread producing frames and
its GL worker when enabled (Android normally keeps both on one thread) with APerformanceHintManager. Each SDK frame wait reports the
work cycle since the last wait ended, then starts the next cycle after the
intentional pacing sleep. The budget follows the actual retrace count per
frame (including 30 fps menus/movies and 60 fps gameplay). Work includes
synchronous driver/graphics waits; it is not a GPU timestamp or CPU utilization
counter. These advisory CPU scheduling hints do not lock clocks or override
thermal limits. Missing/rejected APIs disable the session once without
changing game timing. Runtime symbol lookup keeps Android 10/11 supported.
Backgrounding closes the session and discards partial frame timing; returning
recreates it. Normal game shutdown explicitly closes the session. See
`platform/android/tests/test_performance_session.cpp` for thread-group,
budget-update, pause/resume and service-failure checks.

The Android launcher declares the application category as game. Its
**Android performance hints** switch defaults on; switch it off for a device
comparison (`SMS_ANDROID_PERFORMANCE_HINTS=0`). The **Show performance overlay**
switch reports our own frame rate/timings rather than only SurfaceFlinger FPS.
A device's game policies and clocks remain under Android/OEM control.

The opt-in **Request performance headroom** setting requests 80% of the
actual frame interval as the ADPF work deadline (13.35 ms at nominal 60 FPS),
reserving 20% for presentation/scheduling variation. Measurements remain
unaltered and game cadence is unchanged. `SMS_ANDROID_PERFORMANCE_HEADROOM=0`
restores the v8 full-frame deadline. This candidate is intended to test whether
an earlier deadline improves the tester's low-clock behavior; it has not been
validated on Samsung hardware. The tester reported v9 was substantially
slower, near 30 FPS until a screenshot boosted it, so v10 defaults to the
original full-frame deadline and no display-cadence request. The cause of
that regression is unverified. Android 15's optional power-efficiency setting
is explicitly set false on each new session; false is already the OS default,
so this is not itself evidence of a boost. Errors in that optional request do
not discard a working basic hint session.

The launcher sends a Surface frame-rate request for the selected 30/60 FPS on
Android 11+, with a switch for comparisons. It reads Game Mode on each resume
and reports interruptible gameplay/no gameplay via Android 13+ Game State on
resume/pause. It does not claim uninterruptible play or continuous loading.
No game-mode configuration disables OEM interventions; the user selects
Performance mode in the device's game dashboard. Startup policy and display
information are snapshots, not proof of a policy's continued enforcement.

`SMS_TIMING_LOG=1` enables low-rate, hidden-overlay diagnostics: the overlay's
one-second game/shown FPS and frame-time windows are logged roughly every
two seconds. Native workload and SDK-pacing averages/maxima log every 120
frames. Native workload includes wall time and main-thread CPU time, plus a
point-in-time CPU core/frequency read when permitted (-1 unavailable).
Detailed renderer CPU timers remain enabled even when the overlay is hidden;
no overlay bitmap is drawn or uploaded for logging alone. Logging has some
overhead. These are not asynchronous GPU timestamps. Use a stationary scene
and export the log after 30 seconds without recording, notifications or screen
touches; export before another Play replaces it.


`SMS_ANDROID_PERFORMANCE_CORES=1` (launcher **Use performance CPU cores**)
is an independent, default-off core-placement experiment. The current frame
producer and optional GL worker query their own allowed CPU mask and rank
those CPUs using complete `cpu_capacity` metadata, falling back to complete
`cpufreq/cpuinfo_max_freq` metadata. Ranking sources are never mixed. The
selected group includes cores at least 75% of the peak ranking, only when
it is a distinct subset with at least two CPUs. Uniform or missing topology,
an already-fast/narrow allowed mask, a lone prime or syscall rejection leaves
placement unchanged. CPU IDs are discovered, not hard-coded. This influences
where frame work runs; Android still chooses clocks and applies power/thermal
policy. Helpers/UI are not explicitly assigned a new mask; any threads later
created by a restricted thread inherit its Linux affinity normally.

The policy runs once per participating native thread before hint initialization,
so ADPF can be disabled for a core-placement comparison. The launcher uses a
fresh game process for each Play, so toggling off discards the prior restriction.
Requested/effective CPU masks or the fallback reason are logged. The host test
`platform/android/tests/test_performance_cores.cpp` covers big+prime tiers,
sparse/permuted IDs, incomplete metadata, homogeneous/already-fast masks,
a lone prime, the threshold boundary and arithmetic overflow. No v11 Android
runtime or performance validation has been performed locally; the candidate
needs a physical-device comparison, not an ARM64-translating container benchmark.

The S24 Ultra tester confirmed v11 applied the requested faster-core mask,
but reported unchanged performance with that option alone. Hints/headroom
combinations also had no effect. Their all-options-off timing log shows
roughly 11–13 ms average frame work and 0.5–0.6 ms swaps, yet frequent
33–35 ms frames and 29–66 forced background yields per 120 frames. Frequency
samples reach 3.15 GHz. These measurements support investigating the extra
retrace in our old background fallback rather than attributing every drop
to low clocks. Timing windows are not perfectly aligned and CPU frequencies
are point samples; these logs do not establish the cause of every slow frame.

v12 replaces that fallback with the short cooperative handoff above.
SDK-pacing logs additionally report background-handoff average/max wall time.
Run `bash platform/os/tests/check_background_yield.sh` on a Linux x86_64 host
to test the Android-configured scheduler and actual frame-wait path without
a ROM, graphics context or VI timer. It checks worker progress, early return,
deadline handling through yields and IRQ polls, priority, IRQ state, guards
and worker exit. An already-due frame must return without another retrace.
This is host regression coverage; device loading/FPS require tester validation.

The tester reports v12 is much faster and supplies sustained nominal-60 FPS
4:3 gameplay (about 12.6–12.8 ms average work), with hints/cores disabled.
Their widescreen log still has roughly 55–60 FPS sections with 17–18 ms work.
These are not a controlled matching-camera benchmark and frequency samples
do not identify the cause of every slow frame.

v13 reduces CPU work in widescreen draw placement. Known centred artwork
and pane-anchored 3D HUD pieces bypass unnecessary screen-bound transforms.
The remaining scans visit each cached vertex run once and reuse consecutive
position matrices, preserving arithmetic order. Projection-aspect threshold
calculation moves to widescreen setup. Compilation and floating-point rules
are preserved. A broader -O3 experiment exposed graphics failures with the
optional GL worker on the host and is not included in this candidate.
Resolution, FOV, frame targets and clock policy are unchanged. Timing logs
add existing vertex/draw/texture/copy/peek timers to identify remaining costs.
Host gx_extent_test compares 1500 varied fixtures bit-for-bit with the prior
algorithm; gx_widescreen_test validates real rendered placement at 1x/2x.
Its CPU routine benchmark measures only bounds scanning, not device FPS.
Physical-device performance still requires comparison; do not run the
Android container/emulator for this follow-up.

An Adreno 740 device running v13 reported a display-list vertex-read crash
after MEM1's preferred 0x80000000 mapping failed. The old fallback placed
the arena at 0x7126bc8000, while arena rounding and legacy raw pointer fields
truncated its addresses. SDK cached/physical conversions and GX's default
MEM1 window also still assumed the preferred address. This is an address
layout bug in the port's 64-bit compatibility layer.

v15 retains the preferred arena when available, otherwise searches for a
complete pointer-compatible mapping without replacing occupied pages. The
low allocator checks fragmented address gaps at 1 MiB intervals; MEM1 may
also use the upper half of the 32-bit compatibility window. Both arena
endpoints must fit raw u32 fields (an ArenaHi exactly at 4 GiB is rejected).
No arbitrary high heap fallback is accepted. Android SDK physical addresses
are relative to the actual arena; CPU cached/uncached aliases share it. The
renderer receives that base/size before the game emits commands, and native
boot-info writes use the actual arena. Existing mappings are preserved.
The application and game remain ARM64; legacy address fields are not a
32-bit process or a limit on the device's total memory.

Run `bash platform/tests/check_mem1.sh` on Linux x86_64 for a host regression
test of the actual Android-configured runtime plus patched SDK header. It
blocks the preferred range, verifies alternate placement/round-trips/GX
setup, preserves sentinel mappings, tests fragmentation and upper-half
placement, and rejects an exhausted window including the 4 GiB endpoint.
It runs without a ROM, GPU or Android runtime. Device loading still requires
testing; do not run the Android container/emulator for this follow-up.


v16 corrects two J3D display-list writers that used `OSPhysicalToCached`
on an already-cached texture/palette pointer as a retail-address shortcut.
It happened to yield an arena offset at 0x80000000 after u32 truncation,
but fails at relocated addresses such as the tester's 0x40800000. Android
now uses `OSCachedToPhysical` before packing the texture-image/TLUT load
commands. The register IDs, palette selection/flush sequence and desktop
writers are preserved. This addresses the remaining bad texture addresses;
actual image recovery after the intro still requires device confirmation.

`bash platform/tests/check_j3d_addresses.sh` compiles the actual JRenderer
writers and Android SDK patch, confirms the old writers fail relocation,
then checks emitted texture/palette bytes, all eight texture slots, aligned
addresses across a 64 MiB arena at preferred and relocated bases, round trips
through the GPU address format, bounds and the palette command sequence.
It also checks the unchanged desktop path at its original base. No ROM or
Android runtime is run. v15 artifacts remain unchanged.

Two more retail-address assumptions left the picture black from the title
onward, with audio still playing, when MEM1 is relocated (an Adreno 650
device at 0x40800000). `J3DLoadArrayBasePtr` packed vertex-array bases as
`(u32)ptr & 0x7fffffff`, which is an arena offset only at 0x80000000, so
every J3D shape read its positions, normals, colors and texture coordinates
from the wrong place. Android now packs `OSCachedToPhysical(ptr)`. The draw
sync thread told FIFO pointers from u16 tokens by `msg >= 0x80000000`; a
relocated pointer fell into the shutdown range, the thread exited, and the
game thread then blocked sending to its full queue. Android now treats any
message above the 0x10000 shutdown value as a pointer. Desktop code is
unchanged in both places.

`bash platform/tests/check_j3d_array_addresses.sh` and
`bash platform/tests/check_draw_sync_pointers.sh` compile the actual
J3DTevs and DrawSyncManager code, confirm the old Android code fails at
relocated bases, then check the patched Android and unchanged desktop code.


Preview 17 adds an SDL mapping fallback for the reported 0810:0001 legacy
USB Gamepad. Android emits its generic BUTTON_1..12 keys as SDL raw buttons
20..31, and its hat as D-pad buttons 11..14. SDL's Android auto-mapper rejects
it because it has no named A/B/X/Y keys; the game only opens mapped controllers.
The fallback translates the existing SDL Linux PS1 profile for this VID/PID
into Android's ABI, with four stick axes and digital trigger buttons.

It runs only for an unmapped matching VID/PID with four axes, at least 32 raw
buttons, no raw hats and the expected Android GUID capability bits. Existing
mappings and other layouts are preserved. A raw joystick-add event installs
the profile and opens the controller immediately, because adding a mapping
alone does not generate another controller-add event. Instance-ID checks avoid
duplicate opens; existing disconnect and reconnect handling is retained.
The accepted mapping is logged. Desktop profile selection is unchanged.

Run `bash platform/gx/tests/check_android_controller.sh` for a host test using
SDL's virtual joystick (no video/Android runtime): exclusions, actual mapping
recognition, numbered-button/controller events, digital trigger pressure,
D-pad/sticks/releases, instance IDs and reconnect. Physical layout confirmation
is still needed, especially shoulder assignments and right-stick orientation.
The launcher's Input test remains useful for collecting that report.


Preview 18 supports user-configured layouts from the launcher's Controller
configuration screen. Saved per-descriptor overrides arrive in a private file
on each Play, identified by VID/PID and SDL's descriptor CRC. Native code
validates targets, bounded button/axis expressions and live device counts,
then merges only edited controls into the automatic mapping. `none` explicitly
unbinds a control; removing the profile restores automatic behavior next Play.
Unmapped devices can be opened after a player supplies assignments. Matching
uses stable descriptor identity rather than ephemeral Android input IDs.

The launcher's captured key/axis indices use SDL's Android backend. Nonzero
configured VID/PID pairs are added to SDL's existing HID ignore list before
initialization so they cannot use HIDAPI's different raw numbering. This
routing applies to matching model pairs; custom mode uses Android events and
has no SDL controller rumble. Unknown product-zero IDs never form wildcards.
Ordinary unconfigured models preserve their driver/mapping. Device Back can
be explicitly bound; system navigation Back still returns to the launcher.

Host Java checks cover Android raw buttons, SDL axis order, trigger rest/range,
inversion, CRC and serialized entries. Host SDL virtual-controller checks cover
profile file parsing/identity, malformed/out-of-range exclusion, override and
clear semantics, preserved defaults, events, inverted camera and half-axis
trigger calibration. Existing graphics checks and Android builds are separate.
No Android container/emulator is run. Configuration UI/capture/persistence and
physical gameplay application need tester validation; no FPS change is claimed.

Profiles may also bind `paddle1`/`paddle2` and single stick directions
(`-leftx`, `+lefty`, `-rightx` and so on). The launcher uses the paddles to
carry soft L and R presses, and binds them to `L_SOFT`/`R_SOFT` through the
`SMS_BINDINGS` file it passes on Play; a stick direction lets a button push
the stick fully that way while the stick's own axis keeps working. The SDL
virtual-controller check covers both.

A stick direction can also be given a stick axis (`-rightx:+a2` makes pushing
the stick right turn the camera left). SDL hands a raw axis value to the first
binding whose input range holds it, so the player's entries now come first in
the mapping, and a direction given an axis (or `none`) takes that direction
away from the stick's whole-axis binding: `rightx:a2` with `-rightx:+a2`
becomes `-rightx:+a2,+rightx:+a2`, and `a2~` swaps which half feeds which
direction. A direction given a button still only adds the button. Before
this, a direction row given a stick axis was appended after the automatic
`rightx:a2`, which always matched first, so the row did nothing. The host
check covers swapped and single direction rows, an inverted row split around
a cleared direction, and a direction taken from another stick's axis.
