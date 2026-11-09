#include "det_clock.h"
#include "memory.h"
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "runtime_log.h"
#include "os_internal.h"

#include <cstdint>
#include <iostream>

namespace {
// Read the 64-bit time base the way the SDK does: retry until the upper word is
// unchanged across the lower-word read, so a rollover in between is never
// observed as a jump.
void ReadTimeBaseRegisters(uint32_t& outHi, uint32_t& outLo)
{
    while (true) {
        const uint32_t hi1 = PPC_Mftbu();
        const uint32_t lo = PPC_Mftb();
        const uint32_t hi2 = PPC_Mftbu();
        if (hi1 == hi2) {
            outHi = hi1;
            outLo = lo;
            return;
        }
    }
}
} // namespace

namespace OsHleInternal {
uint64_t ReadSystemTime(bool guestRead)
{
    uint64_t timeBase = 0;
    if (DetClock::Enabled()) {
        // The HLE's own reads (alarm due checks, OSSetAlarm) don't step the deterministic timeline. A guest read
        // does: Brawl re-arms its periodic pad alarm from the alarm itself, and a read that never moves gives it
        // the same fire time forever.
        timeBase = guestRead ? DetClock::ReadForGuest() : DetClock::Now();
    } else {
        uint32_t hi1 = 0;
        uint32_t lo = 0;
        ReadTimeBaseRegisters(hi1, lo);
        timeBase = (static_cast<uint64_t>(hi1) << 32) | lo;
    }
    const uint32_t baseHi = ::Memory::Read32(0x800030D8u);
    const uint32_t baseLo = ::Memory::Read32(0x800030DCu);
    const uint64_t base = (static_cast<uint64_t>(baseHi) << 32) | baseLo;
    return base + timeBase;
}
} // namespace OsHleInternal

// OSGetTime -> returns 64-bit time base (hi in r3, lo in r4).
extern "C" void OS__GetTime_HLE(CpuContext* ctx)
{
    if (!ctx) {
        return;
    }

    uint32_t hi = 0;
    uint32_t lo = 0;
    ReadTimeBaseRegisters(hi, lo);
    ctx->gpr[3] = hi;
    ctx->gpr[4] = lo;
}

PPC_NATIVE_OVERRIDE_VOID(801E1B34, OS__GetTime_HLE, (CpuContext* ctx), (ctx));


// ----------------------------------------------------------------------------
// OSGetSystemTime HLE - map system time structure to a 64-bit return
extern "C" uint32_t OS____GetSystemTime_801aad7c(uint32_t /*r3_hi*/, uint32_t /*r4_lo*/, uint32_t /*r5*/, int32_t /*r6*/, uint32_t /*r7*/, uint32_t /*r8*/) {
    // Save/disable interrupts (mirrors the original implementation).
    int saved_level = OS__DisableInterrupts_801a65ac();

    uint64_t now = 0;
    try {
        now = ReadSystemTime(true);
    } catch (const ::Memory::AccessViolation& e) {
        LogMemoryError(RT_TAG_OS, "OS____GetSystemTime_801aad7c", e);
        now = (static_cast<uint64_t>(PPC_Mftbu()) << 32) | PPC_Mftb();
    }
    const uint32_t hi = static_cast<uint32_t>(now >> 32);
    const uint32_t lo = static_cast<uint32_t>(now & 0xFFFFFFFFu);
    OS__RestoreInterrupts_801a65d4(saved_level);

    if (CpuContext* ctx = CurrentCpuContext()) {
        ctx->gpr[3] = hi;
        ctx->gpr[4] = lo;
    }
    return hi;
}

PPC_NATIVE_OVERRIDE(801E1B54, OS____GetSystemTime_801aad7c, uint32_t, (uint32_t r3_low, uint32_t r4_high, uint32_t r5_unused, int32_t r6_unused, uint32_t r7_unused, uint32_t r8_unused), (r3_low, r4_high, r5_unused, r6_unused, r7_unused, r8_unused));
