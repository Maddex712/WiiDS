#ifndef AURORA_PIPELINE_CACHE_H
#define AURORA_PIPELINE_CACHE_H

// Pipeline precaching (lib/gfx/pipeline_cache.cpp).
//
// Render pipelines (WGSL generated from the GX TEV/vertex state + blend/depth state) are
// created on worker threads. Every pipeline config ever used is remembered in
// <cachePath>/pipeline_cache.db and all of them are rebuilt in the background at startup
// (and again when the render target layout, the GX accuracy flags or the enhanced-lighting
// parts that select shader variants change; 0217: each recorded config is expanded into the
// variants the current settings produce), so draws
// normally find their pipeline ready. A draw whose pipeline is still being compiled waits
// for it instead of being skipped (a hitch, never a missing object).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/enhanced.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AuroraPipelineStats {
  uint32_t precacheTotal;   // pipelines queued by background precaching (seed + known configs)
  uint32_t precacheDone;    // ... of which finished
  uint32_t pending;         // pipelines queued or compiling right now (any priority)
  uint32_t demandCreated;   // pipelines a draw needed that precaching had not queued (cache misses)
  uint32_t stalledDraws;    // draws that had to wait for their pipeline
  uint64_t stallNs;         // total time draws waited
  uint64_t maxStallNs;      // longest single wait
  uint32_t workerThreads;   // pipeline compile threads
  uint32_t seeded;          // configs added by aurora_pipeline_cache_seed
  uint32_t droppedDraws;    // draws skipped: pipeline creation failed, or not ready with waiting disabled
  uint32_t rebuildPending;  // 0217: pipelines the last rebuilds of the known set (startup, settings
                            // or render target changes) queued that are not built yet
  // 0226: the Dawn blob cache (dawn_cache.db) this session: entries read, lookups that missed
  // (Dawn compiled), entries written, time spent in lookups / stores (lock waits included), and
  // whether the database could not be used at all.
  uint32_t blobHits;
  uint32_t blobMisses;
  uint32_t blobStores;
  uint32_t blobCacheBroken;
  uint64_t blobLoadNs;
  uint64_t blobStoreNs;
  // 0226: GX pipeline creation time summed over all threads: WGSL generation, shader module
  // creation, render pipeline creation.
  uint64_t wgslNs;
  uint64_t moduleNs;
  uint64_t pipelineNs;
  // 0226: rebuilds of the known set so far (each queues a batch counted in rebuildPending), and
  // whether the enhanced-lighting pass prewarm of the last one is still running.
  uint32_t rebuildCount;
  uint32_t prewarmPending;
  // 0226: entries dawn_cache.db had when this session opened it, and its session count (1 = the
  // first launch with this cache file).
  uint32_t blobEntriesAtOpen;
  uint32_t blobSession;
  uint32_t modulesReused;  // 0227: GX pipelines that reused a shader module of this session
  // 0231: how the previous session ended: 0 cleanly, 1 closed or killed (no crash report), 2 crashed
  // (aurora_pipeline_cache_note_crash); and whether the caches were reset after repeated crashes.
  uint32_t prevExit;
  uint32_t cachesReset;
  // 0550: the standby set (aurora_pipeline_cache_set_standby): pipelines queued or being built
  // (its pass prewarm included), queued so far, built so far.
  uint32_t standbyPending;
  uint32_t standbyQueued;
  uint32_t standbyDone;
} AuroraPipelineStats;

/** Adds the pipeline configs in a precache list file (format below) to the known set; they
 *  are compiled in the background with the rest. Call after aurora_initialize and before the
 *  first frame. Returns the number of configs accepted (rows for another config version or
 *  struct size are ignored; damaged or invalid rows are skipped with a log line). Ignored after
 *  repeated startup crashes (crash journal, lib/gfx/pipeline_cache.cpp).
 *
 *  File: "AUPC" magic, u32 format (2), u32 flags (bit 0: payload zstd-compressed), u32 row
 *  count, u64 raw payload size, u32 CRC-32 of the raw payload, u32 reserved, then the payload:
 *  rows of { u8 type (0 clear, 1 GX), u32 configVersion, u32 configSize, u32 firstFrameUsed,
 *  u32 CRC-32 of the config, bytes }. Format 1 (no CRCs) is still read.
 *  All little-endian. tools/pipelines/pipelines.py writes it from pipeline_cache.db files. */
uint32_t aurora_pipeline_cache_seed(const char* path);
/** Writes every known pipeline config in the format above (uncompressed). */
bool aurora_pipeline_cache_export(const char* path);
void aurora_pipeline_cache_get_stats(AuroraPipelineStats* out);
/** Logs every pipeline creation (source, time) and every draw that waits. */
void aurora_pipeline_cache_set_log(bool enabled);
/** Testing: false restores Aurora's original behaviour of skipping a draw whose pipeline is not
 *  ready yet (objects missing for a few frames = the first-run flicker). Default true. */
void aurora_pipeline_cache_set_wait(bool enabled);
/** Worker threads compiling pipelines (default: half the hardware threads, 1..8). Takes effect
 *  at initialization; call before aurora_initialize. */
void aurora_pipeline_cache_set_threads(uint32_t threads);
/** 0227: true lets all hardware threads but one create pipelines (default count otherwise), e.g.
 *  while the application waits for a batch that loads from the shader cache (CPU bound, nothing
 *  else running). The stats' workerThreads is the number currently taking work. */
void aurora_pipeline_cache_set_boost(bool enabled);
/** 0550: a second settings combination (GX accuracy flags + enhanced-lighting configuration,
 *  e.g. the current settings with enhanced lighting switched the other way) whose pipeline
 *  variants are built in the background, at the lowest priority, on at most half the pipeline
 *  threads (AURORA_STANDBY_THREADS), after everything a hold or a draw waits for: a later switch
 *  to it finds its pipelines (and the enhanced passes' pipelines) built and needs no compile
 *  hold. Never part of rebuildPending. Re-queued when the combination, the variant bits it
 *  selects or the render target layout changes. Ignored without pipeline threads. */
void aurora_pipeline_cache_set_standby(bool enabled, uint32_t accuracy, const AuroraEnhancedConfig* config);
/** 0550: the pipeline variant bits a configuration selects (two configurations with the same
 *  bits and accuracy flags use the same pipelines). */
uint32_t aurora_pipeline_cache_variant_bits(const AuroraEnhancedConfig* config);
/** 0550: true once the standby set for exactly this combination is queued and nothing of the
 *  standby queue is pending (switching to it now needs no new pipeline). */
bool aurora_pipeline_cache_standby_ready(uint32_t accuracy, const AuroraEnhancedConfig* config);
/** 0231: tells the crash journal this run is crashing (call from the application's crash handler;
 *  only stores a flag). Without it an unclean end counts as closed or killed: the configs being
 *  built are not suspected and nothing is deleted. */
void aurora_pipeline_cache_note_crash(void);
/** 0233: pipeline creations that have been running for at least minNs nanoseconds (a driver stuck
 *  in one): returns how many, and describes them ("config <hash> (type <n>) for <s> s", comma
 *  separated) into buf when given. */
uint32_t aurora_pipeline_cache_describe_stuck(uint64_t minNs, char* buf, size_t bufSize);

#ifdef __cplusplus
}
#endif

#endif
