#pragma once
#include <unwind.h>
#include <stdint.h>
struct XdTrace { void** frames; int count,capacity; };
static inline _Unwind_Reason_Code xd_trace_frame(struct _Unwind_Context* context,void* opaque) {
    struct XdTrace* t=(struct XdTrace*)opaque;
    if(t->count>=t->capacity) return _URC_END_OF_STACK;
    uintptr_t ip=_Unwind_GetIP(context);
    if(ip) t->frames[t->count++]=(void*)ip;
    return _URC_NO_REASON;
}
static inline int backtrace(void** frames,int capacity) {
    struct XdTrace t={frames,0,capacity}; _Unwind_Backtrace(xd_trace_frame,&t); return t.count;
}
