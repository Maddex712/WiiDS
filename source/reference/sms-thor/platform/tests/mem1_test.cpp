// Exercise the real Android runtime allocator without a ROM, GPU or OS loop.
#include "../port_runtime.cpp"
#undef main
#include <cassert>
#include <sys/wait.h>

static void* rendererBase;
static uint32_t rendererSize;
extern "C" void GXPC_SetMemoryWindow(void* base, uint32_t size) { rendererBase = base; rendererSize = size; }
void port_crash_set_mem1(uintptr_t, size_t) {}

static void translations() {
    assert(rendererBase == port_mem1_base && rendererSize == port_mem1_size);
    const uintptr_t end = (uintptr_t)port_mem1_base + port_mem1_size;
    assert(end < 0x100000000ULL && (uintptr_t)(u32)end == end && OSRoundDown32B(end) == end);
    assert(OSPhysicalToCached(0) == port_mem1_base);
    for (uint32_t offset : {0u, 0x4000u, 0x10000u, port_mem1_size - 32}) {
        void* p = port_mem1_base + offset;
        assert(OSCachedToPhysical(p) == offset);
        assert(OSUncachedToPhysical(p) == offset);
        assert(OSPhysicalToCached(offset) == p);
        assert(OSPhysicalToUncached(offset) == p);
        assert(OSCachedToUncached(p) == p && OSUncachedToCached(p) == p);
        assert((uintptr_t)(u32)(uintptr_t)p == (uintptr_t)p); // game raw pointer slots
        assert(OSRoundUp32B((uintptr_t)p) == (uintptr_t)p && OSRoundDown32B((uintptr_t)p) == (uintptr_t)p);
        *static_cast<uint32_t*>(p) = offset ^ 0x12345678;
        assert(*static_cast<uint32_t*>(OSPhysicalToCached(OSCachedToPhysical(p))) == (offset ^ 0x12345678));
    }
}
int main() {
    setenv("SMS_MEM_MB", "64", 1);
    map_mem1();
    assert(port_mem1_base == reinterpret_cast<uint8_t*>(uintptr_t(0x80000000u)));
    translations();
    munmap(port_mem1_base, port_mem1_size);

    // Occupy the entire preferred arena. The allocator must leave it intact
    // and give SDK/GX users the same different low-address mapping.
    void* blocked = map_exact(reinterpret_cast<void*>(uintptr_t(0x80000000u)), 64u << 20);
    assert(blocked != MAP_FAILED);
    uint32_t* first = static_cast<uint32_t*>(blocked);
    uint32_t* last = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(blocked) + (64u << 20) - 4);
    *first = 0xabcdef01; *last = 0x12345678;
    void* fragment = map_exact(reinterpret_cast<void*>(uintptr_t(0x40000000u)), 0x100000u);
    assert(fragment != MAP_FAILED);
    *static_cast<uint32_t*>(fragment) = 0xdeadbeef;
    map_mem1();
    assert(port_mem1_base != blocked);
    assert(port_mem1_base == reinterpret_cast<uint8_t*>(uintptr_t(0x40100000u)));
    translations();
    assert(*first == 0xabcdef01 && *last == 0x12345678);
    assert(*static_cast<uint32_t*>(fragment) == 0xdeadbeef);
    munmap(port_mem1_base, port_mem1_size);
    munmap(fragment, 0x100000u);

    // A fragmented low half can still place MEM1 above 2 GiB, while keeping
    // the full arena and SDK pointer round-trips below 4 GiB.
    fflush(nullptr);
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        void* low = mmap(reinterpret_cast<void*>(uintptr_t(0x40000000u)), 0x40000000u, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (low != reinterpret_cast<void*>(uintptr_t(0x40000000u))) _Exit(90);
        map_mem1(); translations();
        assert((uintptr_t)port_mem1_base >= 0x84000000u);
        _Exit(0);
    }
    int status = 0; assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    // Exhaust all compatibility candidates in a child. Startup must fail rather
    // than accept the old arbitrary high mmap fallback. PROT_NONE uses no RAM.
    fflush(nullptr);
    child = fork(); assert(child >= 0);
    if (!child) {
        void* low = mmap(reinterpret_cast<void*>(uintptr_t(0x40000000u)), 0x40000000u, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (low != reinterpret_cast<void*>(uintptr_t(0x40000000u))) _Exit(90);
        // Leave the last 64 MiB free: its ArenaHi would truncate from 4 GiB
        // to zero, so it must not be accepted as an apparently fitting range.
        void* high = mmap(reinterpret_cast<void*>(uintptr_t(0x84000000u)), 0x78000000u, PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (high != reinterpret_cast<void*>(uintptr_t(0x84000000u))) _Exit(92);
        map_mem1();
        _Exit(91); // must not continue with incompatible memory
    }
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    assert(*first == 0xabcdef01 && *last == 0x12345678);
    munmap(blocked, 64u << 20);
    puts("PASS Android MEM1: preferred/collision allocation, pointer/SDK/GX coherence, preserved mappings, safe exhaustion failure");
}
