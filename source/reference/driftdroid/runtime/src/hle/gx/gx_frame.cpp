#include "gx_internal.h"
#include "runtime_log.h"

#include <cstdio>

GxDisplayListState g_dlRecordState{};

void BeginDisplayListRecording(uint32_t listAddr, uint32_t sizeBytes) {
    g_dlRecordState.base = listAddr;
    g_dlRecordState.size = sizeBytes;
    g_dlRecordState.writePtr = listAddr;
    g_dlRecordState.count = 0;
    g_dlRecordState.active = listAddr != 0 && sizeBytes != 0;
}

void EndDisplayListRecording() {
    if (!g_dlRecordState.active) {
        return;
    }
    g_dlRecordState.active = false;
    // Publish the shadow cursor/count so the guest-visible fifo object at
    // kDlFifoAddr matches exactly what the per-write guest updates produced
    // before the state was cached runtime-side.
    try {
        Memory::Write32(kDlWritePtrAddr, g_dlRecordState.writePtr);
        Memory::Write32(kDlCountAddr, g_dlRecordState.count);
    } catch (const Memory::AccessViolation&) {
    }
}

void WriteDisplayListData(uint32_t val, uint32_t sizeBytes) {
    GxDisplayListState& dl = g_dlRecordState;
    if (!dl.active || dl.base == 0 || dl.size == 0 || dl.writePtr == 0) {
        return;
    }
    try {
        const uint32_t writePtr = dl.writePtr;
        switch (sizeBytes) {
        case 1:
            Memory::Write8(writePtr, static_cast<uint8_t>(val));
            break;
        case 2:
            Memory::Write16(writePtr, static_cast<uint16_t>(val));
            break;
        default:
            Memory::Write32(writePtr, val);
            sizeBytes = 4;
            break;
        }

        uint32_t nextPtr = writePtr + sizeBytes;
        const uint32_t end = dl.base + dl.size;
        if (nextPtr > end) {
            // The wrap flag is read straight out of guest memory by
            // GX__EndDisplayList_80172eb4, so keep writing it through. Wrapping
            // is a once-per-overflow event, not a per-write cost.
            Memory::Write8(kDlFifoAddr + kDlWrapFlagOffset, 1);
            nextPtr = dl.base + (nextPtr - end);
        }

        dl.writePtr = nextPtr;
        dl.count += sizeBytes;
    } catch (const Memory::AccessViolation&) {
    }
}

void BeginNextAuroraFrameWithRetry(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    uint32_t attempts = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        UpdateAuroraAndProcessEvents();
        ++attempts;
        if (BeginAuroraFrame()) {
            g_auroraFrameActive.store(true, std::memory_order_release);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Retry window expired: the aurora frame stays INACTIVE and every GX
    // command until the next EnsureAuroraFrameActive is silently dropped
    // (draws, one-shot bakes, copies). This is a frame-loss event.
    static uint32_t s_beginExpiredLogCount = 0;
    if (s_beginExpiredLogCount < 256 || (s_beginExpiredLogCount & 255) == 0) {
        RT_LOGF(RT_TAG_GX, "BeginAuroraFrame retry EXPIRED after %u attempts; frame remains inactive (n=%u)\n",
                attempts, s_beginExpiredLogCount + 1);
    }
    ++s_beginExpiredLogCount;
}

void EnsureAuroraFrameActive() {
    if (!g_auroraFrameActive.load(std::memory_order_acquire)) {
        BeginNextAuroraFrameWithRetry();
    }
}
