#include "hle_stubs.h"
#include "settings_overlay.h"
#include "runtime_product.h"

#include <cstdint>
#include <stdexcept>

#if defined(__ANDROID__)
// The standalone base library has no mod objects. RR's retained translated
// overlay supplies this symbol in its library; never invent its controller state.
extern "C" void rr_overlay_800077C8(CpuContext* ctx) __attribute__((weak));
#endif

// StrapScene::calc calls CheckInput only after the scene's loading and timing
// gates have completed. Accepting that check advances on the first eligible
// frame without depending on a Code.pul function address or guest controller
// hardware. The PAL base DOL hash pins this entry point.
extern "C" uint32_t StrapScene__CheckInput_Skip(uint32_t scenePtr)
{
    (void)scenePtr;
    settings_overlay::NotifyStrapInputAccepted();
    return 1;
}

extern "C" void StrapScene__CheckInput_ProductAware(CpuContext* ctx) {
#if defined(__ANDROID__)
    if (RuntimeProduct::IsRetroRewind()) {
        if (!rr_overlay_800077C8) {
            throw std::runtime_error("Retro Rewind strap overlay is missing from the native library");
        }
        // The real Code.pul overlay records the accepting controller for RR's
        // later boot arguments. Auto-accepting here bypassed that side effect.
        rr_overlay_800077C8(ctx);
        if (ctx->gpr[3] != 0) settings_overlay::NotifyStrapInputAccepted();
        return;
    }
#endif
    ctx->gpr[3] = StrapScene__CheckInput_Skip(ctx->gpr[3]);
}
REGISTER_NATIVE_FUNCTION_AS(0x800077C8, StrapScene__CheckInput_ProductAware, "StrapScene__CheckInput_ProductAware");
