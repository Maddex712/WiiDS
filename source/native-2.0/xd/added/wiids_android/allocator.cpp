#include "port_low_arena.h"
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>
// Only the game DSO binds these symbols locally. SDL/JVM retain their allocators.
extern "C" void* malloc(size_t n) { return PortLow_Malloc(n); }
extern "C" void* calloc(size_t n,size_t s) { return PortLow_Calloc(n,s); }
extern "C" void free(void* p) {
    if(!p) return;
    if(PortLow_Owns(p)) { PortLow_Free(p); return; }
    using Free=void(*)(void*);
    static Free systemFree=reinterpret_cast<Free>(dlsym(RTLD_NEXT,"free"));
    if(!systemFree) std::abort();
    systemFree(p);
}
extern "C" void* realloc(void* p,size_t n) {
    if(!p || PortLow_Owns(p)) return PortLow_Realloc(p,n);
    using Size=size_t(*)(const void*);
    static Size usable=reinterpret_cast<Size>(dlsym(RTLD_NEXT,"malloc_usable_size"));
    if(!usable) std::abort();
    void* q=PortLow_Malloc(n);
    if(q) { size_t old=usable(p); std::memcpy(q,p,old<n?old:n); free(p); }
    return q;
}
extern "C" int posix_memalign(void** out,size_t alignment,size_t n) {
    if(alignment<sizeof(void*) || (alignment&(alignment-1))) return EINVAL;
    void* p=PortLow_Memalign(alignment,n); if(!p) return ENOMEM; *out=p; return 0;
}
extern "C" void* aligned_alloc(size_t a,size_t n) { return PortLow_Memalign(a,n); }
