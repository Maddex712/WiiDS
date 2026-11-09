#pragma once

#include <cstdint>

// Determinism diagnostics for the lockstep mode (det_clock.h): fingerprints of guest memory taken at
// VI retraces, so two runs, or two devices, can be compared frame by frame.
//
//   NSMBW_DET_HASH=<file>[:<every>]   append "<retrace> <ticks> <mem1> <mem2>" every <every>
//                                      retraces (default 60) to <file>
//   NSMBW_DET_DUMP=<retrace>[,...]    write mem1/mem2 images at those retraces next to the hash file
//                                      (or in the log directory), for diffing two runs
//
// On Android the variables come from the debug.nsmbw.dethash / debug.nsmbw.detdump properties.
namespace DetHash {

// XXH3 of MEM1 (24 MiB) and the 64 MiB of MEM2 the game can use.
struct Digest {
    uint64_t mem1 = 0;
    uint64_t mem2 = 0;
};
Digest HashGuestMemory();

// Called at every VI retrace in deterministic mode, before guest callbacks run.
void OnRetrace(uint32_t retraceCount);

} // namespace DetHash
