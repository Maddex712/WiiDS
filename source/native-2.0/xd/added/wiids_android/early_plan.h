#pragma once
#include <stddef.h>
#include <stdint.h>
struct XdEarlyMemory { uint32_t abi; uint32_t ready; uintptr_t mem1, cache, low; };
constexpr size_t xdMemSize=0x01800000, xdCacheSize=0x4000, xdLowSize=0x10000000;
constexpr uintptr_t xdMemBase=0xC0000000, xdCacheBase=0xE0000000;
// Never put the data arena over ELF windows, SDK low data, coroutine stacks,
// original MEM1, or relocated MEM1. Data remains below signed 2 GiB.
template<class Backend> bool xdReserveEarly(XdEarlyMemory& s,Backend& b) {
    if(s.ready)return true;
    s={1,0,0,0,0};
    if(!b.exact(xdMemBase,xdMemSize,true))return false;
    s.mem1=xdMemBase;
    if(!b.exact(xdCacheBase,xdCacheSize,true)) {
        b.release(s.mem1,xdMemSize);s.mem1=0;return false;
    }
    s.cache=xdCacheBase;
    // Scan actual gaps, rather than six fixed 256 MiB slots. The higher
    // window starts beyond the SDK/stack windows; the ELF planner sees
    // this reservation as occupied, including its 60000000-70000000 window.
    s.low=b.gap(0x51000000,0x80000000,xdLowSize);
    if(!s.low) {
        b.release(s.cache,xdCacheSize);b.release(s.mem1,xdMemSize);
        s.mem1=s.cache=0;return false;
    }
    s.ready=1;return true;
}
