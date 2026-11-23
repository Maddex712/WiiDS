#pragma once
// Present timing (LM patch 0197): when each frame's GPU work was submitted, when Present returned
// and when the GPU finished the frame (with a variable refresh rate display, about when it is
// shown). Times are std::chrono::steady_clock nanoseconds since its epoch.
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AuroraPresentTiming {
  uint64_t frame;     // aurora_frame_counter() of the frame
  int64_t submitNs;   // the frame's command buffers submitted (render worker)
  int64_t presentNs;  // Present returned
  int64_t doneNs;     // the GPU finished everything up to the present (0: unknown)
} AuroraPresentTiming;

// Starts recording (off by default: one GPU fence wait per frame on a helper thread).
void aurora_present_timing_enable(bool enable);
// Frames begun so far (the counter of the frame the last aurora_begin_frame opened).
uint64_t aurora_frame_counter(void);
// Completed records, oldest first; returns the number written (at most `max`).
size_t aurora_present_timings(AuroraPresentTiming* out, size_t max);
// Blocks until the GPU finished every frame up to `frame` or `timeoutNs` passed; true if finished.
bool aurora_wait_frame_done(uint64_t frame, int64_t timeoutNs);
// 0215: filler GPU work per frame (0 = none, at most 400; one unit is one read pass over a 64 MiB
// buffer, about 0.06 ms on a fast GPU at full clocks), so a light frame does not let the GPU clock down.
void aurora_set_gpu_keep_warm(uint32_t units);
// 0236: what Aurora's threads (host frame calls, render worker, GX FIFO thread, present-timing
// waiter) have been doing for at least `minMs` in one call, as one line, e.g. "render worker:
// acquiring the swap chain texture (GetCurrentTexture) for 15.2 s; host: waiting for a frame slot
// (render worker busy) for 15.1 s". Returns the length written (0: nothing that long).
size_t aurora_describe_activity(char* out, size_t size, uint32_t minMs);
// 0236: frames whose GPU completion the present-timing waiter gave up on (no completion within 5 s);
// a backend whose completion events never arrive makes this grow with every frame.
uint64_t aurora_present_timing_timeouts(void);

#ifdef __cplusplus
}
#endif
