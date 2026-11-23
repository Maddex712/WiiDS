#pragma once
#include <cerrno>
#include <cstdio>
#ifndef XD_STACK_MAPS_PATH
#define XD_STACK_MAPS_PATH "/proc/self/maps"
#endif
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

// Task pointers can be stored in signed 32-bit game fields. Search actual
// gaps below 2 GiB; never carve stacks out of the separately owned data arena.
static void* xdMapStack(size_t bytes, size_t page) {
    if (!bytes || bytes > 0x10000000 || !page || (page & (page-1))) {
        errno=EINVAL; return MAP_FAILED;
    }
    bytes=(bytes+page-1)&~(page-1);
    for (unsigned retry=0; retry<16; ++retry) {
        FILE* f=std::fopen(XD_STACK_MAPS_PATH,"r");
        if (!f) return MAP_FAILED;
        uintptr_t cursor=0x10000000, choice=0;
        constexpr uintptr_t end=0x80000000;
        char line[2048];
        while (std::fgets(line,sizeof(line),f)) {
            unsigned long long a,z;
            if (std::sscanf(line,"%llx-%llx",&a,&z)!=2 || a>=z) continue;
            if (z<=cursor) continue;
            uintptr_t limit=a<end?uintptr_t(a):end;
            if (cursor<limit && bytes<=limit-cursor) { choice=cursor; break; }
            if (a>=end) break;
            if (z>=end) { cursor=end; break; }
            cursor=(uintptr_t(z)+0xffff)&~uintptr_t(0xffff);
        }
        bool good=!std::ferror(f); std::fclose(f);
        if (!good) { errno=EIO; return MAP_FAILED; }
        if (!choice && cursor<end && bytes<=end-cursor) choice=cursor;
        if (!choice) { errno=ENOMEM; return MAP_FAILED; }
        void* p=mmap(reinterpret_cast<void*>(choice),bytes,PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
        if (p==MAP_FAILED && errno==EINVAL)
            p=mmap(reinterpret_cast<void*>(choice),bytes,PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if (p==reinterpret_cast<void*>(choice)) return p;
        // Old kernels can ignore NOREPLACE. Reject alternatives, including
        // high addresses, and release only the mapping just returned to us.
        if (p!=MAP_FAILED) munmap(p,bytes);
        // A concurrent mmap can win the gap. Re-read maps before retrying.
    }
    errno=ENOMEM; return MAP_FAILED;
}
