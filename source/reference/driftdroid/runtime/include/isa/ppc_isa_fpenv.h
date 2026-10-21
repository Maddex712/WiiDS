#pragma once
// FPSCR[NI] (non-IEEE flush-to-zero) modeled on the host FP environment, plus
// the thread-local mirror of that state the hot paths read instead of MXCSR.

#include "ppc_isa_config.h"

#include <cstdint>

// Software-flushing Gekko's single-precision denormals per op roughly doubled the THP IDCT
// kernel's cycle count, so instead the runtime mirrors guest FPSCR[NI] into the host FP control
// register's flush-to-zero bit(s) wherever FPSCR can change (PPC_Mtfs*, fiber context switches,
// CpuContextScope), making per-op flushes free. Accepted deviations (same trade Dolphin makes):
// on x86 MXCSR FTZ also flushes double denormals unlike real NI, and a pre-round-flush edge near
// FLT_MIN rounds via cvtsd2ss instead. On arm64 FPCR.FZ only affects single-precision ops/
// conversions (double ops are untouched), which is actually a closer match to real Gekko NI than
// the x86 behavior above.
#if defined(__aarch64__)
inline constexpr uint32_t kMkwMxcsrFlushToZeroBits = 1u << 24; // FPCR.FZ
#else
inline constexpr uint32_t kMkwMxcsrFlushToZeroBits = (1u << 15) | (1u << 6); // FTZ | DAZ
#endif

// Reads/writes the host FP control register (MXCSR on x86, FPCR on arm64). Every site that
// touches the host FP environment goes through these two so the arch split lives in one place.
#if defined(__aarch64__)
inline uint32_t MkwReadHostFpControl() noexcept
{
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    return static_cast<uint32_t>(fpcr);
}

inline void MkwWriteHostFpControl(uint32_t value) noexcept
{
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr = (fpcr & ~static_cast<uint64_t>(0xFFFFFFFFu)) | value;
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
}
#else
inline uint32_t MkwReadHostFpControl() noexcept
{
    return _mm_getcsr();
}

inline void MkwWriteHostFpControl(uint32_t value) noexcept
{
    _mm_setcsr(value);
}
#endif


#if defined(__SWITCH__) || defined(__ANDROID__)
inline bool g_mkwHostNiActive = false;  // see g_currentCpuContext
#else
inline thread_local bool g_mkwHostNiActive = false;
#endif

// Same state in the form PpcForceSingleValueInline consumes: the pre-round subnormal threshold
// while NI is active, 0.0 (identity, `|value| < 0.0` is always false) otherwise, so that path
// needs no branch. Every writer of g_mkwHostNiActive must write this beside it in agreement.
inline constexpr double kMkwNiFlushThreshold = 0x1p-126;  // 0x3810000000000000
#if defined(__SWITCH__) || defined(__ANDROID__)
inline double g_mkwNiFlushThreshold = 0.0;  // see g_currentCpuContext
#else
inline thread_local double g_mkwNiFlushThreshold = 0.0;
#endif

inline void MkwApplyHostNiMode(uint32_t fpscr) noexcept
{
    const uint32_t csr = MkwReadHostFpControl();
    const bool wantNi = (fpscr & 0x4u) != 0;
    const uint32_t want = wantNi
        ? (csr | kMkwMxcsrFlushToZeroBits)
        : (csr & ~kMkwMxcsrFlushToZeroBits);
    if (want != csr)
        MkwWriteHostFpControl(want);
    // `want` has both bits set or both clear (x86; a single bit on arm64), so this is exactly
    // `(MkwReadHostFpControl() & kMkwMxcsrFlushToZeroBits) != 0` after the write - the mirror
    // cannot disagree with the register even if the incoming CSR held only some of the bits.
    g_mkwHostNiActive = wantNi;
    g_mkwNiFlushThreshold = wantNi ? kMkwNiFlushThreshold : 0.0;
}

/// <summary>
/// Restores a previously captured host FP control register value and re-derives the mirror
/// from it. Every raw restore has to go through here; a bare write would leave the mirror
/// describing the FP environment that was just replaced.
/// </summary>
inline void MkwRestoreHostMxcsr(uint32_t csr) noexcept
{
    MkwWriteHostFpControl(csr);
    const bool niActive = (csr & kMkwMxcsrFlushToZeroBits) != 0;
    g_mkwHostNiActive = niActive;
    g_mkwNiFlushThreshold = niActive ? kMkwNiFlushThreshold : 0.0;
}
