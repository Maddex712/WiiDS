#if defined(__SWITCH__)
#include "switch_bundled_files.h"
#include "switch_launcher.h"
namespace GuestFlat {
void SwitchReleaseForHandoff() noexcept;
}
#endif
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <utility>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <array>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <crtdbg.h>
#include <windows.h>
#include <mmsystem.h>
#include <dbghelp.h>
#elif defined(__SWITCH__)
// Horizon's newlib has no <ucontext.h> and no <sched.h> affinity API, and libnx reports faults
// through its own exception handler rather than POSIX signals - see the __SWITCH__ branches
// further down.
#include <cerrno>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <switch.h>
#include <malloc.h>
#include <unwind.h>
#else
#include <cerrno>
#include <sched.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#endif

#include "abi_bridge.h"
#include "android_gpu_driver.h"
#if defined(__ANDROID__) && defined(MKW_PGO_GENERATE)
void StartAndroidPgoRecording();
#endif
#include "guest_flat_memory.h"
#include "gx_guest_write.h"
#include "hle/audio/ax_dsp.h"
#include "memory.h"
#include "system_bridge.h"
#include "ppc_runtime.h"
#include "aurora_events.h"
#include "fiber_manager.h"
#include "hle_stubs.h"
#include "runtime_config.h"
#include "runtime_log.h"
#include "runtime_product.h"
#include "recomp_mod_loader.h"
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

#include <tracy/Tracy.hpp>

// Defined in `runtime/src/hle/vi.cpp` (used by GX/VI HLE).
extern std::atomic_bool g_auroraFrameActive;
extern "C" int g_gxFrameCount;
extern "C" const char* DVDResolveHostPathForTest(const char* dvdPath);
bool OS_HLE_InterruptsEnabled() noexcept;

#if defined(__SWITCH__)
// hbloader runs every homebrew .nro inside its own process, so an unhandled CPU fault produces
// no Atmosphere crash report naming this module - libnx hands it to __libnx_exception_handler
// instead, and whatever that writes is the only record of the crash. stdout/stderr are already
// pointed at the SD card by RuntimeMain, so plain stdio is the right sink here.
//
// Every address is printed module-relative ("+0x..."), because the NRO is loaded at a different
// randomised base on every launch; `aarch64-none-elf-addr2line -e WiiCompiled.elf <offset>`
// resolves these directly against the unstripped ELF next to the .nro.
void SwitchCrashWrite(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fflush(stderr);
}

// `__start__` is an absolute symbol that stays 0 at runtime, so it cannot give the load base of a
// position-independent NRO (a first attempt using it printed raw 0xea... addresses). Ask the
// kernel instead: the code region that contains one of our own functions starts at the module
// base, which is exactly what addr2line needs subtracted.
u64 SwitchModuleBase() {
    static const u64 base = [] {
        MemoryInfo info{};
        u32 pageInfo = 0;
        if (R_FAILED(svcQueryMemory(&info, &pageInfo, reinterpret_cast<u64>(&SwitchModuleBase)))) {
            return u64{0};
        }
        return info.addr;
    }();
    return base;
}

u64 SwitchModuleOffset(u64 address) {
    const u64 base = SwitchModuleBase();
    return address >= base ? address - base : address;
}

// runtime/CMakeLists.txt links with `-Wl,--wrap=__syscall_thread_create`, which redirects every
// newlib/libnx thread creation here. libnx defaults a std::thread to a 128 KiB stack, which Tint's
// resolver overflows on aurora's pipeline compilation workers; this raises the floor to 2 MiB for
// every thread the C++ runtime spawns.
//
// Only when the caller let libnx allocate the stack (stack_addr == nullptr). If a caller supplied
// its own buffer, growing the size it declares would run the thread off the end of that buffer.
//
// This must exist whenever that link option does: with --wrap and no __wrap_ symbol, thread
// creation resolves to nothing and every std::thread constructor fails with ENOSYS, which is
// exactly what killed gfx::initialize() after this file's Switch code was lost on 2026-09-17.
extern "C" int __real___syscall_thread_create(void** thread, void* entry, void* arg, void* stackAddr,
                                              size_t stackSize);

// The core the guest/emulation thread runs on, recorded by RuntimeMain. -1 until then, which
// leaves libnx's own placement alone for anything created before that point.
std::atomic<int> g_switchGuestCore{-1};
std::atomic<uint32_t> g_switchHelperCoreCursor{0};

// libnx creates every thread with ideal core -2 (the process default) and the full process core
// mask, so the GX worker, frame worker, pipeline worker and audio mixer all prefer the same core
// as the guest thread. Offloading work to a thread that contends for the guest's own core buys
// nothing - measured on 2026-09-20, threaded GX gave 13.1 vs 12.75 FPS with this missing.
//
// So give each helper an ideal core drawn round-robin from the cores the guest is NOT on. The
// mask stays every non-guest core rather than a single one: that biases placement without hard
// pinning, so a helper can still migrate instead of stalling behind a busy core.
void SwitchSpreadHelperThread(u32 handle) noexcept {
    const int guestCore = g_switchGuestCore.load(std::memory_order_relaxed);
    if (guestCore < 0) {
        return;
    }
    u64 processMask = 0;
    if (R_FAILED(svcGetInfo(&processMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) {
        return;
    }
    const u64 helperMask = processMask & ~(1ull << static_cast<unsigned>(guestCore));
    if (helperMask == 0) {
        return;  // single-core budget: nothing to spread onto
    }
    // Round-robin over the set bits of helperMask.
    const uint32_t slot = g_switchHelperCoreCursor.fetch_add(1, std::memory_order_relaxed);
    const int available = __builtin_popcountll(helperMask);
    int target = static_cast<int>(slot % static_cast<uint32_t>(available));
    int ideal = -1;
    for (int core = 0; core < 64; ++core) {
        if ((helperMask & (1ull << core)) == 0) {
            continue;
        }
        if (target-- == 0) {
            ideal = core;
            break;
        }
    }
    if (ideal >= 0) {
        svcSetThreadCoreMask(handle, ideal, helperMask);
    }
}

// Called by aurora's GX worker when it starts (see aurora-main/lib/gx/fifo.cpp). The worker is on
// the game thread's critical path - every sync point waits for it - but as an ordinary std::thread
// it ran at libnx's time-sliced priority 0x3B and could share a core with the frame worker or the
// audio mixer. Pin it to the first non-guest core at the guest thread's own priority; the other
// helpers keep both non-guest cores in their masks, so they can move off it when it is busy.
// Recorded for the profiler's window files, which are the one log reliably read back from the card.
std::atomic<int> g_switchGxWorkerCore{-1};
std::atomic<int> g_switchAudioMixCore{-1};
// The two helper threads the profiler also samples (0 = GX worker, 1 = AX mix worker).
std::atomic<Handle> g_switchHelperThreadHandles[2]{INVALID_HANDLE, INVALID_HANDLE};

extern "C" void SwitchConfigureGxWorkerThread() {
    g_switchHelperThreadHandles[0].store(threadGetCurHandle(), std::memory_order_relaxed);
    const int guestCore = g_switchGuestCore.load(std::memory_order_relaxed);
    u64 processMask = 0;
    if (guestCore < 0 || R_FAILED(svcGetInfo(&processMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) {
        return;
    }
    const u64 helperMask = processMask & ~(1ull << static_cast<unsigned>(guestCore));
    if (helperMask == 0) {
        return;
    }
    const int core = __builtin_ctzll(helperMask);
    const Result maskRc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1ull << core);
    const Result prioRc = svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2C);
    if (R_SUCCEEDED(maskRc) && R_SUCCEEDED(prioRc)) {
        g_switchGxWorkerCore.store(core, std::memory_order_relaxed);
    }
    RT_LOGF(RT_TAG_RUNTIME, "GX worker pinned to core %d (rc 0x%x), priority 0x2C (rc 0x%x)\n", core,
            static_cast<unsigned>(maskRc), static_cast<unsigned>(prioRc));
}

// Called by the AX mix worker when it starts (runtime/src/hle/audio/ax_mix.cpp). The guest thread
// joins it every audio block (~2.5% of a race frame was that join), so it prefers the non-guest
// core the GX worker is NOT pinned to. Its mask keeps every non-guest core so it can still run
// elsewhere when that core is busy.
extern "C" void SwitchConfigureAudioMixThread() {
    g_switchHelperThreadHandles[1].store(threadGetCurHandle(), std::memory_order_relaxed);
    const int guestCore = g_switchGuestCore.load(std::memory_order_relaxed);
    u64 processMask = 0;
    if (guestCore < 0 || R_FAILED(svcGetInfo(&processMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) {
        return;
    }
    const u64 helperMask = processMask & ~(1ull << static_cast<unsigned>(guestCore));
    if (helperMask == 0) {
        return;
    }
    const int core = 63 - __builtin_clzll(helperMask);  // highest helper core; GX takes the lowest
    // 0x2D, below the GX worker. Safe only because SDL's audio output thread is raised above both
    // (SwitchRaiseAudioOutputThread): without that, raising this worker starved the output thread
    // and audout's buffer queue ran dry (audible stutter).
    const Result maskRc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, helperMask);
    const Result prioRc = svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2D);
    if (R_SUCCEEDED(maskRc) && R_SUCCEEDED(prioRc)) {
        g_switchAudioMixCore.store(core, std::memory_order_relaxed);
    }
    RT_LOGF(RT_TAG_RUNTIME, "AX mix worker prefers core %d (rc 0x%x), priority 0x2D (rc 0x%x)\n", core,
            static_cast<unsigned>(maskRc), static_cast<unsigned>(prioRc));
}

// Called once on SDL's audio device thread (audio_backend.cpp postmix callback). Highest of the
// helper priorities: it only copies a mixed buffer to audout, but it must never miss its turn.
std::atomic<int> g_switchAudioOutputPriorityRc{-1};
extern "C" void SwitchRaiseAudioOutputThread() {
    const Result rc = svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2B);
    g_switchAudioOutputPriorityRc.store(static_cast<int>(rc), std::memory_order_relaxed);
}

extern "C" int __wrap___syscall_thread_create(void** thread, void* entry, void* arg, void* stackAddr,
                                              size_t stackSize) {
    constexpr size_t kMinimumThreadStack = 2u * 1024u * 1024u;
    if (stackAddr == nullptr && stackSize < kMinimumThreadStack) {
        stackSize = kMinimumThreadStack;  // 4 KiB aligned, as libnx requires
    }
    const int result = __real___syscall_thread_create(thread, entry, arg, stackAddr, stackSize);
    if (result == 0 && thread != nullptr && *thread != nullptr) {
        // libnx stores the thread Handle as the first word of the object it hands back - confirmed
        // by disassembling __syscall_thread_create, which does `ldr w0,[x21]` and feeds exactly
        // that to svcSetThreadCoreMask a few instructions later.
        SwitchSpreadHelperThread(*static_cast<const u32*>(*thread));
    }
    return result;
}

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    SwitchCrashWrite("[switch-crash] exception desc=0x%x pc=+0x%llx lr=+0x%llx sp=0x%llx far=0x%llx\n",
                     ctx->error_desc, SwitchModuleOffset(ctx->pc.x), SwitchModuleOffset(ctx->lr.x), ctx->sp.x,
                     ctx->far.x);
    for (int reg = 0; reg < 29; reg += 4) {
        SwitchCrashWrite("[switch-crash] x%02d %016llx %016llx %016llx %016llx\n", reg, ctx->cpu_gprs[reg].x,
                         reg + 1 < 29 ? ctx->cpu_gprs[reg + 1].x : 0, reg + 2 < 29 ? ctx->cpu_gprs[reg + 2].x : 0,
                         reg + 3 < 29 ? ctx->cpu_gprs[reg + 3].x : 0);
    }
    // Frame-pointer walk. Guest fibers run on libco stacks that the compiler's frame chain still
    // threads correctly, so this reaches back through the runtime even from translated code; the
    // bounds check just stops a corrupted frame from looping forever.
    u64 fp = ctx->fp.x;
    for (int frame = 0; frame < 32 && fp != 0 && (fp & 0xF) == 0; ++frame) {
        const auto* entry = reinterpret_cast<const u64*>(fp);
        const u64 next = entry[0];
        const u64 lr = entry[1];
        if (lr == 0) {
            break;
        }
        SwitchCrashWrite("[switch-crash] #%02d +0x%llx\n", frame, SwitchModuleOffset(lr));
        if (next <= fp) {
            break;  // frame pointers must grow towards the stack base
        }
        fp = next;
    }
    SwitchCrashWrite("[switch-crash] end\n");
    // hbloader unmaps this module's code while the crashing thread's siblings are still live, so
    // returning (or exiting normally) faults again inside the unmapped text. Leave immediately.
    svcExitProcess();
}

// Guest-code sampling profiler. Horizon has no perf/simpleperf and the Atmosphere GDB stub is far
// too slow to sample at any useful rate, so sample in-process: the Switch build keeps the current
// translated guest PC in a plain global (recomp_mod_loader.h), and the generated symbol table is
// already linked in for crash reports. A thread reads that PC, folds it to its enclosing guest
// function and histograms it.
//
// Enabled only when `sdmc:/switch/WiiCompiled/profile.flag` exists, so a normal run pays nothing.
extern "C" {
extern const uint32_t kGuestMapSymbolCount;
extern const uint32_t kGuestMapSymbolAddresses[];
extern const char* const kGuestMapSymbolNames[];
}

constexpr size_t kProfileSlots = 8192;  // power of two, open addressed, never resized
struct ProfileSlot {
    uint32_t symbol;
    uint64_t hits;
};
ProfileSlot g_profileSlots[kProfileSlots]{};
uint64_t g_profileSamples = 0;
uint64_t g_profileOutsideGuest = 0;

// Floor lookup over the sorted guest symbol table: folds a PC to the start of the function that
// contains it, so samples aggregate per function rather than per instruction.
uint32_t ProfileSymbolFor(uint32_t address) {
    uint32_t lo = 0;
    uint32_t hi = kGuestMapSymbolCount;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (kGuestMapSymbolAddresses[mid] <= address) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0 || address - kGuestMapSymbolAddresses[lo - 1] >= 0x10000u) {
        return 0;
    }
    return kGuestMapSymbolAddresses[lo - 1];
}

const char* ProfileNameFor(uint32_t symbol) {
    uint32_t lo = 0;
    uint32_t hi = kGuestMapSymbolCount;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (kGuestMapSymbolAddresses[mid] < symbol) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return (lo < kGuestMapSymbolCount && kGuestMapSymbolAddresses[lo] == symbol) ? kGuestMapSymbolNames[lo] : "?";
}

void ProfileRecord(uint32_t symbol) {
    size_t slot = (symbol * 2654435761u) & (kProfileSlots - 1);
    for (size_t probe = 0; probe < kProfileSlots; ++probe) {
        ProfileSlot& entry = g_profileSlots[slot];
        if (entry.hits == 0) {
            entry.symbol = symbol;
            entry.hits = 1;
            return;
        }
        if (entry.symbol == symbol) {
            ++entry.hits;
            return;
        }
        slot = (slot + 1) & (kProfileSlots - 1);
    }
}

void ProfileWriteReport() {
    std::vector<ProfileSlot> ranked;
    ranked.reserve(256);
    for (const auto& entry : g_profileSlots) {
        if (entry.hits != 0) {
            ranked.push_back(entry);
        }
    }
    std::sort(ranked.begin(), ranked.end(), [](const ProfileSlot& a, const ProfileSlot& b) { return a.hits > b.hits; });

    // Written fresh and closed each time: a file held open reports size 0 over the SD card.
    FILE* out = std::fopen("sdmc:/switch/WiiCompiled/profile.txt", "w");
    if (out == nullptr) {
        return;
    }
    const uint64_t total = g_profileSamples;
    std::fprintf(out, "samples=%llu outside_guest=%llu (%.1f%%) distinct_functions=%zu\n",
                 static_cast<unsigned long long>(total),
                 static_cast<unsigned long long>(g_profileOutsideGuest),
                 total ? 100.0 * static_cast<double>(g_profileOutsideGuest) / static_cast<double>(total) : 0.0,
                 ranked.size());
    const size_t shown = std::min<size_t>(ranked.size(), 50);
    for (size_t i = 0; i < shown; ++i) {
        std::fprintf(out, "%6.2f%%  %8llu  0x%08X  %s\n",
                     total ? 100.0 * static_cast<double>(ranked[i].hits) / static_cast<double>(total) : 0.0,
                     static_cast<unsigned long long>(ranked[i].hits), ranked[i].symbol,
                     ProfileNameFor(ranked[i].symbol));
    }
    std::fclose(out);
}

// Host-PC sampling, alongside the guest-PC histogram above.
//
// The guest PC global is only written at *indirect* dispatch boundaries - the generated-to-
// generated direct-call fast path in DispatchKnownTranslatedCpuTargetStatic deliberately skips it
// to save a store. So profile.txt is inclusive call-subtree time rooted at each indirect dispatch,
// not self time, and it silently bills every native HLE/aurora call to its guest caller
// (outside_guest is therefore always 0.0%). Reading it as self time is how a 16-instruction
// function like nw4r::ef::DrawBillboardStrategy::Draw appeared to cost 3 ms a frame.
//
// Sampling the guest thread's real ARM64 PC fixes both problems at once and needs no change to the
// 43 MB of generated code: svcGetThreadContext3 on the guest thread gives the true PC, module-
// relative addresses are symbolised offline against the ELF, and host frames simply resolve to
// host symbols. Raw offsets are histogrammed here - no symbol table is consulted on-console.
// Power of two, open addressed, never resized. At 16384 slots and 4-byte PC granularity the table
// saturated partway through a race and silently dropped every newly-seen PC after that - one
// 105 s window lost ~45% of its samples. 64K slots plus 64-byte bucketing (16 instructions, well
// under a typical function) gives ~16x the headroom for the same fidelity at function level.
constexpr size_t kHostProfileSlots = 65536;
constexpr uint64_t kHostProfileBucketMask = ~uint64_t{63};
struct HostProfileSlot {
    uint64_t offset;  // module-relative PC, bucketed to 4 bytes
    uint64_t hits;
};
HostProfileSlot g_hostProfileSlots[kHostProfileSlots]{};
uint64_t g_hostProfileSamples = 0;
uint64_t g_hostProfileFailures = 0;
uint64_t g_hostProfileOffModule = 0;
uint64_t g_hostProfileDropped = 0;
Result g_hostProfileLastError = 0;
Handle g_profiledThreadHandle = INVALID_HANDLE;

void HostProfileRecord(uint64_t offset) {
    size_t slot = static_cast<size_t>((offset * 1099511628211ull) >> 20) & (kHostProfileSlots - 1);
    for (size_t probe = 0; probe < kHostProfileSlots; ++probe) {
        HostProfileSlot& entry = g_hostProfileSlots[slot];
        if (entry.hits == 0) {
            entry.offset = offset;
            entry.hits = 1;
            return;
        }
        if (entry.offset == offset) {
            ++entry.hits;
            return;
        }
        slot = (slot + 1) & (kHostProfileSlots - 1);
    }
    ++g_hostProfileDropped;  // table full: this PC is not counted at all, so report it
}

// (PC bucket, call chain) for the current window only, so a leaf like memmove or a libnx wait SVC
// can be attributed to whoever is really waiting. frames[0] is LR; the rest come from walking the
// saved frame-pointer chain while the thread is paused. Cleared after each window is written.
constexpr size_t kHostStackSlots = 32768;
constexpr int kHostStackFrames = 6;
struct HostStackSlot {
    uint64_t pc;
    uint64_t frames[kHostStackFrames];
    uint64_t hits;
};
HostStackSlot g_hostStackSlots[kHostStackSlots]{};

void HostStackRecord(uint64_t pc, const uint64_t (&frames)[kHostStackFrames]) {
    uint64_t hash = pc * 1099511628211ull;
    for (uint64_t frame : frames) {
        hash = (hash ^ frame) * 1099511628211ull;
    }
    size_t slot = static_cast<size_t>(hash >> 20) & (kHostStackSlots - 1);
    for (size_t probe = 0; probe < 64; ++probe) {
        HostStackSlot& entry = g_hostStackSlots[slot];
        if (entry.hits == 0) {
            entry.pc = pc;
            std::memcpy(entry.frames, frames, sizeof(frames));
            entry.hits = 1;
            return;
        }
        if (entry.pc == pc && std::memcmp(entry.frames, frames, sizeof(frames)) == 0) {
            ++entry.hits;
            return;
        }
        slot = (slot + 1) & (kHostStackSlots - 1);
    }
}

// Walks the paused thread's frame-pointer chain. Every read is bounds-checked against the one
// mapping that holds the stack pointer (the thread stack, or the heap block of a guest fiber
// stack), and frames must move strictly upward, so a garbage FP ends the walk instead of faulting.
void WalkPausedStack(const ThreadContext& ctx, u64 base, uint64_t (&frames)[kHostStackFrames]) {
    auto rel = [base](u64 addr) -> uint64_t { return addr >= base ? addr - base : 0; };
    frames[0] = rel(ctx.lr);
    for (int i = 1; i < kHostStackFrames; ++i) {
        frames[i] = 0;
    }
    MemoryInfo info{};
    u32 pageInfo = 0;
    if (R_FAILED(svcQueryMemory(&info, &pageInfo, ctx.sp)) || (info.perm & Perm_R) == 0) {
        return;
    }
    const u64 lo = info.addr;
    const u64 hi = info.addr + info.size;
    u64 fp = ctx.fp;
    for (int i = 1; i < kHostStackFrames; ++i) {
        if (fp < lo || fp + 16 > hi || (fp & 7) != 0) {
            return;
        }
        const u64* record = reinterpret_cast<const u64*>(fp);
        const u64 nextFp = record[0];
        frames[i] = rel(record[1]);
        if (nextFp <= fp) {
            return;
        }
        fp = nextFp;
    }
}

void HostProfileWriteReport() {
    std::vector<HostProfileSlot> ranked;
    ranked.reserve(4096);
    for (const auto& entry : g_hostProfileSlots) {
        if (entry.hits != 0) {
            ranked.push_back(entry);
        }
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const HostProfileSlot& a, const HostProfileSlot& b) { return a.hits > b.hits; });

    FILE* out = std::fopen("sdmc:/switch/WiiCompiled/profile_host.txt", "w");
    if (out == nullptr) {
        return;
    }
    // module_base is recorded so a run can still be symbolised if the raw PCs are ever dumped;
    // every offset below is already module-relative and feeds aarch64-none-elf-addr2line directly.
    std::fprintf(out,
                 "host_samples=%llu failures=%llu (last rc 0x%x) off_module=%llu dropped_table_full=%llu distinct_pcs=%zu module_base=0x%llx\n",
                 static_cast<unsigned long long>(g_hostProfileSamples),
                 static_cast<unsigned long long>(g_hostProfileFailures),
                 static_cast<unsigned int>(g_hostProfileLastError),
                 static_cast<unsigned long long>(g_hostProfileOffModule),
                 static_cast<unsigned long long>(g_hostProfileDropped), ranked.size(),
                 static_cast<unsigned long long>(SwitchModuleBase()));
    const size_t shown = std::min<size_t>(ranked.size(), 4000);
    for (size_t i = 0; i < shown; ++i) {
        std::fprintf(out, "%8llu 0x%llx\n", static_cast<unsigned long long>(ranked[i].hits),
                     static_cast<unsigned long long>(ranked[i].offset));
    }
    std::fclose(out);
}

// The reports above are cumulative since launch, so a session that spent ten minutes in movie
// menus drowns out the race that followed. Each ~15 s window is also written on its own - the
// hits since the previous window - next to the frames presented in it, so one race yields a clean
// race-only profile with its frame rate attached. Windows cycle through kHostProfileWindowFiles
// files; index.txt lists every window in order.
// Defined in hle/gx/gx_dl.cpp; guest-thread-only counters, read here without a lock because a torn
// read only skews one diagnostic window.
struct DlScanCacheCounters {
    uint64_t calls = 0;
    uint64_t hits = 0;
    uint64_t digests = 0;
    uint64_t scans = 0;
    uint64_t clears = 0;
};
extern DlScanCacheCounters g_dlScanCacheCounters;

constexpr unsigned kHostProfileWindowFiles = 80;

// The GX worker and the AX mix worker, sampled at a quarter of the guest thread's rate: where a
// helper thread spends its time decides how long the guest waits for it. Only the sampler thread
// touches these tables. Key = (PC bucket, LR bucket), so a leaf like memcpy keeps its caller.
struct HelperProfile {
    const char* tag;
    std::unordered_map<uint64_t, uint64_t> hits;  // (pc >> 6) << 32 | (lr >> 6)
    uint64_t samples = 0;
    uint64_t failures = 0;
};
HelperProfile g_helperProfiles[2] = {{"gx", {}, 0, 0}, {"mix", {}, 0, 0}};

void HelperProfileSample(int index) {
    const Handle handle = g_switchHelperThreadHandles[index].load(std::memory_order_relaxed);
    const u64 base = SwitchModuleBase();
    if (handle == INVALID_HANDLE || base == 0) {
        return;
    }
    HelperProfile& profile = g_helperProfiles[index];
    ThreadContext ctx{};
    ++profile.samples;
    const Result pauseRc = svcSetThreadActivity(handle, ThreadActivity_Paused);
    const Result ctxRc = R_SUCCEEDED(pauseRc) ? svcGetThreadContext3(&ctx, handle) : pauseRc;
    if (R_SUCCEEDED(pauseRc)) {
        svcSetThreadActivity(handle, ThreadActivity_Runnable);
    }
    if (R_FAILED(ctxRc) || ctx.pc.x < base) {
        ++profile.failures;
        return;
    }
    const uint64_t pc = (ctx.pc.x - base) >> 6;
    const uint64_t lr = ctx.lr >= base ? (ctx.lr - base) >> 6 : 0;
    ++profile.hits[(pc << 32) | (lr & 0xFFFFFFFFull)];
}

void HelperProfileWriteWindows(unsigned window) {
    for (HelperProfile& profile : g_helperProfiles) {
        char path[96];
        std::snprintf(path, sizeof(path), "sdmc:/switch/WiiCompiled/profile_windows/w%02u_%s.txt",
                      window % kHostProfileWindowFiles, profile.tag);
        if (FILE* out = std::fopen(path, "w")) {
            std::fprintf(out, "helper=%s window=%u samples=%llu failures=%llu\n", profile.tag, window,
                         static_cast<unsigned long long>(profile.samples),
                         static_cast<unsigned long long>(profile.failures));
            for (const auto& [key, count] : profile.hits) {
                std::fprintf(out, "%llu 0x%llx 0x%llx\n", static_cast<unsigned long long>(count),
                             static_cast<unsigned long long>((key >> 32) << 6),
                             static_cast<unsigned long long>((key & 0xFFFFFFFFull) << 6));
            }
            std::fclose(out);
        }
        profile.hits.clear();
        profile.samples = 0;
        profile.failures = 0;
    }
}
uint64_t g_hostProfilePrevHits[kHostProfileSlots]{};
uint64_t g_hostProfilePrevSamples = 0;
uint64_t g_hostProfilePrevPresents = 0;
unsigned g_hostProfileWindow = 0;

void HostProfileWriteWindow(double seconds) {
    AuroraPresentTiming timing{};
    aurora_get_present_timing(&timing);
    const uint64_t presents = timing.totalPresentCount - g_hostProfilePrevPresents;
    g_hostProfilePrevPresents = timing.totalPresentCount;
    const uint64_t samples = g_hostProfileSamples - g_hostProfilePrevSamples;
    g_hostProfilePrevSamples = g_hostProfileSamples;

    std::vector<HostProfileSlot> ranked;
    ranked.reserve(4096);
    for (size_t i = 0; i < kHostProfileSlots; ++i) {
        const HostProfileSlot& entry = g_hostProfileSlots[i];
        const uint64_t delta = entry.hits - g_hostProfilePrevHits[i];
        g_hostProfilePrevHits[i] = entry.hits;
        if (delta != 0) {
            ranked.push_back({entry.offset, delta});
        }
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const HostProfileSlot& a, const HostProfileSlot& b) { return a.hits > b.hits; });

    const unsigned window = g_hostProfileWindow++;
    const double fps = seconds > 0.0 ? static_cast<double>(presents) / seconds : 0.0;
    mkdir("sdmc:/switch/WiiCompiled/profile_windows", 0777);
    HelperProfileWriteWindows(window);
    char path[96];
    std::snprintf(path, sizeof(path), "sdmc:/switch/WiiCompiled/profile_windows/w%02u.txt",
                  window % kHostProfileWindowFiles);
    if (FILE* out = std::fopen(path, "w")) {
        std::fprintf(out,
                     "host_samples=%llu window=%u seconds=%.1f presents=%llu fps=%.2f module_base=0x%llx\n",
                     static_cast<unsigned long long>(samples), window, seconds,
                     static_cast<unsigned long long>(presents), fps,
                     static_cast<unsigned long long>(SwitchModuleBase()));
        // Game-thread waits on the threaded-GX worker in this window, per calling site. Lines start
        // with '#' so tools reading the PC histogram below can skip them.
        {
            static std::unordered_map<u64, std::pair<u64, u64>> previousSites;
            static u64 previousWaits = 0;
            static u64 previousNanos = 0;
            AuroraGxSyncSite sites[64];
            u64 waits = 0;
            u64 nanos = 0;
            const u32 count = AuroraGetGxSyncStats(&waits, &nanos, sites, 64);
            std::fprintf(out, "# threads guest_core=%d gx_worker_core=%d audio_mix_core=%d audio_out_prio_rc=%d\n",
                         g_switchGuestCore.load(std::memory_order_relaxed),
                         g_switchGxWorkerCore.load(std::memory_order_relaxed),
                         g_switchAudioMixCore.load(std::memory_order_relaxed),
                         g_switchAudioOutputPriorityRc.load(std::memory_order_relaxed));
            std::fprintf(out, "# gx_sync waits=%llu ms=%.2f\n",
                         static_cast<unsigned long long>(waits - previousWaits),
                         static_cast<double>(nanos - previousNanos) / 1e6);
            previousWaits = waits;
            previousNanos = nanos;
            const u64 base = SwitchModuleBase();
            for (u32 i = 0; i < count; ++i) {
                auto& previous = previousSites[sites[i].site];
                const u64 siteWaits = sites[i].waits - previous.first;
                const u64 siteNanos = sites[i].nanos - previous.second;
                previous = {sites[i].waits, sites[i].nanos};
                if (siteWaits != 0) {
                    std::fprintf(out, "# gx_sync_site 0x%llx waits=%llu ms=%.2f\n",
                                 static_cast<unsigned long long>(sites[i].site - base),
                                 static_cast<unsigned long long>(siteWaits),
                                 static_cast<double>(siteNanos) / 1e6);
                }
            }
        }
        {
            static uint64_t previous[8]{};
            const uint64_t now[8] = {
                g_audioMixCounters.mixes.load(std::memory_order_relaxed),
                g_audioMixCounters.mixNanos.load(std::memory_order_relaxed),
                g_audioMixCounters.joins.load(std::memory_order_relaxed),
                g_audioMixCounters.joinWaits.load(std::memory_order_relaxed),
                g_audioMixCounters.joinWaitNanos.load(std::memory_order_relaxed),
                g_audioMixCounters.ticksWithBlocks.load(std::memory_order_relaxed),
                g_audioMixCounters.blocks.load(std::memory_order_relaxed),
                g_audioMixCounters.multiBlockTicks.load(std::memory_order_relaxed),
            };
            std::fprintf(out,
                         "# audio_mix mixes=%llu mix_ms=%.2f joins=%llu join_waits=%llu join_wait_ms=%.2f "
                         "ticks=%llu blocks=%llu multi_block_ticks=%llu\n",
                         static_cast<unsigned long long>(now[0] - previous[0]),
                         static_cast<double>(now[1] - previous[1]) / 1e6,
                         static_cast<unsigned long long>(now[2] - previous[2]),
                         static_cast<unsigned long long>(now[3] - previous[3]),
                         static_cast<double>(now[4] - previous[4]) / 1e6,
                         static_cast<unsigned long long>(now[5] - previous[5]),
                         static_cast<unsigned long long>(now[6] - previous[6]),
                         static_cast<unsigned long long>(now[7] - previous[7]));
            std::copy(std::begin(now), std::end(now), std::begin(previous));
        }
        {
            static DlScanCacheCounters previous{};
            const DlScanCacheCounters now = g_dlScanCacheCounters;
            std::fprintf(out, "# dl_scan_cache calls=%llu hits=%llu digests=%llu scans=%llu clears=%llu\n",
                         static_cast<unsigned long long>(now.calls - previous.calls),
                         static_cast<unsigned long long>(now.hits - previous.hits),
                         static_cast<unsigned long long>(now.digests - previous.digests),
                         static_cast<unsigned long long>(now.scans - previous.scans),
                         static_cast<unsigned long long>(now.clears - previous.clears));
            {
                // Grows from window to window when something leaks.
                const struct mallinfo heap = mallinfo();
                std::fprintf(out, "# heap in_use_mib=%llu obtained_mib=%llu\n",
                             static_cast<unsigned long long>(static_cast<uint64_t>(heap.uordblks) >> 20),
                             static_cast<unsigned long long>(static_cast<uint64_t>(heap.arena) >> 20));
            }
            previous = now;
        }
        {
            std::vector<const HostStackSlot*> stacks;
            stacks.reserve(8192);
            for (const auto& entry : g_hostStackSlots) {
                if (entry.hits != 0) {
                    stacks.push_back(&entry);
                }
            }
            std::sort(stacks.begin(), stacks.end(),
                      [](const HostStackSlot* a, const HostStackSlot* b) { return a->hits > b->hits; });
            const size_t shownStacks = std::min<size_t>(stacks.size(), 32768);
            for (size_t i = 0; i < shownStacks; ++i) {
                const HostStackSlot& entry = *stacks[i];
                std::fprintf(out, "#stack %llu 0x%llx", static_cast<unsigned long long>(entry.hits),
                             static_cast<unsigned long long>(entry.pc));
                for (uint64_t frame : entry.frames) {
                    std::fprintf(out, " 0x%llx", static_cast<unsigned long long>(frame));
                }
                std::fputc('\n', out);
            }
            std::memset(g_hostStackSlots, 0, sizeof(g_hostStackSlots));
        }
        const size_t shown = std::min<size_t>(ranked.size(), 60000);
        for (size_t i = 0; i < shown; ++i) {
            std::fprintf(out, "%8llu 0x%llx\n", static_cast<unsigned long long>(ranked[i].hits),
                         static_cast<unsigned long long>(ranked[i].offset));
        }
        std::fclose(out);
    }
    if (FILE* index = std::fopen("sdmc:/switch/WiiCompiled/profile_windows/index.txt", "a")) {
        std::fprintf(index, "window=%u file=w%02u.txt seconds=%.1f fps=%.2f samples=%llu\n", window,
                     window % kHostProfileWindowFiles, seconds, fps,
                     static_cast<unsigned long long>(samples));
        std::fclose(index);
    }
}

void StartSwitchGuestProfiler() {
    FILE* flag = std::fopen("sdmc:/switch/WiiCompiled/profile.flag", "r");
    if (flag == nullptr) {
        return;
    }
    std::fclose(flag);
    RT_LOGF(RT_TAG_RUNTIME, "guest sampling profiler enabled (1 kHz -> profile.txt)\n");

    // This function runs on the thread that goes on to host the guest fibers, so that is the
    // thread to sample. svcGetThreadContext3 needs a real handle, and the only real handle we can
    // name from another thread is the main thread's, so confirm by thread id rather than assume.
    {
        u64 selfId = 0;
        u64 mainId = 0;
        const Handle mainHandle = envGetMainThreadHandle();
        if (R_SUCCEEDED(svcGetThreadId(&selfId, CUR_THREAD_HANDLE)) &&
            R_SUCCEEDED(svcGetThreadId(&mainId, mainHandle)) && selfId == mainId) {
            g_profiledThreadHandle = mainHandle;
            RT_LOGF(RT_TAG_RUNTIME, "host PC sampling enabled (-> profile_host.txt, module base 0x%llx)\n",
                    static_cast<unsigned long long>(SwitchModuleBase()));
        } else {
            RT_LOGF(RT_TAG_RUNTIME,
                    "host PC sampling unavailable: guest runs on thread %llu, not main thread %llu\n",
                    static_cast<unsigned long long>(selfId), static_cast<unsigned long long>(mainId));
        }
    }

    // Deliberately leaked rather than detached: devkitA64's newlib returns ENOSYS from
    // pthread_detach, so std::thread::detach() throws, and unwinding then runs ~thread() on a
    // still-joinable thread, which calls std::terminate(). See [switch-no-pthread-detach].
    new std::thread([] {
        // Let the first frames settle before sampling; nothing here is urgent.
        std::this_thread::sleep_for(std::chrono::seconds(3));
        int sinceReport = 0;
        auto windowStart = std::chrono::steady_clock::now();
        // A fresh index per launch, so windows from an older run are never read as this one's.
        std::remove("sdmc:/switch/WiiCompiled/profile_windows/index.txt");
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const uint32_t pc = __atomic_load_n(&RecompMod::g_currentTranslatedExecutionAddress, __ATOMIC_RELAXED);
            ++g_profileSamples;
            if (pc == 0) {
                ++g_profileOutsideGuest;
            } else {
                const uint32_t symbol = ProfileSymbolFor(pc);
                if (symbol != 0) {
                    ProfileRecord(symbol);
                }
            }
            if (g_profiledThreadHandle != INVALID_HANDLE) {
                ThreadContext ctx{};
                ++g_hostProfileSamples;
                // svcGetThreadContext3 only dumps registers for a thread that is *paused*; on a
                // running thread it fails outright (the first attempt at this failed 135000 of
                // 135000 samples). Pause, read, resume - a few microseconds of guest stall per
                // sample, which is why a profiled run reports a lower frame rate than a clean one.
                const Result pauseRc =
                    svcSetThreadActivity(g_profiledThreadHandle, ThreadActivity_Paused);
                const Result ctxRc =
                    R_SUCCEEDED(pauseRc) ? svcGetThreadContext3(&ctx, g_profiledThreadHandle) : pauseRc;
                // The stack is only stable while the thread is paused, so walk it before resuming.
                uint64_t frames[kHostStackFrames]{};
                if (R_SUCCEEDED(ctxRc) && SwitchModuleBase() != 0) {
                    WalkPausedStack(ctx, SwitchModuleBase(), frames);
                }
                if (R_SUCCEEDED(pauseRc)) {
                    svcSetThreadActivity(g_profiledThreadHandle, ThreadActivity_Runnable);
                }
                if (R_FAILED(ctxRc)) {
                    ++g_hostProfileFailures;
                    g_hostProfileLastError = ctxRc;
                } else {
                    const u64 base = SwitchModuleBase();
                    const u64 hostPc = ctx.pc.x;
                    if (base != 0 && hostPc >= base) {
                        HostProfileRecord((hostPc - base) & kHostProfileBucketMask);
                        HostStackRecord((hostPc - base) & kHostProfileBucketMask, frames);
                    } else {
                        ++g_hostProfileOffModule;
                    }
                }
            }
            if ((sinceReport & 3) == 0) {
                HelperProfileSample((sinceReport >> 2) & 1);  // each helper every 8 ms
            }
            if (++sinceReport >= 15000) {  // ~15 s of samples
                sinceReport = 0;
                ProfileWriteReport();
                if (g_profiledThreadHandle != INVALID_HANDLE) {
                    HostProfileWriteReport();
                    const auto now = std::chrono::steady_clock::now();
                    HostProfileWriteWindow(std::chrono::duration<double>(now - windowStart).count());
                    windowStart = now;
                }
            }
        }
    });
}

#if defined(MKW_PGO_GENERATE)
extern "C" void __gcov_dump(void);
extern "C" void __gcov_reset(void);

// The first PGO recording build showed a black screen and never reached RuntimeMain's log
// redirection. Append-and-close markers survive any hang, so the next run names the stage.
void SwitchPgoBootTrace(const char* stage) {
    if (FILE* f = std::fopen("sdmc:/switch/WiiCompiled/pgo_boot_trace.txt", "a")) {
        std::fprintf(f, "%llu ms %s\n",
                     static_cast<unsigned long long>(armTicksToNs(armGetSystemTick()) / 1000000ull), stage);
        std::fclose(f);
    }
}

__attribute__((constructor(101))) static void SwitchPgoBootTraceStaticInit() {
    SwitchPgoBootTrace("static init begins");
}

// A PGO run ends by closing the console, never by returning from main, so the counters would
// never be flushed. Dump them on a timer instead. libgcov MERGES each dump into the .gcda already
// on disk, and the in-memory counters are cumulative, so without the reset every dump would add
// the whole run so far again and the first minutes (boot, menus) would be counted many times.
std::atomic<bool> g_switchGcovTimerStop{false};
std::thread* g_switchGcovTimerThread = nullptr;

void StartSwitchGcovDumpTimer() {
    // Kept (not detached: pthread_detach is ENOSYS on devkitA64, see StartSwitchGuestProfiler) so
    // StopSwitchGcovDumpTimer can join it.
    g_switchGcovTimerThread = new std::thread([] {
        for (;;) {
            for (int tenths = 0; tenths < 600; ++tenths) {
                if (g_switchGcovTimerStop.load(std::memory_order_relaxed)) {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            SwitchPgoBootTrace("gcov dump begins");
            __gcov_dump();
            __gcov_reset();
            SwitchPgoBootTrace("gcov dump done");
        }
    });
}

// The launcher's hand-off to another program reuses this process: a timer thread still running
// (or mid-dump) then executes code that is no longer there. The first recording build crashed on
// every Retro Rewind start that way. Stop it, and flush what this program counted.
void StopSwitchGcovDumpTimer() {
    if (g_switchGcovTimerThread == nullptr) {
        return;
    }
    g_switchGcovTimerStop.store(true, std::memory_order_relaxed);
    g_switchGcovTimerThread->join();
    delete g_switchGcovTimerThread;
    g_switchGcovTimerThread = nullptr;
    SwitchPgoBootTrace("gcov final dump begins");
    __gcov_dump();
    __gcov_reset();
    SwitchPgoBootTrace("gcov final dump done");
}
#endif
#endif // __SWITCH__

namespace {

// Defined below, beside the fatal-log machinery.
std::string FormatHostStackTrace(unsigned framesToSkip = 0);

void ServiceGuestTimingDuringAuroraFrameWait() {
    // Aurora can block inside FIFO drains before control returns to GX HLE, for as long as a
    // whole display period. Keep VI retraces, alarms and audio moving at wall-clock cadence here
    // while still suppressing guest rescheduling and recursive Aurora work.
    ZoneScoped;
    VI_HLE_ProcessRetracesDeferred(8);
    OS_HLE_ProcessAlarmsDeferred(8);
    Audio_HLE_PollDeferred();
}

#if defined(_WIN32)
int __cdecl WindowsCrtReportHook(int reportType, char* message, int* returnValue) {
    if (returnValue) {
        *returnValue = 0;
    }

    RT_LOG(RT_TAG_RUNTIME) << "CRT report type=" << reportType;
    if (message) {
        std::cerr << ": " << message;
    } else {
        std::cerr << '\n';
    }

    RT_LOG(RT_TAG_RUNTIME) << "CRT report stack:\n" << FormatHostStackTrace(1);
    std::cerr.flush();
    return TRUE;
}

void ConfigureWindowsFatalDialogBehavior() {
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(WindowsCrtReportHook);
}

class WindowsTimerResolutionGuard {
public:
    WindowsTimerResolutionGuard() {
        const MMRESULT result = ::timeBeginPeriod(1);
        if (result == TIMERR_NOERROR) {
            armed_ = true;
        } else {
            RT_LOG(RT_TAG_RUNTIME) << "timeBeginPeriod(1) failed: " << result << std::endl;
        }
    }

    ~WindowsTimerResolutionGuard() {
        if (armed_) {
            ::timeEndPeriod(1);
        }
    }

    WindowsTimerResolutionGuard(const WindowsTimerResolutionGuard&) = delete;
    WindowsTimerResolutionGuard& operator=(const WindowsTimerResolutionGuard&) = delete;

private:
    bool armed_ = false;
};
#endif
} // namespace

namespace {

std::string g_lastEntryLabel;
std::atomic_bool g_auroraInitialized{false};
std::atomic_flag g_abortSignalHandled = ATOMIC_FLAG_INIT;
std::atomic_bool g_fatalErrorReported{false};
std::atomic_bool g_fatalPopupShown{false};
std::atomic<int> g_lastExitCode{0};
std::atomic_bool g_exitCodeSet{false};

struct ProcessTranscriptState {
    bool enabled = false;
    std::filesystem::path path;
    std::ofstream file;
    std::mutex fileMutex;
    std::atomic_bool initialized{false};
    int savedStdoutFd = -1;
    int savedStderrFd = -1;
    int stdoutPipeReadFd = -1;
    int stdoutPipeWriteFd = -1;
    int stderrPipeReadFd = -1;
    int stderrPipeWriteFd = -1;
    std::thread stdoutThread;
    std::thread stderrThread;
};

std::filesystem::path GetDefaultRuntimeLogDirectory() {
    return RuntimeConfigFile::ApplicationDataDirectory() / "Logs";
}

// Every entry in the Logs root - both the per-run folders written by this
// scheme and any flat .log files left over from the previous one - is removed
// once it is older than the retention window. A crash writes an uncompressed
// MEM1+MEM2 snapshot (~150-190MB) into its run folder regardless of how minor
// the fault was (confirmed on-device: a burst of testing produced 66 folders,
// 5.2GB, well inside the 4-day age window), so age alone doesn't bound size on
// a device that happens to crash repeatedly in a short span - cap the *count*
// of retained runs too, independent of age.
void PruneOldRunLogs(const std::filesystem::path& logRoot) {
    constexpr auto kRetention = std::chrono::hours(24 * 4);
    constexpr size_t kMaxRetainedRuns = 10;

    std::error_code ec;
    const auto now = std::filesystem::file_time_type::clock::now();
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> survivors;
    for (const auto& entry : std::filesystem::directory_iterator(logRoot, ec)) {
        std::error_code entryEc;
        const auto writeTime = std::filesystem::last_write_time(entry.path(), entryEc);
        if (entryEc) {
            continue;
        }
        if (now - writeTime > kRetention) {
            std::filesystem::remove_all(entry.path(), entryEc);
        } else {
            survivors.emplace_back(writeTime, entry.path());
        }
    }

    if (survivors.size() <= kMaxRetainedRuns) {
        return;
    }
    std::sort(survivors.begin(), survivors.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = kMaxRetainedRuns; i < survivors.size(); ++i) {
        std::error_code removeEc;
        std::filesystem::remove_all(survivors[i].second, removeEc);
    }
}

// Logs/<product>_<epochSeconds>_pid<pid>/ - one folder per run. The console
// transcript and every crash artifact for the run land in here, so "zip this
// folder" is a complete diagnostic. Created lazily so even a crash before
// transcript setup still has somewhere to write.
const std::filesystem::path& GetRunLogDirectory() {
    static const std::filesystem::path runDirectory = [] {
        const std::filesystem::path logRoot = GetDefaultRuntimeLogDirectory();

        std::error_code ec;
        std::filesystem::create_directories(logRoot, ec);
        PruneOldRunLogs(logRoot);

#if defined(_WIN32)
        const unsigned long pid = ::GetCurrentProcessId();
#else
        const auto pid = static_cast<unsigned long>(::getpid());
#endif
        const auto now = std::chrono::system_clock::now();
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        std::ostringstream name;
        name << (RuntimeProduct::IsRetroRewind() ? "retro_rewind" : "base")
             << "_" << secs << "_pid" << pid;
        const std::filesystem::path directory = logRoot / name.str();
        std::filesystem::create_directories(directory, ec);
        return directory;
    }();
    return runDirectory;
}

ProcessTranscriptState& GetProcessTranscriptState() {
    static ProcessTranscriptState state;
    return state;
}

void WriteProcessTranscriptChunk(ProcessTranscriptState& state, const char* data, size_t size) {
    if (!state.enabled || !state.file || size == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(state.fileMutex);
    state.file.write(data, static_cast<std::streamsize>(size));
    state.file.flush();
}

void PumpTranscriptPipe(ProcessTranscriptState& state, int readFd, int mirrorFd) {
    std::array<char, 4096> buffer{};
    for (;;) {
#if defined(_WIN32)
        const int bytesRead = _read(readFd, buffer.data(), static_cast<unsigned int>(buffer.size()));
#else
        const ssize_t bytesRead = ::read(readFd, buffer.data(), buffer.size());
#endif
        if (bytesRead <= 0) {
            break;
        }

        if (mirrorFd >= 0) {
            size_t offset = 0;
            while (offset < static_cast<size_t>(bytesRead)) {
#if defined(_WIN32)
                const int written = _write(mirrorFd,
                                           buffer.data() + offset,
                                           static_cast<unsigned int>(static_cast<size_t>(bytesRead) - offset));
#else
                const ssize_t written = ::write(mirrorFd,
                                                buffer.data() + offset,
                                                static_cast<size_t>(bytesRead) - offset);
#endif
                if (written <= 0) {
                    break;
                }
                offset += static_cast<size_t>(written);
            }
        }

        WriteProcessTranscriptChunk(state, buffer.data(), static_cast<size_t>(bytesRead));
    }
}

#if defined(_WIN32)
int GetFileDescriptor(FILE* file) {
    return _fileno(file);
}

int DuplicateFileDescriptor(int fd) {
    return _dup(fd);
}

int DuplicateFileDescriptorTo(int sourceFd, int targetFd) {
    return _dup2(sourceFd, targetFd);
}

void CloseFileDescriptor(int fd) {
    _close(fd);
}
#else
int GetFileDescriptor(FILE* file) {
    return fileno(file);
}

int DuplicateFileDescriptor(int fd) {
    return ::dup(fd);
}

int DuplicateFileDescriptorTo(int sourceFd, int targetFd) {
    return ::dup2(sourceFd, targetFd);
}

void CloseFileDescriptor(int fd) {
    ::close(fd);
}
#endif

bool InstallTranscriptPipe(int& outReadFd, int& outWriteFd, int targetFd) {
#if defined(__SWITCH__)
    // Horizon's newlib has no pipe(), and the transcript would be redundant anyway: RuntimeMain
    // already reopens stdout/stderr onto the SD card (log.txt / stderr.txt), and nxlink takes
    // over those descriptors when the build is launched from the network loader. Reporting
    // failure here makes InitializeProcessTranscript unwind cleanly and leave stdio alone.
    (void)outReadFd;
    (void)outWriteFd;
    (void)targetFd;
    return false;
#else
#if defined(_WIN32)
    int pipeFds[2]{-1, -1};
    if (_pipe(pipeFds, 8192, _O_BINARY) != 0) {
        return false;
    }
#else
    int pipeFds[2]{-1, -1};
    if (::pipe(pipeFds) != 0) {
        return false;
    }
#endif

    outReadFd = pipeFds[0];
    outWriteFd = pipeFds[1];
    if (targetFd == GetFileDescriptor(stdout)) {
        std::fflush(stdout);
    } else if (targetFd == GetFileDescriptor(stderr)) {
        std::fflush(stderr);
    }

    if (DuplicateFileDescriptorTo(outWriteFd, targetFd) < 0) {
        CloseFileDescriptor(outReadFd);
        CloseFileDescriptor(outWriteFd);
        outReadFd = -1;
        outWriteFd = -1;
        return false;
    }
    return true;
#endif // !__SWITCH__
}

#if defined(_WIN32)
void AttachParentConsoleForDiagnostics() {
    // GUI-subsystem products get no console and no bound stdout/stderr unless a parent already
    // redirected them (pipe/file: leave alone) or has a console to attach to (bind only the
    // streams still unbound). With no parent console, fall back to NUL rather than leaving
    // _fileno(stdout) == -2, which would stop InitializeProcessTranscript from redirecting into
    // the log file at all.
    const HANDLE outHandle = ::GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE errHandle = ::GetStdHandle(STD_ERROR_HANDLE);
    const bool haveOut = outHandle != nullptr && outHandle != INVALID_HANDLE_VALUE;
    const bool haveErr = errHandle != nullptr && errHandle != INVALID_HANDLE_VALUE;
    if (haveOut && haveErr) {
        return;
    }

    const bool attached =
        ::GetConsoleWindow() != nullptr || ::AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    const char* const sink = attached ? "CONOUT$" : "NUL";
    if (!haveOut) {
        (void)std::freopen(sink, "w", stdout);
    }
    if (!haveErr) {
        (void)std::freopen(sink, "w", stderr);
    }
    std::cout.clear();
    std::cerr.clear();
}
#else
void AttachParentConsoleForDiagnostics() {}
#endif

// The setup writes build-fingerprint.json beside every product executable; its SetupVersion is
// the only version identity the runtime has (products are compiled locally, so nothing is baked
// into the binary). Surface it at the top of the transcript so every attached log self-identifies.
std::string ReadInstalledSetupVersion() {
    const auto directory = RuntimeConfigFile::ExecutableDirectory();
    if (!directory) {
        return {};
    }
    std::ifstream file(*directory / "build-fingerprint.json", std::ios::binary);
    if (!file) {
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    constexpr std::string_view kKey = "\"SetupVersion\"";
    const auto keyPos = text.find(kKey);
    if (keyPos == std::string::npos) {
        return {};
    }
    const auto colon = text.find(':', keyPos + kKey.size());
    const auto open = colon == std::string::npos ? std::string::npos : text.find('"', colon + 1);
    const auto close = open == std::string::npos ? std::string::npos : text.find('"', open + 1);
    if (close == std::string::npos) {
        return {};
    }
    return text.substr(open + 1, close - open - 1);
}

void InitializeProcessTranscript(int argc, char** argv) {
    auto& state = GetProcessTranscriptState();
    if (state.initialized.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    const std::filesystem::path path = GetRunLogDirectory() / "console.log";
    state.file.open(path.string(), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!state.file) {
        return;
    }

    state.enabled = true;
    state.path = path;

#if defined(_WIN32)
    const unsigned long pid = ::GetCurrentProcessId();
#else
    const auto pid = static_cast<unsigned long>(::getpid());
#endif

    const std::string setupVersion = ReadInstalledSetupVersion();

    {
        std::lock_guard<std::mutex> lock(state.fileMutex);
        state.file << "[runtime] WiiCompiled "
                   << (setupVersion.empty() ? "version unknown" : setupVersion) << "\n";
        state.file << "[runtime] process transcript started\n";
        state.file << "[runtime] pid=" << pid << "\n";
        state.file << "[runtime] argv=";
        for (int i = 0; i < argc; ++i) {
            if (i != 0) {
                state.file << ' ';
            }
            state.file << argv[i];
        }
        state.file << "\n";
        state.file.flush();
    }

    state.savedStdoutFd = DuplicateFileDescriptor(GetFileDescriptor(stdout));
    state.savedStderrFd = DuplicateFileDescriptor(GetFileDescriptor(stderr));

    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    auto restoreFailedSetup = [&state]() {
        if (state.savedStdoutFd >= 0) {
            DuplicateFileDescriptorTo(state.savedStdoutFd, GetFileDescriptor(stdout));
        }
        if (state.savedStderrFd >= 0) {
            DuplicateFileDescriptorTo(state.savedStderrFd, GetFileDescriptor(stderr));
        }
        if (state.stdoutPipeReadFd >= 0) {
            CloseFileDescriptor(state.stdoutPipeReadFd);
            state.stdoutPipeReadFd = -1;
        }
        if (state.stdoutPipeWriteFd >= 0) {
            CloseFileDescriptor(state.stdoutPipeWriteFd);
            state.stdoutPipeWriteFd = -1;
        }
        if (state.stderrPipeReadFd >= 0) {
            CloseFileDescriptor(state.stderrPipeReadFd);
            state.stderrPipeReadFd = -1;
        }
        if (state.stderrPipeWriteFd >= 0) {
            CloseFileDescriptor(state.stderrPipeWriteFd);
            state.stderrPipeWriteFd = -1;
        }
        if (state.savedStdoutFd >= 0) {
            CloseFileDescriptor(state.savedStdoutFd);
            state.savedStdoutFd = -1;
        }
        if (state.savedStderrFd >= 0) {
            CloseFileDescriptor(state.savedStderrFd);
            state.savedStderrFd = -1;
        }
        state.enabled = false;
        state.file.close();
    };

    if (!InstallTranscriptPipe(state.stdoutPipeReadFd, state.stdoutPipeWriteFd, GetFileDescriptor(stdout)) ||
        !InstallTranscriptPipe(state.stderrPipeReadFd, state.stderrPipeWriteFd, GetFileDescriptor(stderr))) {
        restoreFailedSetup();
        return;
    }

    state.stdoutThread = std::thread([&state]() {
        PumpTranscriptPipe(state, state.stdoutPipeReadFd, state.savedStdoutFd);
    });
    state.stderrThread = std::thread([&state]() {
        PumpTranscriptPipe(state, state.stderrPipeReadFd, state.savedStderrFd);
    });
}

void ShutdownProcessTranscript() {
    auto& state = GetProcessTranscriptState();
    if (!state.enabled) {
        return;
    }

    std::fflush(stdout);
    std::fflush(stderr);
    std::cout.flush();
    std::cerr.flush();

    if (state.savedStdoutFd >= 0) {
        DuplicateFileDescriptorTo(state.savedStdoutFd, GetFileDescriptor(stdout));
    }
    if (state.savedStderrFd >= 0) {
        DuplicateFileDescriptorTo(state.savedStderrFd, GetFileDescriptor(stderr));
    }

    if (state.stdoutPipeWriteFd >= 0) {
        CloseFileDescriptor(state.stdoutPipeWriteFd);
        state.stdoutPipeWriteFd = -1;
    }
    if (state.stderrPipeWriteFd >= 0) {
        CloseFileDescriptor(state.stderrPipeWriteFd);
        state.stderrPipeWriteFd = -1;
    }

    if (state.stdoutThread.joinable()) {
        state.stdoutThread.join();
    }
    if (state.stderrThread.joinable()) {
        state.stderrThread.join();
    }

    if (state.stdoutPipeReadFd >= 0) {
        CloseFileDescriptor(state.stdoutPipeReadFd);
        state.stdoutPipeReadFd = -1;
    }
    if (state.stderrPipeReadFd >= 0) {
        CloseFileDescriptor(state.stderrPipeReadFd);
        state.stderrPipeReadFd = -1;
    }
    if (state.savedStdoutFd >= 0) {
        CloseFileDescriptor(state.savedStdoutFd);
        state.savedStdoutFd = -1;
    }
    if (state.savedStderrFd >= 0) {
        CloseFileDescriptor(state.savedStderrFd);
        state.savedStderrFd = -1;
    }

    {
        std::lock_guard<std::mutex> lock(state.fileMutex);
        state.file << "\n[runtime] process transcript ended\n";
        state.file.flush();
    }
    state.file.close();
    state.enabled = false;
}

// The one host stack walker. Every caller - the CRT report hook, the fatal log
// and the stderr crash dump - goes through this, so the log and the console see
// exactly the same frames, including the TranslatedFunctionRegistry fallback for
// addresses DbgHelp cannot name.
std::string FormatHostStackTrace(unsigned framesToSkip) {
#if defined(_WIN32)
    static std::atomic_bool s_symbolsReady{false};
    HANDLE process = GetCurrentProcess();
    if (!s_symbolsReady.load(std::memory_order_acquire)) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        if (SymInitialize(process, nullptr, TRUE)) {
            s_symbolsReady.store(true, std::memory_order_release);
        }
    }
    const bool symbolsReady = s_symbolsReady.load(std::memory_order_acquire);

    void* frames[64]{};
    const USHORT captured = CaptureStackBackTrace(static_cast<DWORD>(framesToSkip),
                                                  static_cast<DWORD>(std::size(frames)), frames, nullptr);
    std::ostringstream out;
    if (captured == 0) {
        out << "[runtime] host stack trace unavailable (CaptureStackBackTrace returned 0)\n";
        return out.str();
    }
    out << "[runtime] host stack trace (most recent call first):\n";
    for (USHORT i = 0; i < captured; ++i) {
        const DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
        HMODULE module = nullptr;
        char modulePath[MAX_PATH] = "?";
        DWORD64 moduleBase = 0;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(frames[i]),
                               &module) != 0 &&
            module != nullptr) {
            moduleBase = reinterpret_cast<DWORD64>(module);
            (void)GetModuleFileNameA(module, modulePath, MAX_PATH);
        }

        const char* symbolName = "?";
        std::string translatedFuncName;
        uint32_t ppcAddress = 0;
        DWORD64 symbolDisp = 0;
        std::array<char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> symbolBuffer{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer.data());
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        if (symbolsReady && SymFromAddr(process, addr, &symbolDisp, symbol)) {
            symbolName = symbol->Name;
        } else if (auto info = TranslatedFunctionRegistry::FindByHostAddress(static_cast<uintptr_t>(addr))) {
            // DbgHelp could not name it; the translated-function registry can.
            translatedFuncName = info->name;
            ppcAddress = info->address;
            if (!translatedFuncName.empty()) {
                symbolName = translatedFuncName.c_str();
                symbolDisp = addr - reinterpret_cast<DWORD64>(info->entryPoint);
            }
        }

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisp = 0;
        const char* fileName = nullptr;
        DWORD lineNumber = 0;
        if (symbolsReady && SymGetLineFromAddr64(process, addr, &lineDisp, &line)) {
            fileName = line.FileName;
            lineNumber = line.LineNumber;
        }

        out << "  ["
            << std::setw(2) << std::setfill('0') << static_cast<unsigned>(i)
            << std::setfill(' ') << "] "
            << modulePath << "!" << symbolName
            << " + 0x" << std::hex << std::uppercase << symbolDisp
            << " (0x" << addr;
        if (moduleBase != 0) {
            out << ", module+0x" << (addr - moduleBase);
        }
        if (ppcAddress != 0) {
            out << ", PPC:0x" << std::setw(8) << std::setfill('0') << ppcAddress << std::setfill(' ');
        }
        out << std::dec << std::nouppercase;
        if (fileName) {
            out << ", " << fileName << ":" << lineNumber;
        }
        out << ")\n";
    }
    return out.str();
#elif defined(__SWITCH__)
    // libnx has no backtrace API and devkitA64's newlib has no execinfo.h, but libgcc's DWARF
    // unwinder is linked in regardless (C++ exceptions need it), and it works whether or not the
    // optimiser kept a frame pointer. Addresses are module-relative so
    // `aarch64-none-elf-addr2line -e WiiCompiled.elf <offset>` resolves them against the
    // unstripped ELF that sits next to the .nro - same convention as __libnx_exception_handler.
    struct UnwindState {
        unsigned skip;
        unsigned emitted;
        std::ostringstream* out;
    };
    UnwindState state{framesToSkip + 1, 0, nullptr};
    std::ostringstream out;
    state.out = &out;
    _Unwind_Backtrace(
        [](_Unwind_Context* context, void* arg) -> _Unwind_Reason_Code {
            auto* st = static_cast<UnwindState*>(arg);
            if (st->skip > 0) {
                --st->skip;
                return _URC_NO_REASON;
            }
            if (st->emitted >= 48) {
                return _URC_END_OF_STACK;
            }
            const auto pc = static_cast<uintptr_t>(_Unwind_GetIP(context));
            if (pc == 0) {
                return _URC_END_OF_STACK;
            }
            char line[64];
            // _Unwind_GetIP gives the return address; step back into the call itself so
            // addr2line lands on the calling line rather than the one after it.
            std::snprintf(line, sizeof(line), "[runtime]   #%02u +0x%llx\n", st->emitted,
                          static_cast<unsigned long long>(SwitchModuleOffset(pc - 4)));
            *st->out << line;
            ++st->emitted;
            return _URC_NO_REASON;
        },
        &state);
    if (state.emitted == 0) {
        return "[runtime] host stack trace unavailable (unwinder produced no frames)\n";
    }
    char header[96];
    std::snprintf(header, sizeof(header), "[runtime] host stack trace (module base 0x%llx):\n",
                  static_cast<unsigned long long>(SwitchModuleBase()));
    return std::string(header) + out.str();
#else
    (void)framesToSkip;
    return {};
#endif
}

void WriteFatalLogImpl(std::string_view reason, std::string_view extraDetails = {},
                       const uint32_t* missingGuestTarget = nullptr) {
    const std::filesystem::path runDirectory = GetRunLogDirectory();
    std::string fileName = "crash_";
    fileName.append(reason);
    fileName.append(".txt");
    std::ofstream out((runDirectory / fileName).string(), std::ios::out | std::ios::trunc);
    if (!out) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto nowSecs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    out << "[runtime] fatal log" << std::endl;
    out << "[runtime] reason: " << reason << std::endl;
#if defined(__SWITCH__)
    {
        // Whether the process had run out of memory, e.g. behind a std::bad_alloc. The kernel's
        // "used" figure is no help (the whole heap is claimed at start, so it always equals the
        // total); what counts is how much of that heap malloc has handed out.
        u64 total = 0;
        svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        const struct mallinfo heap = mallinfo();
        out << "[runtime] heap: " << (static_cast<uint64_t>(heap.uordblks) >> 20) << " MiB in use, "
            << (static_cast<uint64_t>(heap.arena) >> 20) << " MiB obtained, process total " << (total >> 20)
            << " MiB" << std::endl;
    }
#endif
    out << "[runtime] timestamp(seconds): " << nowSecs << std::endl;
    out << "[runtime] entry: " << (g_lastEntryLabel.empty() ? "<unknown>" : g_lastEntryLabel) << std::endl;
    if (!extraDetails.empty()) {
        out << "[runtime] details: " << extraDetails << std::endl;
    }
    if (missingGuestTarget) {
        out << "[runtime] guest jump target: 0x" << std::hex << std::uppercase
            << *missingGuestTarget << std::dec << std::endl;
    }

    const CpuContext* cpu = TryGetCpuContext();
    if (!cpu) {
        // Exceptions can unwind CpuContextScope before we get here.
        // Fall back to the persistent CPU context snapshot so fatal logs still include registers.
        cpu = &GetPersistentCpuContext();
    }
    if (cpu) {
        SystemBridge::DumpCrashHeuristics(out, cpu, missingGuestTarget);
        SystemBridge::DumpCpuState(out, cpu);
    } else {
        out << "[runtime] CPU context unavailable." << std::endl;
    }

    // Guest memory snapshots so the heap/object state can be walked offline.
    // Written once per process: several fatal paths can fire in sequence
    // (e.g. an exception followed by the exit-code report) and the snapshots
    // are large.
    static std::atomic_bool s_memorySnapshotWritten{false};
    if (!s_memorySnapshotWritten.exchange(true, std::memory_order_acq_rel)) {
        SystemBridge::WriteGuestMemorySnapshot(out, (runDirectory / "mem1.bin").string().c_str());
    }

    out.flush();
    RT_LOG(RT_TAG_RUNTIME) << "crash artifacts written to " << runDirectory.string() << std::endl;
}

void SetRuntimeExitCodeImpl(int code) {
    g_lastExitCode.store(code, std::memory_order_relaxed);
    g_exitCodeSet.store(true, std::memory_order_relaxed);
}

} // namespace

namespace {

void DumpHostStackTrace() {
#if defined(_WIN32)
    static std::atomic_flag s_inProgress = ATOMIC_FLAG_INIT;
    if (s_inProgress.test_and_set()) {
        return;
    }
    const std::string trace = FormatHostStackTrace(1);
    std::fputs(trace.c_str(), stderr);
    std::fflush(stderr);
    s_inProgress.clear();
#elif defined(__SWITCH__)
    static std::atomic_flag s_inProgress = ATOMIC_FLAG_INIT;
    if (s_inProgress.test_and_set()) {
        return;
    }
    const std::string trace = FormatHostStackTrace(1);
    std::fputs(trace.c_str(), stderr);
    std::fflush(stderr);
    s_inProgress.clear();
#else
    RT_LOGF(RT_TAG_RUNTIME, "Host stack trace unavailable on this platform\n");
    std::fflush(stderr);
#endif
}

} // namespace

extern "C" void DumpHostStackTraceForRuntimeHelper() {
    DumpHostStackTrace();
}

void MarkFatalErrorReported() {
    g_fatalErrorReported.store(true, std::memory_order_release);
}

void ShowRuntimeFatalPopup(std::string_view category, std::string_view details) noexcept {
    if (g_fatalPopupShown.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    try {
        std::string message;
        message.reserve(category.size() + details.size() + 220);
        message.append("The game stopped because ");
        message.append(category.empty() ? "a fatal error occurred." : category);
        message.append(".\n\n");
        if (details.empty()) {
            message.append("No additional details were available.");
        } else {
            constexpr size_t kMaxPopupDetails = 4096;
            message.append(details.data(), std::min(details.size(), kMaxPopupDetails));
            if (details.size() > kMaxPopupDetails) {
                message.append("\n\n[Additional details were written to the crash log.]");
            }
        }
        message.append("\n\nSee the WiiCompiled Logs folder for the full diagnostic.");
#if defined(_WIN32)
        ::MessageBoxA(nullptr, message.c_str(), "WiiCompiled - Fatal Error",
                      MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TASKMODAL);
#else
        // The shipped product is Windows-first. Keep non-Windows builds safe
        // and retain the console diagnostic when no native dialog is available.
        RT_LOGF(RT_TAG_RUNTIME, "fatal dialog: %s\n", message.c_str());
#endif
    } catch (...) {
        // Reporting a crash must never throw or mask the original failure.
    }
}

namespace RuntimeCrash {

void WriteCrashArtifacts(std::string_view reason, std::string_view extraDetails,
                         const uint32_t* missingGuestTarget) noexcept {
    try {
        WriteFatalLogImpl(reason, extraDetails, missingGuestTarget);
    } catch (...) {
        // Crash reporting must never mask the original failure.
    }
}

[[noreturn]] void FatalMissingGuestTarget(uint32_t target, CpuContext* cpu) noexcept {
    RT_LOG(RT_TAG_RUNTIME) << "InvokeIndirectCpu: target 0x" << std::hex << target
              << " not translated (missing function)" << std::dec << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << "Caller LR = 0x" << std::hex << (cpu ? cpu->lr : 0u)
              << std::dec << std::endl;
    try {
        SystemBridge::DumpCrashHeuristics(std::cerr, cpu, &target);
        SystemBridge::DumpCpuState(cpu);
    } catch (...) {
    }
    std::fflush(stderr);

    std::ostringstream message;
    message << "The game stopped because it tried to execute guest address 0x"
            << std::hex << target
            << ", but that function was not translated or registered.\n\n"
            << "Caller LR: 0x" << (cpu ? cpu->lr : 0u);
    if (target == 0) {
        message << "\n\nA jump to address 0 usually means a virtual call through a bad "
                   "object pointer; the crash log heuristics have details.";
    }
    WriteCrashArtifacts("missing_target", message.str(), &target);
    ShowRuntimeFatalPopup("Missing translated function", message.str());
    MarkFatalErrorReported();
    std::exit(EXIT_FAILURE);
}

} // namespace RuntimeCrash



namespace {

void RuntimeAuroraLogCallback(AuroraLogLevel level, const char* module,
                              const char* message, unsigned int len) {
    const std::string_view moduleView = module != nullptr ? std::string_view(module) : std::string_view{};
    const std::string_view messageView = message != nullptr ? std::string_view(message, len) : std::string_view{};
    std::cerr << "[aurora] [" << static_cast<int>(level) << "] [" << moduleView << "] "
              << messageView << std::endl;
    if (level == LOG_FATAL) {
        ShowRuntimeFatalPopup("Aurora reported a fatal renderer error", messageView);
    }
}

const TranslatedFunctionInfo* ResolveEntry() {
    const auto* entry = TranslatedFunctionRegistry::FindByAddressPtr(kDefaultEntryAddress);
    if (!entry) {
        std::ostringstream oss;
        oss << "No translated function registered at address 0x" << std::hex << kDefaultEntryAddress;
        throw std::runtime_error(oss.str());
    }
    return entry;
}

void SeedCpuContext(CpuContext& cpu) {
    cpu.gpr[1] = 0x81700000u;
}

void DumpAccessViolationReport(const Memory::AccessViolation& ex,
                               std::string_view entryLabel) {
    const uint32_t address = ex.address();
    const size_t length = ex.length();
    const CpuContext* cpu = TryGetCpuContext();

    RT_LOG(RT_TAG_RUNTIME) << "===== Memory Access Violation =====" << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << "Reason : " << ex.reason() << std::endl;
    std::cerr << std::hex << std::uppercase;
    RT_LOG(RT_TAG_RUNTIME) << "Address: 0x" << std::setw(8) << std::setfill('0') << address
              << " (+0x" << length << ")" << std::dec << std::setfill(' ') << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << "Entry  : " << (entryLabel.empty() ? "(unknown)" : std::string(entryLabel)) << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << "Mode   : strict (trap on unmapped)" << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << "r1 seed: 0x81700000" << std::endl;
    if (cpu) {
        RT_LOG(RT_TAG_RUNTIME) << "CurrentCpuContext: " << cpu << "  r1=0x"
                  << std::hex << std::uppercase << cpu->gpr[1] << std::dec << std::nouppercase << std::endl;
        RT_LOG(RT_TAG_RUNTIME) << "Last recorded PC : 0x" << std::hex << std::uppercase << cpu->pc
                  << std::dec << std::nouppercase << std::endl;
    } else {
        RT_LOG(RT_TAG_RUNTIME) << "CurrentCpuContext: (null)" << std::endl;
    }

    SystemBridge::DumpCpuState(cpu);

    const auto regions = Memory::DescribeRegions();
    if (regions.empty()) {
        RT_LOG(RT_TAG_RUNTIME) << "Memory not initialized; no regions mapped." << std::endl;
        return;
    }

    RT_LOG(RT_TAG_RUNTIME) << "Mapped regions:" << std::endl;
    uint64_t bestDistance = std::numeric_limits<uint64_t>::max();
    std::string bestRegion;
    bool insideRegion = false;

    for (const auto& region : regions) {
        const uint64_t base = region.baseAddress;
        const uint64_t end = base + region.sizeBytes;
        const bool contains = address >= base && address < end;
        if (contains) {
            insideRegion = true;
            bestDistance = 0;
            bestRegion = region.name;
        } else {
            const uint64_t distance = address < base ? base - address : address - end + 1;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestRegion = region.name;
            }
        }

        std::cerr << "  - " << region.name
                  << " 0x" << std::hex << std::setw(8) << std::setfill('0') << region.baseAddress
                  << " .. 0x" << std::setw(8) << (end - 1)
                  << std::dec << std::setfill(' ')
                  << " (" << region.sizeBytes / 1024 << " KiB";
        if (contains) {
            std::cerr << ", <-- access landed here";
        }
        std::cerr << ")" << std::endl;
    }

    if (!bestRegion.empty() && !insideRegion) {
        RT_LOG(RT_TAG_RUNTIME) << "Nearest region: " << bestRegion << " (" << bestDistance << " bytes away)" << std::endl;
    }

    RT_LOG(RT_TAG_RUNTIME) << "Verify that the installed game data and runtime build match." << std::endl;
}

#if defined(_WIN32)
PVOID g_vectoredSehHandle = nullptr;
constexpr DWORD kCppExceptionCodeGcc = 0x20474343; // "GCC" exception code
constexpr DWORD kCppExceptionCodeMsvc = 0xE06D7363;
// AddressSanitizer uses STATUS_FATAL_APP_EXIT when it detects an error and wants to report it.
// We must let ASan's handler run so it can print file/line information.
constexpr DWORD kAsanFatalAppExit = 0x40000015; // STATUS_FATAL_APP_EXIT

void ReportStructuredException(EXCEPTION_POINTERS* info) {
    if (!info || !info->ExceptionRecord) {
        RT_LOG(RT_TAG_RUNTIME) << "Structured exception occurred, but no diagnostic info was captured." << std::endl;
        return;
    }

    const auto* record = info->ExceptionRecord;
    const auto code = record->ExceptionCode;
    std::cerr << std::hex << std::uppercase;
    RT_LOG(RT_TAG_RUNTIME) << "Structured exception 0x" << code;
    if (!g_lastEntryLabel.empty()) {
        std::cerr << " while executing " << g_lastEntryLabel;
    }
    std::cerr << std::dec << std::nouppercase << std::endl;

    const auto faultAddress = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
    std::cerr << std::hex << std::uppercase;
    RT_LOG(RT_TAG_RUNTIME) << "Fault address: 0x" << faultAddress << std::dec << std::nouppercase << std::endl;

    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const auto accessType = record->ExceptionInformation[0];
        const auto accessed = record->ExceptionInformation[1];
        RT_LOG(RT_TAG_RUNTIME) << "Access type: " << (accessType ? "write" : "read")
                  << " at 0x" << std::hex << std::uppercase << accessed << std::dec << std::nouppercase << std::endl;

        // Guardrail: flag raw GX gather pipe touches (0xCC00_8xxx) which must be routed through HLE.
        // Direct stores to this MMIO region (e.g., translated stfs/stb -0x8000(r4) with r4=0xCC010000)
        // will fault on the host. Emit an explicit hint so we know to fix the translation/HLE path instead
        // of chasing generic access violations.
        constexpr uintptr_t kGxGatherLo = 0xCC008000;
        constexpr uintptr_t kGxGatherHi = 0xCC009000; // one page past the 0x100-byte gather range for clarity
        if (accessed >= kGxGatherLo && accessed < kGxGatherHi) {
            RT_LOG(RT_TAG_RUNTIME) << "HINT: guest attempted a direct GX gather pipe write (0xCC00_8xxx)." << std::endl;
            RT_LOG(RT_TAG_RUNTIME) << "      These must go through GX_HLE_FIFO_Write*; check the translated function for" << std::endl;
            RT_LOG(RT_TAG_RUNTIME) << "      literal stores to -0x8000(r4) after lis r4,0xCC01 and route them via HLE." << std::endl;
        }
    }

#if defined(_M_X64) || defined(__x86_64__)
    const auto rip = info->ContextRecord ? info->ContextRecord->Rip : 0;
    const auto rsp = info->ContextRecord ? info->ContextRecord->Rsp : 0;
    std::cerr << std::hex << std::uppercase;
    RT_LOG(RT_TAG_RUNTIME) << "RIP=0x" << rip << " RSP=0x" << rsp << std::dec << std::nouppercase << std::endl;
#elif defined(_M_IX86)
    const auto eip = info->ContextRecord ? info->ContextRecord->Eip : 0;
    const auto esp = info->ContextRecord ? info->ContextRecord->Esp : 0;
    std::cerr << std::hex << std::uppercase;
    RT_LOG(RT_TAG_RUNTIME) << "EIP=0x" << eip << " ESP=0x" << esp << std::dec << std::nouppercase << std::endl;
#endif

    // Always dump CPU state on crash.
    RT_LOG(RT_TAG_RUNTIME) << "===== DUMPING CPU STATE =====" << std::endl;
    SystemBridge::DumpCpuState(TryGetCpuContext());
    std::cerr.flush();

    RT_LOG(RT_TAG_RUNTIME) << "Enable /DEBUG builds or capture a dump for full stack details." << std::endl;
    std::cerr.flush();
}



LONG CALLBACK SehLogger(EXCEPTION_POINTERS* info) {
    // Guest-space faults are the flat memory interception mechanism (MMIO,
    // deferred EFB reads, the executable-write guard, unmapped pages). The
    // flat module registers its own handler first, but registration order is
    // not guaranteed once another VEH is installed later, so consult it here
    // too - resolving a fault twice is a no-op.
    if (info->ExceptionRecord != nullptr && info->ExceptionRecord->NumberParameters >= 2 &&
        GuestFlat::HandleAccessViolation(
            reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]),
            info->ExceptionRecord->ExceptionInformation[0] != 0)) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (g_suppressSehReporting && g_sehJumpTarget) {
        g_sehLastExceptionCode = info->ExceptionRecord->ExceptionCode;
        g_sehLastExceptionAddress = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
        g_sehLastAccessType = 0;
        g_sehLastAccessedAddress = 0;
        if (g_sehLastExceptionCode == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
            g_sehLastAccessType = static_cast<uint32_t>(info->ExceptionRecord->ExceptionInformation[0]);
            g_sehLastAccessedAddress = static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]);
        }
        longjmp(*g_sehJumpTarget, 1);
    }
    if (g_suppressSehReporting) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // Let C++ exceptions propagate to std::terminate so we can log their what().
    if (info->ExceptionRecord->ExceptionCode == kCppExceptionCodeGcc ||
        info->ExceptionRecord->ExceptionCode == kCppExceptionCodeMsvc) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (info->ExceptionRecord->ExceptionCode == 0x40010006 || // DBG_PRINTEXCEPTION_C
        info->ExceptionRecord->ExceptionCode == 0x4001000A || // DBG_PRINTEXCEPTION_WIDE_C (OutputDebugStringW)
        info->ExceptionRecord->ExceptionCode == 0x406D1388 || // SetThreadName
        info->ExceptionRecord->ExceptionCode == kAsanFatalAppExit) { // ASan reporting - let it print first
        return EXCEPTION_CONTINUE_SEARCH;
    }
    
    // Guard against re-entrancy: if we crash while reporting, don't recurse
    static std::atomic_flag s_inCrashHandler = ATOMIC_FLAG_INIT;
    if (s_inCrashHandler.test_and_set()) {
        std::_Exit(EXIT_FAILURE);
    }
    
    // Report the structured exception with detailed information
    ReportStructuredException(info);
    const auto* record = info->ExceptionRecord;
    const DWORD code = record != nullptr ? record->ExceptionCode : 0;
    std::ostringstream popupDetails;
    popupDetails << "A native Windows exception (0x" << std::hex << std::uppercase << code << ") occurred";
    if (!g_lastEntryLabel.empty()) {
        popupDetails << " while executing " << g_lastEntryLabel;
    }
    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        popupDetails << ".\n\nThe game attempted a "
                     << (record->ExceptionInformation[0] ? "write" : "read")
                     << " at host address 0x" << record->ExceptionInformation[1];
    }
    popupDetails << ".\n\nThe process transcript and crash log contain the full CPU and stack diagnostics.";
    ShowRuntimeFatalPopup("a native crash occurred", popupDetails.str());
    DumpHostStackTrace();

    WriteFatalLogImpl("seh");
    
    // CRITICAL: Explicitly flush all output to ensure visibility with PowerShell redirection
    std::cerr << '\n';
    RT_LOG(RT_TAG_RUNTIME) << "===== FLUSHING OUTPUT BEFORE EXIT =====" << std::endl;
    std::cerr.flush();
    std::cout.flush();
    std::fflush(stdout);
    std::fflush(stderr);
    
    std::_Exit(EXIT_FAILURE);
}

void InstallSehLogger() {
    if (!g_vectoredSehHandle) {
        g_vectoredSehHandle = AddVectoredExceptionHandler(1, SehLogger);
    }
}
#else
#if defined(__ANDROID__)
// Set once at startup from the host_tombstone marker file; read from the signal handler, so it is
// a plain atomic rather than a filesystem check at fault time.
std::atomic<bool> g_hostTombstoneRequested{false};
struct sigaction g_previousSigsegvAction {};
struct sigaction g_previousSigbusAction {};
#endif

// POSIX counterpart to SehLogger above. Unlike Windows' AddVectoredExceptionHandler, which lets
// GuestFlat and this module each install their own handler and defensively re-check each other,
// sigaction only allows one handler per signal - the second registration replaces the first
// instead of chaining. So this is the single SIGSEGV/SIGBUS handler for the whole process, and it
// owns checking GuestFlat's fault-interception logic first, exactly mirroring the order SehLogger
// already uses on Windows.

// Horizon delivers CPU faults through libnx's __libnx_exception_handler (defined above), not
// through POSIX signals, and newlib has no sigaction at all - so none of the signal-handling
// machinery below exists on Switch.
#if !defined(__SWITCH__)
void ReportUnhandledSignalFault(int sig, void* faultAddress) {
    RT_LOG(RT_TAG_RUNTIME) << "Signal " << sig << " (fault address 0x" << std::hex
              << reinterpret_cast<uintptr_t>(faultAddress) << std::dec << ")";
    if (!g_lastEntryLabel.empty()) {
        std::cerr << " while executing " << g_lastEntryLabel;
    }
    std::cerr << std::endl;
    if (const CpuContext* cpu = TryGetCpuContext()) {
        RT_LOG(RT_TAG_RUNTIME) << "===== DUMPING CPU STATE =====" << std::endl;
        SystemBridge::DumpCpuState(cpu);
    }
    std::cerr.flush();
}

#if defined(__aarch64__) && !defined(__SWITCH__)
// arm64's uc_mcontext (struct sigcontext) has no direct ESR field the way x86's gregs[REG_ERR]
// does - the ESR value lives in a variable-length list of tagged extension records packed into
// sigcontext::__reserved (fpsimd_context always first, then optionally esr_context, sve_context,
// etc., terminated by a zero-magic/zero-size record). Every arm64 crash handler that wants ESR
// (breakpad, Dolphin's own arm64 fastmem handler, the kernel's own sample code) walks this same
// list; there is no shortcut. Returns false (esr left 0, meaning "read") if the running kernel
// didn't attach an esr_context record - the WnR bit is best-effort diagnostic info, not required
// for GuestFlat::HandleAccessViolation's own resolution (which does not depend on isWrite for
// most fault kinds - see guest_flat_memory.cpp).
//
// VERIFICATION STATUS (Session 2, 2026-09-03): this struct layout was checked against the real
// NDK r27c Bionic headers (asm/sigcontext.h - matches the upstream Linux kernel arm64 signal ABI
// exactly: sigcontext::__reserved holds fpsimd_context first, then optional tagged records ending
// in a zero terminator). qemu-aarch64-static's user-mode SIGSEGV emulation could NOT exercise
// this (it never populates the esr_context record - confirmed separately, a known QEMU
// linux-user limitation, not a bug here). **CONFIRMED WORKING ON REAL HARDWARE** instead: this
// exact algorithm (duplicated standalone to avoid pulling in main.cpp's full SDL3/aurora
// dependency chain) was built into a throwaway JNI test
// (android/app/src/main/cpp/hello.cpp:nativeArm64FaultCheck, since removed/replaced once P5's
// real app exists) and run on the user's Huawei nova 9 (Kirin 985): a deliberate write to a
// PROT_NONE page produced ESR 0x92000047 (WnR bit set, correctly classified as a write), and a
// deliberate read produced ESR 0x92000007 (WnR bit clear, correctly classified as a read). Both
// cases passed. This is now proven, not just structurally plausible.
bool ExtractEsr(const mcontext_t& mc, uint64_t& esrOut) {
    const uint8_t* ptr = mc.__reserved;
    const uint8_t* end = mc.__reserved + sizeof(mc.__reserved);
    while (ptr + sizeof(_aarch64_ctx) <= end) {
        const auto* head = reinterpret_cast<const _aarch64_ctx*>(ptr);
        if (head->magic == 0 && head->size == 0) {
            break;  // terminator record
        }
        if (head->magic == ESR_MAGIC) {
            esrOut = reinterpret_cast<const esr_context*>(ptr)->esr;
            return true;
        }
        if (head->size == 0) {
            break;  // malformed - avoid an infinite loop
        }
        ptr += head->size;
    }
    return false;
}
#endif

void PosixMemoryFaultHandler(int sig, siginfo_t* info, void* ucontextVoid) {
    void* faultAddress = info != nullptr ? info->si_addr : nullptr;
    bool isWrite = false;
#if defined(__x86_64__)
    // Standard glibc technique for a POSIX fastmem-style handler: bit 1 (0x2) of the hardware
    // error code x86 pushes on a page fault records whether it was a write.
    if (ucontextVoid != nullptr) {
        auto* uc = static_cast<ucontext_t*>(ucontextVoid);
        isWrite = (uc->uc_mcontext.gregs[REG_ERR] & 0x2) != 0;
    }
#elif defined(__aarch64__) && !defined(__SWITCH__)
    // sigcontext::fault_address duplicates info->si_addr on arm64 (kept as the primary source
    // above for parity with the x86 branch and because it's populated even when info is null).
    // ESR_ELx.ISS bit 6 (WnR - "Write not Read") is the arm64 equivalent of x86's REG_ERR bit 1:
    // 1 = the aborting access was a write, 0 = a read. Only meaningful for a Data Abort, which is
    // the only kind of fault that reaches SIGSEGV/SIGBUS here.
    if (ucontextVoid != nullptr) {
        auto* uc = static_cast<ucontext_t*>(ucontextVoid);
        uint64_t esr = 0;
        if (ExtractEsr(uc->uc_mcontext, esr)) {
            isWrite = ((esr >> 6) & 1u) != 0;
        }
    }
#endif

    // Guest-space faults are the flat memory interception mechanism (MMIO, deferred EFB reads,
    // the executable-write guard, unmapped pages). Resolving one here means resuming the
    // faulting instruction, which just returning from the handler does.
    if (faultAddress != nullptr && GuestFlat::HandleAccessViolation(faultAddress, isWrite)) {
        return;
    }

    if (g_suppressSehReporting && g_sehJumpTarget) {
        g_sehLastExceptionCode = static_cast<uint32_t>(sig);
        g_sehLastExceptionAddress = reinterpret_cast<uintptr_t>(faultAddress);
        g_sehLastAccessType = isWrite ? 1u : 0u;
        g_sehLastAccessedAddress = reinterpret_cast<uintptr_t>(faultAddress);
        siglongjmp(*g_sehJumpTarget, 1);
    }
    if (g_suppressSehReporting) {
        // Reporting suppressed but nobody armed a recovery jump: restore the default disposition
        // and re-raise so the process still terminates, instead of returning into the same fault.
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }

#if defined(__ANDROID__)
    // Debug escape hatch: our own handler reports rich GUEST state but no host backtrace
    // ("Host stack trace unavailable on this platform"), which is useless when the faulting
    // pointer is a host address rather than a guest one. Creating the marker file
    // WiiCompiled/host_tombstone re-raises the signal into debuggerd's handler instead, so
    // Android writes a real tombstone with a symbolised native stack (read it from `adb logcat`).
    // Restoring SIG_DFL is NOT enough and was the first attempt: our sigaction() REPLACED
    // debuggerd's handler, so the default disposition just kills the process with bionic's
    // "exiting due to SIG_DFL handler for signal 11" and no trace at all. Chain to the handler
    // that was installed before ours. A marker file rather than an environment variable because
    // `am start` cannot set one, and it is resolved at startup so the handler itself stays
    // async-signal-safe. Off unless explicitly asked for, because it sacrifices the guest
    // diagnostics that are usually the more useful half.
    if (g_hostTombstoneRequested.load(std::memory_order_relaxed)) {
        struct sigaction* previous = sig == SIGBUS ? &g_previousSigbusAction : &g_previousSigsegvAction;
        sigaction(sig, previous, nullptr);
        raise(sig);
        return;
    }
#endif

    // Guard against re-entrancy: if we crash while reporting, don't recurse.
    static std::atomic_flag s_inCrashHandler = ATOMIC_FLAG_INIT;
    if (s_inCrashHandler.test_and_set()) {
        std::_Exit(EXIT_FAILURE);
    }

    ReportUnhandledSignalFault(sig, faultAddress);
    std::ostringstream popupDetails;
    popupDetails << "A native signal (" << sig << ") occurred";
    if (!g_lastEntryLabel.empty()) {
        popupDetails << " while executing " << g_lastEntryLabel;
    }
    if (faultAddress != nullptr) {
        popupDetails << ".\n\nThe game attempted a " << (isWrite ? "write" : "read")
                     << " at host address 0x" << std::hex
                     << reinterpret_cast<uintptr_t>(faultAddress) << std::dec;
    }
    popupDetails << ".\n\nThe process transcript and crash log contain the full CPU and stack "
                    "diagnostics.";
    ShowRuntimeFatalPopup("a native crash occurred", popupDetails.str());
    DumpHostStackTrace();
    WriteFatalLogImpl(sig == SIGBUS ? "sigbus" : "sigsegv");

    std::cerr.flush();
    std::cout.flush();
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(EXIT_FAILURE);
}

void InstallPosixMemoryFaultHandler() {
#if defined(__ANDROID__)
    {
        std::error_code markerEc;
        const auto marker = RuntimeConfigFile::ResolveConfigPath().parent_path() / "host_tombstone";
        if (std::filesystem::exists(marker, markerEc)) {
            g_hostTombstoneRequested.store(true, std::memory_order_relaxed);
            RT_LOG(RT_TAG_RUNTIME)
                << "host_tombstone marker present: crashes will produce a debuggerd tombstone "
                   "instead of the usual guest diagnostics"
                << std::endl;
        }
    }
#endif
    struct sigaction action {};
    action.sa_sigaction = PosixMemoryFaultHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
#if defined(__ANDROID__)
    sigaction(SIGSEGV, &action, &g_previousSigsegvAction);
#else
    sigaction(SIGSEGV, &action, nullptr);
#endif
    // A touch beyond a memfd-backed mapping's ftruncate()'d size raises SIGBUS rather than
    // SIGSEGV on Linux; region sizing should make this unreachable, but routing it to the same
    // handler costs nothing and avoids a silent gap if it ever isn't.
#if defined(__ANDROID__)
    sigaction(SIGBUS, &action, &g_previousSigbusAction);
#else
    sigaction(SIGBUS, &action, nullptr);
#endif
}
#endif // !__SWITCH__

#if defined(ANDROID)
// Bumping this thread's nice value (SDLActivity.java's THREAD_PRIORITY_URGENT_DISPLAY) is only a
// scheduling hint - on a heterogeneous mobile SoC (e.g. a 1 prime + 3 performance + 4 efficiency
// core layout) Android's scheduler can still migrate this thread, which runs the entire guest CPU
// emulation and render-submission loop every frame, onto an efficiency core under load. Pinning
// its CPU affinity to whichever cores report the highest max frequency makes that a hard
// constraint instead of a hint. Deliberately conservative: does nothing on a single-tier (all
// cores same max frequency) machine, and only excludes the single lowest frequency tier rather
// than picking one "best" core, so there's always real parallelism headroom left for this thread
// plus its audio/worker threads.
void PinCallingThreadToFastestCores() {
    std::vector<std::pair<int, long>> coreFreqs;
    for (int cpu = 0; cpu < 32; ++cpu) {
        char path[128];
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
        FILE* file = std::fopen(path, "r");
        if (!file) {
            continue;
        }
        long freq = 0;
        const int scanned = std::fscanf(file, "%ld", &freq);
        std::fclose(file);
        if (scanned == 1 && freq > 0) {
            coreFreqs.emplace_back(cpu, freq);
        }
    }

    std::set<long> distinctFreqs;
    for (const auto& [cpu, freq] : coreFreqs) {
        distinctFreqs.insert(freq);
    }
    if (distinctFreqs.size() < 2) {
        // Nothing to distinguish "fast" from "slow" cores (desktop-under-emulation, a homogeneous
        // SoC, or /sys wasn't readable) - leave the default affinity alone rather than guess.
        return;
    }
    const long slowestTier = *distinctFreqs.begin();

    cpu_set_t mask;
    CPU_ZERO(&mask);
    for (const auto& [cpu, freq] : coreFreqs) {
        if (freq > slowestTier) {
            CPU_SET(cpu, &mask);
        }
    }
    if (CPU_COUNT(&mask) == 0) {
        return;
    }
    if (sched_setaffinity(0, sizeof(mask), &mask) != 0) {
        // Some Android cgroup/cpuset configurations refuse a mask outside what's currently
        // allowed (e.g. a backgrounded process's cpuset) - non-fatal, the thread just keeps
        // whatever affinity it already had.
        RT_LOG(RT_TAG_RUNTIME) << "PinCallingThreadToFastestCores: sched_setaffinity failed: "
                                << std::strerror(errno) << std::endl;
    }
}
#endif
#endif

void AbortSignalHandler(int signum) {
    // Guard against re-entrancy if multiple aborts are raised in quick succession
    if (g_abortSignalHandled.test_and_set()) {
        std::_Exit(EXIT_FAILURE);
    }

    // abort() bypasses atexit, so the guest-memory fault summary has to be
    // emitted here too. It is idempotent, so a later AtExitHandler is a no-op.
    GuestFlat::LogFaultSummary();

    ShowRuntimeFatalPopup("a fatal internal error occurred",
                          "The process called abort while running the game or Aurora renderer.\n\n"
                          "This usually means an unimplemented function, failed renderer assertion, "
                          "or another unrecoverable runtime condition was reached.");

    // Skip detailed dump if already reported by another handler
    if (g_fatalErrorReported.load(std::memory_order_acquire)) {
        std::fflush(stderr);
        std::fflush(stdout);
        std::_Exit(EXIT_FAILURE);
    }

    WriteFatalLogImpl("sigabrt");

    std::fflush(stderr);
    std::fflush(stdout);
    std::_Exit(EXIT_FAILURE);
}

} // namespace

// Retained as the thin public wrapper over WriteFatalLogImpl (declared in
// system_bridge.h); it has no in-tree callers because every fatal path now goes
// through RuntimeCrash::WriteCrashArtifacts.
void WriteFatalLog(std::string_view reason) {
    WriteFatalLogImpl(reason);
}

void SetRuntimeExitCode(int code) {
    SetRuntimeExitCodeImpl(code);
}

// Global handler called via atexit() to flush buffers before any exit
static void AtExitHandler() {
    // End-of-run guest memory report. This runs before the fatal-report check
    // below because the counters describe the whole session and are just as
    // interesting after a crash as after a clean exit.
    GuestFlat::LogFaultSummary();

    // Skip if already reported by another handler
    if (g_fatalErrorReported.load(std::memory_order_acquire)) {
        return;
    }
    if (g_exitCodeSet.load(std::memory_order_relaxed) &&
        g_lastExitCode.load(std::memory_order_relaxed) != 0) {
        ShowRuntimeFatalPopup("the runtime exited with an error",
                              "The game stopped after reporting a fatal error. Check the process transcript and crash log for details.");
        WriteFatalLogImpl("exitcode");
    }

    std::cerr.flush();
    std::cout.flush();
    std::fflush(stdout);
    std::fflush(stderr);
}

// Global terminate handler for uncaught exceptions
static void TerminateHandler() {
    // Skip detailed dump if already reported
    if (g_fatalErrorReported.load(std::memory_order_acquire)) {
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
    }
    std::string terminateDetails;
    if (auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            terminateDetails = std::string("Unhandled C++ exception: ") + e.what();
            RT_LOG(RT_TAG_RUNTIME) << "Unhandled C++ exception: " << e.what() << std::endl;
        } catch (...) {
            terminateDetails = "Unhandled non-std C++ exception.";
            RT_LOG(RT_TAG_RUNTIME) << "Unhandled non-std C++ exception." << std::endl;
        }
    } else {
        terminateDetails = "std::terminate() without current exception.";
    }
    ShowRuntimeFatalPopup("an unhandled C++ exception occurred", terminateDetails);
    const std::string hostStackSummary = FormatHostStackTrace(1);
    if (!hostStackSummary.empty()) {
        terminateDetails.append("\n");
        terminateDetails.append(hostStackSummary);
    }
    WriteFatalLogImpl("terminate", terminateDetails);
    RT_LOG(RT_TAG_RUNTIME) << "std::terminate() called - program exiting" << std::endl;
    std::cerr.flush();
    DumpHostStackTrace();
    if (const CpuContext* cpu = TryGetCpuContext()) {
        RT_LOG(RT_TAG_RUNTIME) << "CPU state at terminate:" << std::endl;
        SystemBridge::DumpCpuState(cpu);
    }
    std::fflush(stderr);
    std::_Exit(EXIT_FAILURE);
}

int RuntimeMain(int argc, char** argv) {
#if defined(__SWITCH__)
    // There is no libnx text console here - the framebuffer belongs to aurora/deko3d - and an
    // hbmenu launch has nowhere to stream stdio to, so put both streams on the SD card before
    // anything else can print. Unbuffered, because a crash must not lose the lines that explain
    // it. An nxlink launch overrides these descriptors again later, which is intentional.
    // First, before the log files below (this also creates the data folder they live in): put the
    // files bundled in the NRO next to Config.toml if they are not there yet.
    const std::string bundledFilesSummary = SwitchInstallBundledFiles();
    std::freopen("sdmc:/switch/WiiCompiled/log.txt", "w", stdout);
    std::freopen("sdmc:/switch/WiiCompiled/stderr.txt", "w", stderr);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::fprintf(stderr, "[runtime] %s\n", bundledFilesSummary.c_str());
    // The guest runs on this thread, so every helper thread created from here on should prefer a
    // different core (see SwitchSpreadHelperThread).
    g_switchGuestCore.store(static_cast<int>(svcGetCurrentProcessorNumber()), std::memory_order_relaxed);
    RT_LOGF(RT_TAG_RUNTIME, "guest thread on core %d\n",
            g_switchGuestCore.load(std::memory_order_relaxed));
#if defined(MKW_PGO_GENERATE)
    // gcov writes its .gcda files next to the compile-time object paths, which do not exist on
    // the console; redirect the whole tree onto the SD card and strip the build-machine prefix.
    ::setenv("GCOV_PREFIX", "sdmc:/gcov", 1);
    ::setenv("GCOV_PREFIX_STRIP", "0", 1);
    SwitchPgoBootTrace("logs redirected, starting gcov timer");
    StartSwitchGcovDumpTimer();
#endif
#endif
    // Must run before the transcript duplicates stdout/stderr: it decides what
    // those descriptors are mirrored to now that the products are GUI-subsystem.
    AttachParentConsoleForDiagnostics();
#if defined(_WIN32)
    ConfigureWindowsFatalDialogBehavior();
    InstallSehLogger();
    WindowsTimerResolutionGuard timerResolutionGuard;
#elif defined(__SWITCH__)
    // Faults arrive through __libnx_exception_handler instead; nothing to install.
#else
    InstallPosixMemoryFaultHandler();
#if defined(ANDROID)
    PinCallingThreadToFastestCores();
#endif
#endif
    InitializeProcessTranscript(argc, argv);
    std::signal(SIGABRT, AbortSignalHandler);
    // Install exit/terminate handlers to ensure we get crash info
    std::atexit(AtExitHandler);
    std::set_terminate(TerminateHandler);
    
    std::string currentEntryLabel;

    try {
        if (argc != 1) {
            throw std::invalid_argument("The game runtime does not accept command-line options; use Config.toml through the installed host.");
        }
        // Retro Rewind's generated code queues its mod setup (the Kamek module's memory, its data
        // patches and constructors, the Riivolution XML to load) behind its profile name. Android's
        // combined library activates the profile when the app picks a game; a standalone product
        // (desktop, Switch) has to do it here, or Retro Rewind starts with none of that applied and
        // dies on the first call into the module. A no-op when the profile is already active.
        RecompMod::ActivateProfile(RuntimeProduct::IsRetroRewind() ? "retro_rewind" : "base");
        RuntimeConfigFile::LogLoadedConfig();
        SystemBridge::Initialize();
        TranslatedFunctionRegistry::Finalize();

        // Initialize Aurora (graphics backend)
        // We use auto backend (or specific if needed) and set a default window size.
        // This is required for GX commands (like texture loading) to work.
        AuroraConfig auroraConfig = {};
        auroraConfig.appName = RuntimeProduct::Active().displayName.data();
        const auto applicationDataDirectory = RuntimeConfigFile::ApplicationDataDirectory();
        const auto rendererCacheDirectory = applicationDataDirectory / "Cache";
        std::error_code rendererPathError;
        std::filesystem::create_directories(rendererCacheDirectory, rendererPathError);
        if (rendererPathError) {
            RT_LOG(RT_TAG_RUNTIME) << "Unable to create renderer cache directory "
                      << rendererCacheDirectory << ": " << rendererPathError.message() << std::endl;
        }
        const std::string auroraUserPath = applicationDataDirectory.string();
        const std::string auroraCachePath = rendererCacheDirectory.string();
        auroraConfig.userPath = auroraUserPath.c_str();
        auroraConfig.cachePath = auroraCachePath.c_str();
        auroraConfig.logCallback = &RuntimeAuroraLogCallback;
        auroraConfig.logLevel = LOG_DEBUG;
        const bool configWidescreen = RuntimeConfigFile::WidescreenEnabled(true);
        auroraConfig.windowWidth = configWidescreen ? 854 : 640;
        auroraConfig.windowHeight = 480;
        auroraConfig.windowWidth = RuntimeConfigFile::WindowWidth(auroraConfig.windowWidth);
        auroraConfig.windowHeight = RuntimeConfigFile::WindowHeight(auroraConfig.windowHeight);
        auroraConfig.hasWindowPosition = RuntimeConfigFile::WindowPosition(
            auroraConfig.windowPosX, auroraConfig.windowPosY);
        auroraConfig.allowJoystickBackgroundEvents = true;
        auroraConfig.disableCopyFilter = RuntimeConfigFile::DisableCopyFilter(true);
        // Dolphin-style custom textures. Aurora indexes <userPath>/texture_replacements
        // once during aurora_initialize, so both knobs only take effect on the next launch.
        // Dumps name each unmatched texture the way its replacement would have to be named,
        // which is only useful while the index is live - hence the conjunction.
        auroraConfig.allowTextureReplacements = RuntimeConfigFile::TextureReplacements(false);
        auroraConfig.allowTextureDumps = auroraConfig.allowTextureReplacements &&
                                         RuntimeConfigFile::TextureDumps(false);
        // No vsync knob: aurora always configures a non-blocking present mode.
        auroraConfig.desiredBackend = BACKEND_AUTO;
        const float resolutionMultiplier = RuntimeConfigFile::ResolutionMultiplier(1.0f);
        ConfigureMkwDynamicAspect(configWidescreen, auroraConfig.windowWidth, auroraConfig.windowHeight);
        VISetFrameBufferScale(resolutionMultiplier);
        // One table for both directions. RuntimeConfigFile::IsSupportedGraphicsApi
        // whitelists exactly these config names, so an unrecognised value has
        // already been rejected (and reported) at parse time.
        struct GraphicsBackendEntry {
            const char* configName;
            AuroraBackend backend;
        };
#if defined(__SWITCH__)
        static constexpr std::array<GraphicsBackendEntry, 3> kGraphicsBackends{{
            {"auto", BACKEND_AUTO}, {"opengles", BACKEND_OPENGLES}, {"deko3d", BACKEND_DEKO3D},
        }};
#elif defined(__ANDROID__)
        // "auto" tries Vulkan first and falls back to OpenGL ES; "opengles" forces the fallback
        // for a device whose Vulkan driver reports support but misbehaves.
        static constexpr std::array<GraphicsBackendEntry, 3> kGraphicsBackends{{
            {"auto", BACKEND_AUTO}, {"vulkan", BACKEND_VULKAN}, {"opengles", BACKEND_OPENGLES},
        }};
#else
        static constexpr std::array<GraphicsBackendEntry, 3> kGraphicsBackends{{
            {"auto", BACKEND_AUTO}, {"d3d12", BACKEND_D3D12}, {"vulkan", BACKEND_VULKAN},
        }};
#endif
        const auto backendDisplayName = [](AuroraBackend value) -> const char* {
            for (const auto& entry : kGraphicsBackends) {
                if (entry.backend == value) {
                    return entry.configName;
                }
            }
            return "unknown";
        };

        const std::string backend = RuntimeConfigFile::GraphicsApi("auto");
        for (const auto& entry : kGraphicsBackends) {
            if (backend == entry.configName) {
                auroraConfig.desiredBackend = entry.backend;
                break;
            }
        }
#if defined(__SWITCH__)
        // deko3d is the Switch renderer. "auto" (what a freshly written Config.toml says) used to
        // fall through to OpenGL ES here, which crashes at startup on a first-time install.
        if (auroraConfig.desiredBackend == BACKEND_AUTO) {
            auroraConfig.desiredBackend = BACKEND_DEKO3D;
        }
#endif
        const AuroraBackend requestedBackend = auroraConfig.desiredBackend;
        if (requestedBackend != BACKEND_OPENGLES) {
            // Only Dawn's Vulkan backend consults it; must precede the Vulkan instance.
            LoadAndroidCustomGpuDriver();
        }
#if defined(__ANDROID__) && defined(MKW_PGO_GENERATE)
        StartAndroidPgoRecording();
#endif

#if defined(__SWITCH__)
        // Switch system fonts for the launcher and the settings panel (runs inside aurora_initialize).
        auroraConfig.imGuiInitCallback = &SwitchLauncherInitFonts;
#elif defined(__ANDROID__)
        auroraConfig.imGuiInitCallback = &settings_overlay::InitializeThorSettingsAssets;
#endif
        const AuroraInfo auroraInfo = aurora_initialize(0, nullptr, &auroraConfig);
#if defined(ANDROID)
        // Defense-in-depth for a real device crash (GitHub issue #3, PowerVR B-Series GPU): Null
        // is Dawn's no-op CPU testing backend, never meant to actually render anything - Android
        // no longer compiles it in at all (AuroraDawnProvider.cmake's DAWN_ENABLE_NULL is now OFF
        // there), so landing here at runtime would mean something upstream of this check still
        // let it through. Failing clearly HERE, before any real rendering is attempted, beats
        // what used to happen: proceeding into gfx::initialize() and crashing later with a
        // confusing low-level "Usages requested... not supported by the adapter" WebGPU error
        // that never told the user their actual problem (their device has no working Vulkan
        // driver) at all.
        if (auroraInfo.backend == BACKEND_NULL) {
            ShowRuntimeFatalPopup(
                "graphics initialization failed",
                "This device's graphics drivers could not start either renderer.\n\n"
                "Vulkan was unavailable, and the OpenGL ES fallback did not start either. You can "
                "pick a renderer by hand under Graphics on the app's start screen, but this is a "
                "device/driver limitation rather than something the app can work around.");
            MarkFatalErrorReported();
            SetRuntimeExitCode(EXIT_FAILURE);
            std::exit(EXIT_FAILURE);
        }
#endif
        if (requestedBackend != BACKEND_AUTO && auroraInfo.backend != requestedBackend) {
            RT_LOG(RT_TAG_RUNTIME) << "graphics_api=\"" << backend
                      << "\" is not available on this system; aurora fell back to \""
                      << backendDisplayName(auroraInfo.backend)
                      << "\". See the [aurora::gpu] lines above for the reason." << std::endl;
            settings_overlay::NotifyBackendFallback(backendDisplayName(requestedBackend),
                                                    backendDisplayName(auroraInfo.backend));
        } else {
            RT_LOG(RT_TAG_RUNTIME) << "graphics backend: " << backendDisplayName(auroraInfo.backend)
                      << std::endl;
#if defined(__ANDROID__)
            // Automatic tries Vulkan first; ending up on OpenGL ES means Vulkan failed to start.
            if (requestedBackend == BACKEND_AUTO && auroraInfo.backend == BACKEND_OPENGLES) {
                settings_overlay::NotifyBackendFallback("vulkan", "opengles");
            }
#endif
        }
#if defined(__SWITCH__)
        // First boot / install / browse: the launcher opens when the game data is missing or
        // incomplete (or L is held at start) and returns once the player presses Play, or false
        // if they quit. It uses aurora's frame loop, so it runs before the guest takes over.
        if (!SwitchRunLauncher()) {
            aurora_shutdown();
#if defined(MKW_PGO_GENERATE)
            StopSwitchGcovDumpTimer();
#endif
            // Quit, or Retro Rewind queued to run next: give the loader its heap back intact.
            GuestFlat::SwitchReleaseForHandoff();
            return 0;
        }
        // The launcher returns with the next aurora frame already begun (see its exit path).
        g_auroraFrameActive.store(true, std::memory_order_release);
        // Started here rather than at the top of RuntimeMain: sampling before the runtime is
        // fully up aborted the process inside libnx before guest memory was even reserved. And only
        // after the launcher: its thread is never stopped, and a thread still running when the
        // launcher hands the process to Retro Rewind crashes that program on start.
        StartSwitchGuestProfiler();
        // Horizon's BSD sockets do nothing until the service is opened; without this every
        // connection the game makes fails and online reports error 20100. Opened only once the
        // game is really starting, so the launcher's hand-off to another program holds no session.
        {
            const Result socketRc = socketInitializeDefault();
            {
                u64 total = 0;
                svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
                const struct mallinfo heap = mallinfo();
                RT_LOGF(RT_TAG_RUNTIME, "heap at game start: %llu MiB in use, %llu MiB obtained, process total %llu MiB\n",
                        static_cast<unsigned long long>(static_cast<uint64_t>(heap.uordblks) >> 20),
                        static_cast<unsigned long long>(static_cast<uint64_t>(heap.arena) >> 20),
                        static_cast<unsigned long long>(total >> 20));
            }
            RT_LOGF(RT_TAG_RUNTIME, "network sockets: %s (rc 0x%x)\n",
                    R_SUCCEEDED(socketRc) ? "ready" : "unavailable", static_cast<unsigned>(socketRc));
            // Hold an internet request for as long as the game runs. Without one the console treats
            // the game as offline and is free to power Wi-Fi down mid-session, after which every
            // lookup and connection fails (online error 20100 until a restart).
            static NifmRequest internetRequest;
            Result nifmRc = nifmInitialize(NifmServiceType_User);
            if (R_SUCCEEDED(nifmRc)) {
                nifmRc = nifmCreateRequest(&internetRequest, true);
            }
            if (R_SUCCEEDED(nifmRc)) {
                nifmRc = nifmRequestSubmit(&internetRequest);
            }
            RT_LOGF(RT_TAG_RUNTIME, "internet request: %s (rc 0x%x)\n",
                    R_SUCCEEDED(nifmRc) ? "submitted" : "not available", static_cast<unsigned>(nifmRc));
        }
#endif
        aurora_set_frame_worker_wait_callback(ServiceGuestTimingDuringAuroraFrameWait);
        GxGuestWrite::InstallAuroraHooks();
#if defined(__SWITCH__)
        if (FILE* flag = std::fopen("sdmc:/switch/WiiCompiled/no_write_tracking.flag", "r")) {
            std::fclose(flag);
            GxGuestWrite::g_trackingDisabled = true;
            RT_LOG(RT_TAG_GX) << "write tracking disabled by no_write_tracking.flag: every GX cache "
                                 "re-digests on every use" << std::endl;
        }
#endif
        UpdateMkwDynamicAspectSurface(auroraInfo.windowSize.native_fb_width,
                                      auroraInfo.windowSize.native_fb_height);
        settings_overlay::InitializeRuntimeSettings();
        RT_LOG(RT_TAG_CONFIG) << "video.widescreen=" << (configWidescreen ? "true" : "false")
                  << " SCGetAspectRatio=" << (configWidescreen ? 1 : 0)
                  << " resolutionMultiplier=" << resolutionMultiplier
                  << " window=" << auroraInfo.windowSize.width << "x" << auroraInfo.windowSize.height
                  << " native=" << auroraInfo.windowSize.native_fb_width << "x"
                  << auroraInfo.windowSize.native_fb_height
                  << " viewportPolicy=" << (g_dynamicAspectRatioEnabled ? "stretch" : "fit")
                  << " presentAspect="
                  << (g_dynamicAspectRatioEnabled ? "surface (dynamic EGG canvas)" : "4:3")
                  << std::endl;
        g_auroraInitialized.store(true, std::memory_order_release);

        auto entry = ResolveEntry();
        InitializePersistentCpuContext();
        auto& cpu = GetPersistentCpuContext();
        SeedCpuContext(cpu);
        
        // Initialize the fiber-based threading system
        Fiber::GuestFiberManager::Initialize();
        
        CpuContextScope cpuScope(&cpu);

        std::string label = entry->name;
        if (label.empty()) {
            std::ostringstream oss;
            oss << "0x" << std::hex << entry->address;
            label = oss.str();
        }
        currentEntryLabel = label;
        g_lastEntryLabel = currentEntryLabel;

        InvokeIndirectCpu(entry->address, &cpu);
        const uint32_t result = cpu.gpr[3];
        RT_LOG(RT_TAG_RUNTIME) << label << " => 0x" << std::hex << result << std::dec << " (" << result << ")" << std::endl;
        
        // Shutdown fiber system
        Fiber::GuestFiberManager::Shutdown();
        WindowPlacementPersistence::Flush(true);
        aurora_shutdown();
        SetRuntimeExitCodeImpl(0);
        ShutdownProcessTranscript();
        return 0;
    } catch (const Memory::AccessViolation& ex) {
        std::cerr << "Runtime error: " << ex.what() << std::endl;
        DumpAccessViolationReport(ex, currentEntryLabel);
        std::ostringstream details;
        details << "addr=0x" << std::hex << std::uppercase << ex.address()
                << " len=0x" << ex.length()
                << std::dec << std::nouppercase
                << " reason=" << ex.reason();
        ShowRuntimeFatalPopup("a guest memory access was out of bounds", details.str());
        WriteFatalLogImpl("access_violation", details.str());
        SetRuntimeExitCodeImpl(1);
        Fiber::GuestFiberManager::Shutdown();
        WindowPlacementPersistence::Flush(true);
        aurora_shutdown();
        ShutdownProcessTranscript();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Runtime error: " << ex.what() << std::endl;
        SystemBridge::DumpCpuState(TryGetCpuContext());
        ShowRuntimeFatalPopup("a runtime exception occurred", ex.what());
        WriteFatalLogImpl("exception");
        SetRuntimeExitCodeImpl(1);
        Fiber::GuestFiberManager::Shutdown();
        WindowPlacementPersistence::Flush(true);
        aurora_shutdown();
        ShutdownProcessTranscript();
        return 1;
    }
}

int main(int argc, char** argv) {
#if defined(__SWITCH__) && defined(MKW_PGO_GENERATE)
    SwitchPgoBootTrace("main entered");
#endif
    return RuntimeMain(argc, argv);
}
extern "C" bool g_dynamicAspectRatioEnabled = false;
