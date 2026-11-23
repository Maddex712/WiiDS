#ifndef GSTHREAD_PORT_H
#define GSTHREAD_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t (*PortGSthreadEntry)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                       uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef void (*PortGSthreadExit)(uint32_t result);

#define PORT_GSTHREAD_MAX_ARGS 12

void PortGSthread_Create(void* thread, void* entry, PortGSthreadExit exitFunc, uint32_t stackSize);

void PortGSthread_SetArg(void* thread, int index, uintptr_t value);

void PortGSthread_Run(void* thread);

void PortGSthread_SwitchToScheduler(void);

extern void (*PortGSthread_SwitchOutHook)(void);

void PortGSthread_Destroy(void* thread);

#ifdef __cplusplus
}
#endif

#endif
