// OSSleepTicks / OSSleepThread parking plus the host-side sleep-timer table.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "abi_bridge.h"
#include "memory.h"
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "fiber_manager.h"
#include "timebase_contract.h"
#include "runtime_log.h"
#include "os_internal.h"
#include "hle/net/network.h"

namespace {
// A guest thread that can't park here is one running a "wait ~N frames for an async op"
// idiom (e.g. DWC/SO readiness polling) from inside a deferred guest-callback batch where
// switching fibers isn't safe (see SchedulerCanSwitchAway, below). On real hardware each of
// those waits is a real park that gives the OS a chance to run the async work; here the retry
// is instant and synchronous, so nothing ever pumps the host-side network/NAND completions
// that the awaited condition depends on - the retry loop just burns through its bounded retry
// count and the guest gives up (observed as WFC's "error during login"). Pump those
// completions directly and yield a slice of real wall-clock time so the async work (which
// normally runs on genuine host threads - see network_deferred.cpp) gets a chance to finish
// before the guest's retry budget runs out.
//
// TRIED AND REVERTED: scoping a much larger
// (~500ms) pump budget to the known DWC connect-poll queue looked promising, but on-device
// logs proved the guest gives up right after its last permitted OSSleepThread call regardless
// of how long that one call's pump runs - the real gap wasn't wall-clock time here at all, it
// was that RunDeferredReschedule (os_alarm.cpp) never got a chance to flush the wake this pump
// produces while still inside VI's retrace dispatch. That's fixed at the source now
// (VI_HLE_ProcessRetracesDeferred calls OS_HLE_RunDeferredReschedule once the dispatch unwinds,
// in vi.cpp), so this stays a flat, cheap slice for every caller again.
void PumpHostWorkWhileUnparkable(CpuContext* cpu)
{
    if (cpu != nullptr) {
        Network_HLE_ProcessCompletions(cpu);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
} // namespace

namespace OsHleInternal {
std::mutex gSleepTimerMutex;
std::vector<SleepTimerEntry> gSleepTimers;

void CancelSleepTimer(uint32_t threadPtr)
{
    std::lock_guard<std::mutex> lock(gSleepTimerMutex);
    std::erase_if(gSleepTimers, [threadPtr](const SleepTimerEntry& entry) {
        return entry.threadPtr == threadPtr;
    });
}

bool SleepTimerIsPending(uint32_t threadPtr)
{
    std::lock_guard<std::mutex> lock(gSleepTimerMutex);
    for (const SleepTimerEntry& entry : gSleepTimers) {
        if (entry.threadPtr == threadPtr) {
            return true;
        }
    }
    return false;
}

// Threads with an outstanding OSSleepTicks park (timer armed, wake not yet delivered). If one ends
// up park-shaped with no pending timer, the reconciler in ProcessSleepTimers heals it; nothing
// outside this set is ever force-started.
std::mutex gOutstandingParkMutex;
std::unordered_set<uint32_t> gOutstandingParks;

void MarkParkOutstanding(uint32_t threadPtr)
{
    std::lock_guard<std::mutex> lock(gOutstandingParkMutex);
    gOutstandingParks.insert(threadPtr);
}

void ClearOutstandingPark(uint32_t threadPtr)
{
    std::lock_guard<std::mutex> lock(gOutstandingParkMutex);
    gOutstandingParks.erase(threadPtr);
}

void ScheduleSleepTimer(uint32_t threadPtr, uint64_t ticks)
{
    using Clock = std::chrono::steady_clock;

    const auto duration = TimeBaseContract::TicksToDuration(ticks);
    const auto deadline = Clock::now() + duration;

    std::lock_guard<std::mutex> lock(gSleepTimerMutex);
    std::erase_if(gSleepTimers, [threadPtr](const SleepTimerEntry& entry) {
        return entry.threadPtr == threadPtr;
    });
    gSleepTimers.push_back({threadPtr, deadline});
}

bool ProcessSleepTimers(CpuContext* cpu)
{
    using Clock = std::chrono::steady_clock;

    // Pop and process ONE due timer at a time, straight from the shared table. Resuming a
    // sleeper re-enters the scheduler (OSResumeThread -> SelectThread) and can switch fibers
    // away from this call. Timers that had already been popped into a private list would then
    // sit on the suspended fiber's stack with their threads parked and no entry in the table:
    // exactly the "park-shaped with no pending wake timer" strand the reconciler below heals
    // 100ms late, followed by a "sleep-timer stale" drop when this fiber finally resumes.
    // Leaving unprocessed timers in the table keeps them visible to every other pump (idle
    // loop, other threads' SelectThread) while this one is switched away.
    bool processedAny = false;
    constexpr size_t kMaxTimersPerCall = 64;
    size_t processedCount = 0;
    const auto now = Clock::now();
    while (processedCount < kMaxTimersPerCall) {
        SleepTimerEntry timer{0, {}};
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(gSleepTimerMutex);
            for (auto it = gSleepTimers.begin(); it != gSleepTimers.end(); ++it) {
                if (it->deadline <= now) {
                    timer = *it;
                    gSleepTimers.erase(it);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            break;
        }
        ++processedCount;
        processedAny = true;

        const uint32_t threadPtr = timer.threadPtr;
        if (threadPtr == 0 ||
            !Memory::Contains(threadPtr + kThreadSuspendOffset, sizeof(uint32_t))) {
            continue;
        }

        // A sleep park always leaves the thread Ready with a suspension. A Running thread with a due
        // timer is mid-park (fire raced the self-suspend), so defer instead of consuming it there or
        // the sleeper strands. Any other shape means the sleeper already woke; drop the stale timer
        // without touching the thread.
        const uint16_t stateAtFire = Memory::Read16(threadPtr + kThreadStateOffset);
        const int32_t countAtFire =
            static_cast<int32_t>(Memory::Read32(threadPtr + kThreadSuspendOffset));
        if (stateAtFire == kThreadStateRunning) {
            std::lock_guard<std::mutex> lock(gSleepTimerMutex);
            bool present = false;
            for (const SleepTimerEntry& entry : gSleepTimers) {
                if (entry.threadPtr == threadPtr) {
                    present = true;
                    break;
                }
            }
            if (!present) {
                gSleepTimers.push_back({threadPtr, now + std::chrono::milliseconds(2)});
            }
            continue;
        }
        if (stateAtFire != kThreadStateReady || countAtFire < 1) {
            // The thread already woke on its own (state/suspend count moved past what a timer
            // fire expects) - drop the stale timer without consuming a suspension.
            ClearOutstandingPark(threadPtr);
            continue;
        }

        ClearOutstandingPark(threadPtr);
        cpu->gpr[3] = threadPtr;
        OSResumeThread_HLE_801aa58c(cpu);

        // OSResumeThread only peels one suspension level, so if the sleeper's count is inflated
        // (failed park, overlapping suspend) dropping the timer here would strand it. Keep it armed
        // instead of consuming it; remaining suspensions after the park's own are foreign ones woken
        // by their own OSResumeThread. Rearming here used to steal those foreign suspensions, which
        // is how the DWC re-init thread got stranded.
    }

    // Stranded-sleeper reconciler: heals a thread whose park has no pending wake timer (lost to a
    // race) by resuming it once that shape persists for 100ms, sampled every 50ms so no strand is
    // missed.
    //
    // TRIED TIGHTENING TO 10ms/5ms, THEN REVERTED:
    // looked promising for DWC's connect-poll (near-zero patience for a stranded wake), but
    // on-device logs later showed the guest still gave up right after a 10ms-healed resume just
    // as it did before - the reconciler's own latency was never the deciding factor, so shrinking
    // it process-wide only bought continuous extra mutex contention and host wakeups for the rest
    // of the game, working against the goal of not needing max-perf mode on Android. The
    // DWC-specific fix now lives in the connect-in-flight-gated padding below instead, which
    // targets only the one call site that actually needs the real wall-clock time.
    // The atomic CAS below lets exactly one caller run the scan when ProcessSleepTimers executes
    // on more than one host thread; the rest skip it.
    static std::atomic<Clock::rep> strandScanDueAt{0};
    auto strandScanClaim = strandScanDueAt.load(std::memory_order_relaxed);
    const bool runStrandReconciler =
        now.time_since_epoch().count() >= strandScanClaim &&
        strandScanDueAt.compare_exchange_strong(
            strandScanClaim,
            static_cast<Clock::rep>(
                (now + std::chrono::milliseconds(50)).time_since_epoch().count()),
            std::memory_order_relaxed);
    if (runStrandReconciler) {
        static std::mutex strandMutex;
        static std::unordered_map<uint32_t, Clock::time_point> strandFirstSeen;

        std::vector<uint32_t> outstanding;
        {
            std::lock_guard<std::mutex> lock(gOutstandingParkMutex);
            outstanding.assign(gOutstandingParks.begin(), gOutstandingParks.end());
        }
        // Decide who to heal under the lock, but resume OUTSIDE it: the resume
        // re-enters the scheduler and thus this function, and holding the lock
        // across it is a guaranteed self-deadlock.
        std::vector<uint32_t> toHeal;
        {
            std::lock_guard<std::mutex> strandLock(strandMutex);
            // Drop debounce records for threads whose park was satisfied elsewhere.
            for (auto it = strandFirstSeen.begin(); it != strandFirstSeen.end();) {
                if (std::find(outstanding.begin(), outstanding.end(), it->first) ==
                    outstanding.end()) {
                    it = strandFirstSeen.erase(it);
                } else {
                    ++it;
                }
            }
            const auto reconcileNow = Clock::now();
            for (const uint32_t threadPtr : outstanding) {
                if (SleepTimerIsPending(threadPtr)) {
                    strandFirstSeen.erase(threadPtr);
                    continue;
                }
                if (!Memory::Contains(threadPtr + kThreadSuspendOffset, sizeof(uint32_t)) ||
                    Fiber::GuestFiberManager::IsTerminated(threadPtr)) {
                    ClearOutstandingPark(threadPtr);
                    strandFirstSeen.erase(threadPtr);
                    continue;
                }
                const uint16_t state = Memory::Read16(threadPtr + kThreadStateOffset);
                const int32_t count =
                    static_cast<int32_t>(Memory::Read32(threadPtr + kThreadSuspendOffset));
                if (state != kThreadStateReady || count < 1) {
                    ClearOutstandingPark(threadPtr);
                    strandFirstSeen.erase(threadPtr);
                    continue;
                }
                const auto seen = strandFirstSeen.find(threadPtr);
                if (seen == strandFirstSeen.end()) {
                    strandFirstSeen.emplace(threadPtr, reconcileNow);
                    continue;
                }
                if (reconcileNow - seen->second < std::chrono::milliseconds(100)) {
                    continue;
                }
                strandFirstSeen.erase(seen);
                ClearOutstandingPark(threadPtr);
                toHeal.push_back(threadPtr);
            }
        }
        for (const uint32_t threadPtr : toHeal) {
            RT_LOG(RT_TAG_OS) << "reconciler: thread 0x" << std::hex << threadPtr
                      << std::dec << " park-shaped with no pending wake timer for 100ms; "
                      << "resuming lost sleeper" << std::endl;
            cpu->gpr[3] = threadPtr;
            OSResumeThread_HLE_801aa58c(cpu);
        }
    }

    return processedAny;
}
} // namespace OsHleInternal

// ---------------------------------------------------------------------------
// OSSleepTicks HLE - mirror the SDK behavior by suspending the current guest
// thread and resuming it from a host-side timer. A host sleep here starves
// lower-priority guest workers because the scheduler never gets control.
// ---------------------------------------------------------------------------
extern "C" void OS__SleepTicks_HLE_801aaca8(CpuContext* ctx)
{
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    if (!cpu) {
        return;
    }

    const uint64_t ticks = (static_cast<uint64_t>(cpu->gpr[3]) << 32) | cpu->gpr[4];
    const int32_t irqState = OS__DisableInterrupts_801a65ac();
    const auto entryTime = std::chrono::steady_clock::now();
    const bool isDwcConnectPoll = (cpu->lr == kDwcConnectPollLr);
    // See kDwcConnectPollLr's comment (os_internal.h): pad this one known call site up to
    // roughly one render frame of real elapsed time, regardless of whether the fiber switch
    // above actually happened, so the guest's bounded connect-retry loop spans real wall-clock
    // time comparable to real hardware instead of completing in single-digit milliseconds.
    //
    // SCOPED FURTHER: this same retry idiom is
    // reused for every earlier DNS lookup DWC does before it ever reaches the actual WFC connect
    // (gpcm/gpsp/gamestats/natneg/etc.) - on-device logs showed ~100 of these padded calls firing
    // before SO_CONNECT was even issued, burning ~6.7 of DWC's real ~7-8s total login patience on
    // waits that had nothing to actually wait for. Only pad while a real deferred connect is
    // in flight, so that budget survives for the part that actually needs it.
    const auto padToMinimumDwcConnectPollDuration = [&]() {
        if (!isDwcConnectPoll || !Network_HLE_HasPendingConnect()) {
            return;
        }
        constexpr auto kMinDuration = std::chrono::milliseconds(16);
        const auto deadline = entryTime + kMinDuration;
        while (std::chrono::steady_clock::now() < deadline) {
            ProcessSleepTimers(cpu);
            Audio_HLE_Poll(cpu);
            VI_HLE_PollRetrace(cpu);
            ProcessAlarmQueue(cpu, 8);
            Network_HLE_ProcessCompletions(cpu);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    try {
        uint32_t currentThread = ::Memory::Read32(kOSRunningContextAddr);
        if (Fiber::GuestFiberManager::IsInitialized()) {
            const uint32_t fiberThread = Fiber::GuestFiberManager::GetCurrentGuestThread();
            if (fiberThread != 0) {
                currentThread = fiberThread;
            }
        }
        if (currentThread == 0) {
            OS__RestoreInterrupts_801a65d4(irqState);
            return;
        }

        // Parking here is OSSuspendThread plus a one-shot timer that can only peel one suspension.
        // If the scheduler cannot switch away (OSDisableScheduler nesting raised around a deferred
        // guest-callback batch), suspending would inflate the count with nothing left to zero it,
        // parking forever (the silent DWC SOStartupEx hang). Mirror OSSleepThread: refuse to park
        // and let the caller retry.
        const uint32_t sleepIdleFlag = ::Memory::Read32(kSchedulerIdleFlagAddr);
        const uint32_t sleepCurrentContext = ::Memory::Read32(kOSCurrentContextAddr);
        if (sleepIdleFlag != 0 ||
            (sleepCurrentContext != 0 && sleepCurrentContext != currentThread)) {
            thread_local std::unordered_set<uint32_t> reportedUnparkableTickSleeps;
            if (reportedUnparkableTickSleeps.insert(currentThread).second) {
                RT_LOG(RT_TAG_OS) << "OSSleepTicks: thread 0x" << std::hex << currentThread
                          << std::dec << " cannot park (scheduler nesting "
                          << sleepIdleFlag << "); busy-returning so the caller retries."
                          << std::endl;
            }
            PumpHostWorkWhileUnparkable(cpu);
            padToMinimumDwcConnectPollDuration();
            OS__RestoreInterrupts_801a65d4(irqState);
            return;
        }

        ScheduleSleepTimer(currentThread, ticks);
        MarkParkOutstanding(currentThread);
        cpu->gpr[3] = currentThread;
        OSSuspendThread_HLE_801aa6a8(cpu);

        // Defence in depth: SelectThread has other refusal paths (context mismatch, drained run
        // queues), and there's a fire-before-park race where the timer fires and its resume is
        // consumed before we suspend. Either way we're still executing with a positive suspension
        // count, so revert it fully instead of leaving an inflated counter or a stale timer that
        // could half-wake a future sleep.
        const int32_t suspendCount =
            static_cast<int32_t>(::Memory::Read32(currentThread + kThreadSuspendOffset));
        if (suspendCount > 0) {
            CancelSleepTimer(currentThread);
            ClearOutstandingPark(currentThread);
            ::Memory::Write32(currentThread + kThreadSuspendOffset,
                              static_cast<uint32_t>(suspendCount - 1));
            if (suspendCount == 1) {
                if (::Memory::Read16(currentThread + kThreadStateOffset) == kThreadStateReady &&
                    ::Memory::Read32(currentThread + kThreadQueueOffset) == 0) {
                    ::Memory::Write16(currentThread + kThreadStateOffset, kThreadStateRunning);
                }
                if (Fiber::GuestFiberManager::IsInitialized() &&
                    Fiber::GuestFiberManager::HasFiber(currentThread)) {
                    Fiber::GuestFiberManager::ResumeGuestThread(currentThread);
                }
            }
            // A caller that asks to sleep N ticks and gets back instantly (because the fiber
            // switch above could not happen) sees an elapsed time of ~0 instead of ~N - for a
            // "wait, then check if the network op finished" retry loop that burns through its whole retry
            // budget in a single guest-visible instant. Honor the requested duration for real by
            // busy-waiting it out here, pumping the same host/idle work SelectThread's own idle
            // loop pumps so completions keep flowing.
            //
            // Capped at one render frame, not the request itself: this path can be hit by ANY
            // guest thread's OSSleepTicks (not just DWC's connect-poll - that call site gets its
            // own, separately-gated padding below), so a caller asking for a much longer sleep
            // still only pays up to ~one frame of extra host-thread blocking here instead of
            // hanging its caller for the full requested duration (previously capped at 250ms,
            // which is a lot of dead time for an unrelated audio/render-adjacent thread to eat).
            {
                const uint64_t requestedNs = (ticks * TimeBaseContract::kTickRatioDenominator) /
                                              TimeBaseContract::kTickRatioNumerator;
                const auto requestedDuration = std::min(std::chrono::nanoseconds(requestedNs),
                    std::chrono::nanoseconds(std::chrono::milliseconds(16)));
                const auto deadline = std::chrono::steady_clock::now() + requestedDuration;
                while (std::chrono::steady_clock::now() < deadline) {
                    ProcessSleepTimers(cpu);
                    Audio_HLE_Poll(cpu);
                    VI_HLE_PollRetrace(cpu);
                    ProcessAlarmQueue(cpu, 8);
                    Network_HLE_ProcessCompletions(cpu);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            PumpHostWorkWhileUnparkable(cpu);
        }
        padToMinimumDwcConnectPollDuration();
    } catch (const ::Memory::AccessViolation& e) {
        LogMemoryError(RT_TAG_OS, "OSSleepTicks", e);
    }

    OS__RestoreInterrupts_801a65d4(irqState);
}
REGISTER_NATIVE_FUNCTION(0x801AACA8, OS__SleepTicks_HLE_801aaca8);

namespace {
// OSSleepThread must reach SelectThread with the scheduler-disable count at zero, or the thread
// relinks into the wait queue a second time and OSWakeupThread livelocks. The SDK guarantees this;
// deferred guest-callback batches (VI retrace, alarms) can raise the count and violate it.
//
// TRIED AND REVERTED: splitting our own VI/audio/
// alarm dispatch bookkeeping out of kSchedulerIdleFlagAddr and gating only on
// VI_HLE_IsAdvancingRetrace() here looked right (it's what breaks Retro WFC's login, error
// 20912) but on-device testing showed the blanket refusal is protecting more than VI's
// renderer-ownership window alone - relaxing it FOR EVERY QUEUE let a guest OSSleepThread call
// made during an audio-callback-driven archive-load wait actually switch fibers, and something
// that ran during that switch left HomeMenuMgr's constructor observing a half-built object
// (crash on ordinary boot). This version is narrower: only the one specific wait queue known to
// be DWC's connect poll is exempted from the blanket refusal; every other queue (including
// whatever else legitimately relies on the blanket protection during VI/audio/alarm dispatch)
// keeps the exact prior behavior.
bool SchedulerCanSwitchAway(uint32_t queuePtr)
{
    if (queuePtr == kDwcConnectWaitQueueAddr) {
        return true;
    }
    return ::Memory::Read32(kSchedulerIdleFlagAddr) == 0;
}

// A blocking call from a non-switchable region can't park, so the caller's retry loop spins; that's
// survivable if host-side work (alarm, IOS completion) eventually satisfies the wait, terminal if
// not. Report once per wait queue and escalate if a site stays wedged long enough to read as a freeze.
void ReportUnparkableSleep(uint32_t queuePtr, uint32_t thread, uint32_t pc)
{
    using Clock = std::chrono::steady_clock;

    // thread_local rather than static: guest scheduling normally runs on one
    // host thread, but this is reachable from the host frame loop too and a
    // diagnostic must not be the thing that introduces a data race.
    thread_local std::unordered_set<uint32_t> reportedQueues;
    thread_local uint32_t wedgedQueue = 0;
    thread_local Clock::time_point wedgedSince{};
    thread_local bool escalated = false;

    const uint32_t disableCount = ::Memory::Read32(kSchedulerIdleFlagAddr);

    if (reportedQueues.insert(queuePtr).second) {
        RT_LOG(RT_TAG_OS) << "OSSleepThread: thread 0x" << std::hex << thread
                  << " tried to block on wait queue 0x" << queuePtr
                  << " from pc=0x" << pc
                  << std::dec << " but the scheduler could not switch away";
        if (disableCount != 0) {
            std::cerr << " (OSDisableScheduler nesting count " << disableCount << ")";
        }
        std::cerr << ". Not parking - the caller retries instead of corrupting"
                     " the wait queue." << std::endl;
    }

    const auto now = Clock::now();
    if (queuePtr != wedgedQueue) {
        wedgedQueue = queuePtr;
        wedgedSince = now;
        escalated = false;
        return;
    }

    constexpr auto kWedgeThreshold = std::chrono::seconds(5);
    if (!escalated && now - wedgedSince >= kWedgeThreshold) {
        escalated = true;
        RT_LOG(RT_TAG_OS) << "OSSleepThread: wait queue 0x" << std::hex << queuePtr
                  << " has been unsatisfiable for " << std::dec
                  << std::chrono::duration_cast<std::chrono::seconds>(now - wedgedSince).count()
                  << "s from a non-switchable region (last retry pc=0x" << std::hex << pc
                  << std::dec << "). The guest callback that is"
                     " blocking here must not block, or the deferred-callback"
                     " scope around it is wrong." << std::endl;
    }
}
} // namespace

// OSSleepThread (0x801aa9b8)
// Puts the current thread to sleep on a specified wait queue.
extern "C" void OSSleepThread_HLE_801aa9b8(CpuContext* ctx)
{
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    const uint32_t queuePtr = cpu->gpr[3];
    
    if (queuePtr == 0) {
        RT_LOG(RT_TAG_OS) << "OSSleepThread: null queue pointer!" << std::endl;
        return;
    }

    const int32_t irqState = OS__DisableInterrupts_801a65ac();
    
    try {
        uint32_t currentThread = ::Memory::Read32(kOSRunningContextAddr);
        
        // If no current thread is set but we have a default thread, use that
        // This can happen during early boot before the scheduler has switched threads
        if (currentThread == 0) {
            currentThread = kDefaultThreadContextAddr;
            if (!Memory::Contains(currentThread, 4)) {
                RT_LOG(RT_TAG_OS) << "OSSleepThread: no current thread and default thread not valid!" << std::endl;
                OS__RestoreInterrupts_801a65d4(irqState);
                return;
            }
            // Set the default thread as current (both running and context)
            ::Memory::Write32(kOSRunningContextAddr, currentThread);
            OS__SetCurrentContext_801a1e70(currentThread);
            
            // Also register as a fiber if not already
            if (Fiber::GuestFiberManager::IsInitialized() && 
                !Fiber::GuestFiberManager::HasFiber(currentThread)) {
                RT_LOG(RT_TAG_OS) << "OSSleepThread: registering default thread 0x"
                          << std::hex << currentThread << " as fiber" << std::dec << std::endl;
                Fiber::GuestFiberManager::RegisterMainThreadAsFiber(currentThread, cpu);
            }
        }
        
        // See SchedulerCanSwitchAway: parking is only safe when SelectThread is
        // permitted to switch. Otherwise leave the thread RUNNING and the wait
        // queue untouched and let the caller's retry loop re-test its condition.
        if (!SchedulerCanSwitchAway(queuePtr)) {
            ReportUnparkableSleep(queuePtr, currentThread, cpu->lr);
            PumpHostWorkWhileUnparkable(cpu);
            OS__RestoreInterrupts_801a65d4(irqState);
            return;
        }

        // Mark thread as WAITING
        ::Memory::Write16(currentThread + 0x2C8u, 4);

        // Store the queue we're waiting on
        ::Memory::Write32(currentThread + 0x2DCu, queuePtr);

        // Insert into wait queue (sorted by priority). The shared helper also
        // re-writes the queue pointer stored above and, for a run-queue
        // address, publishes the pending bit - neither applies to a wait queue.
        const int32_t ourPrio = static_cast<int32_t>(::Memory::Read32(currentThread + 0x2D0u));
        InsertThreadIntoQueueByPriority(queuePtr, currentThread, ourPrio);

        // Mark fiber as waiting
        if (Fiber::GuestFiberManager::IsInitialized()) {
            Fiber::GuestFiberManager::SuspendGuestThread(currentThread);
        }

        // Set reschedule flag and switch threads
        ::Memory::Write32(kSchedulerReschedCounterAddr, 1);
        
        // Match the original SDK behavior: sleep yields via SelectThread(0).
        cpu->gpr[3] = 0;
        SelectThread_801a9c08(cpu);

        // Defence in depth: SelectThread has other paths that return without switching (context
        // mismatch, uninitialised thread system, a run queue that drained mid-enqueue). None of
        // them may leave this thread WAITING and linked, or the caller's retry loop inserts the same
        // node twice; a real switch resumes here already RUNNING with the queue pointer cleared, so
        // this block never fires there.
        if (::Memory::Read16(currentThread + kThreadStateOffset) == kThreadStateWaiting &&
            ::Memory::Read32(currentThread + kThreadQueueOffset) == queuePtr) {
            // DWC's connect poll (func_8020FE24) treats this sleep as a real "wait one frame"
            // and aborts the whole login the moment it comes back unsatisfied - it has no retry
            // budget to spend, which is what surfaces as Retro WFC error 20912. SelectThread could not switch away, so
            // nothing else will advance the frame on our behalf; run the same work its own idle
            // loop runs, inline, until the guest's retrace/alarm handling wakes this thread off
            // the queue. Scoped to that one queue: every other unparkable sleep keeps the
            // existing unlink-and-retry behavior below.
            if (queuePtr == kDwcConnectWaitQueueAddr) {
                using Clock = std::chrono::steady_clock;
                const auto deadline = Clock::now() + std::chrono::milliseconds(200);
                while (Clock::now() < deadline) {
                    ProcessSleepTimers(cpu);
                    Audio_HLE_Poll(cpu);
                    VI_HLE_PollRetrace(cpu);
                    ProcessAlarmQueue(cpu, 8);
                    Network_HLE_ProcessCompletions(cpu);
                    if (::Memory::Read16(currentThread + kThreadStateOffset) !=
                            kThreadStateWaiting ||
                        ::Memory::Read32(currentThread + kThreadQueueOffset) != queuePtr) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                // Woken for real: the queue released this thread, so return as a normal sleep.
                if (::Memory::Read16(currentThread + kThreadStateOffset) != kThreadStateWaiting ||
                    ::Memory::Read32(currentThread + kThreadQueueOffset) != queuePtr) {
                    OS__RestoreInterrupts_801a65d4(irqState);
                    return;
                }
            }

            // Reverse of the priority-sorted insert above, so a thread is never
            // left WAITING and linked while its caller keeps running on the
            // same stack.
            UnlinkGuestListNode(currentThread, kThreadNextOffset, kThreadPrevOffset, queuePtr,
                                queuePtr + 4u);
            ::Memory::Write32(currentThread + kThreadQueueOffset, 0);
            ::Memory::Write16(currentThread + kThreadStateOffset, kThreadStateRunning);
            if (Fiber::GuestFiberManager::IsInitialized() &&
                Fiber::GuestFiberManager::HasFiber(currentThread)) {
                Fiber::GuestFiberManager::ResumeGuestThread(currentThread);
            }
            ReportUnparkableSleep(queuePtr, currentThread, cpu->lr);
            PumpHostWorkWhileUnparkable(cpu);
        }

    } catch (const ::Memory::AccessViolation& e) {
        LogMemoryError(RT_TAG_OS, "OSSleepThread", e);
    }
    
    OS__RestoreInterrupts_801a65d4(irqState);
}
PPC_NATIVE_OVERRIDE_VOID(801AA9B8, OSSleepThread_HLE_801aa9b8, (CpuContext* ctx), (ctx));
