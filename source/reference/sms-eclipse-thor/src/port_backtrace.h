#pragma once
#ifndef __ANDROID__
#include <execinfo.h>
#else
#include <unwind.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

struct PortBacktrace { void** frames; int capacity, count; };
static inline _Unwind_Reason_Code port_backtrace_frame(_Unwind_Context* context, void* opaque) {
    PortBacktrace* out = static_cast<PortBacktrace*>(opaque);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc && out->count < out->capacity) out->frames[out->count++] = reinterpret_cast<void*>(pc);
    return out->count == out->capacity ? _URC_END_OF_STACK : _URC_NO_REASON;
}
static inline int backtrace(void** frames, int capacity) {
    PortBacktrace out{frames, capacity, 0};
    _Unwind_Backtrace(port_backtrace_frame, &out);
    return out.count;
}
static inline char** backtrace_symbols(void* const* frames, int count) {
    if (count <= 0) return nullptr;
    const size_t stride = 1024;
    char** result = static_cast<char**>(malloc(size_t(count) * (sizeof(char*) + stride)));
    if (!result) return nullptr;
    char* names = reinterpret_cast<char*>(result + count);
    for (int i = 0; i < count; ++i) {
        result[i] = names + size_t(i) * stride;
        Dl_info info{};
        if (dladdr(frames[i], &info))
            snprintf(result[i], stride, "%s+0x%lx (%s)", info.dli_fname,
                static_cast<unsigned long>(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(info.dli_fbase)),
                info.dli_sname ? info.dli_sname : "");
        else snprintf(result[i], stride, "%p", frames[i]);
    }
    return result;
}
static inline void backtrace_symbols_fd(void* const* frames, int count, int fd) {
    for (int i = 0; i < count; ++i) {
        Dl_info info{};
        if (dladdr(frames[i], &info))
            dprintf(fd, "[port] frame %d %p %s+0x%lx %s\n", i, frames[i], info.dli_fname,
                    static_cast<unsigned long>(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(info.dli_fbase)),
                    info.dli_sname ? info.dli_sname : "");
        else dprintf(fd, "[port] frame %d %p\n", i, frames[i]);
    }
}
#endif
