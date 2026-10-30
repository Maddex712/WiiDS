# gn3_12 to 60 fps: Aurora per-draw CPU plan

## Source data
- Profile: `autotest\map-sweep-20261016-170916\Play\profile_stacks.txt` (12000 samples, gn3_12). It was resolved
  against the exe copy in that same Play folder, so the RVAs match. That run is the post-scan-cache-fix build:
  its log shows `fps 50-57, game 17.5-20 ms, gx 8.1 ms, lock wait 0.58 ms, 10042 draws`.
- Conversion used below: % of samples x ~18 ms (the game thread's frame). The profiler samples the game thread, and
  sleep and wait time are included, so % of samples roughly equals % of wall time.
- `[dll]` samples were attributed by their return address in the exe (scratchpad `attr.py` plus objdump):

| call site (exe RVA) | what | % samples |
|---|---|---|
| 24DD5B7 / 24DD5C6 / 24DD5DE / 24E03BC | 4x `steady_clock::now()` in `fifo::process` (ProcessTimer ctor, lockStart, after lock, ProcessTimer dtor) | 8.6 |
| 24E03B3 / 24DD5D9 | `recursive_mutex::unlock` / `lock` in `process` | 3.4 |
| 24E03BF/C7, 24DD5E0 (self) | 3 `lock add` stat atomics per `process` | ~1.4 |
| `XXH3_hashLong_64b_withSeed` < `cached_pipeline_state` | seeded XXH3 over the 2.9 KB PipelineConfig | 4.3 |
| 24EBA28 | `memcmp` of the whole PipelineConfig in `cached_pipeline_state` | 2.6 |
| GX__CallDisplayList self, 0xb13c0-0xb1440 loop | default-construct `HleGxVertexStateSnapshot` (208 VtxAttrFmt defaults = 2.5 KB of stores) every call | ~2.5 |
| GX__CallDisplayList self, 0xb1b50/0xb1bf0/0xb2500 | libc++ unordered_map scan-cache lookup (`div` + 3 dependent cache misses) | ~4.6 |
| `update_zfreeze_reference` incl. | CPU transform of the last triangle on every non-frozen draw | 2.7 |
| `GXDrawDone` -> `wait_for_frame_worker_sealed` | game thread idle, waiting on the frame worker | 10.3 |

The SPM diagnostics (`spm_dump_draw`, `spm_bbox_accumulate`, flat-test, merge-suspects, NSMBW_NO_MERGE) all exit on a
static flag and do not show in the profile. `update_zfreeze_reference` is the only per-draw extra that does real work,
and that work is functional (coplanar), not diagnostic.

## Patches (each applies on its own to the current tree, and they also stack; `patch -p1 --binary` from repo root)
Checked: all three dry-run apply cleanly, alone and stacked. The stacked command_processor.cpp passes `clang++ -fsyntax-only`
with the aurora_gx flags from build.ninja, with and without NDEBUG. Patched gx_dl.cpp passes `-fsyntax-only` (the file
was checked alone; in the real build it is compiled inside unity_gx_bridge_cxx). Nothing was built or run.

### 01-process-stat-clocks.patch (command_processor.cpp) - est. -0.8 to -0.9 ms/frame, risk low
- `process()` takes the GPU mutex with `std::try_to_lock` and reads the clock only when the lock was contended. That
  cuts 2 `now()` per outermost call and both for nested calls. libc++ `now()` on Windows is QPC plus three 64-bit
  divisions, and there are ~20k process() calls per frame (drain plus display list for each of 10k DL calls).
- ProcessTimer adds to `g_statProcessTotalNanos` only (one `lock add` instead of two). `AuroraTakeGxStats` now returns
  the delta of the total since its last call. Its only caller is vi.cpp's 2 s fps window, so the value is the same.
- Behaviour: identical locking. The "lock wait" stat now leaves out the uncontended acquire (~20 ns x 20k = ~0.4 ms of
  what it used to report was the clock reads themselves), so expect it to drop. The gx ms in slow-frame and fps
  lines also drops by the removed overhead.
- Measure: NSMBW_LOG_FPS=1 sweep of gn3_12. The `game` ms should drop by ~0.8. Then re-profile (NSMBW_PROFILE) and
  check that call sites 24DD5C6/24DD5DE are gone.

### 02-gx-dl-lazy-state-snapshot.patch (runtime gx_dl.cpp) - est. -0.4 to -0.5 ms/frame, risk very low
- `HleGxVertexStateSnapshot stateBeforeScan;` became `std::optional<...>`. It is built only where it was filled before
  (cached path with CP writes, uncached scan path). Every restore site checks it or dereferences it under the same
  conditions as the old `haveStateBeforeScan` bool. A list on the cached path with no CP writes (the common gn3_12 case)
  no longer pays the 2.5 KB default-init.
- Same state transitions as before: the snapshot contents and every Restore call are unchanged.
- Measure: same sweep, and the profile should show the 0xb13c0 loop gone from GX__CallDisplayList self.
  Regression check: aa1_02 / gn4_15 / he4_08 / ta2_02 black% as in AGENTS.md (exercises the flatten and restore paths).

### 03-pipeline-config-partial-hash.patch (command_processor.cpp) - est. -0.7 to -0.9 ms/frame, risk low
- `cached_pipeline_state` hashes and compares only the used bytes: the prefix up to `tevStages[tevStageCount]` and the
  tail from `tevStageCount` on (~1 KB instead of ~2.9 KB for 1-3 stage materials). The hash is unseeded XXH3 (seed 1
  sent it down `XXH3_hashLong_64b_withSeed`, which rebuilds a 192-byte secret on every call; common.hpp notes the same
  for hash_combine).
- Why equality is preserved: the only producer is `resolve_pipeline_state`: `PipelineConfig config{}; populate_pipeline_config(...)`,
  and populate writes `tevStages[i]` only for `i < numTevStages` (gx.cpp:1924). So unused stages are always
  default-constructed in both configs. A `static_assert` proves that only unused stages sit between prefix and tail, and
  a debug-only CHECK verifies on every hit that the full memcmp agrees. The hash collision path is still guarded by memcmp.
- `configHash` is in-process only: it is used for the table slot and for the interpolation draw signature
  (command_processor.cpp:3051), and is never persisted (pipeline_ref and the disk cache hash the config themselves).
- Measure: profile; `XXH3_hashLong_64b_withSeed` should disappear and `cached_pipeline_state` inclusive should drop from
  ~7% to ~2-3%. Visual check: the same 4 maps; a wrong cache hit would show wrong materials immediately.
- Caveat: if anyone later adds a second caller that builds a PipelineConfig without starting from `{}`, the invariant
  breaks. The debug CHECK catches it in debug builds only (release defines NDEBUG).

Expected total for 01+02+03: ~2.0-2.3 ms/frame, so game 17.5-20 ms becomes ~15.3-17.8 ms. That is borderline for a
steady 60, so follow-ups 4-6 are probably needed for the worst frames (jitter 3.5-6.4 ms in the log).

## Further ranked candidates (not patched)
4. Outer stat scope over the whole `GXCallDisplayList` (drain + process_display_list): est. -0.35 ms. Expose a tiny
   RAII wrapper of ProcessTimer from command_processor and put it at the top of GXCallDisplayList/LE (nested timers are
   already free). Side effect: gx ms then includes drain's frame-worker wait. Alternative with no semantic change:
   accumulate raw QPC ticks instead of `steady_clock::now()` (no divisions) and convert in the AuroraTake functions. Risk low.
5. Scan cache container (gx_dl.cpp `DlScanCacheState::entries`): libc++ `unordered_map` with prime bucket modulo (`div`)
   and bucket -> before-node -> node pointer chasing, ~4.6% = ~0.8 ms. Use an open-addressing map of
   `uint64_t -> std::unique_ptr<DlScanCacheRecord>` (pointer-stable; the probe's record pointer is never held across a
   store). abseil is NOT on mkw_runtime_common's include path (build.ninja), so this is either a CMake change or a
   small hand-rolled table. Est. -0.3 to -0.5 ms, risk medium (cache invalidation and clear-on-overflow must stay identical).
6. `update_zfreeze_reference` lazy evaluation: ~2.7% = ~0.5 ms. Correct only if the draw's inputs are snapshotted (3 vertices'
   bytes, the up to 3 referenced pnMtx rows, xfProjection/projType/widen, POS attr fmt/desc/array ptr) and the plane is
   computed when a frozen draw resolves. Computing it when coplanar is switched on is NOT safe: the decal's matrix may
   already be loaded by then. Risk medium. It must keep `zFreezePlane.w()` going 0 -> 1 before the first frozen
   `populate_pipeline_config`. Coplanar is a fresh fix, so I would leave it for later.
7. `resolve_pipeline_state` miss path: `PipelineConfig config{}` default-init plus populate, self ~2.7% = ~0.5 ms. Could copy
   from a static default instance or reset only stages [n, oldN). Risk low-medium, small gain.
8. Renderer GPU mutex `unlock` costs 3.1% (LeaveCriticalSection with a waiter means a wake), and lock wait is 0.58 ms. Something
   (frame worker / gfx/common.cpp:1275) contends per process() call. Worth tracing who takes it and how long.
9. The frame worker: GXDrawDone waits ~10% (1.9 ms) of the frame. After cuts 1-3 the game thread may hit the worker's
   own encode time; check `[vi] interp:` / worker timings before chasing more game-thread ms.
10. Frame interpolation is ON by default for players (120 fps) but the sweeps force it off, so this profile has none of
   `pn_mtx_usage`, `draw_geometry_signature` (hashes every unmerged draw's vertices) or the uniform x(1+MaxInterpolatedFrames)
   staging. Measure gn3_12 with SPM_SWEEP_INTERP=120. Real-play cost may be well above the sweep's.

## How to measure (once the other session's build/sweep is done)
1. Apply 01, 02, 03 (`patch -p1 --binary < scratchpad\perf\0N-*.patch` from repo root), run `tools\Build Super Paper Mario.cmd`.
2. `tools\Sweep whole game.cmd --maps gn3_12 --seconds 40` with NSMBW_LOG_FPS=1 and NSMBW_PROFILE=10:20. Compare the
   `[vi] fps` lines with 170916 (game 17.5-20 ms, gx 8.1 ms), then run `python tools\profile-resolve.py <out>\Play\profile_stacks.txt <out>\Play\SuperPaperMario.exe`.
3. Visual regression: `--maps aa1_02,gn4_15,he4_08,ta2_02,mi1_01` (with SPM_SWEEP_GSW0=70 for mi1_01, coplanar) and compare
   black% and shots with map-sweep-20261016-171038.
