// Save states' share of the window (gx_savestate_overlay.cpp): the line that
// says a state was saved or loaded.
#ifndef SMS_GX_SAVESTATE_OVERLAY_H
#define SMS_GX_SAVESTATE_OVERLAY_H

namespace gx {

#ifdef SMS_GX_HAVE_SDL2
// On the GL thread, between presenting the frame and the swap.
void savestateOverlayRender(int winW, int winH);
#else
inline void savestateOverlayRender(int, int) {}
#endif

}  // namespace gx

#endif
