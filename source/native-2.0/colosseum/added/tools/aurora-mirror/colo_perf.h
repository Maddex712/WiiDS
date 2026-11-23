// Performance summary for the Android build: one logcat line a second (tag
// WiiDsColoPerf) while debug.wiids.colo.perf is 1. Off, it costs one property
// read every 60 guest fields. Included by standalone.cpp only; tick() runs on
// the guest thread at each VI field.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#ifdef __ANDROID__
#include <android/log.h>
#include <sys/system_properties.h>
#define COLO_PERF_LOG(...) __android_log_print(ANDROID_LOG_INFO, "WiiDsColoPerf", __VA_ARGS__)
#else
#define COLO_PERF_LOG(...) (std::fprintf(stderr, "WiiDsColoPerf: " __VA_ARGS__), std::fputc('\n', stderr))
#endif

// native.cpp: renderer counters since the last take (render thread work and the
// guest thread's waits on it).
struct AuroraNativePerf {
  uint64_t render_cpu_ns = 0;     // CPU time of the thread that runs Aurora (async: its own)
  uint64_t feed_ns = 0;           // GX decode/record (aurora_backend_gx_write_bytes)
  uint64_t present_ns = 0;        // aurora_backend_present, incl. interpolation layers and their pacing
  uint64_t render_idle_ns = 0;    // render thread waiting for a batch (async)
  uint64_t ship_wait_ns = 0;      // guest thread waiting for a free snapshot slot (async)
  uint64_t ship_ns = 0;           // guest thread copying the snapshot (async)
  uint64_t snapshot_bytes = 0;
  uint64_t batches = 0;
  uint64_t late_pages = 0;
  uint64_t fed_bytes = 0;
  uint32_t async = 0;
  int32_t width = 0, height = 0;  // drawable size in pixels (render target)
};
extern "C" void aurora_native_perf_take(AuroraNativePerf* out);
// aurora_graphics.cpp: images presented (interpolated ones included), Aurora
// draw calls, texture upload bytes, EFB copies, since the last take.
extern "C" void aurora_backend_perf_take(uint64_t* presents, uint64_t* draws, uint64_t* texture_upload_bytes,
                                         uint64_t* efb_copies);

namespace colo_perf {

inline uint64_t thread_cpu_ns() {
#if defined(_WIN32)
  return 0;
#else
  timespec ts{};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
  return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
#endif
}

inline bool g_on = false;
inline uint64_t g_pacing_sleep_ns = 0;   // guest thread: vi_field's real-time sleep
inline uint64_t g_gp_external = 0;       // gather-pipe stores that reached the host one by one
inline uint64_t g_gp_bursts = 0;         // fast gather-pipe bursts absorbed
inline uint64_t g_interp_instructions = 0;  // guest instructions run by gekko_interp
inline uint64_t g_interp_ns = 0;
inline uint64_t g_idle_hits = 0, g_idle_cycles = 0;  // idle-loop waits and the guest cycles they skipped
inline uint64_t g_ax_ns = 0;                 // AX HLE command lists (mixing), guest thread
inline uint64_t g_dispatch_sampled_ns = 0;   // module->dispatch time, every 16th dispatch

// Dispatch start PCs, one in 64 dispatches: where the guest spends its slices.
inline uint32_t g_pc_keys[256];
inline uint32_t g_pc_counts[256];
inline void sample_pc(uint32_t pc) {
  uint32_t h = (pc >> 2) * 2654435761u >> 24;
  for (int probe = 0; probe < 8; ++probe, h = (h + 1) & 255) {
    if (g_pc_keys[h] == pc || g_pc_counts[h] == 0) {
      g_pc_keys[h] = pc;
      ++g_pc_counts[h];
      return;
    }
  }
}
inline void top_pcs(char* out, size_t size) {
  out[0] = 0;
  size_t used = 0;
  bool taken[256] = {};
  for (int rank = 0; rank < 8; ++rank) {
    int best = -1;
    for (int i = 0; i < 256; ++i)
      if (!taken[i] && g_pc_counts[i] && (best < 0 || g_pc_counts[i] > g_pc_counts[best]))
        best = i;
    if (best < 0)
      break;
    taken[best] = true;
    const int n = std::snprintf(out + used, size - used, " %08X:%u", g_pc_keys[best], g_pc_counts[best]);
    if (n < 0 || size_t(n) >= size - used)
      break;
    used += size_t(n);
  }
  for (int i = 0; i < 256; ++i)
    g_pc_counts[i] = g_pc_keys[i] = 0;
}

inline bool property_on() {
#ifdef __ANDROID__
  char value[PROP_VALUE_MAX] = {};
  __system_property_get("debug.wiids.colo.perf", value);
  return value[0] == '1';
#else
  const char* v = std::getenv("WIIDS_COLO_PERF");
  return v && v[0] == '1';
#endif
}

// Each VI field, guest thread.
inline void tick(uint64_t fields, uint64_t frames, uint64_t dispatches, bool fast_gp, int frame_rate) {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point last{};
  static uint64_t last_fields = 0, last_frames = 0, last_dispatches = 0, last_cpu = 0;
  if (fields % 60 == 0) {
    const bool on = property_on();
    if (on && !g_on) {
      last = Clock::now();
      last_fields = fields;
      last_frames = frames;
      last_dispatches = dispatches;
      last_cpu = thread_cpu_ns();
      g_pacing_sleep_ns = g_gp_external = g_gp_bursts = 0;
      g_interp_instructions = g_interp_ns = g_idle_hits = g_idle_cycles = g_ax_ns = g_dispatch_sampled_ns = 0;
      char discard_pcs[8];
      top_pcs(discard_pcs, sizeof discard_pcs);
      AuroraNativePerf discard;
      aurora_native_perf_take(&discard);
      uint64_t a, b, c, d;
      aurora_backend_perf_take(&a, &b, &c, &d);
    }
    g_on = on;
  }
  if (!g_on)
    return;
  const auto now = Clock::now();
  const double wall = std::chrono::duration<double>(now - last).count();
  if (wall < 1.0)
    return;
  const uint64_t cpu = thread_cpu_ns();
  AuroraNativePerf r;
  aurora_native_perf_take(&r);
  uint64_t presents = 0, draws = 0, upload = 0, copies = 0;
  aurora_backend_perf_take(&presents, &draws, &upload, &copies);
  const double ms = 1e-6 / wall;  // ns over the window -> ms per second
  const uint64_t frames_d = frames - last_frames;
  COLO_PERF_LOG(
      "fields=%.1f/s guest_frames=%.1f/s presented=%.1f/s async=%u size=%dx%d rate=%d | guest_cpu=%.0f ms/s "
      "pacing_sleep=%.0f ms/s ship_wait=%.1f ms/s ship_copy=%.1f ms/s (%.0f KiB/frame, late %llu) "
      "dispatches=%.0fk/s gp_fast=%d bursts=%.0fk/s gp_ext=%.0fk/s | render_cpu=%.0f ms/s gx_feed=%.0f ms/s "
      "present=%.0f ms/s render_idle=%.0f ms/s fed=%.0f KiB/frame draws=%.0f/frame tex_upload=%.0f KiB/s "
      "efb_copies=%.1f/frame",
      double(fields - last_fields) / wall, double(frames_d) / wall, double(presents) / wall, r.async, r.width,
      r.height, frame_rate, double(cpu - last_cpu) * ms, double(g_pacing_sleep_ns) * ms,
      double(r.ship_wait_ns) * ms, double(r.ship_ns) * ms,
      frames_d ? double(r.snapshot_bytes) / 1024.0 / double(frames_d) : 0.0, (unsigned long long)r.late_pages,
      double(dispatches - last_dispatches) / 1000.0 / wall, fast_gp ? 1 : 0, double(g_gp_bursts) / 1000.0 / wall,
      double(g_gp_external) / 1000.0 / wall, double(r.render_cpu_ns) * ms, double(r.feed_ns) * ms,
      double(r.present_ns) * ms, double(r.render_idle_ns) * ms,
      frames_d ? double(r.fed_bytes) / 1024.0 / double(frames_d) : 0.0,
      frames_d ? double(draws) / double(frames_d) : 0.0, double(upload) / 1024.0 / wall,
      frames_d ? double(copies) / double(frames_d) : 0.0);
  char pcs[160];
  top_pcs(pcs, sizeof pcs);
  COLO_PERF_LOG("guest: interp=%.0fk instr/s (%.0f ms/s) idle_hits=%.0f/s idle_skipped=%.1f Mcycles/s ax=%.1f ms/s "
                "dispatch~%.0f ms/s (sampled 1/16) | top slice PCs (1/64):%s",
                double(g_interp_instructions) / 1000.0 / wall, double(g_interp_ns) * ms, double(g_idle_hits) / wall,
                double(g_idle_cycles) / 1e6 / wall, double(g_ax_ns) * ms, double(g_dispatch_sampled_ns) * 16.0 * ms,
                pcs);
  g_interp_instructions = g_interp_ns = g_idle_hits = g_idle_cycles = g_ax_ns = g_dispatch_sampled_ns = 0;
  last = now;
  last_fields = fields;
  last_frames = frames;
  last_dispatches = dispatches;
  last_cpu = cpu;
  g_pacing_sleep_ns = g_gp_external = g_gp_bursts = 0;
}

}  // namespace colo_perf
