#pragma once

// Hooks for the host application's thread policy (priority, power throttling, CPU sets on hybrid
// CPUs). Aurora names its long-lived threads ("Main thread", "Aurora render worker", "Aurora FIFO
// processor", ...); the start hook runs on each such thread right after Aurora applied its own
// options, so the application can adjust the thread. The tick hook runs on the render worker after
// each queue item and on the FIFO processor after each batch (cheap: keep it to a few nanoseconds;
// used to sample which processor the thread runs on).

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*AuroraThreadStartHook)(const char* name);
typedef void (*AuroraThreadTickHook)(void);

// Set before aurora_initialize (threads started earlier are not reported). Either may be null.
void aurora_set_thread_hooks(AuroraThreadStartHook start, AuroraThreadTickHook tick);

#ifdef __cplusplus
}
#endif
