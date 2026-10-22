// GX FIFO objects and the CPU->GPU command path.
//
// When the CPU FIFO is linked to the GP FIFO, GX writes land in a large host
// staging buffer that the command processor drains at sync points (GXFlush,
// draw-done, EFB peeks, overflow).  When unlinked (GXBeginDisplayList), writes
// go straight into the game's display-list buffer, with the SDK's overflow
// ("wrap") semantics.
#include <string.h>

#include "gpu.h"
#include "port/gx_fifo.h"
#include "port/heap_routing.h"
#include "port/port.h"
#include "revolution/gx.h"
#include "revolution/gx/GXFifo.h"
#include "revolution/gx/GXPerf.h"
#include "revolution/os/OSInterrupt.h"

extern "C" {
uint8_t* __PortGXFifoPtr;
uint8_t* __PortGXFifoLimit;
}

static __GXFifoObj sCPUFifo;
static __GXFifoObj sGPFifo;
static bool sCPUReady, sGPReady, sLinked;

static constexpr size_t kStagingSize = 16u << 20;
static constexpr size_t kSlack = 64;
static uint8_t* sStaging;
static uint8_t sOverflowSink[256];
static bool sInFlush;

static void bindWritePointer() {
    if (sLinked) {
        if (!sStaging) {
            sStaging = (uint8_t*)port_host_alloc(kStagingSize, 64);
            __PortGXFifoPtr = sStaging;
        }
        if (__PortGXFifoPtr < sStaging || __PortGXFifoPtr >= sStaging + kStagingSize) {
            __PortGXFifoPtr = sStaging;
        }
        __PortGXFifoLimit = sStaging + kStagingSize - kSlack;
    } else if (sCPUReady) {
        __PortGXFifoPtr = (uint8_t*)sCPUFifo.wrPtr;
        __PortGXFifoLimit = sCPUFifo.top;
    } else {
        __PortGXFifoPtr = sOverflowSink;
        __PortGXFifoLimit = sOverflowSink + sizeof(sOverflowSink) - 16;
    }
}

// Drains the staging buffer through the command processor.
extern "C" void __PortGXFifoFlush(void) {
    if (!sLinked || !sStaging || sInFlush) {
        return;
    }
    sInFlush = true;
    size_t len = (size_t)(__PortGXFifoPtr - sStaging);
    PortHostAllocScope hostAlloc;  // the GPU emulation allocates from the host heap
    int64_t t0 = port_host_time_ns();
    size_t used = gpu::process(sStaging, len, false);
    port_perf_record(port_host_time_ns() - t0);
    size_t left = len - used;
    if (left) {
        memmove(sStaging, sStaging + used, left);
    }
    __PortGXFifoPtr = sStaging + left;
    sInFlush = false;
}

extern "C" void __PortGXFifoOverflow(void) {
    if (sLinked) {
        size_t before = (size_t)(__PortGXFifoPtr - sStaging);
        __PortGXFifoFlush();
        if ((size_t)(__PortGXFifoPtr - sStaging) >= before) {
            // A single command larger than the staging buffer; nothing we can do.
            port_fatal("gx: command larger than the %zu MiB staging FIFO", kStagingSize >> 20);
        }
        return;
    }
    // Display list overflow: remember it and discard further writes.
    if (__PortGXFifoPtr >= sOverflowSink && __PortGXFifoPtr < sOverflowSink + sizeof(sOverflowSink)) {
        __PortGXFifoPtr = sOverflowSink;
        return;
    }
    sCPUFifo.wrap = GX_TRUE;
    sCPUFifo.wrPtr = sCPUFifo.top;
    __PortGXFifoPtr = sOverflowSink;
    __PortGXFifoLimit = sOverflowSink + sizeof(sOverflowSink) - 16;
}

static void syncCPUWritePointer() {
    if (!sLinked && sCPUReady && !(__PortGXFifoPtr >= sOverflowSink && __PortGXFifoPtr < sOverflowSink + sizeof(sOverflowSink))) {
        sCPUFifo.wrPtr = __PortGXFifoPtr;
        sCPUFifo.count = (s32)((u8*)sCPUFifo.wrPtr - sCPUFifo.base);
    }
}

static bool linkCheck() {
    return sCPUReady && sGPReady && sCPUFifo.base == sGPFifo.base && sCPUFifo.top == sGPFifo.top;
}

extern "C" {

void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size) {
    __GXFifoObj* f = (__GXFifoObj*)fifo;
    f->base = (u8*)base;
    f->top = (u8*)base + size - 4;
    f->size = size;
    f->count = 0;
    f->hiWatermark = size - 0x4000;
    f->loWatermark = (size >> 1) & ~31u;
    f->rdPtr = base;
    f->wrPtr = base;
    f->wrap = GX_FALSE;
    f->bind_cpu = GX_FALSE;
    f->bind_gp = GX_FALSE;
}

void GXSetCPUFifo(const GXFifoObj* fifo) {
    syncCPUWritePointer();
    if (fifo == NULL) {
        sCPUReady = false;
        sLinked = false;
        bindWritePointer();
        return;
    }
    bool wasLinked = sLinked;
    sCPUFifo = *(const __GXFifoObj*)fifo;
    sCPUReady = true;
    sCPUFifo.bind_cpu = GX_TRUE;
    sLinked = linkCheck();
    sCPUFifo.bind_gp = sLinked ? GX_TRUE : GX_FALSE;
    if (wasLinked && !sLinked) {
        __PortGXFifoFlush();
    }
    bindWritePointer();
}

void GXSetGPFifo(const GXFifoObj* fifo) {
    if (fifo == NULL) {
        sGPReady = false;
        sLinked = false;
        bindWritePointer();
        return;
    }
    sGPFifo = *(const __GXFifoObj*)fifo;
    sGPReady = true;
    sGPFifo.bind_gp = GX_TRUE;
    sLinked = linkCheck();
    bindWritePointer();
}

GXBool GXGetCPUFifo(GXFifoObj* fifo) {
    syncCPUWritePointer();
    if (!sCPUReady) {
        return GX_FALSE;
    }
    *(__GXFifoObj*)fifo = sCPUFifo;
    return GX_TRUE;
}

GXBool GXGetGPFifo(GXFifoObj* fifo) {
    if (!sGPReady) {
        return GX_FALSE;
    }
    *(__GXFifoObj*)fifo = sGPFifo;
    return GX_TRUE;
}

void GXGetFifoPtrs(const GXFifoObj* fifo, void** readPtr, void** writePtr) {
    const __GXFifoObj* f = (const __GXFifoObj*)fifo;
    *readPtr = f->rdPtr;
    *writePtr = f->wrPtr;
}

u32 GXGetFifoCount(const GXFifoObj* fifo) { return (u32)((const __GXFifoObj*)fifo)->count; }

GXBool GXGetFifoWrap(const GXFifoObj* fifo) { return ((const __GXFifoObj*)fifo)->wrap; }

void GXGetGPStatus(GXBool* overhi, GXBool* underlow, GXBool* readIdle, GXBool* cmdIdle, GXBool* brkpt) {
    __PortGXFifoFlush();
    *overhi = GX_FALSE;
    *underlow = GX_TRUE;
    *readIdle = GX_TRUE;
    *cmdIdle = GX_TRUE;
    *brkpt = GX_FALSE;
}

GXBool __GXIsGPFifoReady(void) { return sGPReady ? GX_TRUE : GX_FALSE; }
void __GXFifoInit(void) {}
void __GXCleanGPFifo(void) {
    if (sStaging) {
        __PortGXFifoPtr = sStaging;
    }
}

void GXEnableBreakPt(void* pBreakPoint) { (void)pBreakPoint; }
void GXDisableBreakPt(void) {}
GXBreakPtCallback GXSetBreakPtCallback(GXBreakPtCallback cb) {
    (void)cb;
    return nullptr;
}

void GXSetGPMetric(GXPerf0 perf0, GXPerf1 perf1) {
    (void)perf0;
    (void)perf1;
}
void GXClearGPMetric(void) {}
void GXReadXfRasMetric(u32* xfWaitIn, u32* xfWaitOut, u32* rasBusy, u32* clocks) {
    *xfWaitIn = *xfWaitOut = *rasBusy = *clocks = 0;
}

u32 __PortGXPeekARGB(u16 x, u16 y) {
    __PortGXFifoFlush();
    gpu::Backend* b = gpu::backend();
    return b ? b->peekColor(x, y) : 0;
}

u32 __PortGXPeekZ(u16 x, u16 y) {
    __PortGXFifoFlush();
    gpu::Backend* b = gpu::backend();
    return b ? b->peekZ(x, y) : 0x00FFFFFF;
}

}  // extern "C"

// PE interrupts are forwarded to the handlers the GX library registered.
extern "C" void port_call_sdk_interrupt(int interrupt);

static void peTokenIrq() { port_call_sdk_interrupt(__OS_INTERRUPT_PI_PE_TOKEN); }
static void peFinishIrq() { port_call_sdk_interrupt(__OS_INTERRUPT_PI_PE_FINISH); }

extern "C" void port_gx_init(void) {
    __PortGXFifoPtr = sOverflowSink;
    __PortGXFifoLimit = sOverflowSink + sizeof(sOverflowSink) - 16;
    port_irq_set_handler(PORT_IRQ_PE_TOKEN, peTokenIrq);
    port_irq_set_handler(PORT_IRQ_PE_FINISH, peFinishIrq);
}
