#include "hle_stubs.h"
#include "settings_overlay.h"

#include <cstdint>

// StrapScene::calc calls CheckInput only after the scene's loading and timing
// gates have completed. Accepting that check advances on the first eligible
// frame without depending on a Code.pul function address or guest controller
// hardware. The PAL base DOL hash pins this entry point, and Retro Rewind
// overlays the same base-game function.
extern "C" uint32_t StrapScene__CheckInput_Skip(uint32_t scenePtr)
{
    (void)scenePtr;
    settings_overlay::NotifyStrapInputAccepted();
    return 1;
}
#if 0  // NSMBW: MKW StrapScene::CheckInput skip; NSMBW's strap screen is dWiiStrap_c (not ported yet)
PPC_NATIVE_OVERRIDE(800077C8, StrapScene__CheckInput_Skip, uint32_t, (uint32_t scenePtr), (scenePtr));
#endif
