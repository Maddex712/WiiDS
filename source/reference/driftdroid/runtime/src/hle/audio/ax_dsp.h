#pragma once

#include <atomic>
#include <cstdint>

struct CpuContext;

// Profiler-only counters (read by main.cpp's per-window profile writer). The mix worker adds to
// the mix fields from its own thread, hence relaxed atomics.
struct AudioMixCounters {
    std::atomic<uint64_t> mixes{0};
    std::atomic<uint64_t> mixNanos{0};
    std::atomic<uint64_t> joins{0};
    std::atomic<uint64_t> joinWaits{0};
    std::atomic<uint64_t> joinWaitNanos{0};
    std::atomic<uint64_t> ticksWithBlocks{0};
    std::atomic<uint64_t> blocks{0};
    std::atomic<uint64_t> multiBlockTicks{0};
};
extern AudioMixCounters g_audioMixCounters;

namespace AxDspHle {

void Init();
void InitForAXOut(CpuContext* ctx);
void Stop();

uint32_t CheckInit();
uint32_t AddTask(uint32_t taskPtr);
void SendMailToDSP(uint32_t mail);
uint32_t CheckMailToDSP();
uint32_t CheckMailFromDSP();
uint32_t ReadMailFromDSP();
uint32_t AssertTask(uint32_t taskPtr);
void ServiceDeferredCallbacks();

// INVARIANT: no guest code may observe mix output before this returns. Called from
// Audio_HLE_Tick before the AI DMA block reaches the backend and before the AI callback
// runs __AXOutNewFrame, the two places guest code reads the mix's PB/output/aux data.
// Cheap when the mix already finished (the normal case).
void JoinMixWorker();

// Joins and tears down the mix worker thread. Call before the guest memory map
// is destroyed or re-created.
void ShutdownMixWorker();

void SetMixWorkerEnabled(bool enabled);

void InitAram();

}
