// gx_nsmbw.cpp - GX entry points New Super Mario Bros. Wii calls that Mario Kart Wii never did.
#include "gx_internal.h"
#include "runtime_log.h"

#include <dolphin/gx/GXGet.h>

namespace {
void LogOnce(bool& logged, const char* what)
{
    if (!logged) {
        logged = true;
        RT_LOGF(RT_TAG_GX, "%s\n", what);
    }
}
} // namespace

// GXGetCullMode(GXCullMode* mode): Aurora holds the mode GXSetCullMode (gx_pixel.cpp) forwards to it.
// NSMBW reads it while building its warning and message layouts.
extern "C" void GX__GetCullMode_801c5890(uint32_t modePtr) {
    GXCullMode mode = GX_CULL_NONE;
    GXGetCullMode(&mode);
    if (modePtr != 0) {
        Memory::Write32(modePtr, static_cast<uint32_t>(mode));
    }
}
#if 0  // BRAWL: no matching function in the Brawl dol
PPC_NATIVE_OVERRIDE_VOID(8028d6a4, GX__GetCullMode_801c5890, (uint32_t modePtr), (modePtr));
#endif

// GXAbortFrame asks the GPU to drop the frame in flight. Aurora submits whole frames and has nothing
// half-finished to abandon, so there is nothing to do.
extern "C" void GX__AbortFrame_801c4ce0() {
    static bool logged = false;
    LogOnce(logged, "GXAbortFrame: no partial GPU frame to abandon (no-op)");
}
PPC_NATIVE_OVERRIDE_VOID(801f0724, GX__AbortFrame_801c4ce0, (), ());

// GXPoke*: modes for CPU writes straight into the embedded frame buffer (GXPokeARGB/GXPokeZ). On
// hardware they program pixel-engine registers; the runtime does not emulate CPU-to-EFB access, so
// the modes have no consumer. Accept them (Aurora declares but does not implement these).
#define NSMBW_GX_POKE_NOOP(addr, name, params, uses)                                        \
    extern "C" void GX__##name##_##addr params {                                            \
        static bool logged = false;                                                          \
        (void)(uses);                                                                        \
        LogOnce(logged, "GX" #name ": CPU-to-EFB poke modes are not emulated (no-op)");     \
    }

NSMBW_GX_POKE_NOOP(801f0b14, PokeAlphaMode, (uint32_t func, uint32_t threshold), (func, threshold))
PPC_NATIVE_OVERRIDE_VOID(801f0b14, GX__PokeAlphaMode_801f0b14, (uint32_t func, uint32_t threshold), (func, threshold));
NSMBW_GX_POKE_NOOP(801f0b24, PokeAlphaRead, (uint32_t mode), (mode))
PPC_NATIVE_OVERRIDE_VOID(801f0b24, GX__PokeAlphaRead_801f0b24, (uint32_t mode), (mode));
NSMBW_GX_POKE_NOOP(801f0b3c, PokeAlphaUpdate, (uint32_t enable), (enable))
PPC_NATIVE_OVERRIDE_VOID(801f0b3c, GX__PokeAlphaUpdate_801f0b3c, (uint32_t enable), (enable));
NSMBW_GX_POKE_NOOP(801f0b50, PokeBlendMode, (uint32_t type, uint32_t src, uint32_t dst, uint32_t op), (type, src, dst, op))
PPC_NATIVE_OVERRIDE_VOID(801f0b50, GX__PokeBlendMode_801f0b50, (uint32_t type, uint32_t src, uint32_t dst, uint32_t op),
                         (type, src, dst, op));
NSMBW_GX_POKE_NOOP(801f0bac, PokeColorUpdate, (uint32_t enable), (enable))
PPC_NATIVE_OVERRIDE_VOID(801f0bac, GX__PokeColorUpdate_801f0bac, (uint32_t enable), (enable));
NSMBW_GX_POKE_NOOP(801f0bc0, PokeDstAlpha, (uint32_t enable, uint32_t alpha), (enable, alpha))
PPC_NATIVE_OVERRIDE_VOID(801f0bc0, GX__PokeDstAlpha_801f0bc0, (uint32_t enable, uint32_t alpha), (enable, alpha));
NSMBW_GX_POKE_NOOP(801f0bd8, PokeDither, (uint32_t dither), (dither))
PPC_NATIVE_OVERRIDE_VOID(801f0bd8, GX__PokeDither_801f0bd8, (uint32_t dither), (dither));
NSMBW_GX_POKE_NOOP(801f0bec, PokeZMode, (uint32_t compareEnable, uint32_t func, uint32_t updateEnable),
                   (compareEnable, func, updateEnable))
PPC_NATIVE_OVERRIDE_VOID(801f0bec, GX__PokeZMode_801f0bec, (uint32_t compareEnable, uint32_t func, uint32_t updateEnable),
                         (compareEnable, func, updateEnable));

#undef NSMBW_GX_POKE_NOOP
