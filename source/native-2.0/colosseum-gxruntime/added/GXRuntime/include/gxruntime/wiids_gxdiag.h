#pragma once

// WiiDS GX renderer diagnostic (off by default). On Android it is enabled by
// `setprop debug.wiids.colo.gxdiag 1` (re-read about once a second, so it can be
// toggled while the game runs) and logs to logcat tag WiiDsColoGx. Elsewhere it
// reads the WIIDS_COLO_GXDIAG environment variable and logs to stderr.
//
// Level 1: per-frame summary + every dropped draw, EFB copy, draw before the
//          frame's second EFB copy, draw sampling an EFB copy and large
//          (>= 15% of the frame) 3D draw, for one frame every ~2 seconds.
// Level 2: same, every frame (heavy; capture briefly). Level 3: every draw.
//
// debug.wiids.colo.gxfix (WIIDS_COLO_GXFIX), live experiment bits:
//   1 = draws bind EFB-copy textures as opaque white;
//   2 = projected texcoords with q < 0 sample the texture centre.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#if defined(__ANDROID__)
#include <android/log.h>
#include <sys/system_properties.h>
#endif

namespace wiids_gxdiag {

struct State {
  int level = 0;
  int fix = 0;
  unsigned long long frame = 0;
  unsigned long long last_read_frame = ~0ull;
  bool sampling = false; // this frame is a logged frame
  unsigned lines = 0;    // lines logged this frame (capped)
  unsigned copies = 0;   // EFB texture copies so far this frame
};

inline State& state() {
  static State s;
  return s;
}

inline int read_int_prop(const char* prop, const char* env) {
#if defined(__ANDROID__)
  (void)env;
  char value[PROP_VALUE_MAX] = {};
  if (__system_property_get(prop, value) <= 0)
    return 0;
  return static_cast<int>(std::strtol(value, nullptr, 0));
#else
  (void)prop;
  const char* v = std::getenv(env);
  return v != nullptr ? static_cast<int>(std::strtol(v, nullptr, 0)) : 0;
#endif
}

// Call once per presented frame (render thread).
inline void begin_frame() {
  State& s = state();
  ++s.frame;
  if (s.last_read_frame == ~0ull || s.frame - s.last_read_frame >= 60) {
    s.level = read_int_prop("debug.wiids.colo.gxdiag", "WIIDS_COLO_GXDIAG");
    s.fix = read_int_prop("debug.wiids.colo.gxfix", "WIIDS_COLO_GXFIX");
    s.last_read_frame = s.frame;
  }
  s.sampling = s.level >= 2 || (s.level == 1 && (s.frame % 120ull) == 0ull);
  s.lines = 0;
  s.copies = 0;
}

inline bool enabled() { return state().level > 0; }
// True while the current frame is one that logs draw detail.
inline bool sampling() { return state().sampling; }
inline unsigned long long frame() { return state().frame; }
inline int fix_bits() { return state().fix; }

inline void vlog(const char* fmt, va_list args) {
#if defined(__ANDROID__)
  __android_log_vprint(ANDROID_LOG_INFO, "WiiDsColoGx", fmt, args);
#else
  std::fprintf(stderr, "[WiiDsColoGx] ");
  std::vfprintf(stderr, fmt, args);
  std::fputc('\n', stderr);
#endif
}

// Line (when enabled); capped per frame.
inline void log(const char* fmt, ...) {
  State& s = state();
  if (s.level <= 0 || s.lines >= 1500u)
    return;
  ++s.lines;
  va_list args;
  va_start(args, fmt);
  vlog(fmt, args);
  va_end(args);
}

} // namespace wiids_gxdiag
