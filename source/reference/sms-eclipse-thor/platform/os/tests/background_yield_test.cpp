// Host test of the actual Android scheduler path; no ROM, graphics or VI timer.
#include "port_os.h"
#include "port_platform.h"
#include "crash.h"
#include <dolphin/os/OSThread.h>
#include <dolphin/os.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>

extern "C" void port_os_threads_init();
#ifdef SMS_TEST_FRAME_WAIT
extern "C" s32 port_vi_frame_wait(s32, u16);
extern "C" u32 VIGetRetraceCount();
extern "C" void port_android_performance_frame_end() {}
extern "C" void port_android_performance_frame_begin(int64_t) {}
#else
extern "C" int port_vi_idle_advance() { assert(!"background handoff must not wait for VI/idle"); return 0; }
#endif
extern "C" void* port_low_alloc(unsigned long bytes) {
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}
extern "C" void port_log(const char* format, ...) {
    va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args);
}
void port_crash_thread_init() {}
void port_crash_set_thread_describer(PortCrashDescriber) {}
void port_crash_emit(PortCrashLine&) {}
bool port_crash_module(uintptr_t, uintptr_t*, const char**) { return false; }

namespace {
OSThread quick, busy, polling, urgent, exiting;
OSThreadQueue parked;
unsigned quickRuns, busyRuns, pollingRuns, urgentRuns, exitRuns;
bool keepBusy = true, keepPolling = true;
char unusedStack[4096];
void create(OSThread& t, void* (*work)(void*), int priority = 20) {
    assert(OSCreateThread(&t, work, nullptr, unusedStack + sizeof unusedStack, sizeof unusedStack, priority, 0));
}
void start(OSThread& t, void* (*work)(void*)) {
    create(t, work);
    OSResumeThread(&t);
}
void* quickJob(void*) { ++quickRuns; OSSleepThread(&parked); return nullptr; }
void* busyJob(void*) {
    while (keepBusy) { ++busyRuns; OSYieldThread(); }
    return nullptr;
}
void* exitJob(void*) { ++exitRuns; return nullptr; }
void* pollingJob(void*) {
    while (keepPolling) { ++pollingRuns; port_irq_check(); }
    return nullptr;
}
void* urgentJob(void*) {
    assert(!port_os_yield_background(1000000)); // no nested handoff
    usleep(3000); // unrelated higher-priority work must not exhaust the lower worker's turn
    ++urgentRuns;
    return nullptr;
}
}

int main() {
    port_os_threads_init();
    OSThread* render = OSGetCurrentThread();
    assert(OSGetThreadPriority(render) == 16);
    assert(!port_os_yield_background(1000000)); // no work, no sleep
    OSInitThreadQueue(&parked);
    start(quick, quickJob);
    OSDisableScheduler();
    assert(!port_os_yield_background(1000000) && quickRuns == 0);
    OSEnableScheduler();
    port_irq_run_now([] { assert(!port_os_yield_background(1000000)); });
    assert(!port_os_yield_background(0) && quickRuns == 0);
    const BOOL enabled = OSDisableInterrupts();
    assert(port_os_yield_background(1000000000)); // quiescence returns before a one-second budget
    assert(quickRuns == 1 && quick.state == OS_THREAD_STATE_WAITING);
    assert(OSGetCurrentThread() == render && OSGetThreadPriority(render) == 16);
    assert(!OSDisableInterrupts()); // caller's interrupt state preserved
    OSRestoreInterrupts(enabled);
    OSCancelThread(&quick);
    assert(!port_os_yield_background(1000000));
    start(busy, busyJob);
    assert(port_os_yield_background(1000000)); // deadline resumes render without a VI interrupt
    assert(busyRuns > 0 && keepBusy && OSGetCurrentThread() == render);
    keepBusy = false;
    OSJoinThread(&busy, nullptr);
    start(polling, pollingJob);
    assert(port_os_yield_background(1000000)); // polling deadline works without pending IRQs
    assert(pollingRuns > 0 && keepPolling && OSGetCurrentThread() == render);
    keepPolling = false;
    OSJoinThread(&polling, nullptr);
    start(quick, quickJob);
    create(urgent, urgentJob, 10);
    port_irq_defer([] { OSResumeThread(&urgent); });
    assert(port_os_yield_background(1000000));
    assert(urgentRuns == 1 && quickRuns == 2); // priority respected, lower work still runs
    OSCancelThread(&quick);
    start(exiting, exitJob);
    assert(port_os_yield_background(1000000000)); // exit also resumes render immediately
    assert(exitRuns == 1 && OSGetCurrentThread() == render);
    assert(!port_os_yield_background(1000000)); // waiter cleared across repeated handoffs
#ifdef SMS_TEST_FRAME_WAIT
    // A frame already due must service a short background job and return
    // without another retrace. No VI timer runs in this fixture; the old
    // VIWaitForRetrace fallback would stall here waiting for that interrupt.
    start(quick, quickJob);
    const s32 due = VIGetRetraceCount();
    assert(port_vi_frame_wait(due, 1) == due + 1);
    assert(VIGetRetraceCount() == u32(due) && quickRuns == 3 && OSGetCurrentThread() == render);
    OSCancelThread(&quick);
#endif
    puts("Android scheduler: background progress, early return, yield/poll deadlines, urgent work, exit, guards and IRQ state passed");
    fflush(stdout);
    _Exit(0); // scheduler-owned host threads deliberately remain process-scoped
}
