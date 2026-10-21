// This file owns the one write to the Switch guest base (see the declaration in the header).
#define MKW_GUEST_FLAT_BASE_WRITER
#include "guest_flat_memory.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "memory.h"
#include "ppc_runtime.h"
#include "recomp_mod_loader.h"
#include "runtime_log.h"
#include "system_bridge.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__SWITCH__)
// Nintendo Switch homebrew (devkitA64/libnx): no mmap()/mprotect()/memfd_create() at all -
// Horizon OS's user-mode memory API is svc*-call-based instead.
//
// Multi-view aliasing, hardware-verified 2026-09-14 (1/64/256 MiB):
//   1. memalign() ordinary heap pages (state Normal), one guard page longer than what gets mapped
//      (see kSwitchHeapTailSlack),
//   2. svcMapProcessCodeMemory(self, hostView, heap, size) turns them into code memory at
//      hostView (the heap source becomes locked/inaccessible, which is fine - nothing uses it),
//   3. svcSetProcessMemoryPermission(self, hostView, size, RW) makes that view writable,
//   4. svcMapProcessMemory(guestView, self, hostView + offset, size) adds any number of further
//      live views (state SharedCode) of the same pages.
// Writes through any view are visible through all of them. Why not shared memory: under
// nx-hbloader svcCreateSharedMemory is capped at ~1-4 MiB (rc 0x10801 LimitReached), and a plain
// heap source for svcMapProcessMemory is rejected (rc 0xd401). Neither the CodeData view nor the
// SharedCode views accept svcSetMemoryPermission/svcSetProcessMemoryPermission on sub-ranges
// (rc 0xd401), so ProtectRange() reports failure and every caller falls back to the checked
// path, which is also the only correct behavior here: Switch has no fault handler to service a
// protected-page trap anyway.
#include <malloc.h>
#include <switch.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__ANDROID__)
// Bionic's memfd_create() libc wrapper is gated __INTRODUCED_IN(30) (Android 11), but the
// memfd_create kernel syscall itself has existed since kernel 3.17 (2014) - long before any
// kernel version an API-28+ device could ship - so this project's minSdk 28 target reaches the
// syscall directly instead of raising minSdk just for a libc wrapper around it. MFD_CLOEXEC
// (a plain numeric flag, not gated by API level) comes from linux/memfd.h; SYS_memfd_create
// comes from sys/syscall.h's per-architecture unistd.h.
#include <linux/memfd.h>
#include <sys/syscall.h>
#endif
#endif

namespace GuestFlat {
#if defined(__SWITCH__)
uintptr_t g_switchFlatGuestBase __asm__("mkw_switch_flat_guest_base")
    __attribute__((visibility("hidden"))) = 0;

namespace {
// svcMapProcessCodeMemory makes its source pages inaccessible, but newlib's allocator keeps a
// boundary tag for the *following* chunk in the last bytes of a block (and memalign frees its
// trailing remainder right there). Mapping the whole block hid that tag, and the next malloc
// touching it took a data abort inside _malloc_r. Allocating one extra page that is never mapped
// keeps every allocator header on accessible memory.
constexpr size_t kSwitchHeapTailSlack = 0x1000;
} // namespace
#endif
namespace {

#if defined(_WIN32)
// Placeholder / view constants. Declared here so the build does not depend on
// the exact Windows SDK version that first shipped them.
constexpr DWORD kMemReplacePlaceholder = 0x00004000;
constexpr DWORD kMemReservePlaceholder = 0x00040000;
constexpr DWORD kMemPreservePlaceholder = 0x00000002;
#endif
constexpr size_t kAllocationGranularity = 0x10000;  // 64 KiB
constexpr size_t kHostPageSize = 0x1000;

// Named, platform-neutral protection modes so every fault-interception call site below (the
// MMIO window, the executable-write guard, deferred-EFB-read protection, the on-demand
// unmapped-block commit) can stay identical text on both platforms; only ProtectRange() and
// CommitPlaceholder() below branch on VirtualProtect vs. mprotect.
#if defined(_WIN32)
using ProtectionFlags = DWORD;
constexpr ProtectionFlags kProtNone = PAGE_NOACCESS;
constexpr ProtectionFlags kProtRead = PAGE_READONLY;
constexpr ProtectionFlags kProtReadWrite = PAGE_READWRITE;
#elif defined(__SWITCH__)
using ProtectionFlags = u32;
constexpr ProtectionFlags kProtNone = Perm_None;
constexpr ProtectionFlags kProtRead = Perm_R;
constexpr ProtectionFlags kProtReadWrite = Perm_Rw;
#else
using ProtectionFlags = int;
constexpr ProtectionFlags kProtNone = PROT_NONE;
constexpr ProtectionFlags kProtRead = PROT_READ;
constexpr ProtectionFlags kProtReadWrite = PROT_READ | PROT_WRITE;
#endif

#if defined(_WIN32)
using VirtualAlloc2Fn = PVOID(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, void*, ULONG);
using MapViewOfFile3Fn = PVOID(WINAPI*)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, void*, ULONG);

VirtualAlloc2Fn g_virtualAlloc2 = nullptr;
MapViewOfFile3Fn g_mapViewOfFile3 = nullptr;
#endif

uint8_t* g_base = nullptr;
bool g_initialized = false;
std::vector<RegionRequest> g_activeRegions;
#if defined(_WIN32)
PVOID g_vectoredHandle = nullptr;
#elif defined(__SWITCH__)
// Owns the flat guest space's outer VA reservation for the process lifetime - never removed
// (matches the other platforms, which likewise never release the 4 GiB reservation once made).
VirtmemReservation* g_switchReservation = nullptr;
#endif

std::mutex& StateMutex() {
    static std::mutex mutex;
    return mutex;
}

struct SectionKey {
    Backing backing = Backing::Owned;
    uint32_t ownedBase = 0;

    bool operator==(const SectionKey& other) const {
        return backing == other.backing && ownedBase == other.ownedBase;
    }
};

struct SectionKeyHash {
    size_t operator()(const SectionKey& key) const {
        return (static_cast<size_t>(key.ownedBase) << 3) ^ static_cast<size_t>(key.backing);
    }
};

struct Section {
#if defined(_WIN32)
    HANDLE handle = nullptr;
#elif defined(__SWITCH__)
    // Heap pages backing the section. Locked by svcMapProcessCodeMemory once hostView exists;
    // every guest view (MapGuestView) is an svcMapProcessMemory alias of hostView.
    uint8_t* heapSource = nullptr;
#else
    int fd = -1;
#endif
    uint64_t size = 0;
    uint8_t* hostView = nullptr;
};

std::unordered_map<SectionKey, Section, SectionKeyHash>& Sections() {
    static std::unordered_map<SectionKey, Section, SectionKeyHash> sections;
    return sections;
}

struct MappedRegion {
    uint32_t guestBase = 0;
    uint64_t guestSize = 0;      // requested size (page-table authority)
    uint64_t mappedSize = 0;     // rounded to allocation granularity
    uint64_t sectionOffset = 0;
    uint8_t* hostView = nullptr; // section host view base
};

std::vector<MappedRegion>& MappedRegions() {
    static std::vector<MappedRegion> regions;
    return regions;
}

struct GuardedRange {
    uint32_t start = 0;
    uint32_t end = 0;
};

std::vector<GuardedRange>& ExecutableRanges() {
    static std::vector<GuardedRange> ranges;
    return ranges;
}

// 4 KiB guest pages currently PAGE_READONLY for the executable-write guard.
std::vector<uint8_t>& ExecutableProtectedPages() {
    static std::vector<uint8_t> pages(1u << 20, 0);  // 2^32 / 4 KiB
    return pages;
}

std::vector<GuardedRange>& DeferredRanges() {
    static std::vector<GuardedRange> ranges;
    return ranges;
}

#if !defined(_WIN32)
// Windows disambiguates a racing "unmapped touch" fault via VirtualQuery (did some other thread
// already commit this 64 KiB block, and is it actually accessible enough to satisfy this access).
// mprotect has no query counterpart, so this tracks the same fact ourselves: one bit per 64 KiB
// block, set the first time this module ever commits it, checked-and-set under StateMutex() so
// two threads racing on the same never-yet-committed block still report/commit exactly once.
std::vector<uint8_t>& UnmappedCommittedBlocks() {
    static std::vector<uint8_t> blocks(1u << 16, 0);  // 2^32 / 64 KiB
    return blocks;
}
#endif

std::atomic<uint32_t> g_countMmio{0};
std::atomic<uint32_t> g_countEfb{0};
std::atomic<uint32_t> g_countXGuard{0};
std::atomic<uint32_t> g_countUnmapped{0};
std::atomic<uint32_t> g_countUnmappedRegions{0};

// Full register dumps for the first few committed regions. The one-line record
// below is emitted for every region regardless; the dump is what the historical
// checked path produced for an unmapped access, and it stays useful only while
// the log is still readable - a pointer that walks a large stride would
// otherwise bury the run in 65536 dumps.
constexpr uint32_t kUnmappedCpuDumpLimit = 16;

uint64_t RoundUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

std::string LastErrorText(const char* what) {
    std::ostringstream oss;
#if defined(_WIN32)
    oss << what << " failed (GetLastError=" << GetLastError() << ")";
#else
    oss << what << " failed (" << std::strerror(errno) << ")";
#endif
    return oss.str();
}

#if defined(__SWITCH__)
// errno is not meaningful for svc-call failures (Result codes never touch it), so Switch call
// sites report the Result code directly instead of going through LastErrorText().
std::string SwitchErrorText(const char* what, Result rc) {
    std::ostringstream oss;
    oss << what << " failed (rc=0x" << std::hex << rc << std::dec << ")";
    return oss.str();
}
#endif

// Protects [address, address+size) with `protection`, bridging VirtualProtect (Windows) and
// mprotect (POSIX) so every fault-interception call site below can stay platform-neutral.
bool ProtectRange(uint8_t* address, uint64_t size, ProtectionFlags protection) {
#if defined(_WIN32)
    DWORD previous = 0;
    return VirtualProtect(address, static_cast<SIZE_T>(size), protection, &previous) != FALSE;
#elif defined(__SWITCH__)
    // Horizon refuses reprotection of code/aliased memory (see the file-level comment), and a
    // protected page would have no fault handler to service it. Failing without a syscall keeps
    // ApplyExecutableProtectionLocked from issuing thousands of doomed svcs at startup.
    (void)address;
    (void)size;
    (void)protection;
    return false;
#else
    return mprotect(address, static_cast<size_t>(size), protection) == 0;
#endif
}

#if defined(__ANDROID__)
// See the file-level comment by the linux/memfd.h include above: this project's minSdk (28)
// predates Bionic's memfd_create() libc wrapper (30), so this goes straight to the syscall.
int MkwMemfdCreate(const char* name, unsigned flags) {
    return static_cast<int>(::syscall(SYS_memfd_create, name, flags));
}
#elif !defined(_WIN32) && !defined(__SWITCH__)
// Switch has no memfd_create (or any mmap/fd-based memory API at all - see the file-level
// __SWITCH__ comment by the <switch.h> include above) and never calls this: its section-creation
// path in Initialize() uses heap-backed code memory instead.
inline int MkwMemfdCreate(const char* name, unsigned flags) {
    return memfd_create(name, flags);
}
#endif

#if defined(_WIN32)
void ResolvePlacementApi() {
    if (g_virtualAlloc2 != nullptr && g_mapViewOfFile3 != nullptr) return;
    HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
    if (kernelBase == nullptr) kernelBase = LoadLibraryW(L"kernelbase.dll");
    if (kernelBase != nullptr) {
        g_virtualAlloc2 =
            reinterpret_cast<VirtualAlloc2Fn>(GetProcAddress(kernelBase, "VirtualAlloc2"));
        g_mapViewOfFile3 =
            reinterpret_cast<MapViewOfFile3Fn>(GetProcAddress(kernelBase, "MapViewOfFile3"));
    }
    if (g_virtualAlloc2 == nullptr || g_mapViewOfFile3 == nullptr) {
        throw std::runtime_error(
            "Flat guest memory requires Windows 10 1803 or newer (VirtualAlloc2/MapViewOfFile3 "
            "are unavailable on this system).");
    }
}
#endif

void EnsureReservation() {
    if (g_base != nullptr) return;
#if defined(_WIN32)
    ResolvePlacementApi();

    void* requested = reinterpret_cast<void*>(kFixedFlatGuestBase);

    // One extra granule stays an uncommitted placeholder so an access that
    // straddles 0xFFFFFFFF faults instead of corrupting whatever the allocator
    // happened to place directly after the reservation.
    void* reserved = g_virtualAlloc2(GetCurrentProcess(), requested,
                                     static_cast<SIZE_T>(kGuestSpaceSize + kAllocationGranularity),
                                     MEM_RESERVE | kMemReservePlaceholder, PAGE_NOACCESS,
                                     nullptr, 0);
    if (reserved == nullptr) {
        std::ostringstream oss;
        oss << "Unable to reserve the 4 GiB flat guest address space at 0x" << std::hex
            << reinterpret_cast<uintptr_t>(requested) << std::dec
            << " (GetLastError=" << GetLastError()
            << "). The translated code addresses guest memory through this fixed base, so it "
               "cannot fall back to another one. Something else in this process reserved the "
               "16 TiB region first - an injected DLL, an overlay or a debugging tool is the "
               "usual cause.";
        throw std::runtime_error(oss.str());
    }
    if (reserved != requested) {
        throw std::runtime_error(
            "The flat guest reservation did not land on the fixed base the translated code was "
            "compiled against.");
    }
#elif defined(__SWITCH__)
    // No fixed base (see g_switchFlatGuestBase in the header): virtmemFindCodeMemory only returns
    // windows outside the randomized heap/alias/stack regions and clear of existing mappings -
    // exactly where svcMapProcessMemory views are allowed to go.
    virtmemLock();
    void* reserved = virtmemFindCodeMemory(kGuestSpaceSize + kAllocationGranularity, 0);
    VirtmemReservation* reservation =
        reserved != nullptr
            ? virtmemAddReservation(reserved, kGuestSpaceSize + kAllocationGranularity)
            : nullptr;
    virtmemUnlock();
    if (reservation == nullptr) {
        throw std::runtime_error(
            "Unable to find a free 4 GiB window for the flat guest address space.");
    }
    g_switchReservation = reservation;
    g_switchFlatGuestBase = reinterpret_cast<uintptr_t>(reserved);
#else
    void* requested = reinterpret_cast<void*>(kFixedFlatGuestBase);

    // No MAP_FIXED here (and deliberately no MAP_FIXED_NOREPLACE, which needs Linux 4.17+ -
    // this must work on kernels as old as 4.9): `requested` is only a hint. The kernel's
    // get_unmapped_area honors a page-aligned hint when the whole range is free, so this lands
    // on the fixed base in the normal case; if anything already occupies part of the range, the
    // kernel silently picks a different address instead of clobbering it, which the check below
    // catches - same "something got there first" contract as the Windows path, without needing
    // a specific kernel version.
    void* reserved = mmap(requested, kGuestSpaceSize + kAllocationGranularity, kProtNone,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (reserved == MAP_FAILED) {
        std::ostringstream oss;
        oss << "Unable to reserve the 4 GiB flat guest address space at 0x" << std::hex
            << reinterpret_cast<uintptr_t>(requested) << std::dec
            << " (" << std::strerror(errno)
            << "). The translated code addresses guest memory through this fixed base, so it "
               "cannot fall back to another one.";
        throw std::runtime_error(oss.str());
    }
    if (reserved != requested) {
        munmap(reserved, kGuestSpaceSize + kAllocationGranularity);
        std::ostringstream oss;
        oss << "Unable to reserve the 4 GiB flat guest address space at 0x" << std::hex
            << reinterpret_cast<uintptr_t>(requested) << std::dec
            << ". Something else in this process already occupies part of the 16 TiB region - "
               "an injected library, an overlay or a debugging tool is the usual cause.";
        throw std::runtime_error(oss.str());
    }
#endif

    g_base = static_cast<uint8_t*>(reserved);
}

#if defined(_WIN32)
// Carves `size` bytes out of the enclosing placeholder so a view or a private
// commit can replace it. Splitting an exact-size placeholder is a no-op that
// reports ERROR_INVALID_PARAMETER; the caller validates the replacement.
void SplitPlaceholder(uint8_t* address, uint64_t size) {
    VirtualFree(address, static_cast<SIZE_T>(size), MEM_RELEASE | kMemPreservePlaceholder);
}
#endif

void MapGuestView(const Section& section, uint64_t sectionOffset, uint32_t guestBase,
                  uint64_t mappedSize) {
    uint8_t* target = g_base + guestBase;
#if defined(_WIN32)
    SplitPlaceholder(target, mappedSize);
    void* view = g_mapViewOfFile3(section.handle, GetCurrentProcess(), target, sectionOffset,
                                  static_cast<SIZE_T>(mappedSize), kMemReplacePlaceholder,
                                  PAGE_READWRITE, nullptr, 0);
    if (view == nullptr) {
        std::ostringstream oss;
        oss << "Unable to map guest region 0x" << std::hex << guestBase << " (+0x" << mappedSize
            << ") into the flat reservation" << std::dec << " (GetLastError=" << GetLastError()
            << ")";
        throw std::runtime_error(oss.str());
    }
#elif defined(__SWITCH__)
    // An additional live alias of the section's host view (see the file-level comment), like
    // MapViewOfFile3/mmap(MAP_SHARED) mapping the same backing object twice elsewhere.
    Result rc = svcMapProcessMemory(target, envGetOwnProcessHandle(),
                                    reinterpret_cast<u64>(section.hostView) + sectionOffset,
                                    mappedSize);
    if (R_FAILED(rc)) {
        std::ostringstream oss;
        oss << "Unable to map guest region 0x" << std::hex << guestBase << " (+0x" << mappedSize
            << ") into the flat reservation" << std::dec << " (rc=0x" << std::hex << rc
            << std::dec << ")";
        throw std::runtime_error(oss.str());
    }
#else
    // MAP_FIXED is safe (and needs no particular kernel version) here specifically because we're
    // deliberately overwriting a sub-range of the PROT_NONE reservation this module already owns
    // exclusively (see EnsureReservation) - unlike the initial reservation itself, there's no
    // "something else might already be there" concern to guard against.
    void* view = mmap(target, static_cast<size_t>(mappedSize), kProtReadWrite,
                      MAP_SHARED | MAP_FIXED, section.fd, static_cast<off_t>(sectionOffset));
    if (view == MAP_FAILED) {
        std::ostringstream oss;
        oss << "Unable to map guest region 0x" << std::hex << guestBase << " (+0x" << mappedSize
            << ") into the flat reservation" << std::dec << " (" << std::strerror(errno) << ")";
        throw std::runtime_error(oss.str());
    }
#endif
}

// Replaces a placeholder with private committed memory. Used for the MMIO
// window (read-only zeros) and for on-demand commits of stray guest pages.
bool CommitPlaceholder(uint8_t* address, uint64_t size, ProtectionFlags protection) {
#if defined(_WIN32)
    SplitPlaceholder(address, size);
    void* result = g_virtualAlloc2(GetCurrentProcess(), address, static_cast<SIZE_T>(size),
                                   MEM_RESERVE | MEM_COMMIT | kMemReplacePlaceholder, protection,
                                   nullptr, 0);
    return result != nullptr;
#elif defined(__SWITCH__)
    if (protection == kProtNone) {
        // The MMIO window: the outer virtmemAddReservation is already inaccessible-by-construction
        // (genuinely unmapped, not just permission-denied) until something explicitly maps into
        // it - there is nothing to "commit" the way a POSIX PROT_NONE reservation (already backed,
        // just protected) needs. A touch here faults as an unmapped access either way.
        return true;
    }
    // The on-demand unmapped-block commit path (HandleAccessViolation case 4): only reached for
    // anomalous wild guest pointers, never the hot path. Private (single-view) memory, so the
    // heap pages are mapped straight at the target as code memory and made writable. The heap
    // block stays locked by the mapping for the process lifetime, like the commit itself.
    const Handle self = envGetOwnProcessHandle();
    void* source = memalign(0x1000, static_cast<size_t>(size) + kSwitchHeapTailSlack);
    if (source == nullptr) return false;
    if (R_FAILED(svcMapProcessCodeMemory(self, reinterpret_cast<u64>(address),
                                         reinterpret_cast<u64>(source), size))) {
        std::free(source);
        return false;
    }
    return R_SUCCEEDED(
        svcSetProcessMemoryPermission(self, reinterpret_cast<u64>(address), size, Perm_Rw));
#else
    // No separate reserve-vs-commit step is needed: the anonymous PROT_NONE reservation this
    // range came from is already demand-zero backed, so mprotect() alone both "commits" and
    // protects it.
    return ProtectRange(address, size, protection);
#endif
}

// One definition of the two windows lives in memory_access.h; these are the
// names the fault handler below reads.
bool IsMmio(uint32_t address) { return MemoryInline::IsMmioAddress(address); }
bool IsGpuFifo(uint32_t address) { return MemoryInline::IsGpuFifoAddress(address); }

void ApplyExecutableProtectionLocked() {
    if (g_base == nullptr) return;
    auto& protectedPages = ExecutableProtectedPages();
    for (const auto& range : ExecutableRanges()) {
        // Only pages fully inside the range are protected: edge pages often share a page with data
        // (MKW's THP buffers do), so guarding them would fault legitimate stores; their writes still go through the checked path.
        const uint64_t first = RoundUp(range.start, kHostPageSize);
        const uint64_t last = static_cast<uint64_t>(range.end) & ~(kHostPageSize - 1u);
        if (last <= first) continue;
        for (uint64_t page = first; page < last; page += kHostPageSize) {
            const uint32_t pageIndex = static_cast<uint32_t>(page >> 12);
            if (protectedPages[pageIndex] != 0) continue;
            if (ProtectRange(g_base + page, kHostPageSize, kProtRead)) {
                protectedPages[pageIndex] = 1;
            }
        }
    }
}

bool SameLayout(const std::vector<RegionRequest>& lhs, const std::vector<RegionRequest>& rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].base != rhs[index].base || lhs[index].size != rhs[index].size ||
            lhs[index].backing != rhs[index].backing) {
            return false;
        }
    }
    return true;
}

uint64_t SectionOffsetFor(const RegionRequest& region) {
    switch (region.backing) {
    case Backing::Mem1:
        return region.base & 0x01FFFFFFu;  // 32 MiB MEM1 window
    case Backing::Mem2:
        return region.base & 0x0FFFFFFFu;  // 256 MiB MEM2 window
    case Backing::Owned:
    default:
        return 0;
    }
}

SectionKey KeyFor(const RegionRequest& region) {
    SectionKey key;
    key.backing = region.backing;
    key.ownedBase = region.backing == Backing::Owned ? region.base : 0;
    return key;
}

void ZeroMappedStorage() {
    for (auto& [key, section] : Sections()) {
        (void)key;
        if (section.hostView != nullptr && section.size != 0) {
            std::memset(section.hostView, 0, static_cast<size_t>(section.size));
        }
    }
}

#if defined(_WIN32)
LONG CALLBACK FlatGuestVectoredHandler(EXCEPTION_POINTERS* info) {
    const auto* record = info->ExceptionRecord;
    if (record == nullptr || record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        record->NumberParameters < 2) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    void* faultAddress = reinterpret_cast<void*>(record->ExceptionInformation[1]);
    const bool isWrite = record->ExceptionInformation[0] != 0;
    if (HandleAccessViolation(faultAddress, isWrite)) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallVectoredHandler() {
    if (g_vectoredHandle != nullptr) return;
    g_vectoredHandle = AddVectoredExceptionHandler(1, FlatGuestVectoredHandler);
    if (g_vectoredHandle == nullptr) {
        throw std::runtime_error(LastErrorText("AddVectoredExceptionHandler"));
    }
}
#endif

void ReportFatalGuestFault(const char* category, uint32_t guestAddress, bool isWrite,
                           const char* detail) {
    RT_LOG(RT_TAG_MEMORY) << "FATAL " << category << std::endl;
    std::cerr << "  guest address: 0x" << std::hex << std::uppercase << std::setw(8)
              << std::setfill('0') << guestAddress << std::dec << std::setfill(' ') << std::endl;
    std::cerr << "  access:        " << (isWrite ? "write" : "read") << std::endl;
    // The fault record carries the faulting address but not the width of the
    // access, so the size is genuinely unavailable on this path. The checked
    // path (runtime/src/memory.cpp) reports the exact length instead.
    std::cerr << "  access size:   unknown (not recoverable from the fault record)" << std::endl;
    std::cerr << "  active func:   0x" << std::hex << std::uppercase
              << RecompMod::CurrentTranslatedExecutionAddress() << std::dec << std::nouppercase
              << std::endl;
    std::cerr << "  detail:        " << detail << std::endl;
    if (auto* cpu = TryGetCpuContext()) {
        std::cerr << "  guest pc:      0x" << std::hex << cpu->pc << " lr=0x" << cpu->lr << std::dec
                  << std::endl;
        SystemBridge::DumpCpuState(cpu);
    }
    std::cerr.flush();
    std::ostringstream message;
    message << "The game stopped because translated code performed a forbidden guest memory "
               "access at 0x"
            << std::hex << std::uppercase << guestAddress << ".\n\n"
            << detail;
    ShowRuntimeFatalPopup(category, message.str());
    std::abort();
}

// Logged once per newly committed 64 KiB block since a silent commit would hide a wild guest pointer bug.
// Called with StateMutex() released so the register dump (which reads guest memory) can't deadlock against it.
void ReportUnmappedCommit(uint32_t guestAddress, uint64_t blockBase, bool isWrite,
                          uint32_t regionOrdinal) {
    RT_LOG(RT_TAG_MEMORY) << "WARNING unmapped guest touch: no mapped region for 0x" << std::hex
              << std::uppercase << std::setw(8) << std::setfill('0') << guestAddress
              << std::setfill(' ') << " (" << (isWrite ? "write" : "read")
              << "); committed zero-filled block 0x" << blockBase << "-0x"
              << (blockBase + kAllocationGranularity) << " active=0x"
              << RecompMod::CurrentTranslatedExecutionAddress();
    if (auto* cpu = TryGetCpuContext()) {
        std::cerr << " pc=0x" << cpu->pc << " lr=0x" << cpu->lr << " r1=0x" << cpu->gpr[1];
    }
    std::cerr << std::dec << std::nouppercase << " region#" << regionOrdinal << std::endl;

    if (regionOrdinal <= kUnmappedCpuDumpLimit) {
        if (auto* cpu = TryGetCpuContext()) {
            RT_LOG(RT_TAG_MEMORY) << "===== DUMPING CPU STATE (unmapped touch) =====" << std::endl;
            SystemBridge::DumpCpuState(cpu);
        }
        if (regionOrdinal == kUnmappedCpuDumpLimit) {
            RT_LOG(RT_TAG_MEMORY) << "further unmapped commits log the one-line record only; the "
                         "shutdown summary reports the totals."
                      << std::endl;
        }
    }
    std::cerr.flush();
}

} // namespace

bool IsActive() {
    return g_initialized;
}

void Initialize(const std::vector<RegionRequest>& regions) {
    std::lock_guard<std::mutex> lock(StateMutex());

    if (g_initialized) {
        if (!SameLayout(g_activeRegions, regions)) {
            throw std::runtime_error(
                "The flat guest address space is mapped once per process; a second Memory::Init "
                "requested a different region layout. Restart the process instead of remapping.");
        }
        // Re-init keeps the mapping and restores the pristine interception
        // state: deferred ranges are gone, executable pages stay protected.
        for (const auto& range : DeferredRanges()) {
            const uint64_t first = static_cast<uint64_t>(range.start) & ~(kHostPageSize - 1u);
            const uint64_t last = RoundUp(range.end, kHostPageSize);
            ProtectRange(g_base + first, last - first, kProtReadWrite);
        }
        DeferredRanges().clear();
        ZeroMappedStorage();
        ApplyExecutableProtectionLocked();
        return;
    }

    EnsureReservation();

    // Size every section from the highest byte any of its regions reaches.
    std::unordered_map<SectionKey, uint64_t, SectionKeyHash> sizes;
    for (const auto& region : regions) {
        if (region.size == 0) continue;
        if ((region.base % kAllocationGranularity) != 0) {
            std::ostringstream oss;
            oss << "Guest region base 0x" << std::hex << region.base
                << " is not 64 KiB aligned; the flat mapping cannot place it.";
            throw std::runtime_error(oss.str());
        }
        const uint64_t end = SectionOffsetFor(region) + region.size;
        auto& current = sizes[KeyFor(region)];
        current = std::max(current, end);
    }

    for (auto& [key, size] : sizes) {
        const uint64_t rounded = RoundUp(size, kAllocationGranularity);
        Section section;
        section.size = rounded;
#if defined(_WIN32)
        section.handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                            static_cast<DWORD>(rounded >> 32),
                                            static_cast<DWORD>(rounded & 0xFFFFFFFFu), nullptr);
        if (section.handle == nullptr) {
            throw std::runtime_error(LastErrorText("CreateFileMapping for guest RAM"));
        }
        section.hostView = static_cast<uint8_t*>(
            MapViewOfFile(section.handle, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(rounded)));
        if (section.hostView == nullptr) {
            throw std::runtime_error(LastErrorText("MapViewOfFile for the host guest-RAM alias"));
        }
#elif defined(__SWITCH__)
        // Same "one backing store, several VA aliases" trick as the other platforms, built from
        // heap-backed code memory (see the file-level comment): the host view is the code-memory
        // mapping itself, and MapGuestView() below adds per-region aliases of it. Every section
        // maps at least once, so the process lives with its heap pages locked for good; the
        // kernel returns them when the process exits.
        const Handle self = envGetOwnProcessHandle();
        section.heapSource = static_cast<uint8_t*>(
            memalign(0x1000, static_cast<size_t>(rounded) + kSwitchHeapTailSlack));
        if (section.heapSource == nullptr) {
            throw std::runtime_error("Out of heap memory for guest RAM");
        }
        // memalign does not guarantee zeroed pages, and the other platforms' sections are
        // demand-zero; the source becomes inaccessible once mapped, so clear it now.
        std::memset(section.heapSource, 0, static_cast<size_t>(rounded));
        virtmemLock();
        void* hostViewAddr = virtmemFindCodeMemory(rounded, 0);
        VirtmemReservation* hostReservation =
            hostViewAddr != nullptr ? virtmemAddReservation(hostViewAddr, rounded) : nullptr;
        virtmemUnlock();
        if (hostReservation == nullptr) {
            throw std::runtime_error(
                "No free address range found for the host guest-RAM alias view");
        }
        {
            Result rc = svcMapProcessCodeMemory(self, reinterpret_cast<u64>(hostViewAddr),
                                                reinterpret_cast<u64>(section.heapSource), rounded);
            if (R_FAILED(rc)) {
                throw std::runtime_error(
                    SwitchErrorText("svcMapProcessCodeMemory for the host guest-RAM alias", rc));
            }
            rc = svcSetProcessMemoryPermission(self, reinterpret_cast<u64>(hostViewAddr), rounded,
                                               Perm_Rw);
            if (R_FAILED(rc)) {
                throw std::runtime_error(
                    SwitchErrorText("making the host guest-RAM alias writable", rc));
            }
        }
        section.hostView = static_cast<uint8_t*>(hostViewAddr);
#else
        // The section is an anonymous shared-memory object: the SAME physical pages get mapped
        // twice below (once here as the always-accessible host view, once per-region as the
        // guest view whose protection the fault handler controls), the same "one backing store,
        // two VA aliases" trick CreateFileMapping/MapViewOfFile(3) gives Windows.
        section.fd = MkwMemfdCreate("wiicompiled-guest-ram", MFD_CLOEXEC);
        if (section.fd < 0) {
            throw std::runtime_error(LastErrorText("memfd_create for guest RAM"));
        }
        if (ftruncate(section.fd, static_cast<off_t>(rounded)) != 0) {
            throw std::runtime_error(LastErrorText("ftruncate for guest RAM"));
        }
        section.hostView = static_cast<uint8_t*>(
            mmap(nullptr, static_cast<size_t>(rounded), kProtReadWrite, MAP_SHARED, section.fd, 0));
        if (section.hostView == MAP_FAILED) {
            section.hostView = nullptr;
            throw std::runtime_error(LastErrorText("mmap for the host guest-RAM alias"));
        }
#endif
        Sections()[key] = section;
    }

    for (const auto& region : regions) {
        if (region.size == 0) continue;
        const auto& section = Sections()[KeyFor(region)];
        const uint64_t offset = SectionOffsetFor(region);
        const uint64_t mappedSize =
            std::min<uint64_t>(RoundUp(region.size, kAllocationGranularity), section.size - offset);
        MapGuestView(section, offset, region.base, mappedSize);
        MappedRegions().push_back(
            MappedRegion{region.base, region.size, mappedSize, offset, section.hostView});
    }

    // MMIO stays inaccessible in both directions so the vectored handler can report missing HLE; the old
    // PAGE_READONLY read window that returned zero turned missing devices into silent hangs instead.
    if (!CommitPlaceholder(g_base + 0xCC000000u, 0x02000000u, kProtNone)) {
        throw std::runtime_error(LastErrorText("committing the no-access MMIO window"));
    }

    ApplyExecutableProtectionLocked();
#if defined(_WIN32)
    InstallVectoredHandler();
#endif

    // Freshly created section objects are demand-zero, so no explicit clear is
    // needed on the first mapping (that would fault in all 152 MiB at startup).
    g_activeRegions = regions;
    g_initialized = true;

    RT_LOG(RT_TAG_MEMORY) << "guest address space reserved at 0x" << std::hex
              << reinterpret_cast<uintptr_t>(g_base) << std::dec << " (" << MappedRegions().size()
              << " regions, " << Sections().size() << " backing stores)" << std::endl;
}

uint8_t* HostPointer(uint32_t guestAddress) {
    if (!g_initialized) return nullptr;
    for (const auto& region : MappedRegions()) {
        if (guestAddress < region.guestBase) continue;
        const uint64_t offset = static_cast<uint64_t>(guestAddress) - region.guestBase;
        if (offset >= region.guestSize) continue;
        return region.hostView + region.sectionOffset + offset;
    }
    return nullptr;
}

void ProtectDeferredRange(uint32_t address, size_t length) {
    if (!g_initialized || length == 0) return;
    const uint64_t end = static_cast<uint64_t>(address) + length;
    if (end > kGuestSpaceSize) return;
    std::lock_guard<std::mutex> lock(StateMutex());
    const uint64_t first = static_cast<uint64_t>(address) & ~(kHostPageSize - 1u);
    const uint64_t last = RoundUp(end, kHostPageSize);
    if (!ProtectRange(g_base + first, last - first, kProtNone)) {
        // An unmapped destination cannot be trapped; the checked path still
        // clears the readable bias, so nothing silently reads stale bytes.
        return;
    }
    DeferredRanges().push_back(GuardedRange{address, static_cast<uint32_t>(end)});
}

void UnprotectDeferredRange(uint32_t address, size_t length) {
    if (!g_initialized || length == 0) return;
    std::lock_guard<std::mutex> lock(StateMutex());
    auto& ranges = DeferredRanges();
    const uint64_t end = static_cast<uint64_t>(address) + length;
    const auto it = std::find_if(ranges.begin(), ranges.end(), [&](const GuardedRange& range) {
        return range.start == address && range.end == static_cast<uint32_t>(end);
    });
    if (it == ranges.end()) return;
    ranges.erase(it);
    const uint64_t first = static_cast<uint64_t>(address) & ~(kHostPageSize - 1u);
    const uint64_t last = RoundUp(end, kHostPageSize);
    ProtectRange(g_base + first, last - first, kProtReadWrite);
}

void RegisterExecutableRange(uint32_t start, uint32_t end) {
    if (end <= start) return;
    std::lock_guard<std::mutex> lock(StateMutex());
    auto& ranges = ExecutableRanges();
    if (std::any_of(ranges.begin(), ranges.end(), [&](const GuardedRange& range) {
            return range.start == start && range.end == end;
        })) {
        return;
    }
    ranges.push_back(GuardedRange{start, end});
    ApplyExecutableProtectionLocked();
}

FaultCounters Counters() {
    FaultCounters counters;
    counters.mmio = g_countMmio.load(std::memory_order_relaxed);
    counters.efb = g_countEfb.load(std::memory_order_relaxed);
    counters.xguard = g_countXGuard.load(std::memory_order_relaxed);
    counters.unmapped = g_countUnmapped.load(std::memory_order_relaxed);
    counters.unmappedRegions = g_countUnmappedRegions.load(std::memory_order_relaxed);
    return counters;
}

#if defined(__SWITCH__)
// Undoes Initialize() so the homebrew loader can start another program after this one. The loader
// reuses this process and its heap; heap pages still locked under a code-memory mapping made it
// abort (kernel result 0xD401) the moment the launcher handed over to RetroRewind.nro. Only call
// this when nothing will touch guest memory again.
void SwitchReleaseForHandoff() noexcept {
    std::lock_guard lock{StateMutex()};
    const Handle self = envGetOwnProcessHandle();
    unsigned failures = 0;
    for (const MappedRegion& region : MappedRegions()) {
        const Result rc = svcUnmapProcessMemory(g_base + region.guestBase, self,
                                                reinterpret_cast<u64>(region.hostView) + region.sectionOffset,
                                                region.mappedSize);
        failures += R_FAILED(rc) ? 1u : 0u;
    }
    MappedRegions().clear();
    for (auto& [key, section] : Sections()) {
        if (section.hostView == nullptr || section.heapSource == nullptr) {
            continue;
        }
        const Result rc = svcUnmapProcessCodeMemory(self, reinterpret_cast<u64>(section.hostView),
                                                    reinterpret_cast<u64>(section.heapSource), section.size);
        if (R_SUCCEEDED(rc)) {
            std::free(section.heapSource);
        } else {
            ++failures;
        }
        section.hostView = nullptr;
        section.heapSource = nullptr;
    }
    Sections().clear();
    g_initialized = false;
    RT_LOGF(RT_TAG_MEMORY, "guest memory released for hand-off (%u failure(s))\n", failures);
}
#endif

void LogFaultSummary() noexcept {
    static std::atomic<bool> reported{false};
    if (reported.exchange(true, std::memory_order_relaxed)) return;
    const FaultCounters counters = Counters();
    if (counters.unmapped == 0) {
        RT_LOG(RT_TAG_MEMORY) << "shutdown summary: no unmapped guest touches (efb="
                  << counters.efb << " xguard=" << counters.xguard << " mmio=" << counters.mmio
                  << ")" << std::endl;
        std::cerr.flush();
        return;
    }
    RT_LOG(RT_TAG_MEMORY) << "WARNING shutdown summary: " << counters.unmapped
              << " unmapped guest touches across " << counters.unmappedRegions
              << " distinct 64 KiB regions were absorbed by on-demand commits. Each one is a "
                 "guest pointer that addressed nothing; search the log for "
                 "'[" RT_TAG_MEMORY "] WARNING unmapped guest touch' for the faulting addresses."
              << std::endl;
    RT_LOG(RT_TAG_MEMORY) << "shutdown summary: efb=" << counters.efb
              << " xguard=" << counters.xguard << " mmio=" << counters.mmio << std::endl;
    std::cerr.flush();
}

bool HandleAccessViolation(void* faultAddress, bool isWrite) noexcept {
    if (!g_initialized || faultAddress == nullptr) return false;

    const uintptr_t fault = reinterpret_cast<uintptr_t>(faultAddress);
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_base);
    if (fault < base || fault - base >= kGuestSpaceSize) return false;

    const uint32_t guestAddress = static_cast<uint32_t>(fault - base);

    // 1) Deferred (EFB) read: materialize the pending copy and drop the trap for the whole 4 KiB page span,
    //    not just the registered range, since protection is page-granular. Leaving a range registered but
    //    unprotected would serve stale bytes without ever faulting again.
    {
        bool covered = false;
        uint32_t rangeStart = 0;
        uint32_t rangeEnd = 0;
        uint64_t spanFirst = 0;
        uint64_t spanLast = 0;
        {
            std::lock_guard<std::mutex> lock(StateMutex());
            auto& ranges = DeferredRanges();
            const auto it = std::find_if(ranges.begin(), ranges.end(), [&](const GuardedRange& r) {
                const uint64_t first = static_cast<uint64_t>(r.start) & ~(kHostPageSize - 1u);
                const uint64_t last = RoundUp(r.end, kHostPageSize);
                return guestAddress >= first && guestAddress < last;
            });
            if (it != ranges.end()) {
                covered = true;
                rangeStart = it->start;
                rangeEnd = it->end;
                ranges.erase(it);
                spanFirst = static_cast<uint64_t>(rangeStart) & ~(kHostPageSize - 1u);
                spanLast = RoundUp(rangeEnd, kHostPageSize);
                ProtectRange(g_base + spanFirst, spanLast - spanFirst, kProtReadWrite);
            }
        }
        if (covered) {
            g_countEfb.fetch_add(1, std::memory_order_relaxed);
            try {
                MemoryInline::ResolveDeferredReads(
                    static_cast<uint32_t>(spanFirst), static_cast<size_t>(spanLast - spanFirst));
            } catch (const std::exception& error) {
                ReportFatalGuestFault("deferred read materialization failed", guestAddress, isWrite,
                                      error.what());
            }
            return true;
        }
    }

    // 2) Executable-write guard. Only writes trap (the pages are PAGE_READONLY),
    //    so a fault here is exactly the event the guard exists to report.
    {
        const uint32_t pageIndex = guestAddress >> 12;
        bool guarded = false;
        {
            std::lock_guard<std::mutex> lock(StateMutex());
            guarded = ExecutableProtectedPages()[pageIndex] != 0;
        }
        if (guarded) {
            g_countXGuard.fetch_add(1, std::memory_order_relaxed);
            // Aborts inside CheckExecutableWrite for an unsupported patch; the
            // width and value are not recoverable from the fault record, so the
            // report carries the exact address instead.
            if (!RecompMod::HandleExecutableWrite(guestAddress, 1, 0)) {
                // A permitted write (the REL loader relocating its own text).
                // Those arrive in bulk, so the page is opened permanently
                // rather than trapping every relocation.
                std::lock_guard<std::mutex> lock(StateMutex());
                if (ProtectRange(g_base + (static_cast<uint64_t>(pageIndex) << 12), kHostPageSize,
                                 kProtReadWrite)) {
                    ExecutableProtectedPages()[pageIndex] = 0;
                }
            }
            return true;
        }
    }

    // 3) MMIO. PAGE_NOACCESS makes both directions faults: a write has no backing device (including a GPU
    //    FIFO store the translator failed to lower), and a read would have to invent a register value;
    //    answering zero would turn a missing device into a silent hang, so both are reported instead.
    if (IsMmio(guestAddress)) {
        g_countMmio.fetch_add(1, std::memory_order_relaxed);
        if (IsGpuFifo(guestAddress)) {
            if (isWrite) {
                ReportFatalGuestFault(
                    "GPU FIFO write reached the flat memory path", guestAddress, isWrite,
                    "Gather-pipe stores must be lowered to GX_HLE_FIFO_Write*; the written value "
                    "cannot be recovered from a fault. Fix the translator lowering for this "
                    "store.");
            }
            ReportFatalGuestFault(
                "GPU FIFO read blocked", guestAddress, isWrite,
                "The gather pipe is write-only; nothing can be read back from it. The guest code "
                "that issued this load needs GX HLE, not a memory access.");
        }
        if (isWrite) {
            ReportFatalGuestFault("MMIO write blocked (non-GPU)", guestAddress, isWrite,
                                  "Hardware registers have no backing store. Add HLE for this "
                                  "device instead of letting the write land.");
        }
        ReportFatalGuestFault("MMIO read blocked (non-GPU)", guestAddress, isWrite,
                              "Hardware registers have no backing store. Answering zero would "
                              "hang the caller in a status poll instead of reporting the gap; "
                              "add HLE for this device.");
        return true;
    }

    // 4) Unmapped address: commit the block on demand (reproducing the old zero-fill sparse-map behavior)
    //    and report it, since a commit here means a wild guest pointer that would otherwise walk over silently.
    const uint64_t blockBase = static_cast<uint64_t>(guestAddress) & ~(kAllocationGranularity - 1u);
    g_countUnmapped.fetch_add(1, std::memory_order_relaxed);
    bool committed = false;
    {
        std::lock_guard<std::mutex> lock(StateMutex());
#if defined(_WIN32)
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(g_base + blockBase, &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.State == MEM_COMMIT) {
            // Another thread already committed this block: re-running the
            // access succeeds. Any other committed-but-inaccessible state is
            // not ours to fix - resuming would fault forever, so hand the
            // exception to the crash reporter instead.
            const bool writable = (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                                                  PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
            const bool readable = writable || (mbi.Protect & (PAGE_READONLY | PAGE_EXECUTE_READ |
                                                              PAGE_EXECUTE)) != 0;
            return isWrite ? writable : readable;
        }
#else
        // mprotect has no VirtualQuery counterpart to ask "is this block already committed and
        // how", so this module tracks the same fact itself (UnmappedCommittedBlocks, checked and
        // set under this same lock): once a block has been committed READ|WRITE by an earlier
        // call here (this thread's or a racing one's), every subsequent fault on it is a no-op
        // resume - there is no POSIX equivalent of "committed but insufficiently permissioned"
        // for a block only this function ever touches.
        const uint32_t blockIndex = static_cast<uint32_t>(blockBase / kAllocationGranularity);
        if (UnmappedCommittedBlocks()[blockIndex] != 0) {
            return true;
        }
#endif
        if (!CommitPlaceholder(g_base + blockBase, kAllocationGranularity, kProtReadWrite)) {
            return false;
        }
#if !defined(_WIN32)
        UnmappedCommittedBlocks()[blockIndex] = 1;
#endif
        committed = true;
    }
    if (committed) {
        const uint32_t ordinal = g_countUnmappedRegions.fetch_add(1, std::memory_order_relaxed) + 1u;
        ReportUnmappedCommit(guestAddress, blockBase, isWrite, ordinal);
    }
    return true;
}

} // namespace GuestFlat
