#include "stack_bridge.h"
#include "xd_context.h"
#include <pthread.h>
#include <android/log.h>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
namespace {
struct Request { XdContext caller{}; void (*fn)(void*); void* arg; const char* name; };
thread_local XdContext* root = nullptr;
thread_local Request* pending = nullptr;
thread_local uintptr_t stackLo = 0, stackHi = 0;
thread_local unsigned dispatched = 0;
void stackBounds() {
    pthread_attr_t attr;
    void* base = nullptr; size_t size = 0;
    if (pthread_getattr_np(pthread_self(), &attr)) std::abort();
    int err = pthread_attr_getstack(&attr, &base, &size);
    pthread_attr_destroy(&attr);
    if (err || !base || !size) std::abort();
    stackLo = reinterpret_cast<uintptr_t>(base); stackHi = stackLo + size;
}
}
extern "C" bool XdStack_IsForeign() {
    if (!stackLo) stackBounds();
    char local;
    auto sp = reinterpret_cast<uintptr_t>(&local);
    return sp < stackLo || sp >= stackHi;
}
extern "C" void XdStack_Call(void (*fn)(void*), void* arg, const char* name) {
    if (!XdStack_IsForeign()) { fn(arg); return; }
    if (!root || pending) {
        __android_log_print(ANDROID_LOG_FATAL, "XD-stack", "Unregistered foreign stack for %s tid=%d", name, gettid());
        std::abort();
    }
    Request request{}; request.fn = fn; request.arg = arg; request.name = name;
    pending = &request;
    XdContext_Swap(&request.caller, root);
}
extern "C" void XdStack_RunMain(XdContext* start, XdContext* game) {
    if (root || XdStack_IsForeign()) std::abort();
    root = start;
    __android_log_print(ANDROID_LOG_INFO, "XD-stack", "SDL real stack [%p,%p) tid=%d; all SDL/JNI calls marshalled here", (void*)stackLo, (void*)stackHi, gettid());
    XdContext* next = game;
    for (;;) {
        XdContext_Swap(root, next);
        Request* request = pending;
        if (!request) break;
        pending = nullptr;
        if (XdStack_IsForeign()) std::abort();
        if (dispatched++ < 16)
            __android_log_print(ANDROID_LOG_INFO, "XD-stack", "Real-stack call %s tid=%d", request->name, gettid());
        request->fn(request->arg);
        next = &request->caller;
    }
    root = nullptr;
}
