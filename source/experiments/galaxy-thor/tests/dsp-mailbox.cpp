#include <cstdlib>
#include <cstdio>
#include <new>
#include "port/heap_routing.h"

static bool gameThread = false;
static int hostDepth = 0;
static int wrongHeapAllocations = 0;
static int hostAllocations = 0;
void* operator new(size_t bytes) {
    if (gameThread && !hostDepth) ++wrongHeapAllocations;
    else if (gameThread) ++hostAllocations;
    void* value = malloc(bytes ? bytes : 1);
    if (!value) abort();
    return value;
}
void operator delete(void* value) noexcept { free(value); }
void operator delete(void* value, size_t) noexcept { free(value); }
extern "C" void port_host_alloc_scope_enter() { ++hostDepth; }
extern "C" void port_host_alloc_scope_leave() { --hostDepth; }

// Include the actual source so its private queue and growth path are exercised.
#include "dsp_mailbox.cpp"
extern "C" void port_irq_set_handler(int, PortIrqHandler) {}
extern "C" void port_irq_raise(int) {}
extern "C" void __DSPHandler(__OSInterrupt, OSContext*) {}
extern "C" OSContext* OSGetCurrentContext() { return nullptr; }

int main() {
    gameThread = true;
    for (int cycle = 0; cycle < 8; ++cycle) {
        for (unsigned i = 0; i < 20000; ++i) queueMail(0xDCD10004, i);
        for (unsigned i = 0; i < 20000; ++i) {
            if (DSPReadMailFromDSP() != 0xDCD10004 || DSPReadMailFromDSP() != i) abort();
        }
        if (DSPCheckMailFromDSP()) abort();
    }
    gameThread = false;
    printf("mailbox: wrongHeap=%d hostAllocations=%d messages=320000 depth=%d\n",
           wrongHeapAllocations, hostAllocations, hostDepth);
    return wrongHeapAllocations == 0 && hostAllocations > 0 && hostDepth == 0 ? 0 : 1;
}
