#pragma once

// Shared internals for the OS HLE files in this directory. Everything here was
// file-local state in the single OS HLE translation unit; it lives in a named
// namespace so the split files can share one definition instead of duplicating
// it, and the using-directive below keeps every call site unchanged.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "abi_bridge.h"
#include "memory.h"
#include "runtime_log.h"

namespace OsHleInternal {

extern std::atomic<bool> g_interrupts_enabled;
// Tracks the current interrupt mask bits; stored as a 32-bit value for MMIO
// tracking on the guest side. Default to 0 (no bits masked on startup).
// OS____MaskInterrupts returns the pre-call value to the guest, so this is
// observable state, not just bookkeeping.
extern std::atomic<uint32_t> g_interrupt_mask;

constexpr uint32_t kInterruptHandlerTablePtrAddr = 0x803868f8u;
constexpr uint32_t kInterruptHandlerTableAddr = 0x80003040u;
constexpr size_t kInterruptHandlerTableBytes = 0x80u;
constexpr uint32_t kInterruptMaskLoAddr = 0x800000c4u;
constexpr uint32_t kInterruptMaskHiAddr = 0x800000c8u;
constexpr uint32_t kOSPhysicalContextAddr = 0x800000c0u;
constexpr uint32_t kOSCurrentContextAddr = 0x800000d4u;
constexpr uint32_t kOSExceptionContextAddr = 0x800000d8u;
constexpr uint32_t kThreadListHeadAddr = 0x800000dcu;    // First thread in thread list (for iteration)
constexpr uint32_t kThreadListTailAddr = 0x800000e0u;    // Last added thread (tail of thread list)
constexpr uint32_t kOSRunningContextAddr = 0x800000e4u;  // Currently running thread context

constexpr uint32_t kDefaultThreadContextAddr = 0x80347498u;
constexpr uint32_t kIdleThreadContextAddr = 0x803478b0u;
constexpr uint32_t kThreadQueueArrayAddr = 0x803477b0u;
constexpr size_t kThreadQueueArrayBytes = 0x100u;
constexpr uint32_t kSwitchThreadCallbackPtrAddr = 0x80385ae0u;
constexpr uint32_t kSchedulerReschedCounterAddr = 0x8038691cu;
constexpr uint32_t kSchedulerPendingFlagAddr = 0x80386920u;
// RVL OS uses this as the OSDisableScheduler/OSEnableScheduler nesting count.
// SelectThread exits early while the count is non-zero.
constexpr uint32_t kSchedulerIdleFlagAddr = 0x80386918u;
constexpr uint32_t kAlarmQueueOffsetFromR13 = 0x6360u;

// The wait queue func_8020FE24 (DWC's per-frame connect-retry poll, called from inside a VI
// retrace callback) blocks on. On-device logs
// proved this specific park is refused exactly ONCE and the guest gives up immediately with no
// further retry, so SchedulerCanSwitchAway (os_sleep.cpp) and SelectThread (os_scheduler.cpp)
// both carve out an exception for this one queue - letting the park actually happen - while
// leaving every other queue's blanket VI/audio/alarm-dispatch protection untouched. The same
// address is also used by an unrelated early-boot async wait (a shared low-level utility, not
// connect-specific); letting that one really park too is expected to be equally safe, since
// it's waiting on host work (NAND) with the same "can't spin through it" shape.
constexpr uint32_t kDwcConnectWaitQueueAddr = 0x804294A4u;

// Guest link-register value on entry to OSSleepTicks for DWC's connect-poll's own short
// (~0.5ms) per-attempt wait. On real hardware
// this idiom effectively costs close to a full render frame per attempt, because something
// higher-priority is normally scheduled in between; that's what gives the guest's bounded
// retry count enough real elapsed time (hundreds of ms) for the actual network connect to
// settle. Our fiber switch resolves this near-instantly whether it "succeeds" or not, so
// OSSleepTicks pads this one known call site up to a minimum real duration - see
// kDwcConnectPollMinDuration below - without touching every other short sleep in the game.
constexpr uint32_t kDwcConnectPollLr = 0x80009700u;

constexpr uint32_t kThreadStateOffset = 0x2C8u;
constexpr uint32_t kThreadAttrOffset = 0x2CAu;
constexpr uint32_t kThreadSuspendOffset = 0x2CCu;
constexpr uint32_t kThreadPriorityOffset = 0x2D0u;
constexpr uint32_t kThreadBasePriorityOffset = 0x2D4u;
constexpr uint32_t kThreadExitValueOffset = 0x2D8u;
constexpr uint32_t kThreadQueueOffset = 0x2DCu;
constexpr uint32_t kThreadNextOffset = 0x2E0u;
constexpr uint32_t kThreadPrevOffset = 0x2E4u;
constexpr uint32_t kThreadJoinQueueOffset = 0x2E8u;
constexpr uint32_t kThreadMutexOffset = 0x2F0u;
constexpr uint32_t kThreadMutexQueueOffset = 0x2F4u;
constexpr uint32_t kThreadMutexTailOffset = 0x2F8u;
constexpr uint32_t kThreadListNextOffset = 0x2FCu;
constexpr uint32_t kThreadListPrevOffset = 0x300u;

constexpr uint16_t kThreadStateReady = 1u;
constexpr uint16_t kThreadStateRunning = 2u;
constexpr uint16_t kThreadStateWaiting = 4u;
constexpr uint16_t kThreadStateMoribund = 8u;
constexpr int32_t kSuspendedWaitPriority = 32;

// OSMessageQueue layout (Wii SDK): sendQueue@0x00, recvQueue@0x08 (OSThreadQueue head/tail),
// msgArray@0x10, msgCount@0x14, firstIndex@0x18, usedCount@0x1C.
constexpr uint32_t kMsgQueueSendOffset = 0x00u;
constexpr uint32_t kMsgQueueRecvOffset = 0x08u;
constexpr uint32_t kMsgQueueArrayOffset = 0x10u;
constexpr uint32_t kMsgQueueCountOffset = 0x14u;
constexpr uint32_t kMsgQueueFirstOffset = 0x18u;
constexpr uint32_t kMsgQueueUsedOffset = 0x1Cu;

constexpr uint32_t kMutexWaitQueueHeadOffset = 0x00u;
constexpr uint32_t kMutexOwnerOffset = 0x08u;
constexpr uint32_t kMutexCountOffset = 0x0Cu;
constexpr uint32_t kMutexThreadNextOffset = 0x10u;
constexpr uint32_t kMutexThreadPrevOffset = 0x14u;

struct SleepTimerEntry {
    uint32_t threadPtr;
    std::chrono::steady_clock::time_point deadline;
};

inline bool ThreadStateIsTerminated(uint16_t state)
{
    return state == 0 || state == kThreadStateMoribund;
}

// Unlink `node` from a doubly linked guest list. `nextOff`/`prevOff` are the
// node's link field offsets; `headAddr`/`tailAddr` are the list's head and tail
// slots (a fixed OS global for the thread list, `queue`/`queue + 4` for an
// OSThreadQueue, a thread field pair for the owned-mutex list).
inline void UnlinkGuestListNode(uint32_t node, uint32_t nextOff, uint32_t prevOff,
                                uint32_t headAddr, uint32_t tailAddr)
{
    const uint32_t next = ::Memory::Read32(node + nextOff);
    const uint32_t prev = ::Memory::Read32(node + prevOff);

    if (next != 0) {
        ::Memory::Write32(next + prevOff, prev);
    } else {
        ::Memory::Write32(tailAddr, prev);
    }

    if (prev != 0) {
        ::Memory::Write32(prev + nextOff, next);
    } else {
        ::Memory::Write32(headAddr, next);
    }

    ::Memory::Write32(node + nextOff, 0);
    ::Memory::Write32(node + prevOff, 0);
}

// Defined in os_thread.cpp.
void RemoveThreadFromQueue(uint32_t threadPtr);
int32_t ComputeThreadEffectivePriority(uint32_t threadPtr);
void InsertThreadIntoQueueByPriority(uint32_t queuePtr, uint32_t threadPtr, int32_t priority);
uint32_t SetThreadEffectivePriority(uint32_t threadPtr, int32_t priority);

// Defined in os_sleep.cpp.
void CancelSleepTimer(uint32_t threadPtr);
void ClearOutstandingPark(uint32_t threadPtr);
bool ProcessSleepTimers(CpuContext* cpu);

// Defined in os_alarm.cpp.
bool ProcessAlarmQueue(CpuContext* cpu, int maxToProcess);

// Defined in os_time.cpp.
uint64_t ReadSystemTime();

} // namespace OsHleInternal

using namespace OsHleInternal;

// Cross-file HLE entry points, declared here for the same reason they were
// declared at the top of the old single OS translation unit: the OS subsystems
// call into each other.
extern "C" int32_t OS__DisableInterrupts_801a65ac();
extern "C" int32_t OS__EnableInterrupts_801a65c0();
extern "C" int32_t OS__RestoreInterrupts_801a65d4(int32_t level);
extern "C" void OS__ClearContext_801a2098(uint32_t contextAddr);
extern "C" void OS__SetCurrentContext_801a1e70(uint32_t contextAddr);
extern "C" [[noreturn]] void OS__LoadContext_801a1f58(CpuContext* ctx);
extern "C" void OSSuspendThread_HLE_801aa6a8(CpuContext* ctx);
extern "C" void OSResumeThread_HLE_801aa58c(CpuContext* ctx);
extern "C" void OSWakeupThread_HLE_801aaaa4(CpuContext* ctx);
extern "C" void OSSleepThread_HLE_801aa9b8(CpuContext* ctx);
extern "C" void SelectThread_801a9c08(CpuContext* ctx);
