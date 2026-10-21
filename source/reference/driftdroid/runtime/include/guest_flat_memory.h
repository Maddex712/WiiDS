#pragma once

// Flat 4 GiB guest address space: a guest access is just `*(T*)(kFlatGuestBase + addr)` plus a
// byte swap, no page-table load/branch. Two views alias the same physical memory: the GUEST
// view at kFlatGuestBase has page protections as the interception mechanism (unmapped/MMIO/
// executable/deferred-read pages are uncommitted or protected), while the HOST view is a plain
// alias native runtime code (image loading, DVD reads, HLE, GX) writes through unchecked.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace GuestFlat {

// Fixed base so the emitted access is `[reg + imm64-in-register]` with no load
// of a global. 16 TiB: clear of the Windows ASan shadow (32 TiB) and of the
// usual image/heap placement.
inline constexpr uint64_t kGuestSpaceSize = 0x1'0000'0000ull;
#if defined(__ANDROID__)
// The desktop base (16 TiB, 2^48) assumes a 48-bit (or wider) virtual address space - true on
// Windows/desktop Linux, but NOT universal on Android. Confirmed directly on the user's real
// device (Huawei nova 9, Kirin 985): the kernel silently ignores any mmap hint at or above 2^39
// (redirects to an unrelated low address instead of failing, so this can't be detected any other
// way than trying it) - most ARM64 Android kernels are built with a narrower VA_BITS
// configuration than the CPU itself could support, to save page-table memory; 39-bit is a very
// common choice. 128 GiB (2^37) has real margin below the empirically-found ~2^38 ceiling on
// that device while still being far above where the heap/stack/loaded libraries actually sit, so
// it should generalize to other/narrower-VA Android devices too, though this has only been
// verified on the one device the project can currently test on - if a different Android device
// hits the same "reserve the 4 GiB flat guest address space" runtime error, try successively
// lower values here (0x0000'0010'0000'0000ull and below) and report which one works.
inline constexpr uintptr_t kFixedFlatGuestBase = 0x0000'0020'0000'0000ull;
#elif defined(__SWITCH__)
// No fixed base on Switch. Horizon places each process's 8 GiB heap region and 6 GiB alias region
// at random addresses inside the 64 GiB ASLR region, and code/aliased memory (the only kind that
// can carry multiple views - see guest_flat_memory.cpp) may not overlap either of them. Any
// constant 4 GiB window therefore collides on some fraction of launches; hardware showed the heap
// sitting at 0x8e8bc7000, right inside the old 32 GiB constant. Initialize() instead picks a free
// window per launch and publishes it here once, before any translated code runs.
//
// Declared const (with a fixed symbol name) everywhere except guest_flat_memory.cpp, which owns
// the one write. A mutable global could be aliased by any guest memory write (they go through
// uint8_t*), so GCC reloaded it before every single guest access; a const object cannot be
// modified by those writes, which lets each translated function load the base once. Hidden
// visibility removes the extra GOT indirection. Safe because the value is set exactly once,
// before any reader runs, and never changes afterwards.
#if defined(MKW_GUEST_FLAT_BASE_WRITER)
extern uintptr_t g_switchFlatGuestBase __asm__("mkw_switch_flat_guest_base")
    __attribute__((visibility("hidden")));
#else
extern const uintptr_t g_switchFlatGuestBase __asm__("mkw_switch_flat_guest_base")
    __attribute__((visibility("hidden")));
#endif
#else
inline constexpr uintptr_t kFixedFlatGuestBase = 0x0000'1000'0000'0000ull;
#endif

#if defined(__SWITCH__)
#define MKW_FLAT_GUEST_BASE (reinterpret_cast<uint8_t*>(GuestFlat::g_switchFlatGuestBase))
#else
#define MKW_FLAT_GUEST_BASE (reinterpret_cast<uint8_t*>(GuestFlat::kFixedFlatGuestBase))
#endif

enum class Backing {
    Owned,
    Mem1,
    Mem2,
};

struct RegionRequest {
    uint32_t base = 0;
    uint64_t size = 0;
    Backing backing = Backing::Owned;
};

struct FaultCounters {
    uint32_t mmio = 0;      // MMIO access that reached the handler
    uint32_t efb = 0;       // deferred (EFB) read materialized from a trap
    uint32_t xguard = 0;    // executable-page write trap
    uint32_t unmapped = 0;  // guest touches that landed outside every mapped region
    uint32_t unmappedRegions = 0;  // distinct 64 KiB blocks committed on demand
};

// True once the reservation exists and translated code may use the flat path.
bool IsActive();

// Reserves the 4 GiB space (once per process) and maps every requested region
// into both views. Throws std::runtime_error with a precise diagnosis when the
// reservation, the section objects or a view cannot be created - a silent
// fallback would let translated code read from an unmapped constant base.
void Initialize(const std::vector<RegionRequest>& regions);

// Host-view pointer for a mapped guest address, or nullptr when the address is
// outside every mapped region. This is what the page table and
// Memory::GetPointer hand out.
uint8_t* HostPointer(uint32_t guestAddress);

// Deferred (EFB) reads: the covered guest pages are made PAGE_NOACCESS in the
// guest view so a flat read traps and materializes the copy.
void ProtectDeferredRange(uint32_t address, size_t length);
void UnprotectDeferredRange(uint32_t address, size_t length);

// Executable-write guard: pages fully covered by a registered executable range
// become PAGE_READONLY in the guest view. Registration order does not matter -
// ranges registered before the mapping exists are re-applied by Initialize.
void RegisterExecutableRange(uint32_t start, uint32_t end);

FaultCounters Counters();

// End-of-run report for the counters above. An unmapped touch is a wild guest
// pointer whose block this module silently committed so execution could go on,
// which makes it invisible unless it is repeated at shutdown; a nonzero count
// is therefore reported as a warning. Idempotent, so every exit path (normal
// return, caught exception, abort handler) may call it.
void LogFaultSummary() noexcept;

// Returns true when the access violation was a guest-space fault this module
// resolved; the caller must then resume execution. `faultAddress` is the raw
// host pointer the access violation trapped on (Windows: ExceptionInformation[1];
// POSIX: siginfo_t::si_addr) and `isWrite` is whether it was a write access
// (Windows: ExceptionInformation[0] != 0; POSIX: derived from the ucontext).
// The platform-specific handler that calls this is expected to have already
// done that extraction - this function only ever works with the parsed pair.
bool HandleAccessViolation(void* faultAddress, bool isWrite) noexcept;

} // namespace GuestFlat
