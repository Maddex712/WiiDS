#include "hle_stubs.h"
#include "runtime_log.h"
#include <cstdint>
#include <cstdio>

extern "C" uint32_t Stub_8002001C(uint32_t ctx, int code)
{
    RT_LOGF(RT_TAG_HLE, "Stub_8002001C(0x%08x, %d)\n", ctx, code);
    (void)ctx;
    (void)code;
    return 0;
}

#if 0  // NSMBW: Stub is not linked into NSMBW (was MKW-only)
#if 0  // SPM: 0x8002001c: no NSMBW symbol at this address
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE(8002001C, Stub_8002001C, uint32_t, (uint32_t ctx, int code), (ctx, code));
#endif
#endif
#endif
