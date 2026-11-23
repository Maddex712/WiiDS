#ifndef AURORA_EFB_RAM_H
#define AURORA_EFB_RAM_H

// EFB copies to RAM (hardware-accurate GXCopyTex). See lib/gfx/efb_ram.hpp.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AuroraEfbRamStats {
  uint32_t copies;        // GXCopyTex calls tracked (supported formats)
  uint32_t encodes;       // copies encoded to GX bytes on the GPU
  uint32_t decodes;       // textures decoded from copied bytes
  uint32_t readbacks;     // copies read back for the CPU
  uint32_t writebacks;    // readbacks written into memory
  uint32_t skipped;       // readbacks dropped because the CPU wrote the range first
  uint32_t cpuOverwrites; // copies dropped because the CPU/DMA overwrote their memory
  uint64_t hashedBytes;   // memory hashed for write detection
  uint64_t cpuNs;         // CPU time spent in copy tracking, texture resolves and writebacks
  uint32_t staleSkipped;  // readbacks dropped because the CPU wrote the range after the guest issued
                          // the copy but before Aurora executed it (lagging replay; would have
                          // overwritten memory the game had reused)
  uint32_t lateChanged;   // read-back copies whose range the game wrote between issuing the copy and
                          // Aurora executing it
} AuroraEfbRamStats;

/** Enables accurate copies (default on). Off: copies stay GPU-only (Aurora's original behaviour). */
void aurora_efb_ram_set_enabled(bool enabled);
bool aurora_efb_ram_enabled(void);
/** A Z16 depth copy read back as an IA8 texture of the same size at the same address (a depth
 *  map compared in the TEV, e.g. Luigi's Mansion's shadow map) uses the copy texture at the
 *  internal resolution (default) instead of the console's 640x480 bytes. Both hold the same
 *  values (I = depth bits 23..16, A = 15..8); only the resolution differs. */
void aurora_efb_ram_set_scaled_depth_reads(bool enabled);
/** Read back every copy into memory (testing/debug; normally only CPU-read ranges are). */
void aurora_efb_ram_set_readback_all(bool enabled);
/** The CPU is about to read [ptr, ptr+size) (e.g. DCInvalidateRange): copies there are read back
 *  into memory from now on, and readbacks that already finished are applied. */
void aurora_efb_ram_cpu_read(const void* ptr, uint32_t size);
/** The game issues GXCopyTex(dest) with this destination size and format (call it on the game's
 *  thread, in order with its memory writes, before GXCopyTex). Aurora executes the copy later
 *  (another thread; with frame interpolation a replay of the frame can come several game frames
 *  later), and a readback may only land if the game has not written the range since it issued
 *  the copy: on the console the copy lands right away and anything the game writes afterwards
 *  stays. Without this the check uses the memory as Aurora found it when executing the copy, so a
 *  late copy into a buffer the game had freed and reused (e.g. for heap blocks) was written over
 *  the new contents. */
void aurora_efb_ram_guest_copy(const void* dest, uint32_t width, uint32_t height, uint32_t format);
/** Always read back copies overlapping [ptr, ptr+size) (e.g. a debug capture buffer). */
void aurora_efb_ram_watch(const void* ptr, uint32_t size);
/** Writes finished readbacks into memory. Returns how many were written. */
uint32_t aurora_efb_ram_apply(void);
/** Called after each writeback (from the thread calling aurora_efb_ram_apply). */
typedef void (*AuroraEfbRamWritebackFn)(const void* dest, uint32_t size, uint32_t format, uint32_t width,
                                        uint32_t height, void* userdata);
void aurora_efb_ram_set_writeback_callback(AuroraEfbRamWritebackFn fn, void* userdata);
/** Counters since start. */
void aurora_efb_ram_get_stats(AuroraEfbRamStats* out);

#ifdef __cplusplus
}
#endif

#endif
