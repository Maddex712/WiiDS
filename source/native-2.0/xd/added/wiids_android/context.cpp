#include "xd_context.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include "low_stack_map.h"
extern "C" void XdContext_Start();
extern "C" int XdContext_Init(XdContext* c) { std::memset(c,0,sizeof(*c)); return 0; }
extern "C" void XdContext_Make(XdContext* c, void (*entry)(), int argc) {
    if(argc || !c->uc_stack.ss_sp || c->uc_stack.ss_size<16384) std::abort();
    c->entry=entry;
    c->saved[0]=reinterpret_cast<uintptr_t>(c);
    c->saved[11]=reinterpret_cast<uintptr_t>(&XdContext_Start);
    c->saved[12]=(reinterpret_cast<uintptr_t>(c->uc_stack.ss_sp)+c->uc_stack.ss_size)&~uintptr_t(15);
}
extern "C" void XdContext_Run(XdContext* c) {
    c->entry();
    if(c->uc_link) XdContext_Swap(c,c->uc_link);
    std::abort();
}
extern "C" void* XdMapLow(size_t bytes) {
    static std::mutex lock;
    std::lock_guard<std::mutex> hold(lock);
    const size_t page=static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return xdMapStack(bytes,page);
}
